#include "magisk.h"
#include <wchar.h>
static void *token;
static int cancel_write;
static void progress(void *ctx, int stage, uint64_t done, uint64_t total, const wchar_t *text) {
    (void)ctx;
    (void)done;
    (void)total;
    (void)text;
    if (cancel_write && stage == 2)
        dr_cancel_set(token);
}
int wmain(int argc, wchar_t **argv) {
    if (argc < 4)
        return 2;
    wchar_t error[OFP_ERROR] = {0};
    token = dr_cancel_new();
    if (argc > 4 && !wcscmp(argv[4], L"cancel"))
        dr_cancel_set(token);
    if (argc > 4 && !wcscmp(argv[4], L"cancel-write"))
        cancel_write = 1;
    int ok = ofp_patch_image(argv[1], argv[2], argv[3], token, progress, NULL, error);
    if (!ok)
        fwprintf(stderr, L"%ls\n", error);
    dr_cancel_free(token);
    return ok ? 0 : 1;
}
