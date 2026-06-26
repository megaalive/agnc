/*
 * permissions.c
 *
 * Prompt permission interaktif untuk eksekusi shell.
 */

#include "agnc/permissions.h"

#include <stdio.h>
#include <string.h>

agnc_status_t agnc_permission_ask_shell(const char *command, int *allowed)
{
    char answer[32];

    if (allowed == NULL) {
        return AGNC_STATUS_INVALID_ARGUMENT;
    }

    *allowed = 0;

    /* stderr agar tidak tercampur jawaban model di stdout. */
    fprintf(stderr, "agnc: Allow shell command? [y/N] %s\n", command != NULL ? command : "(empty)");
    fflush(stderr);

    if (fgets(answer, sizeof(answer), stdin) == NULL) {
        return AGNC_STATUS_OK;
    }

    *allowed = (answer[0] == 'y' || answer[0] == 'Y');
    return AGNC_STATUS_OK;
}
