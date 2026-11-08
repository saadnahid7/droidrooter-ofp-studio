#include "ofp.h"
#include <wchar.h>
static volatile LONG cancel_flag;
static int cancel_after;
static void progress(void *ctx, int i, uint64_t done, uint64_t total, const wchar_t *text) {
    (void)ctx;
    (void)i;
    (void)total;
    (void)text;
    if ((cancel_after == 1 && done) || (cancel_after == 2 && i == -2))
        InterlockedExchange(&cancel_flag, 1);
}
int wmain(int argc, wchar_t **argv) {
    if (argc < 2)
        return 2;
    if (!wcscmp(argv[1], L"--ownership") && argc > 2) {
        wchar_t partial[OFP_PATH], target[OFP_PATH], fresh[OFP_PATH], error[OFP_ERROR];
        swprintf(partial, OFP_PATH, L"%ls\\owned.partial", argv[2]);
        swprintf(target, OFP_PATH, L"%ls\\protected.txt", argv[2]);
        swprintf(fresh, OFP_PATH, L"%ls\\published.txt", argv[2]);
        HANDLE original = CreateFileW(target, GENERIC_WRITE, 0, NULL, CREATE_NEW, 0, NULL);
        DWORD n;
        WriteFile(original, "protected", 9, &n, NULL);
        CloseHandle(original);
        HANDLE h = CreateFileW(partial, GENERIC_READ | GENERIC_WRITE | DELETE, 0, NULL, CREATE_NEW,
                               0, NULL);
        if (h == INVALID_HANDLE_VALUE)
            return 3;
        HANDLE attacker = CreateFileW(partial, GENERIC_WRITE | DELETE,
                                      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                                      OPEN_EXISTING, 0, NULL);
        if (attacker != INVALID_HANDLE_VALUE) {
            CloseHandle(attacker);
            CloseHandle(h);
            return 4;
        }
        if (ofp_finalize_output(h, target, error)) {
            CloseHandle(h);
            return 5;
        }
        if (!ofp_finalize_output(h, fresh, error)) {
            CloseHandle(h);
            return 6;
        }
        ofp_discard_output(h);
        CloseHandle(h);
        return GetFileAttributesW(fresh) == INVALID_FILE_ATTRIBUTES &&
                       GetFileAttributesW(partial) == INVALID_FILE_ATTRIBUTES &&
                       GetFileAttributesW(target) != INVALID_FILE_ATTRIBUTES
                   ? 0
                   : 7;
    }
    OfpArchive a = {0};
    wchar_t error[OFP_ERROR] = {0};
    if (!ofp_inspect(argv[1], &a, error)) {
        fwprintf(stderr, L"%ls\n", error);
        return 1;
    }
    wprintf(L"format=%d files=%d logical=%d\n", a.format, a.count, a.logical_count);
    for (int i = 0; i < a.count; i++)
        wprintf(L"%ls\t%llu\n", a.entries[i].name, (unsigned long long)a.entries[i].size);
    if (argc > 2) {
        OfpOptions o = {1, 1, 0};
        if (argc > 3 && wcscmp(argv[3], L"all") && wcscmp(argv[3], L"cancel") &&
            wcscmp(argv[3], L"scatter") && wcscmp(argv[3], L"cancel-metadata"))
            for (int i = 0; i < a.count; i++)
                a.entries[i].selected = !wcscmp(argv[3], a.entries[i].name);
        if (argc > 3 && !wcscmp(argv[3], L"cancel"))
            cancel_after = 1;
        if (argc > 3 && !wcscmp(argv[3], L"cancel-metadata"))
            cancel_after = 2;
        if (argc > 3 && !wcscmp(argv[3], L"scatter"))
            o.scatter = 1;
        if (argc > 4 && !wcscmp(argv[4], L"fast")) {
            o.verify = 0;
            o.manifest = 0;
        }
        int ok = ofp_extract(&a, argv[2], &o, progress, NULL, &cancel_flag, error);
        if (!ok)
            fwprintf(stderr, L"%ls\n", error);
        ofp_free(&a);
        return ok ? 0 : 1;
    }
    ofp_free(&a);
    return 0;
}
