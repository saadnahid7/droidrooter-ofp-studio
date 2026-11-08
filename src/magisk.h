#ifndef OFP_MAGISK_H
#define OFP_MAGISK_H
#include "ofp.h"
void *dr_cancel_new(void);
void dr_cancel_set(const void *);
int dr_cancelled(const void *);
void dr_cancel_free(void *);
void *dr_patch(const unsigned char *, size_t, const wchar_t *, size_t, const char *, const void *,
               char *, size_t);
const unsigned char *dr_result_bytes(const void *, size_t *);
const unsigned char *dr_result_report(const void *, size_t *);
void dr_result_free(void *);
int ofp_patch_image(const wchar_t *, const wchar_t *, const wchar_t *, void *, OfpProgress, void *,
                    wchar_t *);
void ofp_open_patcher(HWND, const wchar_t *);
int ofp_patcher_dialog_message(MSG *);
int ofp_patcher_is_open(void);
#endif
