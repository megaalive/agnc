/*
 * serve.cpp
 *
 * gRPC server: agnc serve --listen HOST:PORT
 */

#include "agnc/grpc_bridge.h"
#include "agnc/version.h"

#include "agnc/v1/agent.grpc.pb.h"

#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>

#include <condition_variable>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace {

struct CancelFlag {
    volatile int value;
};

class CancelRegistry {
public:
    std::shared_ptr<CancelFlag> register_query(const std::string &query_id)
    {
        auto flag = std::make_shared<CancelFlag>();
        flag->value = 0;
        if (query_id.empty()) {
            return flag;
        }

        std::lock_guard<std::mutex> lock(mutex_);
        active_[query_id] = flag;
        return flag;
    }

    void unregister_query(const std::string &query_id)
    {
        if (query_id.empty()) {
            return;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        active_.erase(query_id);
    }

    bool cancel(const std::string &query_id)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = active_.find(query_id);
        if (it == active_.end()) {
            return false;
        }
        it->second->value = 1;
        return true;
    }

private:
    std::mutex mutex_;
    std::unordered_map<std::string, std::shared_ptr<CancelFlag>> active_;
};

class PermissionGate {
    /* Blokir query saat tool butuh izin; klien jawab via RespondPermission RPC. */
public:
    explicit PermissionGate(std::string query_id) : query_id_(std::move(query_id)) {}

    int ask(const char *kind, const char *detail, const std::function<void()> &emit_prompt)
    {
        std::unique_lock<std::mutex> lock(mutex_);

        pending_kind_ = kind != NULL ? kind : "";
        pending_detail_ = detail != NULL ? detail : "";
        answered_ = false;
        allowed_ = false;

        if (emit_prompt) {
            lock.unlock();
            emit_prompt();
            lock.lock();
        } else {
            fprintf(
                stderr,
                "agnc: [grpc permission] query_id=%s kind=%s detail=%s (call RespondPermission)\n",
                query_id_.c_str(),
                pending_kind_.c_str(),
                pending_detail_.c_str());
            fflush(stderr);
        }

        cv_.wait(lock, [this]() { return answered_; });
        return allowed_ ? 1 : 0;
    }

    bool respond(bool allowed)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (query_id_.empty()) {
            return false;
        }
        allowed_ = allowed;
        answered_ = true;
        cv_.notify_all();
        return true;
    }

    const std::string &query_id() const
    {
        return query_id_;
    }

private:
    std::string query_id_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool answered_ = false;
    bool allowed_ = false;
    std::string pending_kind_;
    std::string pending_detail_;
};

class PermissionRegistry {
public:
    std::shared_ptr<PermissionGate> attach(const std::string &query_id)
    {
        if (query_id.empty()) {
            return nullptr;
        }

        auto gate = std::make_shared<PermissionGate>(query_id);
        std::lock_guard<std::mutex> lock(mutex_);
        active_[query_id] = gate;
        return gate;
    }

    void detach(const std::string &query_id)
    {
        if (query_id.empty()) {
            return;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        active_.erase(query_id);
    }

    bool respond(const std::string &query_id, bool allowed)
    {
        std::shared_ptr<PermissionGate> gate;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = active_.find(query_id);
            if (it == active_.end()) {
                return false;
            }
            gate = it->second;
        }
        return gate->respond(allowed);
    }

private:
    std::mutex mutex_;
    std::unordered_map<std::string, std::shared_ptr<PermissionGate>> active_;
};

CancelRegistry g_cancel_registry;
PermissionRegistry g_permission_registry;

struct StreamBridgeCtx {
    grpc::ServerWriter<agnc::v1::StreamQueryResponse> *writer;
    std::shared_ptr<PermissionGate> permission_gate;
    std::string query_id;
};

struct PermissionBridgeCtx {
    std::shared_ptr<PermissionGate> gate;
    StreamBridgeCtx *stream;
};

