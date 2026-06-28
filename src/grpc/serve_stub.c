/*
 * serve_stub.c
 *
 * Placeholder saat agnc dibuild tanpa gRPC (AGNC_BUILD_GRPC=OFF).
 */

#include <stdio.h>

int agnc_cli_run_serve(const char *listen_address)
{
    (void)listen_address;
    fprintf(stderr, "agnc: subcommand serve membutuhkan build gRPC (rebuild: .\\scripts\\build.ps1 release -Grpc)\n");
    return 1;
}
