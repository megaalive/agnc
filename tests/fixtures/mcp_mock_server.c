/*
 * mcp_mock_server.c
 *
 * Server MCP stdio minimal untuk integration test (tanpa Node/npm).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static long parse_id(const char *line)
{
    const char *cursor = strstr(line, "\"id\"");
    if (cursor == NULL) {
        return 0;
    }

    cursor = strchr(cursor, ':');
    if (cursor == NULL) {
        return 0;
    }

    cursor++;
    while (*cursor == ' ' || *cursor == '\t') {
        cursor++;
    }

    return strtol(cursor, NULL, 10);
}

static void reply_initialize(long id)
{
    const char *test_env = getenv("AGNC_MCP_TEST_ENV");

    if (test_env != NULL && test_env[0] != '\0') {
        printf(
            "{\"jsonrpc\":\"2.0\",\"id\":%ld,\"result\":{"
            "\"protocolVersion\":\"2024-11-05\","
            "\"capabilities\":{\"tools\":{}},"
            "\"serverInfo\":{\"name\":\"env:%s\",\"version\":\"0\"}"
            "}}\n",
            id,
            test_env);
        fflush(stdout);
        return;
    }

    printf(
        "{\"jsonrpc\":\"2.0\",\"id\":%ld,\"result\":{"
        "\"protocolVersion\":\"2024-11-05\","
        "\"capabilities\":{\"tools\":{}},"
        "\"serverInfo\":{\"name\":\"agnc-mock\",\"version\":\"0\"}"
        "}}\n",
        id);
    fflush(stdout);
}

static void reply_tools_list(long id)
{
    printf(
        "{\"jsonrpc\":\"2.0\",\"id\":%ld,\"result\":{"
        "\"tools\":[{\"name\":\"mock_tool\",\"description\":\"mock\",\"inputSchema\":{"
        "\"type\":\"object\",\"properties\":{}}}]"
        "}}\n",
        id);
    fflush(stdout);
}

int main(void)
{
    char line[65536];

    while (fgets(line, sizeof(line), stdin) != NULL) {
        long id = parse_id(line);

        if (strstr(line, "\"initialize\"") != NULL) {
            reply_initialize(id > 0 ? id : 1);
            continue;
        }

        if (strstr(line, "notifications/initialized") != NULL) {
            continue;
        }

        if (strstr(line, "tools/list") != NULL) {
            reply_tools_list(id > 0 ? id : 2);
            continue;
        }

        if (strstr(line, "tools/call") != NULL) {
            printf(
                "{\"jsonrpc\":\"2.0\",\"id\":%ld,\"result\":{"
                "\"content\":[{\"type\":\"text\",\"text\":\"mock ok\"}],"
                "\"isError\":false"
                "}}\n",
                id > 0 ? id : 3);
            fflush(stdout);
            continue;
        }
    }

    return 0;
}