static void grpc_stream_delta_cb(const char *text, size_t length, void *ctx)
{
    StreamBridgeCtx *bridge = static_cast<StreamBridgeCtx *>(ctx);
    agnc::v1::StreamQueryResponse chunk;

    if (bridge == NULL || bridge->writer == NULL || text == NULL || length == 0) {
        return;
    }

    chunk.set_text_delta(std::string(text, length));
    bridge->writer->Write(chunk);
}

static int grpc_permission_cb(const char *kind, const char *detail, void *ctx)
{
    PermissionBridgeCtx *bridge = static_cast<PermissionBridgeCtx *>(ctx);

    if (bridge == NULL || bridge->gate == NULL) {
        return 0;
    }

    if (bridge->stream != NULL && bridge->stream->writer != NULL) {
        return bridge->gate->ask(kind, detail, [&]() {
            agnc::v1::StreamQueryResponse chunk;
            agnc::v1::PermissionRequest *prompt = chunk.mutable_permission_request();
            prompt->set_query_id(bridge->stream->query_id);
            prompt->set_kind(kind != NULL ? kind : "");
            prompt->set_detail(detail != NULL ? detail : "");
            bridge->stream->writer->Write(chunk);
        });
    }

    return bridge->gate->ask(kind, detail, nullptr);
}

static agnc_grpc_query_request_t build_request(
    const std::string &prompt,
    const std::string &session_name,
    bool auto_approve,
    bool enable_tools,
    volatile int *cancel_flag,
    agnc_grpc_stream_delta_fn stream_delta,
    void *stream_delta_ctx,
    agnc_grpc_permission_ask_fn permission_ask,
    void *permission_ask_ctx)
{
    agnc_grpc_query_request_t request;
    memset(&request, 0, sizeof(request));
    request.prompt = prompt.c_str();
    request.session_name = session_name.empty() ? NULL : session_name.c_str();
    request.auto_approve = auto_approve ? 1 : 0;
    request.enable_tools = enable_tools ? 1 : 0;
    request.cancel_flag = cancel_flag;
    request.stream_delta = stream_delta;
    request.stream_delta_ctx = stream_delta_ctx;
    request.permission_ask = permission_ask;
    request.permission_ask_ctx = permission_ask_ctx;
    return request;
}

static void fill_run_response(const agnc_grpc_query_result_t &result, agnc::v1::RunQueryResponse *response)
{
    response->set_status(agnc_grpc_status_label(result.status));
    if (result.assistant_text != NULL) {
        response->set_assistant_text(result.assistant_text);
    }
    if (result.error_message != NULL) {
        response->set_error_message(result.error_message);
    }
    if (result.usage_prompt_tokens >= 0) {
        response->set_usage_prompt_tokens(result.usage_prompt_tokens);
    }
    if (result.usage_completion_tokens >= 0) {
        response->set_usage_completion_tokens(result.usage_completion_tokens);
    }
    if (result.usage_total_tokens >= 0) {
        response->set_usage_total_tokens(result.usage_total_tokens);
    }
}

class AgentServiceImpl final : public agnc::v1::Agent::Service {
public:
    grpc::Status Health(
        grpc::ServerContext *context,
        const agnc::v1::HealthRequest *request,
        agnc::v1::HealthResponse *response) override
    {
        (void)context;
        (void)request;
        response->set_version(AGNC_VERSION_STRING);
        return grpc::Status::OK;
    }

    grpc::Status RunQuery(
        grpc::ServerContext *context,
        const agnc::v1::RunQueryRequest *request,
        agnc::v1::RunQueryResponse *response) override
    {
        agnc_grpc_query_result_t result;
        agnc_grpc_query_request_t bridge_request;
        PermissionBridgeCtx permission_ctx;
        std::string query_id = request->query_id();
        auto cancel_flag = g_cancel_registry.register_query(query_id);
        auto permission_gate = g_permission_registry.attach(query_id);

        (void)context;

        permission_ctx.gate = permission_gate;
        permission_ctx.stream = NULL;

        bridge_request = build_request(
            request->prompt(),
            request->session_name(),
            request->auto_approve(),
            request->enable_tools(),
            &cancel_flag->value,
            NULL,
            NULL,
            permission_gate != NULL ? grpc_permission_cb : NULL,
            permission_gate != NULL ? &permission_ctx : NULL);

        agnc_grpc_run_query(&bridge_request, &result);
        g_cancel_registry.unregister_query(query_id);
        g_permission_registry.detach(query_id);

        fill_run_response(result, response);
        agnc_grpc_query_result_free(&result);
        return grpc::Status::OK;
    }

