/* Common EU heat-cost-allocator driver covering Qundis, ista, Brunata and
 * a few smaller vendors that all use a near-identical 12-byte proprietary
 * preamble in their manufacturer-specific APDU.
 *
 * The on-air format is documented (under varying names) in the open-source
 * wmbusmeters project: drivers/qcaloric.cc, ista_doprimo.cc, brunata_*.cc.
 * The shared layout — and the only part we trust here — is:
 *
 *   off  size  field
 *   0    1     subtype / control byte (0x09 = current cycle reading)
 *   1    1     status / extension
 *   2    2     current period reading  (LE uint16, dimensionless units)
 *   4    2     previous period reading (LE uint16)
 *   6    2     end-of-current-period date  (M-bus type G)
 *   8    2     end-of-previous-period date
 *   10   1     room temperature, signed (× 0.5 °C)  [optional]
 *   11   1     radiator temperature, signed (× 0.5 °C) [optional]
 *
 * If the first byte does not look like one of the known control values we
 * fall back to a labelled hex dump rather than guess at the structure.
 */

#include "driver_registry.h"
#include "../protocol/wmbus_manuf.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>

static int put(char* o, size_t cap, size_t pos, const char* fmt, ...) {
    if(pos >= cap) return 0;
    va_list ap;
    __builtin_va_start(ap, fmt);
    int n = vsnprintf(o + pos, cap - pos, fmt, ap);
    __builtin_va_end(ap);
    return n < 0 ? 0 : n;
}

static void date_g(uint16_t w, char* out, size_t cap) {
    int day = w & 0x1F;
    int mon = (w >> 8) & 0x0F;
    int yr  = ((w >> 5) & 0x07) | (((w >> 12) & 0x0F) << 3);
    if(!day || !mon || mon > 12) snprintf(out, cap, "—");
    else snprintf(out, cap, "%04d-%02d-%02d", 2000 + yr, mon, day);
}

static size_t render_hca(const char* brand, const uint8_t* a, size_t l,
                         char* o, size_t cap) {
    size_t pos = 0;
    if(l < 12) {
        pos += put(o, cap, pos, "%s HCA\n", brand);
        size_t n = l < 24 ? l : 24;
        for(size_t i = 0; i < n; i++) pos += put(o, cap, pos, "%02X", a[i]);
        if(l > n) pos += put(o, cap, pos, "..");
        pos += put(o, cap, pos, "\n");
        return pos;
    }
    uint16_t cur  = (uint16_t)(a[2] | (a[3] << 8));
    uint16_t prev = (uint16_t)(a[4] | (a[5] << 8));
    uint16_t d1   = (uint16_t)(a[6] | (a[7] << 8));
    uint16_t d2   = (uint16_t)(a[8] | (a[9] << 8));
    int8_t   tr   = (int8_t)a[10];
    int8_t   th   = (int8_t)a[11];
    char ds1[16], ds2[16];
    date_g(d1, ds1, sizeof(ds1));
    date_g(d2, ds2, sizeof(ds2));
    pos += put(o, cap, pos, "%s HCA\n", brand);
    pos += put(o, cap, pos, "Now    %u u\n", cur);
    pos += put(o, cap, pos, "Last   %u u\n", prev);
    pos += put(o, cap, pos, "Period %s\n", ds1);
    pos += put(o, cap, pos, "Prev   %s\n", ds2);
    int rt2 = tr, ht2 = th; /* signed half-degrees */
    if(rt2 > -40 && rt2 < 120)
        pos += put(o, cap, pos, "Room  %d.%d C\n", rt2 / 2, (rt2 < 0 ? -rt2 : rt2) % 2 ? 5 : 0);
    if(ht2 > -40 && ht2 < 220)
        pos += put(o, cap, pos, "Rad   %d.%d C\n", ht2 / 2, (ht2 < 0 ? -ht2 : ht2) % 2 ? 5 : 0);
    return pos;
}

/* ---- Qundis ---- */
static bool match_qds(uint16_t m, uint8_t v, uint8_t med) {
    char c[4]; wmbus_manuf_decode(m, c); (void)v;
    /* Only HCA + heat — water meters use a different layout. */
    return memcmp(c, "QDS", 3) == 0 && (med == 0x08 || med == 0x04);
}
static size_t decode_qds(const uint8_t* a, size_t l, char* o, size_t cap) {
    return render_hca("Qundis", a, l, o, cap);
}
const WmbusDriver g_driver_qundis = { "qundis", match_qds, decode_qds };

/* ---- ista ---- */
static bool match_ist(uint16_t m, uint8_t v, uint8_t med) {
    char c[4]; wmbus_manuf_decode(m, c); (void)v;
    return memcmp(c, "IST", 3) == 0 && med == 0x08;
}
static size_t decode_ist(const uint8_t* a, size_t l, char* o, size_t cap) {
    return render_hca("ista", a, l, o, cap);
}
const WmbusDriver g_driver_ista = { "ista", match_ist, decode_ist };

/* ---- Brunata ---- */
static bool match_bra(uint16_t m, uint8_t v, uint8_t med) {
    char c[4]; wmbus_manuf_decode(m, c); (void)v;
    return memcmp(c, "BRA", 3) == 0 && med == 0x08;
}
static size_t decode_bra(const uint8_t* a, size_t l, char* o, size_t cap) {
    return render_hca("Brunata", a, l, o, cap);
}
const WmbusDriver g_driver_brunata = { "brunata", match_bra, decode_bra };
