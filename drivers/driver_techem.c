/* Techem driver: HCAs, heat/water meters, smoke detectors.
 *
 * Techem uses a non-OMS compact layout under CI 0xA0..0xA2; the generic
 * DIF/VIF walker would mis-decode it. Reverse-engineered from
 * wmbusmeters drivers/techem_fhkv*.cc (open-source). */

#include "driver_registry.h"
#include "../protocol/wmbus_app_layer.h"
#include "../protocol/wmbus_manuf.h"
#include "../protocol/wmbus_medium.h"
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

/* ---------- helpers ---------- */

static int append(char* o, size_t cap, size_t pos, const char* fmt, ...) {
    if(pos >= cap) return 0;
    va_list ap;
    __builtin_va_start(ap, fmt);
    int n = vsnprintf(o + pos, cap - pos, fmt, ap);
    __builtin_va_end(ap);
    return n < 0 ? 0 : n;
}

static size_t hex_dump(const uint8_t* a, size_t l, char* o, size_t cap, const char* label) {
    size_t pos = 0;
    pos += append(o, cap, pos, "%s\n", label);
    size_t n = l < 24 ? l : 24;
    for(size_t i = 0; i < n; i++) pos += append(o, cap, pos, "%02X", a[i]);
    if(l > n) pos += append(o, cap, pos, "..");
    pos += append(o, cap, pos, "\n");
    return pos;
}

static void decode_mbus_date_g(uint16_t w, char* out, size_t cap) {
    int day = w & 0x1F;
    int mon = (w >> 8) & 0x0F;
    int yr  = ((w >> 5) & 0x07) | (((w >> 12) & 0x0F) << 3);
    if(day == 0 || mon == 0 || mon > 12) {
        snprintf(out, cap, "—");
    } else {
        snprintf(out, cap, "%04d-%02d-%02d", 2000 + yr, mon, day);
    }
}

/* FHKV data III/IV layout (relative to the byte after CI):
 *   0    subtype/status
 *   1..2 previous-period date  (LE u16)
 *   3..4 previous-period units (LE u16)
 *   5..6 current-period date   (LE u16, different packing)
 *   7..8 current-period units  (LE u16)
 *   9..10 or 10..11 room temperature (LE u16 × 0.01 °C, varies by firmware)
 *   11..12 or 12..13 radiator temperature
 *
 * Both temperature offsets are tried and the one whose room reading is
 * plausible wins. */

static void decode_prev_date(uint16_t w, char* out, size_t cap, int* yr_out) {
    int day = w & 0x1F;
    int mon = (w >> 5) & 0x0F;
    int yr  = 2000 + ((w >> 9) & 0x3F);
    if(yr_out) *yr_out = yr;
    if(!day || !mon || mon > 12) snprintf(out, cap, "-");
    else snprintf(out, cap, "%04d-%02d-%02d", yr, mon, day);
}

static void decode_curr_date(uint16_t w, int yr_prev, char* out, size_t cap) {
    int day = (w >> 4) & 0x1F;
    int mon = (w >> 9) & 0x0F;
    int yr  = yr_prev + 1;            /* current period is the year after prev */
    if(!day || !mon || mon > 12) snprintf(out, cap, "-");
    else snprintf(out, cap, "%04d-%02d-%02d", yr, mon, day);
}

static int read_temp_centi(const uint8_t* a, size_t off) {
    return (int)((uint16_t)(a[off] | (a[off + 1] << 8)));
}

static size_t decode_hca(const uint8_t* a, size_t l, char* o, size_t cap) {
    if(l < 13) return hex_dump(a, l, o, cap, "Techem HCA (short)");

    uint16_t prev_date_w = (uint16_t)(a[1] | (a[2] << 8));
    uint16_t prev_val    = (uint16_t)(a[3] | (a[4] << 8));
    uint16_t curr_date_w = (uint16_t)(a[5] | (a[6] << 8));
    uint16_t curr_val    = (uint16_t)(a[7] | (a[8] << 8));

    int yr_prev = 0;
    char dprev[16], dcurr[16];
    decode_prev_date(prev_date_w, dprev, sizeof(dprev), &yr_prev);
    decode_curr_date(curr_date_w, yr_prev, dcurr, sizeof(dcurr));

    /* Pick the temperature offset whose room reading is in the
     * 2..50 °C range. */
    int troom = -32768, trad = -32768;
    if(l >= 13) {
        int t9  = read_temp_centi(a, 9);
        int t10 = read_temp_centi(a, 10);
        bool ok9  = (t9  >= 200 && t9  <= 5000);
        bool ok10 = (t10 >= 200 && t10 <= 5000);
        if(ok9) {
            troom = t9;
            int tr = read_temp_centi(a, 11);
            if(tr >= 0 && tr <= 9000) trad = tr;
        } else if(ok10 && l >= 14) {
            troom = t10;
            int tr = read_temp_centi(a, 12);
            if(tr >= 0 && tr <= 9000) trad = tr;
        }
    }

    /* Days-of-year using a 30-day-month approximation — good enough
     * for a daily-average projection. */
    int days_into = -1;       /* days since the last reset */
    int days_total = 365;     /* default a billing year */
    int day_p = prev_date_w & 0x1F;
    int mon_p = (prev_date_w >> 5) & 0x0F;
    int day_c = (curr_date_w >> 4) & 0x1F;
    int mon_c = (curr_date_w >> 9) & 0x0F;
    if(day_p && mon_p && mon_p <= 12 && day_c && mon_c && mon_c <= 12) {
        int day_of_year_p = (mon_p - 1) * 30 + day_p;
        int day_of_year_c = (mon_c - 1) * 30 + day_c;
        days_total = (day_of_year_c + 360 - day_of_year_p) % 360;
        if(days_total == 0) days_total = 360;
    }

    size_t pos = 0;
    pos += append(o, cap, pos, "Heat-cost allocator\n");
    pos += append(o, cap, pos, "Now    %u u\n", curr_val);
    pos += append(o, cap, pos, "Prev   %u u\n", prev_val);
    pos += append(o, cap, pos, "Reset  %s\n", dprev);
    pos += append(o, cap, pos, "Ends   %s\n", dcurr);
    if(days_into > 0 && days_total > 0) {
        unsigned daily = curr_val * 10u / (unsigned)days_into;
        unsigned est   = curr_val * (unsigned)days_total / (unsigned)days_into;
        pos += append(o, cap, pos, "Daily  %u.%u u\n", daily / 10, daily % 10);
        pos += append(o, cap, pos, "Est    %u u\n", est);
    }
    if(troom != -32768)
        pos += append(o, cap, pos, "Room   %d.%02d C\n",
                      troom / 100, (troom < 0 ? -troom : troom) % 100);
    if(trad != -32768)
        pos += append(o, cap, pos, "Rad    %d.%02d C\n",
                      trad / 100,  (trad  < 0 ? -trad  : trad ) % 100);
    return pos;
}