    grpc::Status StreamQuery(
        grpc::ServerContext *context,
        const agnc::v1::StreamQueryRequest *request,
        grpc::ServerWriter<agnc::v1::StreamQueryResponse> *writer) override
    {
        agnc_grpc_query_result_t result;
        agnc_grpc_query_request_t bridge_request;
        StreamBridgeCtx stream_ctx;
        PermissionBridgeCtx permission_ctx;
        agnc::v1::StreamQueryResponse done_chunk;
        std::string query_id = request->query_id();
        auto cancel_flag = g_cancel_registry.register_query(query_id);
        auto permission_gate = g_permission_registry.attach(query_id);

        (void)context;

        stream_ctx.writer = writer;
        stream_ctx.permission_gate = permission_gate;
        stream_ctx.query_id = query_id;

        permission_ctx.gate = permission_gate;
        permission_ctx.stream = &stream_ctx;

        bridge_request = build_request(
            request->prompt(),
            request->session_name(),
            request->auto_approve(),
            request->enable_tools(),
            &cancel_flag->value,
            grpc_stream_delta_cb,
            &stream_ctx,
            permission_gate != NULL ? grpc_permission_cb : NULL,
            permission_gate != NULL ? &permission_ctx : NULL);

        agnc_grpc_run_query(&bridge_request, &result);
        g_cancel_registry.unregister_query(query_id);
        g_permission_registry.detach(query_id);

        done_chunk.set_done(true);
        done_chunk.set_status(agnc_grpc_status_label(result.status));
        if (result.usage_prompt_tokens >= 0) {
            done_chunk.set_usage_prompt_tokens(result.usage_prompt_tokens);
        }
        if (result.usage_completion_tokens >= 0) {
            done_chunk.set_usage_completion_tokens(result.usage_completion_tokens);
        }
        if (result.usage_total_tokens >= 0) {
            done_chunk.set_usage_total_tokens(result.usage_total_tokens);
        }
        if (result.error_message != NULL) {
            done_chunk.set_error_message(result.error_message);
        }
        writer->Write(done_chunk);

        agnc_grpc_query_result_free(&result);
        return grpc::Status::OK;
    }

    grpc::Status CancelQuery(
        grpc::ServerContext *context,
        const agnc::v1::CancelQueryRequest *request,
        agnc::v1::CancelQueryResponse *response) override
    {
        (void)context;
        response->set_cancelled(g_cancel_registry.cancel(request->query_id()) ? true : false);
        return grpc::Status::OK;
    }

    grpc::Status RespondPermission(
        grpc::ServerContext *context,
        const agnc::v1::PermissionResponse *request,
        agnc::v1::PermissionAck *response) override
    {
        (void)context;
        response->set_accepted(g_permission_registry.respond(request->query_id(), request->allowed()));
        return grpc::Status::OK;
    }
};

std::string default_listen_address(const char *listen_address)
{
    if (listen_address != NULL && listen_address[0] != '\0') {
        return listen_address;
    }
    return "127.0.0.1:50051";
}

} /* namespace */

extern "C" int agnc_cli_run_serve(const char *listen_address)
{
    const std::string address = default_listen_address(listen_address);
    AgentServiceImpl service;
    grpc::ServerBuilder builder;

    grpc::EnableDefaultHealthCheckService(true);
    grpc::reflection::InitProtoReflectionServerBuilderPlugin();

    builder.AddListeningPort(address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);
    std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
    if (server == NULL) {
        fprintf(stderr, "agnc: gagal bind gRPC ke %s\n", address.c_str());
        return 1;
    }

    printf("agnc serve listening on %s (Ctrl+C to stop)\n", address.c_str());
    server->Wait();
    return 0;
}
