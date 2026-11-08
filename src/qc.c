/* Qualcomm OFP profiles and section semantics adapted from oppo_decrypt (MIT). */
#include "keys.h"
#include "ofp.h"
#include <errno.h>
#include <initguid.h>
#include <shlwapi.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <xmllite.h>
static int number(const wchar_t *s, uint64_t *out) {
    wchar_t *end;
    if (!s || !*s || *s == L'-' || *s == L'+')
        return 0;
    errno = 0;
    *out = wcstoull(s, &end, 10);
    return !errno && !*end;
}
static int ascii_hash(const wchar_t *s, char *out, int n) {
    if (wcslen(s) != (size_t)n)
        return 0;
    for (int i = 0; i < n; i++) {
        if (!((s[i] >= L'0' && s[i] <= L'9') || (s[i] >= L'a' && s[i] <= L'f') ||
              (s[i] >= L'A' && s[i] <= L'F')))
            return 0;
        out[i] = (char)s[i];
    }
    out[n] = 0;
    return 1;
}
static int parse_xml(HANDLE h, OfpArchive *a, unsigned page, uint64_t data_limit, wchar_t *error) {
    IStream *stream = SHCreateMemStream(a->xml, (UINT)a->xml_size);
    IXmlReader *r = NULL;
    if (!stream)
        return ofp_error(error, L"Cannot allocate XML stream.");
    HRESULT hr = CreateXmlReader(&IID_IXmlReader, (void **)&r, NULL);
    int ok = 0;
    if (FAILED(hr) || !r) {
        IStream_Release(stream);
        return ofp_error(error, L"Windows XMLLite reader is unavailable.");
    }
    IXmlReader_SetProperty(r, XmlReaderProperty_DtdProcessing, DtdProcessing_Prohibit);
    IXmlReader_SetProperty(r, XmlReaderProperty_MaxElementDepth, 32);
    if (FAILED(IXmlReader_SetInput(r, (IUnknown *)stream))) {
        ofp_error(error, L"Invalid XML input.");
        goto done;
    }
    XmlNodeType type;
    wchar_t section[128] = L"";
    unsigned section_depth = 0;
    int root_seen = 0;
    while ((hr = IXmlReader_Read(r, &type)) == S_OK) {
        UINT depth = 0;
        IXmlReader_GetDepth(r, &depth);
        if (type == XmlNodeType_DocumentType) {
            ofp_error(error, L"DTD metadata is not allowed.");
            goto done;
        }
        if (type == XmlNodeType_EndElement && depth == section_depth)
            section[0] = 0;
        if (type != XmlNodeType_Element)
            continue;
        const wchar_t *tag;
        UINT len;
        IXmlReader_GetLocalName(r, &tag, &len);
        if (!root_seen) {
            root_seen = 1;
            continue;
        }
        if (depth == 1) {
            if (len >= 128) {
                ofp_error(error, L"XML section name too long.");
                goto done;
            }
            wcscpy(section, tag);
            section_depth = depth;
        }
        OfpEntry e = {0};
        uint64_t sectors = 0, offset = 0, bytes = 0;
        int have_offset = 0, have_size = 0, have_sectors = 0;
        int attrs_ok = 1;
        if (IXmlReader_MoveToFirstAttribute(r) == S_OK)
            do {
                const wchar_t *name, *value;
                IXmlReader_GetLocalName(r, &name, NULL);
                IXmlReader_GetValue(r, &value, NULL);
                if (!wcscmp(name, L"Path") || !wcscmp(name, L"filename")) {
                    if (wcslen(value) >= 512)
                        attrs_ok = 0;
                    else
                        wcscpy(e.name, value);
                } else if (!wcscmp(name, L"FileOffsetInSrc")) {
                    have_offset = 1;
                    attrs_ok &= number(value, &offset);
                } else if (!wcscmp(name, L"SizeInByteInSrc")) {
                    have_size = 1;
                    attrs_ok &= number(value, &bytes);
                } else if (!wcscmp(name, L"SizeInSectorInSrc")) {
                    have_sectors = 1;
                    attrs_ok &= number(value, &sectors);
                } else if (!wcscmp(name, L"sha256") && *value)
                    attrs_ok &= ascii_hash(value, e.vendor_sha, 64);
                else if (!wcscmp(name, L"md5") && *value)
                    attrs_ok &= ascii_hash(value, e.vendor_md5, 32);
            } while (IXmlReader_MoveToNextAttribute(r) == S_OK);
        IXmlReader_MoveToElement(r);
        if (!attrs_ok) {
            ofp_error(error, L"Invalid number, name or checksum in Qualcomm XML.");
            goto done;
        }
        if (!*e.name)
            continue;
        /* Legacy reference accepts sector count as a fallback source offset. */
        if (!have_offset && have_sectors) {
            offset = sectors;
            have_offset = 1;
        }
        if (!have_offset || (!have_size && !have_sectors) || offset > UINT64_MAX / page ||
            sectors > UINT64_MAX / page) {
            ofp_error(error, L"Missing or oversized Qualcomm file span.");
            goto done;
        }
        e.offset = offset * page;
        e.size = have_size ? bytes : sectors * page;
        uint64_t declared = have_sectors ? sectors * page : e.size;
        if (e.offset > data_limit || e.size > data_limit - e.offset ||
            declared > data_limit - e.offset || (have_size && have_sectors && e.size > declared)) {
            ofp_error(error,
                      L"Qualcomm image overlaps protected metadata or exceeds declared sectors.");
            goto done;
        }
        if (depth != 2 && depth != 3) {
            ofp_error(error, L"Unsupported Qualcomm metadata nesting.");
            goto done;
        }
        int direct = depth == 2;
        int copy = direct && (!wcscmp(section, L"Firmware") || !wcscmp(section, L"DigestsToSign") ||
                              !wcscmp(section, L"ChainedTableOfDigests"));
        e.encrypted =
            copy ? 0
                 : (direct && !wcscmp(section, L"Sahara") ? e.size
                                                          : (e.size < 0x40000 ? e.size : 0x40000));
        swprintf(e.label, 128, L"%ls", section);
        if (!ofp_add_entry(a, &e, h, error))
            goto done;
    }
    if (hr != S_FALSE || !root_seen || !a->count) {
        ofp_error(error, L"Malformed or empty Qualcomm XML metadata (0x%08lx).", (unsigned long)hr);
        goto done;
    }
    ok = 1;
done:
    IXmlReader_Release(r);
    IStream_Release(stream);
    return ok;
}
int ofp_qc(HANDLE h, OfpArchive *a, wchar_t *error) {
    unsigned pages[] = {512, 4096}, page = 0;
    unsigned char footer[12];
    uint64_t at = 0, n = 0;
    for (int i = 0; i < 2; i++)
        if (a->source_size >= pages[i] && ofp_read(h, a->source_size - pages[i] + 16, footer, 12) &&
            ofp_u32(footer) == 0x7cef) {
            page = pages[i];
            break;
        }
    if (!page)
        return ofp_error(error, L"Unknown Qualcomm footer.");
    at = (uint64_t)ofp_u32(footer + 4) * page;
    n = ofp_u32(footer + 8);
    uint64_t limit = a->source_size - page;
    if (n < 200) {
        if (at > limit || limit - at < 0x57)
            return ofp_error(error, L"Invalid legacy Qualcomm XML span.");
        n = limit - at - 0x57;
    }
    if (n > 16 * 1024 * 1024 || at > limit || n > limit - at)
        return ofp_error(error, L"Qualcomm XML is oversized or outside the archive.");
    unsigned char *enc = malloc((size_t)n);
    a->xml = malloc((size_t)n + 1);
    if (!enc || !a->xml) {
        free(enc);
        return ofp_error(error, L"Not enough memory.");
    }
    if (!ofp_read(h, at, enc, (DWORD)n)) {
        free(enc);
        return ofp_error(error, L"Cannot read Qualcomm XML.");
    }
    int found = 0;
    for (size_t i = 0; i < sizeof(qc_keys) / sizeof(qc_keys[0]); i++) {
        if (!ofp_derive(qc_keys[i][0], qc_keys[i][1], qc_keys[i][2], a->key, a->iv))
            continue;
        memcpy(a->xml, enc, (size_t)n);
        OfpCipher cipher = {0};
        int good = ofp_cipher_begin(&cipher, a->key, a->iv) &&
                   ofp_cipher_decrypt(&cipher, a->xml, (size_t)n);
        ofp_cipher_close(&cipher);
        a->xml[n] = 0;
        if (good && strstr((char *)a->xml, "<?xml")) {
            found = 1;
            break;
        }
    }
    free(enc);
    if (!found)
        return ofp_error(error, L"Unknown Qualcomm OFP key.");
    /* Ignore encrypted padding beyond the last closing XML delimiter. */
    a->xml_size = (size_t)n;
    while (a->xml_size && a->xml[a->xml_size - 1] != '>')
        a->xml_size--;
    a->format = 2;
    swprintf(a->identity, 256, L"Qualcomm OFP  |  %u-byte pages", page);
    return parse_xml(h, a, page, at, error);
}
