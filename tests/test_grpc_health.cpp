/*
 * test_grpc_health.cpp
 *
 * Integration test in-process: Health RPC via stub Agent service + generated proto.
 */

#include "agnc/v1/agent.grpc.pb.h"

#include <grpcpp/grpcpp.h>

#include <cassert>
#include <memory>
#include <string>

namespace {

class HealthStubService final : public agnc::v1::Agent::Service {
public:
    grpc::Status Health(
        grpc::ServerContext *context,
        const agnc::v1::HealthRequest *request,
        agnc::v1::HealthResponse *response) override
    {
        (void)context;
        (void)request;
        response->set_version("test-health");
        return grpc::Status::OK;
    }
};

} /* namespace */

int main()
{
    HealthStubService service;
    grpc::ServerBuilder builder;
    int selected_port = 0;

    builder.AddListeningPort("127.0.0.1:0", grpc::InsecureServerCredentials(), &selected_port);
    builder.RegisterService(&service);
    std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
    if (server == NULL || selected_port <= 0) {
        return 1;
    }

    const std::string target = "127.0.0.1:" + std::to_string(selected_port);
    auto channel = grpc::CreateChannel(target, grpc::InsecureChannelCredentials());
    auto stub = agnc::v1::Agent::NewStub(channel);

    grpc::ClientContext context;
    agnc::v1::HealthRequest request;
    agnc::v1::HealthResponse response;
    grpc::Status status = stub->Health(&context, request, &response);

    assert(status.ok());
    assert(response.version() == "test-health");

    server->Shutdown();
    return 0;
}
