#include "ofp.h"
#include <commctrl.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <stdlib.h>
#include <string.h>
#include <uxtheme.h>
#include <wchar.h>
#define DONE (WM_APP + 1)
enum {
    ID_INPUT = 101,
    BROWSE,
    OUTPUT,
    FOLDER,
    FILES,
    SEARCH,
    ALL,
    CLEAR,
    MANIFEST,
    VERIFY,
    SCATTER,
    OPEN_AFTER,
    EXTRACT,
    CANCEL,
    OPEN,
    LOG,
    SAVE,
    ABOUT,
    DETAILS,
    NEW_JOB
};
static const COLORREF BG = RGB(14, 20, 31), SIDE = RGB(10, 16, 26), CARD = RGB(22, 31, 45),
                      INK = RGB(231, 238, 247), MUTED = RGB(151, 170, 193),
                      ACCENT = RGB(61, 219, 188);
typedef struct {
    HWND win, controls[160], identity, summary, status, stats, bar, logwin, logedit;
    HFONT font, small, title, bold;
    HBRUSH bg, card;
    int dpi, width, height, busy, inspecting, rebuilding, close_after;
    OfpArchive archive, pending;
    HANDLE thread;
    CRITICAL_SECTION lock;
    volatile LONG cancel;
    wchar_t input[OFP_PATH], dest[OFP_PATH], error[OFP_ERROR], phase[OFP_ERROR], warning[OFP_ERROR],
        logtext[65536];
    OfpOptions options;
    int result, index;
    uint64_t done, total;
    ULONGLONG start;
} App;
static App *app;
static int px(int n) {
    return MulDiv(n, app->dpi, 96);
}
static void text(HWND h, const wchar_t *s) {
    SetWindowTextW(h, s);
}
static void size_text(uint64_t n, wchar_t *s, size_t cap) {
    if (n >= 1073741824ULL)
        swprintf(s, cap, L"%.2f GB", n / 1073741824.0);
    else if (n >= 1048576)
        swprintf(s, cap, L"%.1f MB", n / 1048576.0);
    else if (n >= 1024)
        swprintf(s, cap, L"%.1f KB", n / 1024.0);
    else
        swprintf(s, cap, L"%llu bytes", (unsigned long long)n);
}
static void append_log(const wchar_t *line) {
    size_t n = wcslen(app->logtext), m = wcslen(line);
    if (n + m + 3 < 65536) {
        wcscat(app->logtext, line);
        wcscat(app->logtext, L"\r\n");
    }
    if (app->logedit)
        text(app->logedit, app->logtext);
}
static HWND control(const wchar_t *kind, const wchar_t *caption, DWORD style, int id) {
    HWND h = CreateWindowExW(!wcscmp(kind, L"EDIT") ? WS_EX_CLIENTEDGE : 0, kind, caption,
                             WS_CHILD | WS_VISIBLE | style, 0, 0, 0, 0, app->win,
                             (HMENU)(INT_PTR)id, GetModuleHandle(NULL), NULL);
    SendMessageW(h, WM_SETFONT, (WPARAM)app->font, TRUE);
    if (id < 160)
        app->controls[id] = h;
    return h;
}
static HWND label(const wchar_t *caption) {
    return control(L"STATIC", caption, SS_LEFT, 0);
}
static HWND button(const wchar_t *caption, int id) {
    return control(L"BUTTON", caption, BS_OWNERDRAW | WS_TABSTOP, id);
}
static HWND check(const wchar_t *caption, int id, int on) {
    HWND h = control(L"BUTTON", caption, BS_AUTOCHECKBOX | WS_TABSTOP, id);
    SetWindowTheme(h, L"", L"");
    SendMessageW(h, BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0);
    return h;
}
static void place(HWND h, int x, int y, int w, int ht) {
    MoveWindow(h, px(x), px(y), px(w), px(ht), TRUE);
}
static HWND captions[7];
static void layout(void) {
    RECT r;
    GetClientRect(app->win, &r);
    int w = MulDiv(r.right, 96, app->dpi), h = MulDiv(r.bottom, 96, app->dpi);
    app->width = w;
    app->height = h;
    int x = 228, cw = w - x - 28;
    place(app->identity, x, 89, cw, 25);
    place(captions[0], x + 18, 138, 180, 18);
    place(app->controls[ID_INPUT], x + 18, 163, cw - 174, 32);
    place(app->controls[BROWSE], w - 166, 163, 120, 32);
    place(captions[1], x + 18, 210, 180, 18);
    place(app->controls[OUTPUT], x + 18, 235, cw - 202, 32);
    place(app->controls[FOLDER], w - 194, 235, 148, 32);
    place(captions[2], x, 298, 175, 22);
    place(app->controls[SEARCH], x + 170, 292, cw - 362, 30);
    place(app->controls[ALL], w - 212, 292, 86, 30);
    place(app->controls[CLEAR], w - 118, 292, 90, 30);
    int list_bottom = h - 294;
    place(app->controls[FILES], x, 332, cw, list_bottom - 332);
    place(app->summary, x, list_bottom + 8, cw - 115, 22);
    place(app->controls[DETAILS], w - 128, list_bottom + 2, 100, 30);
    int oy = h - 246;
    place(captions[3], x, oy, 150, 20);
    place(app->controls[MANIFEST], x, oy + 30, cw / 2, 24);
    place(app->controls[VERIFY], x + cw / 2, oy + 30, cw / 2, 24);
    place(app->controls[SCATTER], x, oy + 60, cw / 2, 24);
    place(app->controls[OPEN_AFTER], x + cw / 2, oy + 60, cw / 2, 24);
    place(app->status, x, h - 140, cw, 24);
    place(app->bar, x, h - 110, cw, 10);
    place(app->stats, x, h - 91, cw, 20);
    place(app->controls[EXTRACT], w - 214, h - 52, 186, 36);
    place(app->controls[CANCEL], w - 320, h - 52, 96, 36);
    place(app->controls[OPEN], x, h - 52, 142, 36);
    place(app->controls[NEW_JOB], x + 152, h - 52, 144, 36);
    place(app->controls[LOG], 20, h - 108, 168, 32);
    place(app->controls[SAVE], 20, h - 70, 168, 32);
    place(app->controls[ABOUT], 20, h - 33, 168, 25);
    if (app->logwin && IsWindow(app->logwin)) {
        RECT lr;
        GetClientRect(app->logwin, &lr);
        MoveWindow(app->logedit, 12, 12, lr.right - 24, lr.bottom - 24, TRUE);
    }
    InvalidateRect(app->win, NULL, TRUE);
}
static void enabled(void) {
    int ids[] = {ID_INPUT, BROWSE,   OUTPUT, FOLDER,  FILES,      SEARCH, ALL,
                 CLEAR,    MANIFEST, VERIFY, SCATTER, OPEN_AFTER, NEW_JOB};
    for (size_t i = 0; i < sizeof(ids) / sizeof(ids[0]); i++)
        EnableWindow(app->controls[ids[i]], !app->busy);
    EnableWindow(app->controls[CANCEL], app->busy);
    EnableWindow(app->controls[OPEN], !app->busy && *app->dest);
    EnableWindow(app->controls[DETAILS], !app->busy && app->archive.count);
}
static void totals(void) {
    int n = 0;
    uint64_t total = 0;
    for (int i = 0; i < app->archive.count; i++)
        if (app->archive.entries[i].selected) {
            n++;
            total += app->archive.entries[i].size;
        }
    wchar_t size[64], free_size[64] = L"choose output folder", line[512], path[OFP_PATH];
    size_text(total, size, 64);
    GetWindowTextW(app->controls[OUTPUT], path, OFP_PATH);
    ULARGE_INTEGER available;
    if (*path) {
        wchar_t full[OFP_PATH];
        DWORD len = GetFullPathNameW(path, OFP_PATH, full, NULL);
        if (len && len < OFP_PATH) {
            for (;;) {
                if (GetDiskFreeSpaceExW(full, &available, NULL, NULL)) {
                    size_text(available.QuadPart, free_size, 64);
                    break;
                }
                wchar_t *slash = wcsrchr(full, L'\\');
                if (!slash || slash < full + 2)
                    break;
                if (slash == full + 2) {
                    slash[1] = 0;
                    if (GetDiskFreeSpaceExW(full, &available, NULL, NULL))
                        size_text(available.QuadPart, free_size, 64);
                    break;
                }
                *slash = 0;
            }
        }
    }
    swprintf(line, 512, L"%d of %d selected  /  %ls   |   Free: %ls", n, app->archive.count, size,
             free_size);
    text(app->summary, line);
    EnableWindow(app->controls[EXTRACT], !app->busy && n > 0);
}
static void fill_list(void) {
    app->rebuilding = 1;
    HWND list = app->controls[FILES];
    ListView_DeleteAllItems(list);
    wchar_t filter[512];
    GetWindowTextW(app->controls[SEARCH], filter, 512);
    for (int i = 0; i < app->archive.count; i++) {
        OfpEntry *e = &app->archive.entries[i];
        if (*filter && !StrStrIW(e->name, filter) &&
            !StrStrIW(e->aliases ? e->aliases : e->label, filter))
            continue;
        LVITEMW item = {0};
        item.mask = LVIF_TEXT | LVIF_PARAM;
        item.iItem = ListView_GetItemCount(list);
        item.pszText = e->name;
        item.lParam = i;
        int row = ListView_InsertItem(list, &item);
        wchar_t size[64];
        size_text(e->size, size, 64);
        ListView_SetItemText(list, row, 1, size);
        ListView_SetItemText(list, row, 2, e->aliases ? e->aliases : e->label);
        ListView_SetItemText(list, row, 3, e->completed ? L"Complete" : L"Ready");
        ListView_SetCheckState(list, row, e->selected);
    }
    app->rebuilding = 0;
    totals();
}
static void progress(void *ctx, int index, uint64_t done, uint64_t total, const wchar_t *phase) {
    (void)ctx;
    EnterCriticalSection(&app->lock);
    app->index = index;
    app->done = done;
    app->total = total;
    wcsncpy(app->phase, phase, OFP_ERROR - 1);
    if (index == -1)
        wcsncpy(app->warning, phase, OFP_ERROR - 1);
    LeaveCriticalSection(&app->lock);
}
static DWORD WINAPI worker(void *unused) {
    (void)unused;
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    app->result = app->inspecting
                      ? ofp_inspect_cancel(app->input, &app->pending, &app->cancel, app->error)
                      : ofp_extract(&app->archive, app->dest, &app->options, progress, NULL,
                                    &app->cancel, app->error);
    CoUninitialize();
    PostMessageW(app->win, DONE, 0, 0);
    return 0;
}
static void begin(int inspection) {
    if (app->busy)
        return;
    app->busy = 1;
    app->inspecting = inspection;
    app->done = app->total = 0;
    app->index = -1;
    app->phase[0] = app->warning[0] = app->error[0] = 0;
    InterlockedExchange(&app->cancel, 0);
    app->start = GetTickCount64();
    text(app->status, inspection ? L"Inspecting package..." : L"Preparing selected images...");
    SendMessageW(app->bar, PBM_SETPOS, 0, 0);
    enabled();
    totals();
    app->thread = CreateThread(NULL, 0, worker, NULL, 0, NULL);
    if (!app->thread) {
        app->busy = 0;
        text(app->status, L"Cannot start worker thread.");
        enabled();
        totals();
    }
}
static void inspect_path(const wchar_t *p) {
    if (app->busy)
        return;
    wcsncpy(app->input, p, OFP_PATH - 1);
    text(app->controls[ID_INPUT], p);
    ofp_free(&app->archive);
    ofp_free(&app->pending);
    fill_list();
    text(app->identity, L"Reading package metadata");
    begin(1);
}
static void browse(void) {
    wchar_t path[OFP_PATH] = L"";
    OPENFILENAMEW ofn = {0};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = app->win;
    ofn.lpstrFilter = L"OFP firmware (*.ofp)\0*.ofp\0All files (*.*)\0*.*\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = OFP_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    ofn.lpstrTitle = L"Select OFP firmware";
    if (GetOpenFileNameW(&ofn))
        inspect_path(path);
}
static void folder(void) {
    IFileDialog *dlg = NULL;
    if (SUCCEEDED(CoCreateInstance(&CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER,
                                   &IID_IFileDialog, (void **)&dlg))) {
        IFileDialog_SetOptions(dlg, FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
        IFileDialog_SetTitle(dlg,
                             L"Choose output folder (existing files will never be overwritten)");
        if (SUCCEEDED(IFileDialog_Show(dlg, app->win))) {
            IShellItem *item = NULL;
            if (SUCCEEDED(IFileDialog_GetResult(dlg, &item))) {
                PWSTR path = NULL;
                if (SUCCEEDED(IShellItem_GetDisplayName(item, SIGDN_FILESYSPATH, &path))) {
                    text(app->controls[OUTPUT], path);
                    CoTaskMemFree(path);
                }
                IShellItem_Release(item);
            }
        }
        IFileDialog_Release(dlg);
    }
    totals();
}
static void save_log(void) {
    wchar_t path[OFP_PATH] = L"OFP-Studio-log.txt";
    OPENFILENAMEW ofn = {0};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = app->win;
    ofn.lpstrFilter = L"Text log (*.txt)\0*.txt\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = OFP_PATH;
    ofn.lpstrDefExt = L"txt";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
    if (GetSaveFileNameW(&ofn)) {
        int n = WideCharToMultiByte(CP_UTF8, 0, app->logtext, -1, NULL, 0, NULL, NULL);
        char *bytes = malloc(n);
        if (!bytes)
            return;
        WideCharToMultiByte(CP_UTF8, 0, app->logtext, -1, bytes, n, NULL, NULL);
        HANDLE h =
            CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        DWORD wrote;
        if (h != INVALID_HANDLE_VALUE) {
            WriteFile(h, bytes, (DWORD)n - 1, &wrote, NULL);
            CloseHandle(h);
        } else
            MessageBoxW(app->win, L"Cannot save log to this path.", L"Save log", MB_ICONERROR);
        free(bytes);
    }
}
static LRESULT CALLBACK log_proc(HWND w, UINT m, WPARAM wp, LPARAM lp) {
    if (m == WM_SIZE && app->logedit) {
        MoveWindow(app->logedit, 12, 12, LOWORD(lp) - 24, HIWORD(lp) - 24, TRUE);
        return 0;
    }
    if (m == WM_CLOSE) {
        ShowWindow(w, SW_HIDE);
        return 0;
    }
    return DefWindowProcW(w, m, wp, lp);
}
static void show_log(void) {
    if (!app->logwin) {
        app->logwin = CreateWindowExW(
            WS_EX_TOOLWINDOW, L"OFPStudioLog", L"DroidRooter's OFP Studio - Activity log",
            WS_OVERLAPPEDWINDOW, 180, 160, 820, 520, app->win, NULL, GetModuleHandle(NULL), NULL);
        app->logedit = CreateWindowExW(
            WS_EX_CLIENTEDGE, L"EDIT", app->logtext,
            WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY | WS_VSCROLL | ES_AUTOVSCROLL, 12,
            12, 780, 450, app->logwin, NULL, GetModuleHandle(NULL), NULL);
        SendMessageW(app->logedit, WM_SETFONT, (WPARAM)app->font, TRUE);
    }
    ShowWindow(app->logwin, SW_SHOW);
    SetForegroundWindow(app->logwin);
}
static void file_details(void) {
    HWND list = app->controls[FILES];
    int row = ListView_GetNextItem(list, -1, LVNI_SELECTED);
    if (row < 0) {
        MessageBoxW(app->win, L"Click an image row to view its file details.", L"File details",
                    MB_OK);
        return;
    }
    LVITEMW item = {0};
    item.mask = LVIF_PARAM;
    item.iItem = row;
    ListView_GetItem(list, &item);
    OfpEntry *e = &app->archive.entries[item.lParam];
    wchar_t message[2048];
    swprintf(
        message, 2048,
        L"%ls\n\nPartition / section: %.600ls\nOutput bytes: %llu\nSource offset: "
        L"0x%llX\nEncrypted "
        L"bytes: %llu\n\nVendor SHA-256: %hs\nVendor MD5 (prefix): %hs\nGenerated SHA-256: %hs",
        e->name, e->aliases ? e->aliases : e->label, (unsigned long long)e->size,
        (unsigned long long)e->offset, (unsigned long long)e->encrypted,
        *e->vendor_sha ? e->vendor_sha : "not provided",
        *e->vendor_md5 ? e->vendor_md5 : "not provided", *e->sha256 ? e->sha256 : "not calculated");
    MessageBoxW(app->win, message, L"Image details", MB_OK);
}
static void draw_text(HDC dc, const wchar_t *s, int x, int y, int w, int h, HFONT font,
                      COLORREF color) {
    RECT r = {px(x), px(y), px(x + w), px(y + h)};
    SelectObject(dc, font);
    SetTextColor(dc, color);
    SetBkMode(dc, TRANSPARENT);
    DrawTextW(dc, s, -1, &r, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
}
static void rounded(HDC dc, int x, int y, int w, int h, COLORREF color) {
    HBRUSH b = CreateSolidBrush(color);
    HGDIOBJ oldb = SelectObject(dc, b), oldp = SelectObject(dc, GetStockObject(NULL_PEN));
    RoundRect(dc, px(x), px(y), px(x + w), px(y + h), px(14), px(14));
    SelectObject(dc, oldp);
    SelectObject(dc, oldb);
    DeleteObject(b);
}
static void paint(void) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(app->win, &ps);
    RECT r;
    GetClientRect(app->win, &r);
    FillRect(dc, &r, app->bg);
    RECT side = {0, 0, px(208), r.bottom};
    HBRUSH b = CreateSolidBrush(SIDE);
    FillRect(dc, &side, b);
    DeleteObject(b);
    DrawIconEx(dc, px(20), px(25), LoadIconW(GetModuleHandle(NULL), MAKEINTRESOURCEW(101)), px(52),
               px(52), 0, NULL, DI_NORMAL);
    draw_text(dc, L"DroidRooter", 82, 31, 120, 25, app->bold, INK);
    draw_text(dc, L"OFP STUDIO", 82, 57, 120, 22, app->small, MUTED);

    draw_text(dc, L"01   Choose package", 20, 158, 174, 30, app->font,
              app->archive.count ? ACCENT : INK);
    draw_text(dc, L"02   Select images", 20, 207, 174, 30, app->font, INK);
    draw_text(dc, L"03   Extract & verify", 20, 256, 174, 30, app->font, INK);

    draw_text(dc, L"DroidRooter's OFP Studio", 228, 28, app->width - 260, 42, app->title, INK);
    rounded(dc, 228, 126, app->width - 256, 154, CARD);
    EndPaint(app->win, &ps);
}
static void draw_button(DRAWITEMSTRUCT *d) {
    int id = (int)d->CtlID;
    COLORREF c = id == EXTRACT ? ACCENT : CARD, fg = id == EXTRACT ? SIDE : INK;
    if (d->itemState & ODS_DISABLED) {
        c = CARD;
        fg = RGB(100, 117, 139);
    }
    if (d->itemState & ODS_SELECTED)
        c = RGB(45, 123, 117);
    HBRUSH b = CreateSolidBrush(c);
    FillRect(d->hDC, &d->rcItem, b);
    DeleteObject(b);
    SetBkMode(d->hDC, TRANSPARENT);
    SetTextColor(d->hDC, fg);
    SelectObject(d->hDC, app->font);
    wchar_t caption[128];
    GetWindowTextW(d->hwndItem, caption, 128);
    RECT r = d->rcItem;
    DrawTextW(d->hDC, caption, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    if (d->itemState & ODS_FOCUS) {
        InflateRect(&r, -3, -3);
        DrawFocusRect(d->hDC, &r);
    }
}
static void tick(void) {
    if (!app->busy)
        return;
    uint64_t done, total;
    int index;
    wchar_t phase[OFP_ERROR];
    EnterCriticalSection(&app->lock);
    done = app->done;
    total = app->total;
    index = app->index;
    wcscpy(phase, app->phase);
    LeaveCriticalSection(&app->lock);
    if (app->inspecting) {
        text(app->stats, L"Validating metadata, keys and source ranges...");
        return;
    }
    wchar_t line[1200], have[64], all[64];
    size_text(done, have, 64);
    size_text(total, all, 64);
    double seconds = (GetTickCount64() - app->start) / 1000.0;
    double rate = seconds > 0 ? done / seconds : 0;
    uint64_t eta = rate > 0 ? (uint64_t)((total - done) / rate) : 0;
    swprintf(line, 1200, L"%ls%ls%ls", *phase ? phase : L"Preparing", index >= 0 ? L"  -  " : L"",
             index >= 0 && index < app->archive.count ? app->archive.entries[index].name : L"");
    text(app->status, line);
    swprintf(line, 1200,
             L"%ls / %ls  |  %.1f MB/s  |  %llu:%02llu elapsed  |  ~%llu:%02llu remaining", have,
             all, rate / 1048576.0, (unsigned long long)(seconds / 60),
             (unsigned long long)((uint64_t)seconds % 60), (unsigned long long)(eta / 60),
             (unsigned long long)(eta % 60));
    text(app->stats, line);
    SendMessageW(app->bar, PBM_SETPOS, total ? (WPARAM)((double)done / total * 1000) : 0, 0);
}
static LRESULT CALLBACK window_proc(HWND w, UINT m, WPARAM wp, LPARAM lp) {
    switch (m) {
    case WM_CREATE: {
        app->win = w;
        app->dpi = (int)GetDpiForWindow(w);
        app->bg = CreateSolidBrush(BG);
        app->card = CreateSolidBrush(CARD);
        app->font = CreateFontW(-px(14), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                                CLEARTYPE_QUALITY, 0, L"Segoe UI");
        app->small = CreateFontW(-px(12), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                                 CLEARTYPE_QUALITY, 0, L"Segoe UI");
        app->bold = CreateFontW(-px(18), 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                                CLEARTYPE_QUALITY, 0, L"Segoe UI");
        app->title = CreateFontW(-px(29), 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                                 CLEARTYPE_QUALITY, 0, L"Segoe UI");
        app->identity = label(L"Select an OFP to inspect its contents.");
        captions[0] = label(L"OFP FIRMWARE");
        control(L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, ID_INPUT);
        button(L"Browse OFP", BROWSE);
        captions[1] = label(L"OUTPUT FOLDER");
        control(L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, OUTPUT);
        button(L"Choose output folder", FOLDER);
        captions[2] = label(L"Choose your images");
        control(L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, SEARCH);
        SendMessageW(app->controls[SEARCH], EM_SETCUEBANNER, TRUE,
                     (LPARAM)L"Search images or partitions");
        button(L"Select all", ALL);
        button(L"Clear selection", CLEAR);
        HWND list =
            control(WC_LISTVIEWW, L"Images", LVS_REPORT | LVS_SHOWSELALWAYS | WS_TABSTOP, FILES);
        ListView_SetExtendedListViewStyle(list, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT |
                                                    LVS_EX_DOUBLEBUFFER);
        ListView_SetBkColor(list, CARD);
        ListView_SetTextBkColor(list, CARD);
        ListView_SetTextColor(list, INK);
        SetWindowTheme(list, L"DarkMode_Explorer", NULL);
        const wchar_t *heads[] = {L"Image / file", L"Size", L"Partition / section", L"State"};
        int widths[] = {300, 100, 165, 88};
        for (int i = 0; i < 4; i++) {
            LVCOLUMNW c = {0};
            c.mask = LVCF_TEXT | LVCF_WIDTH;
            c.pszText = (LPWSTR)heads[i];
            c.cx = px(widths[i]);
            ListView_InsertColumn(list, i, &c);
        }
        app->summary = label(L"No images selected");
        button(L"File details", DETAILS);
        captions[3] = label(L"EXTRACTION OPTIONS");
        check(L"Create SHA-256 manifest", MANIFEST, 1);
        check(L"Verify written files (extra disk reads)", VERIFY, 0);
        check(L"Create SP Flash Tool eMMC scatter", SCATTER, 0);
        check(L"Open output folder when finished", OPEN_AFTER, 1);
        app->status = label(L"Ready when you are.");
        app->stats = label(L"Select a firmware package to begin.");
        app->bar = control(PROGRESS_CLASSW, L"Extraction progress", PBS_SMOOTH, 0);
        SetWindowTheme(app->bar, L"", L"");
        SendMessageW(app->bar, PBM_SETBARCOLOR, 0, ACCENT);
        SendMessageW(app->bar, PBM_SETBKCOLOR, 0, CARD);
        SendMessageW(app->bar, PBM_SETRANGE32, 0, 1000);
        button(L"Extract selected", EXTRACT);
        button(L"Cancel", CANCEL);
        button(L"Open output folder", OPEN);
        button(L"Start another", NEW_JOB);
        button(L"Activity log", LOG);
        button(L"Save log", SAVE);
        button(L"About / help", ABOUT);
        for (int id = ID_INPUT; id < 160; id++)
            if (app->controls[id])
                SendMessageW(app->controls[id], EM_SETLIMITTEXT, OFP_PATH - 1, 0);
        DragAcceptFiles(w, TRUE);
        BOOL dark = TRUE;
        DwmSetWindowAttribute(w, 20, &dark, sizeof(dark));
        SetTimer(w, 1, 150, NULL);
        enabled();
        totals();
        append_log(
            L"DroidRooter's OFP Studio 1.0.0 - native Windows extraction. No external runtime.");
        return 0;
    }
    case WM_SIZE:
        layout();
        return 0;
    case WM_GETMINMAXINFO:
        ((MINMAXINFO *)lp)->ptMinTrackSize.x = px(1040);
        ((MINMAXINFO *)lp)->ptMinTrackSize.y = px(800);
        return 0;
    case WM_DPICHANGED: {
        app->dpi = HIWORD(wp);
        RECT *r = (RECT *)lp;
        SetWindowPos(w, NULL, r->left, r->top, r->right - r->left, r->bottom - r->top,
                     SWP_NOZORDER);
        layout();
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        paint();
        return 0;
    case WM_DRAWITEM:
        draw_button((DRAWITEMSTRUCT *)lp);
        return TRUE;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
    case WM_CTLCOLOREDIT: {
        HDC dc = (HDC)wp;
        int card = m == WM_CTLCOLOREDIT || (HWND)lp == captions[0] || (HWND)lp == captions[1];
        SetTextColor(dc, INK);
        SetBkColor(dc, card ? CARD : BG);
        return (LRESULT)(card ? app->card : app->bg);
    }
    case WM_TIMER:
        tick();
        return 0;
    case WM_DROPFILES: {
        HDROP drop = (HDROP)wp;
        if (!app->busy) {
            wchar_t path[OFP_PATH];
            if (DragQueryFileW(drop, 0, path, OFP_PATH))
                inspect_path(path);
        }
        DragFinish(drop);
        return 0;
    }
    case WM_NOTIFY: {
        NMHDR *hdr = (NMHDR *)lp;
        if (hdr->idFrom == FILES && hdr->code == LVN_ITEMCHANGED && !app->rebuilding &&
            !app->busy) {
            NMLISTVIEW *n = (NMLISTVIEW *)lp;
            if (n->iItem >= 0 && (n->uChanged & LVIF_STATE)) {
                LVITEMW item = {0};
                item.mask = LVIF_PARAM;
                item.iItem = n->iItem;
                ListView_GetItem(app->controls[FILES], &item);
                app->archive.entries[item.lParam].selected =
                    ListView_GetCheckState(app->controls[FILES], n->iItem);
                totals();
            }
        }
        if (hdr->idFrom == FILES && hdr->code == NM_DBLCLK && !app->busy)
            file_details();
        return 0;
    }
    case WM_COMMAND: {
        int id = LOWORD(wp), code = HIWORD(wp);
        if (id == BROWSE && !app->busy)
            browse();
        else if (id == FOLDER && !app->busy)
            folder();
        else if (id == SEARCH && code == EN_CHANGE && !app->busy)
            fill_list();
        else if (id == OUTPUT && code == EN_CHANGE && !app->busy)
            totals();
        else if (id == ID_INPUT && code == EN_KILLFOCUS && !app->busy) {
            wchar_t p[OFP_PATH];
            GetWindowTextW(app->controls[ID_INPUT], p, OFP_PATH);
            if (*p && wcscmp(p, app->input))
                inspect_path(p);
        } else if ((id == ALL || id == CLEAR) && !app->busy) {
            for (int i = 0; i < app->archive.count; i++)
                app->archive.entries[i].selected = id == ALL;
            fill_list();
        } else if (id == EXTRACT && !app->busy) {
            GetWindowTextW(app->controls[OUTPUT], app->dest, OFP_PATH);
            if (!*app->dest) {
                MessageBoxW(w, L"Choose an output folder first.", L"Output folder",
                            MB_OK | MB_ICONINFORMATION);
                break;
            }
            app->options.manifest =
                SendMessageW(app->controls[MANIFEST], BM_GETCHECK, 0, 0) == BST_CHECKED;
            app->options.verify =
                SendMessageW(app->controls[VERIFY], BM_GETCHECK, 0, 0) == BST_CHECKED;
            app->options.scatter =
                SendMessageW(app->controls[SCATTER], BM_GETCHECK, 0, 0) == BST_CHECKED;
            append_log(L"Extracting selected files. Existing files will not be overwritten.");
            begin(0);
        } else if (id == CANCEL && app->busy) {
            InterlockedExchange(&app->cancel, 1);
            text(app->status, L"Cancelling safely after the current bounded operation...");
            EnableWindow(app->controls[CANCEL], FALSE);
        } else if (id == OPEN && !app->busy) {
            wchar_t p[OFP_PATH];
            GetWindowTextW(app->controls[OUTPUT], p, OFP_PATH);
            if (*p)
                ShellExecuteW(w, L"open", p, NULL, NULL, SW_SHOWNORMAL);
        } else if (id == NEW_JOB && !app->busy) {
            ofp_free(&app->archive);
            app->input[0] = app->dest[0] = 0;
            text(app->controls[ID_INPUT], L"");
            text(app->controls[OUTPUT], L"");
            text(app->controls[SEARCH], L"");
            fill_list();
            text(app->identity, L"Select an OFP to inspect its contents.");
            text(app->status, L"Ready for another package.");
            text(app->stats, L"Select a firmware package to begin.");
            SendMessageW(app->bar, PBM_SETPOS, 0, 0);
            enabled();
            totals();
        } else if (id == LOG)
            show_log();
        else if (id == SAVE)
            save_log();
        else if (id == DETAILS && !app->busy)
            file_details();
        else if (id == ABOUT)
            MessageBoxW(
                w,
                L"DroidRooter's OFP Studio 1.0.0\nNative C / Windows 10 and 11 x64\n\nSelect an "
                L"OFP, choose an output folder, tick the images you need, then Extract selected. "
                L"Drag-and-drop is optional. Filtering preserves hidden selections.\n\nSupports "
                L"known MediaTek and Qualcomm raw OFP formats. ZIP-wrapped OFP and OnePlus OPS are "
                L"not supported. Local-drive output folders only.\n\nScatter conversion requires "
                L"all referenced images. It preserves the OEM layout and disables preloader "
                L"downloading. No phone flashing is performed.\n\nBased on Bjoern Kerler's "
                L"MIT-licensed oppo_decrypt.\nSource: github.com/saadnahid7/droidrooter-ofp-studio",
                L"About OFP Studio", MB_OK | MB_ICONINFORMATION);
        return 0;
    }
    case DONE: {
        WaitForSingleObject(app->thread, INFINITE);
        CloseHandle(app->thread);
        app->thread = NULL;
        app->busy = 0;
        if (app->inspecting) {
            if (app->result) {
                app->archive = app->pending;
                memset(&app->pending, 0, sizeof(app->pending));
                wchar_t package_size[64], identity[400];
                size_text(app->archive.source_size, package_size, 64);
                swprintf(identity, 400, L"%ls  |  Package: %ls", app->archive.identity,
                         package_size);
                text(app->identity, identity);
                append_log(app->archive.identity);
                wchar_t out[OFP_PATH];
                swprintf(out, OFP_PATH, L"%ls", app->input);
                wchar_t *dot = wcsrchr(out, L'.');
                if (dot)
                    *dot = 0;
                if (wcslen(out) + 11 < OFP_PATH)
                    wcscat(out, L"_extracted");
                text(app->controls[OUTPUT], out);
                text(app->status, L"Package inspected. Choose images and output options.");
                text(app->stats, L"Nothing is written until you click Extract selected.");
            } else {
                text(app->identity, L"Package inspection stopped");
                text(app->status, app->error);
                append_log(app->error);
                if (!InterlockedCompareExchange(&app->cancel, 0, 0))
                    MessageBoxW(w, app->error, L"Cannot inspect package", MB_OK | MB_ICONERROR);
            }
        } else {
            for (int i = 0; i < app->archive.count; i++)
                if (app->archive.entries[i].completed) {
                    wchar_t line[700];
                    swprintf(line, 700, L"Completed: %ls", app->archive.entries[i].name);
                    append_log(line);
                }
            if (app->result) {
                text(app->status, *app->warning
                                      ? L"Images extracted. Scatter was skipped; see activity log."
                                      : L"Extraction complete. Your selected files are ready.");
                SendMessageW(app->bar, PBM_SETPOS, 1000, 0);
                append_log(L"Extraction completed successfully.");
                if (*app->warning)
                    append_log(app->warning);
                if (SendMessageW(app->controls[OPEN_AFTER], BM_GETCHECK, 0, 0) == BST_CHECKED)
                    ShellExecuteW(w, L"open", app->dest, NULL, NULL, SW_SHOWNORMAL);
            } else {
                text(app->status, app->error);
                append_log(app->error);
                if (!InterlockedCompareExchange(&app->cancel, 0, 0))
                    MessageBoxW(w, app->error, L"Extraction stopped", MB_OK | MB_ICONERROR);
            }
            wchar_t elapsed[256];
            double sec = (GetTickCount64() - app->start) / 1000.0;
            swprintf(elapsed, 256, L"Finished in %.1f seconds. %ls", sec,
                     app->options.verify
                         ? (app->result
                                ? L"Written files were reread and verified."
                                : L"Verification did not complete for the entire selection.")
                         : L"Readback verification was not enabled.");
            text(app->stats, elapsed);
            append_log(elapsed);
        }
        app->inspecting = 0;
        enabled();
        fill_list();
        if (app->close_after)
            DestroyWindow(w);
        return 0;
    }
    case WM_CLOSE:
        if (app->busy) {
            if (MessageBoxW(
                    w,
                    L"Cancel the current operation and close? Completed images will be retained.",
                    L"Operation in progress", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                app->close_after = 1;
                InterlockedExchange(&app->cancel, 1);
                if (!app->busy)
                    DestroyWindow(w);
            }
            return 0;
        }
        DestroyWindow(w);
        return 0;
    case WM_DESTROY:
        KillTimer(w, 1);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(w, m, wp, lp);
}
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, PWSTR command, int show) {
    (void)previous;
    (void)command;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    INITCOMMONCONTROLSEX cc = {sizeof(cc), ICC_LISTVIEW_CLASSES | ICC_PROGRESS_CLASS};
    InitCommonControlsEx(&cc);
    app = calloc(1, sizeof(*app));
    if (!app)
        return 1;
    app->dpi = 96;
    InitializeCriticalSection(&app->lock);
    WNDCLASSEXW wc = {0};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = window_proc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(101));
    wc.hIconSm = (HICON)LoadImageW(instance, MAKEINTRESOURCEW(101), IMAGE_ICON,
                                   GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON),
                                   LR_DEFAULTCOLOR);
    wc.lpszClassName = L"OFPStudio";
    RegisterClassExW(&wc);
    wc.lpfnWndProc = log_proc;
    wc.lpszClassName = L"OFPStudioLog";
    RegisterClassExW(&wc);
    HWND w = CreateWindowExW(0, L"OFPStudio", L"DroidRooter's OFP Studio", WS_OVERLAPPEDWINDOW,
                             CW_USEDEFAULT, CW_USEDEFAULT, 1160, 900, NULL, NULL, instance, NULL);
    if (!w)
        return 1;
    ShowWindow(w, show);
    UpdateWindow(w);
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (!IsDialogMessageW(w, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    ofp_free(&app->archive);
    ofp_free(&app->pending);
    DeleteCriticalSection(&app->lock);
    DeleteObject(app->font);
    DeleteObject(app->small);
    DeleteObject(app->title);
    DeleteObject(app->bold);
    DeleteObject(app->bg);
    DeleteObject(app->card);
    free(app);
    CoUninitialize();
    return 0;
}