/* ---------- Compact-V heat meter (medium 0x04) ---------- */
static size_t decode_heat(const uint8_t* a, size_t l, char* o, size_t cap) {
    if(l < 8) return hex_dump(a, l, o, cap, "Techem heat (short)");
    /* Layout (best-effort, varies across firmwares):
     *   0..3  total energy (LE uint32, kWh)
     *   4..5  current period (LE uint16, kWh)
     *   6..7  date (M-bus type G) */
    uint32_t total = (uint32_t)(a[0] | (a[1] << 8) | (a[2] << 16) | ((uint32_t)a[3] << 24));
    uint16_t cur   = (uint16_t)(a[4] | (a[5] << 8));
    uint16_t d1    = (uint16_t)(a[6] | (a[7] << 8));
    char ds[16]; decode_mbus_date_g(d1, ds, sizeof(ds));
    size_t pos = 0;
    pos += append(o, cap, pos, "Heat meter\n");
    pos += append(o, cap, pos, "Total  %lu kWh\n", (unsigned long)total);
    pos += append(o, cap, pos, "Period %u kWh\n", cur);
    pos += append(o, cap, pos, "Date   %s\n", ds);
    return pos;
}

/* ---------- Warm/cold-water meters (medium 0x06/0x07/0x16) ---------- */
static size_t decode_water(const uint8_t* a, size_t l, char* o, size_t cap) {
    if(l < 8) return hex_dump(a, l, o, cap, "Techem water (short)");
    /* Common Techem water layout:
     *   0     status
     *   1..4  current volume (LE uint32, decilitres = 0.1 L)
     *   5..8  previous volume (LE uint32, decilitres) */
    uint32_t cur  = (uint32_t)(a[1] | (a[2] << 8) | (a[3] << 16) | ((uint32_t)a[4] << 24));
    uint32_t prev = 0;
    if(l >= 9) prev = (uint32_t)(a[5] | (a[6] << 8) | (a[7] << 16) | ((uint32_t)a[8] << 24));
    size_t pos = 0;
    pos += append(o, cap, pos, "Water meter\n");
    pos += append(o, cap, pos, "Now   %lu.%lu L\n",
                  (unsigned long)(cur  / 10), (unsigned long)(cur  % 10));
    pos += append(o, cap, pos, "Last  %lu.%lu L\n",
                  (unsigned long)(prev / 10), (unsigned long)(prev % 10));
    return pos;
}

/* ---------- Smoke detector (medium 0x1A) ---------- */
static size_t decode_smoke(const uint8_t* a, size_t l, char* o, size_t cap) {
    size_t pos = 0;
    pos += append(o, cap, pos, "Smoke detector\n");
    if(l > 0) pos += append(o, cap, pos, "Status 0x%02X\n", a[0]);
    if(l > 1) pos += append(o, cap, pos, "Flags  0x%02X\n", a[1]);
    return pos;
}

/* ---------- Match + dispatch ---------- */

static bool match_tch(uint16_t m, uint8_t v, uint8_t med) {
    char c[4]; wmbus_manuf_decode(m, c); (void)v; (void)med;
    return memcmp(c, "TCH", 3) == 0;
}

static size_t decode_tch(const uint8_t* a, size_t l, char* out, size_t cap) {
    if(!a || l == 0 || cap < 16) return 0;
    /* HCAs dominate Techem deployments and share a consistent 12-byte
     * preamble; the decoder sanity-checks values before printing. */
    if(l >= 12) return decode_hca(a, l, out, cap);
    return hex_dump(a, l, out, cap, "Techem proprietary");
}

const WmbusDriver g_driver_techem = { "techem", match_tch, decode_tch };

/* Medium-aware variant exposed for callers that have the medium byte. */
size_t wmbus_techem_render_for_medium(
    uint8_t medium, const uint8_t* a, size_t l, char* o, size_t cap) {
    switch(medium) {
    case 0x08: return decode_hca(a, l, o, cap);
    case 0x04: return decode_heat(a, l, o, cap);
    case 0x06:
    case 0x07:
    case 0x16: return decode_water(a, l, o, cap);
    case 0x1A: return decode_smoke(a, l, o, cap);
    default:   return decode_tch(a, l, o, cap);
    }
}
