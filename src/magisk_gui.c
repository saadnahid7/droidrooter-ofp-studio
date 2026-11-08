#include "magisk.h"
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <uxtheme.h>
#include <dwmapi.h>
#include <wchar.h>
#include <stdlib.h>
#define PATCH_DONE (WM_APP + 20)
enum {
    P_INPUT = 301,
    P_BROWSE,
    P_USE,
    P_OUTPUT,
    P_SAVE,
    P_PREINIT,
    P_PATCH,
    P_CANCEL,
    P_OPEN,
    P_ADVANCED
};
typedef struct {
    HWND w, parent, input, output, preinit, summary, status, log, bar, controls[10], labels[6];
    HFONT font, title;
    HBRUSH bg, field;
    int dpi, busy, close_after, success, stage;
    HANDLE thread;
    void *cancel;
    CRITICAL_SECTION lock;
    uint64_t done, total;
    wchar_t phase[512], error[OFP_ERROR], source[OFP_PATH], dest[OFP_PATH], partition[129],
        suggested[OFP_PATH];
} PatchUI;
static PatchUI *pui;
static int pp(int n) {
    return MulDiv(n, pui->dpi, 96);
}
static HWND make(const wchar_t *cls, const wchar_t *caption, DWORD style, int id) {
    HWND h = CreateWindowExW(
        !wcscmp(cls, L"EDIT") ? WS_EX_CLIENTEDGE : 0, cls, caption,
        WS_CHILD | WS_VISIBLE |
            (!wcscmp(cls, L"BUTTON") ? ((style & ~BS_TYPEMASK) | BS_OWNERDRAW) : style),
        0, 0, 0, 0, pui->w, (HMENU)(INT_PTR)id, GetModuleHandle(NULL), NULL);
    SendMessageW(h, WM_SETFONT, (WPARAM)pui->font, TRUE);
    if (id >= P_INPUT && id <= P_ADVANCED)
        pui->controls[id - P_INPUT] = h;
    return h;
}
static void move(HWND h, int x, int y, int w, int height) {
    MoveWindow(h, pp(x), pp(y), pp(w), pp(height), TRUE);
}
static void patch_layout(void) {
    RECT r;
    GetClientRect(pui->w, &r);
    int w = MulDiv(r.right, 96, pui->dpi), h = MulDiv(r.bottom, 96, pui->dpi);
    move(pui->labels[0], 28, 92, w - 56, 20);
    move(pui->input, 28, 118, w - 220, 32);
    move(pui->controls[P_BROWSE - P_INPUT], w - 178, 118, 150, 32);
    move(pui->controls[P_USE - P_INPUT], 28, 160, 180, 29);
    move(pui->summary, 220, 164, w - 248, 24);
    move(pui->labels[1], 28, 210, w - 56, 20);
    move(pui->output, 28, 236, w - 220, 32);
    move(pui->controls[P_SAVE - P_INPUT], w - 178, 236, 150, 32);
    move(pui->controls[P_ADVANCED - P_INPUT], 28, 290, 350, 30);
    move(pui->labels[2], 28, 323, 230, 20);
    move(pui->preinit, 28, 348, 230, 32);
    move(pui->labels[3], 280, 342, w - 308, 45);
    move(pui->labels[4], 28, 405, w - 56, 42);
    move(pui->status, 28, h - 204, w - 56, 24);
    move(pui->bar, 28, h - 174, w - 56, 12);
    move(pui->log, 28, h - 151, w - 56, 84);
    move(pui->controls[P_OPEN - P_INPUT], 28, h - 48, 160, 32);
    move(pui->controls[P_CANCEL - P_INPUT], w - 306, h - 48, 110, 32);
    move(pui->controls[P_PATCH - P_INPUT], w - 184, h - 48, 156, 32);
    InvalidateRect(pui->w, NULL, TRUE);
}
static void patch_enabled(void) {
    int ids[] = {P_INPUT, P_BROWSE, P_OUTPUT, P_SAVE, P_PREINIT, P_ADVANCED, P_PATCH};
    for (size_t i = 0; i < sizeof(ids) / sizeof(ids[0]); i++)
        EnableWindow(pui->controls[ids[i] - P_INPUT], !pui->busy);
    EnableWindow(pui->controls[P_USE - P_INPUT], !pui->busy && *pui->suggested);
    EnableWindow(pui->controls[P_CANCEL - P_INPUT], pui->busy);
    EnableWindow(pui->controls[P_OPEN - P_INPUT], !pui->busy && pui->success);
}
static void preview(void) {
    if (pui->busy)
        return;
    wchar_t path[OFP_PATH];
    GetWindowTextW(pui->input, path, OFP_PATH);
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    unsigned char header[64];
    LARGE_INTEGER n;
    wchar_t info[256] = L"Select a stock boot or init_boot image.";
    if (h != INVALID_HANDLE_VALUE) {
        if (GetFileSizeEx(h, &n) && ofp_read(h, 0, header, 64) && !memcmp(header, "ANDROID!", 8))
            swprintf(info, 256, L"Android header v%lu  |  %.2f MiB",
                     (unsigned long)ofp_u32(header + 40), n.QuadPart / 1048576.0);
        else
            wcscpy(info, L"Not a standard Android boot image.");
        CloseHandle(h);
    }
    SetWindowTextW(pui->summary, info);
}
static void set_source(const wchar_t *path) {
    SetWindowTextW(pui->input, path);
    wchar_t out[OFP_PATH];
    wcsncpy(out, path, OFP_PATH - 1);
    out[OFP_PATH - 1] = 0;
    wchar_t *slash = wcsrchr(out, L'\\');
    if (slash) {
        if (slash == out + 2)
            slash[1] = 0;
        else
            *slash = 0;
    } else
        out[0] = 0;
    SetWindowTextW(pui->output, out);
    preview();
}
static void browse_image(void) {
    wchar_t path[OFP_PATH] = L"";
    OPENFILENAMEW f = {0};
    f.lStructSize = sizeof(f);
    f.hwndOwner = pui->w;
    f.lpstrFilter = L"Android images (*.img)\0*.img\0All files\0*.*\0";
    f.lpstrFile = path;
    f.nMaxFile = OFP_PATH;
    f.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    f.lpstrTitle = L"Select stock boot.img or init_boot.img";
    if (GetOpenFileNameW(&f))
        set_source(path);
}
static void save_image(void) {
    IFileDialog *dlg = NULL;
    if (SUCCEEDED(CoCreateInstance(&CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER,
                                   &IID_IFileDialog, (void **)&dlg))) {
        IFileDialog_SetOptions(dlg, FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
        IFileDialog_SetTitle(dlg, L"Choose patched image output folder");
        if (SUCCEEDED(IFileDialog_Show(dlg, pui->w))) {
            IShellItem *item = NULL;
            if (SUCCEEDED(IFileDialog_GetResult(dlg, &item))) {
                PWSTR path = NULL;
                if (SUCCEEDED(IShellItem_GetDisplayName(item, SIGDN_FILESYSPATH, &path))) {
                    SetWindowTextW(pui->output, path);
                    CoTaskMemFree(path);
                }
                IShellItem_Release(item);
            }
        }
        IFileDialog_Release(dlg);
    }
}
static void phase(void *ctx, int stage, uint64_t done, uint64_t total, const wchar_t *s) {
    (void)ctx;
    EnterCriticalSection(&pui->lock);
    pui->stage = stage;
    pui->done = done;
    pui->total = total;
    wcsncpy(pui->phase, s, 511);
    pui->phase[511] = 0;
    LeaveCriticalSection(&pui->lock);
}
static DWORD WINAPI patch_worker(void *unused) {
    (void)unused;
    pui->success = ofp_patch_image(pui->source, pui->dest, pui->partition, pui->cancel, phase, NULL,
                                   pui->error);
    PostMessageW(pui->w, PATCH_DONE, 0, 0);
    return 0;
}
static void start_patch(void) {
    if (pui->busy)
        return;
    GetWindowTextW(pui->input, pui->source, OFP_PATH);
    wchar_t folder[OFP_PATH];
    GetWindowTextW(pui->output, folder, OFP_PATH);
    pui->partition[0] = 0;
    if (SendMessageW(pui->controls[P_ADVANCED - P_INPUT], BM_GETCHECK, 0, 0) == BST_CHECKED)
        GetWindowTextW(pui->preinit, pui->partition, 129);
    if (!*pui->source || !*folder) {
        MessageBoxW(pui->w, L"Select a stock image and an output folder.", L"Patcher inputs",
                    MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (wcslen(folder) + 64 >= OFP_PATH) {
        MessageBoxW(pui->w, L"Choose a shorter output folder path.", L"Output folder", MB_OK);
        return;
    }
    // Automatic filename; keep previous images and their reports intact.
    for (unsigned n = 0; n < 10000; n++) {
        if (!n)
            swprintf(pui->dest, OFP_PATH, L"%ls\\magisk_patched_boot.img", folder);
        else
            swprintf(pui->dest, OFP_PATH, L"%ls\\magisk_patched_boot_%u.img", folder, n);
        wchar_t report[OFP_PATH], partial[OFP_PATH], report_partial[OFP_PATH];
        swprintf(report, OFP_PATH, L"%ls.provenance.json", pui->dest);
        swprintf(partial, OFP_PATH, L"%ls.patch.partial", pui->dest);
        swprintf(report_partial, OFP_PATH, L"%ls.patch.partial", report);
        if (GetFileAttributesW(pui->dest) == INVALID_FILE_ATTRIBUTES &&
            GetFileAttributesW(report) == INVALID_FILE_ATTRIBUTES &&
            GetFileAttributesW(partial) == INVALID_FILE_ATTRIBUTES &&
            GetFileAttributesW(report_partial) == INVALID_FILE_ATTRIBUTES)
            break;
        if (n == 9999) {
            MessageBoxW(pui->w, L"Choose another output folder.", L"Output folder is full", MB_OK);
            return;
        }
    }
    pui->cancel = dr_cancel_new();
    if (!pui->cancel) {
        MessageBoxW(pui->w, L"Cannot allocate patch operation.", L"Patcher", MB_ICONERROR);
        return;
    }
    pui->busy = 1;
    pui->success = 0;
    pui->close_after = 0;
    pui->error[0] = 0;
    pui->stage = 0;
    pui->done = pui->total = 0;
    wcscpy(pui->phase, L"Validating patch inputs...");
    SetWindowTextW(pui->log,
                   L"Preparing a separate patched image. The original image will be preserved.");
    patch_enabled();
    SetWindowLongPtrW(pui->bar, GWL_STYLE, GetWindowLongPtrW(pui->bar, GWL_STYLE) | PBS_MARQUEE);
    SendMessageW(pui->bar, PBM_SETMARQUEE, TRUE, 40);
    pui->thread = CreateThread(NULL, 0, patch_worker, NULL, 0, NULL);
    if (!pui->thread) {
        pui->busy = 0;
        dr_cancel_free(pui->cancel);
        pui->cancel = NULL;
        SetWindowTextW(pui->status, L"Cannot start patch worker.");
        patch_enabled();
    }
}
static LRESULT CALLBACK patch_proc(HWND w, UINT m, WPARAM wp, LPARAM lp) {
    switch (m) {
    case WM_CREATE: {
        pui->w = w;
        pui->dpi = (int)GetDpiForWindow(w);
        pui->bg = CreateSolidBrush(RGB(14, 20, 31));
        pui->field = CreateSolidBrush(RGB(22, 31, 45));
        pui->font = CreateFontW(-pp(14), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                                CLEARTYPE_QUALITY, 0, L"Segoe UI");
        pui->title = CreateFontW(-pp(26), 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                                 CLEARTYPE_QUALITY, 0, L"Segoe UI");
        pui->labels[0] = make(L"STATIC", L"STOCK BOOT / INIT_BOOT IMAGE", 0, 0);
        pui->input = make(L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, P_INPUT);
        make(L"BUTTON", L"Browse image", BS_PUSHBUTTON | WS_TABSTOP, P_BROWSE);
        make(L"BUTTON", L"Use extracted image", BS_PUSHBUTTON | WS_TABSTOP, P_USE);
        pui->summary = make(L"STATIC", L"Select a stock Android image.", 0, 0);
        pui->labels[1] = make(L"STATIC", L"PATCHED IMAGE OUTPUT FOLDER", 0, 0);
        pui->output = make(L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, P_OUTPUT);
        make(L"BUTTON", L"Choose folder", BS_PUSHBUTTON | WS_TABSTOP, P_SAVE);
        pui->labels[2] = make(L"STATIC", L"Pre-init override (optional)", 0, 0);
        pui->preinit = make(L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, P_PREINIT);
        SendMessageW(pui->preinit, EM_SETLIMITTEXT, 128, 0);
        pui->controls[P_ADVANCED - P_INPUT] =
            CreateWindowExW(0, L"BUTTON", L"Advanced: override pre-init partition",
                            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, 0, 0, 0, 0, w,
                            (HMENU)(INT_PTR)P_ADVANCED, GetModuleHandle(NULL), NULL);
        SendMessageW(pui->controls[P_ADVANCED - P_INPUT], WM_SETFONT, (WPARAM)pui->font, TRUE);
        SetWindowTheme(pui->controls[P_ADVANCED - P_INPUT], L"", L"");
        pui->labels[3] = make(
            L"STATIC",
            L"Leave unset for official offline patching behavior. No device partition is guessed.",
            0, 0);
        ShowWindow(pui->labels[2], SW_HIDE);
        ShowWindow(pui->preinit, SW_HIDE);
        ShowWindow(pui->labels[3], SW_HIDE);
        pui->labels[4] =
            make(L"STATIC",
                 L"Keeps verity and encryption settings. Rebuilds appended AVB metadata unsigned. "
                 L"The target must accept modified boot images; this tool does not flash a phone.",
                 0, 0);
        pui->status = make(L"STATIC", L"Ready to prepare a patched image.", 0, 0);
        pui->bar = make(PROGRESS_CLASSW, L"Patch progress", PBS_MARQUEE | PBS_SMOOTH, 0);
        SendMessageW(pui->bar, PBM_SETRANGE32, 0, 1000);
        pui->log = make(L"EDIT", L"", ES_MULTILINE | ES_READONLY | WS_VSCROLL, 0);
        make(L"BUTTON", L"Open output folder", BS_PUSHBUTTON | WS_TABSTOP, P_OPEN);
        make(L"BUTTON", L"Cancel", BS_PUSHBUTTON | WS_TABSTOP, P_CANCEL);
        make(L"BUTTON", L"Patch image", BS_DEFPUSHBUTTON | WS_TABSTOP, P_PATCH);
        SendMessageW(pui->input, EM_SETLIMITTEXT, OFP_PATH - 1, 0);
        SendMessageW(pui->output, EM_SETLIMITTEXT, OFP_PATH - 1, 0);
        BOOL dark = TRUE;
        DwmSetWindowAttribute(w, 20, &dark, sizeof(dark));
        SetTimer(w, 1, 150, NULL);
        patch_enabled();
        return 0;
    }
    case WM_SIZE:
        patch_layout();
        return 0;
    case WM_GETMINMAXINFO:
        ((MINMAXINFO *)lp)->ptMinTrackSize.x = pp(900);
        ((MINMAXINFO *)lp)->ptMinTrackSize.y = pp(730);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(w, &ps);
        RECT r;
        GetClientRect(w, &r);
        FillRect(dc, &r, pui->bg);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(231, 238, 247));
        SelectObject(dc, pui->title);
        TextOutW(dc, pp(28), pp(25), L"Magisk image patcher", 20);
        SelectObject(dc, pui->font);
        SetTextColor(dc, RGB(61, 219, 188));
        TextOutW(dc, pp(28), pp(64), L"Bundled Magisk 31.1  /  ARM64",
                 lstrlenW(L"Bundled Magisk 31.1  /  ARM64"));
        EndPaint(w, &ps);
        return 0;
    }
    case WM_DRAWITEM: {
        DRAWITEMSTRUCT *d = (DRAWITEMSTRUCT *)lp;
        int primary = d->CtlID == P_PATCH;
        int disabled = (d->itemState & ODS_DISABLED) != 0;
        COLORREF color = primary && !disabled ? RGB(61, 219, 188) : RGB(22, 31, 45);
        HBRUSH b = CreateSolidBrush(color);
        FillRect(d->hDC, &d->rcItem, b);
        DeleteObject(b);
        SetBkMode(d->hDC, TRANSPARENT);
        SetTextColor(d->hDC, disabled ? RGB(110, 125, 145)
                                      : (primary ? RGB(10, 16, 26) : RGB(231, 238, 247)));
        SelectObject(d->hDC, pui->font);
        wchar_t caption[128];
        GetWindowTextW(d->hwndItem, caption, 128);
        RECT r = d->rcItem;
        DrawTextW(d->hDC, caption, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        if (d->itemState & ODS_FOCUS) {
            InflateRect(&r, -3, -3);
            DrawFocusRect(d->hDC, &r);
        }
        return TRUE;
    }
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORBTN: {
        HDC dc = (HDC)wp;
        int edit = m == WM_CTLCOLOREDIT || (HWND)lp == pui->log;
        SetTextColor(dc, RGB(231, 238, 247));
        SetBkColor(dc, edit ? RGB(22, 31, 45) : RGB(14, 20, 31));
        return (LRESULT)(edit ? pui->field : pui->bg);
    }
    case WM_TIMER:
        if (pui->busy) {
            EnterCriticalSection(&pui->lock);
            SetWindowTextW(pui->status, pui->phase);
            if (pui->stage == 2 && pui->total) {
                SendMessageW(pui->bar, PBM_SETMARQUEE, FALSE, 0);
                SetWindowLongPtrW(pui->bar, GWL_STYLE,
                                  GetWindowLongPtrW(pui->bar, GWL_STYLE) & ~PBS_MARQUEE);
                SendMessageW(pui->bar, PBM_SETPOS, (WPARAM)((double)pui->done / pui->total * 1000),
                             0);
            }
            LeaveCriticalSection(&pui->lock);
        }
        return 0;
    case WM_COMMAND: {
        int id = LOWORD(wp);
        if (id == P_ADVANCED && !pui->busy) {
            int show =
                SendMessageW(pui->controls[P_ADVANCED - P_INPUT], BM_GETCHECK, 0, 0) == BST_CHECKED
                    ? SW_SHOW
                    : SW_HIDE;
            ShowWindow(pui->labels[2], show);
            ShowWindow(pui->preinit, show);
            ShowWindow(pui->labels[3], show);
        } else if (id == P_BROWSE && !pui->busy)
            browse_image();
        else if (id == P_SAVE && !pui->busy)
            save_image();
        else if (id == P_USE && !pui->busy && *pui->suggested)
            set_source(pui->suggested);
        else if (id == P_INPUT && HIWORD(wp) == EN_KILLFOCUS)
            preview();
        else if (id == P_PATCH && !pui->busy)
            start_patch();
        else if (id == P_CANCEL && pui->busy) {
            dr_cancel_set(pui->cancel);
            SetWindowTextW(pui->status, L"Cancelling patch operation...");
            EnableWindow(pui->controls[P_CANCEL - P_INPUT], FALSE);
        } else if (id == P_OPEN && !pui->busy && pui->success) {
            wchar_t folder[OFP_PATH];
            wcscpy(folder, pui->dest);
            wchar_t *s = wcsrchr(folder, L'\\');
            if (s)
                *s = 0;
            ShellExecuteW(w, L"open", folder, NULL, NULL, SW_SHOWNORMAL);
        }
        return 0;
    }
    case PATCH_DONE:
        WaitForSingleObject(pui->thread, INFINITE);
        CloseHandle(pui->thread);
        pui->thread = NULL;
        pui->busy = 0;
        dr_cancel_free(pui->cancel);
        pui->cancel = NULL;
        SendMessageW(pui->bar, PBM_SETMARQUEE, FALSE, 0);
        SetWindowLongPtrW(pui->bar, GWL_STYLE,
                          GetWindowLongPtrW(pui->bar, GWL_STYLE) & ~PBS_MARQUEE);
        if (pui->success) {
            SetWindowTextW(pui->status, L"Patched image and provenance report saved.");
            SetWindowTextW(pui->log,
                           L"The original image is unchanged. The output was reparsed and its "
                           L"Magisk payload, backup and AVB hash metadata were checked. File "
                           L"preparation succeeded; phone bootability remains untested.");
            SendMessageW(pui->bar, PBM_SETPOS, 1000, 0);
        } else {
            SetWindowTextW(pui->status, L"Patch operation stopped.");
            SetWindowTextW(pui->log, pui->error);
        }
        patch_enabled();
        if (pui->close_after)
            DestroyWindow(w);
        return 0;
    case WM_CLOSE:
        if (pui->busy) {
            if (MessageBoxW(w, L"Cancel patching and close this window?", L"Patch in progress",
                            MB_YESNO | MB_ICONQUESTION) == IDYES) {
                pui->close_after = 1;
                if (pui->busy)
                    dr_cancel_set(pui->cancel);
                else
                    DestroyWindow(w);
            }
            return 0;
        }
        DestroyWindow(w);
        return 0;
    case WM_DESTROY: {
        HWND parent = pui->parent;
        KillTimer(w, 1);
        DeleteObject(pui->font);
        DeleteObject(pui->title);
        DeleteObject(pui->bg);
        DeleteObject(pui->field);
        DeleteCriticalSection(&pui->lock);
        free(pui);
        pui = NULL;
        EnableWindow(parent, TRUE);
        SetForegroundWindow(parent);
        return 0;
    }
    }
    return DefWindowProcW(w, m, wp, lp);
}
void ofp_open_patcher(HWND parent, const wchar_t *source) {
    if (pui) {
        SetForegroundWindow(pui->w);
        return;
    }
    pui = calloc(1, sizeof(*pui));
    if (!pui)
        return;
    pui->parent = parent;
    pui->dpi = 96;
    if (source)
        wcsncpy(pui->suggested, source, OFP_PATH - 1);
    InitializeCriticalSection(&pui->lock);
    WNDCLASSEXW wc = {0};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = patch_proc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = LoadIconW(wc.hInstance, MAKEINTRESOURCEW(101));
    wc.lpszClassName = L"DroidRooterMagiskPatcher";
    RegisterClassExW(&wc);
    HWND w = CreateWindowExW(0, wc.lpszClassName, L"DroidRooter's OFP Studio - Magisk patcher",
                             WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 940, 780, NULL,
                             NULL, wc.hInstance, NULL);
    if (!w) {
        DeleteCriticalSection(&pui->lock);
        free(pui);
        pui = NULL;
        return;
    }
    EnableWindow(parent, FALSE);
    if (source && *source)
        set_source(source);
    ShowWindow(w, SW_SHOW);
    SetForegroundWindow(w);
}
int ofp_patcher_dialog_message(MSG *msg) {
    return pui && IsDialogMessageW(pui->w, msg);
}
int ofp_patcher_is_open(void) {
    if (pui) {
        SetForegroundWindow(pui->w);
        return 1;
    }
    return 0;
}
