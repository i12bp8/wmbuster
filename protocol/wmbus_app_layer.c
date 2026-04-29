#include "wmbus_app_layer.h"
#include <stdio.h>
#include <string.h>

/* DIF data length lookup (EN 13757-3 §6.4). */
static int dif_data_len(uint8_t dif_field) {
    switch(dif_field & 0x0F) {
    case 0x0: return 0;          /* no data */
    case 0x1: return 1;
    case 0x2: return 2;
    case 0x3: return 3;
    case 0x4: return 4;
    case 0x5: return 4;          /* 32-bit real */
    case 0x6: return 6;
    case 0x7: return 8;
    case 0x8: return 0;          /* selection for readout */
    case 0x9: return 1;          /* 2-digit BCD */
    case 0xA: return 2;          /* 4-digit BCD */
    case 0xB: return 3;          /* 6-digit BCD */
    case 0xC: return 4;          /* 8-digit BCD */
    case 0xD: return -1;         /* variable length, follows */
    case 0xE: return 6;          /* 12-digit BCD */
    case 0xF: return 0;          /* manufacturer-specific marker */
    }
    return 0;
}

static bool dif_is_bcd(uint8_t dif_field) {
    uint8_t f = dif_field & 0x0F;
    return f == 0x9 || f == 0xA || f == 0xB || f == 0xC || f == 0xE;
}

static int64_t read_int_le(const uint8_t* p, int n) {
    int64_t v = 0;
    for(int i = n - 1; i >= 0; i--) v = (v << 8) | p[i];
    if(n < 8 && (p[n - 1] & 0x80)) {
        for(int i = n; i < 8; i++) v |= ((int64_t)0xFF << (i * 8));
    }
    return v;
}

static int64_t read_bcd(const uint8_t* p, int n) {
    /* EN 13757-3 §6.4.3: sign lives in the top nibble of the MSB. */
    int64_t v = 0;
    bool neg = (p[n - 1] & 0xF0) == 0xF0;
    for(int i = n - 1; i >= 0; i--) {
        uint8_t hi = (p[i] >> 4) & 0x0F;
        uint8_t lo = p[i] & 0x0F;
        if(i == n - 1 && neg) hi = 0;
        v = v * 100 + (hi <= 9 ? hi : 0) * 10 + (lo <= 9 ? lo : 0);
    }
    return neg ? -v : v;
}

/* VIF table: each entry covers a fixed-width run of consecutive VIF
 * codes; the sub-bits encode an exponent offset relative to exp_base. */
typedef struct {
    uint8_t  mask;       /* mask applied to VIF (without ext bit 0x80) */
    uint8_t  match;      /* expected value after mask */
    int8_t   exp_base;   /* 10^x = exp_base + (vif & ~mask) */
    const char* quantity;
    const char* unit;
} VifEntry;

/* Practical subset of EN 13757-3 Annex C primary VIFs. Quantities
 * outside this table are reported as Unknown and filtered out by the
 * renderer. */
static const VifEntry k_vif[] = {
    /* Energy 1 mWh ... 10 GWh  (VIF 0000_0xxx -> 10^(x-3) Wh) */
    {0x78, 0x00, -3, "Energy",      "Wh"},
    /* Energy in kJ (VIF 0000_1xxx -> 10^(x-3) kJ) */
    {0x78, 0x08, -3, "Energy",      "kJ"},
    /* Volume 1 ml .. 10000 m^3 (VIF 0001_0xxx -> 10^(x-6) m^3) */
    {0x78, 0x10, -6, "Volume",      "m^3"},
    /* Mass    1 g .. 10 kt (VIF 0001_1xxx -> 10^(x-3) kg) */
    {0x78, 0x18, -3, "Mass",        "kg"},
    /* Power 1 mW .. 10 GW (VIF 0010_1xxx -> 10^(x-3) W) */
    {0x78, 0x28, -3, "Power",       "W"},
    {0x78, 0x30, -3, "Power",       "kJ/h"},
    /* Volume flow — three units */
    {0x78, 0x38, -6, "Volume flow", "m^3/h"},
    {0x78, 0x40, -7, "Volume flow", "m^3/min"},
    {0x78, 0x48, -9, "Volume flow", "m^3/s"},
    {0x78, 0x50, -3, "Mass flow",   "kg/h"},
    /* Temperatures (4-wide runs) */
    {0x7C, 0x58, -3, "Flow temp",   "C"},
    {0x7C, 0x5C, -3, "Return temp", "C"},
    {0x7C, 0x60, -3, "Temp diff",   "K"},
    {0x7C, 0x64, -3, "Ext temp",    "C"},
    /* Pressure 1 mbar .. 1000 bar */
    {0x7C, 0x68, -3, "Pressure",    "bar"},
    /* Time-of-life / actuality / averaging — exact units depend on the
     * sub-bits but for our display purposes "tick" is fine. */
    {0x7C, 0x20, 0, "On time",      "tick"},
    {0x7C, 0x24, 0, "Operating",    "tick"},
    {0x7C, 0x70, 0, "Averaging",    "tick"},
    {0x7C, 0x74, 0, "Actuality",    "tick"},
    /* Date (type G, 16-bit) and DateTime (type F, 32-bit) */
    {0xFF, 0x6C, 0, "Date",         ""},
    {0xFF, 0x6D, 0, "DateTime",     ""},
    /* Heat-cost allocator units — common after decrypting Techem v6A.
     * Dimensionless integer (the same "n NNNN" you read off the LCD). */
    {0xFF, 0x6E, 0, "HCA units",    "u"},
    /* Fabrication number / enhanced ID */
    {0xFF, 0x78, 0, "Fab no",       ""},
    {0xFF, 0x79, 0, "Enhanced ID",  ""},
    /* Bus address */
    {0xFF, 0x7A, 0, "Bus addr",     ""},
};

static const VifEntry* vif_lookup(uint8_t vif) {
    uint8_t v = vif & 0x7F;
    for(unsigned i = 0; i < sizeof(k_vif)/sizeof(k_vif[0]); i++) {
        if((v & k_vif[i].mask) == k_vif[i].match) return &k_vif[i];
    }
    return NULL;
}

/* DIF/VIF walker. */

size_t wmbus_app_parse(const uint8_t* a, size_t len, WmbusRecordCb cb, void* ctx) {
    size_t i = 0;
    size_t produced = 0;
    while(i < len) {
        WmbusRecord r;
        memset(&r, 0, sizeof(r));

        /* Skip idle fillers */
        if(a[i] == 0x2F) { i++; continue; }
        if(a[i] == 0x0F || a[i] == 0x1F) {
            /* Manufacturer-specific data follows until end. */
            r.kind = WmbusValueRaw;
            r.quantity = "Manufacturer";
            r.unit = "";
            r.raw = &a[i + 1];
            r.raw_len = (uint8_t)((len - i - 1) > 255 ? 255 : (len - i - 1));
            r.hdr[0] = a[i]; r.hdr_len = 1;
            if(cb) cb(&r, ctx);
            produced++;
            break;
        }

        /* DIF + DIFE chain */
        uint8_t dif = a[i++];
        r.hdr[r.hdr_len++] = dif;
        r.storage_no = (dif >> 6) & 0x01;            /* LSB of storage */
        r.tariff = 0;
        r.subunit = 0;
        uint8_t func_field = (dif >> 4) & 0x03;
        (void)func_field;
        while(i < len && (a[i - 1] & 0x80)) {
            uint8_t dife = a[i++];
            if(r.hdr_len < sizeof(r.hdr)) r.hdr[r.hdr_len++] = dife;
            r.storage_no |= (uint8_t)((dife & 0x0F) << (1 + 4 * (r.hdr_len - 2)));
            r.tariff   |= (uint8_t)(((dife >> 4) & 0x03) << (2 * (r.hdr_len - 2)));
            r.subunit  |= (uint8_t)(((dife >> 6) & 0x01) << (r.hdr_len - 2));
        }

        /* VIF + VIFE chain */
        if(i >= len) break;
        uint8_t vif = a[i++];
        if(r.hdr_len < sizeof(r.hdr)) r.hdr[r.hdr_len++] = vif;
        while(i < len && (a[i - 1] & 0x80)) {
            uint8_t vife = a[i++];
            if(r.hdr_len < sizeof(r.hdr)) r.hdr[r.hdr_len++] = vife;
        }

        /* Length & data */
        int dlen = dif_data_len(dif);
        if(dlen < 0) {
            if(i >= len) break;
            dlen = a[i++];
            if(dlen >= 0xC0) dlen -= 0xC0; /* string indicators */
        }
        if(dlen == 0 || (size_t)dlen > len - i) {
            r.kind = WmbusValueNone;
            r.raw = &a[i]; r.raw_len = 0;
            if(cb) cb(&r, ctx);
            produced++;
            break;
        }

        const uint8_t* d = &a[i]; i += dlen;
        r.raw = d; r.raw_len = (uint8_t)dlen;

        const VifEntry* ve = vif_lookup(vif);
        r.quantity = ve ? ve->quantity : "Unknown";
        r.unit     = ve ? ve->unit     : "";
        r.exponent = ve ? (int8_t)(ve->exp_base + (vif & ~ve->mask & 0x7F)) : 0;

        if(dif_is_bcd(dif)) {
            r.kind = WmbusValueBcd;
            r.i = read_bcd(d, dlen);
        } else if((vif & 0x7F) == 0x6C) {
            /* 16-bit "type G" date */
            if(dlen >= 2) {
                uint16_t w = (uint16_t)(d[0] | (d[1] << 8));
                int day = w & 0x1F;
                int mon = (w >> 8) & 0x0F;
                int yr  = ((w >> 5) & 0x07) | (((w >> 12) & 0x0F) << 3);
                snprintf(r.s, sizeof(r.s), "%04d-%02d-%02d", 2000 + yr, mon, day);
                r.kind = WmbusValueDate; r.s_len = strlen(r.s);
            }
        } else if((vif & 0x7F) == 0x6D) {
            /* 32-bit "type F" datetime */
            if(dlen >= 4) {
                int min = d[0] & 0x3F;
                int hr  = d[1] & 0x1F;
                int day = d[2] & 0x1F;
                int mon = d[3] & 0x0F;
                int yr  = ((d[2] >> 5) & 0x07) | (((d[3] >> 4) & 0x0F) << 3);
                snprintf(r.s, sizeof(r.s), "%04d-%02d-%02d %02d:%02d",
                         2000 + yr, mon, day, hr, min);
                r.kind = WmbusValueDate; r.s_len = strlen(r.s);
            }
        } else if((dif & 0x0F) == 0x5) {
            /* 32-bit IEEE 754 float */
            union { uint32_t u; float f; } u = { 0 };
            u.u = (uint32_t)(d[0] | (d[1] << 8) | (d[2] << 16) | ((uint32_t)d[3] << 24));
            r.kind = WmbusValueReal;
            r.r = (double)u.f;
        } else if((dif & 0x0F) == 0xD) {
            /* Variable-length string */
            r.kind = WmbusValueString;
            int max_n = (int)sizeof(r.s) - 1;
            uint8_t n = (uint8_t)((dlen > max_n) ? max_n : dlen);
            for(int k = 0; k < n; k++) r.s[k] = d[n - 1 - k]; /* LSB-first reversed */
            r.s[n] = 0; r.s_len = n;
        } else {
            r.kind = WmbusValueInt;
            r.i = read_int_le(d, dlen);
        }

        if(cb) cb(&r, ctx);
        produced++;
    }
    return produced;
}

/* Renderer: short labels + real units, no scientific notation, drops
 * uninformative records (Unknown, zero fillers, empty dates). */
typedef struct { char* out; size_t cap; size_t pos; uint8_t emitted; } RenderCtx;

#define RENDER_MAX_LINES 10

static void format_scaled_int(int64_t i, int e, const char* unit,
                              char* o, size_t cap) {
    /* Apply 10^e scale via integer math; never use float. */
    int64_t scaled = i;
    int frac_digits = 0;
    if(e >= 0) {
        while(e > 0 && scaled <  (int64_t)1000000000000LL) { scaled *= 10; e--; }
    } else {
        frac_digits = -e;
        if(frac_digits > 6) frac_digits = 6;
    }
    if(frac_digits == 0) {
        snprintf(o, cap, "%lld %s", (long long)scaled, unit ? unit : "");
        return;
    }
    int64_t divisor = 1;
    for(int k = 0; k < frac_digits; k++) divisor *= 10;
    int64_t whole = scaled / divisor;
    int64_t frac  = scaled % divisor;
    if(frac < 0) frac = -frac;
    /* Trim trailing zeros, but never below 1 fraction digit. */
    while(frac_digits > 1 && (frac % 10) == 0) { frac /= 10; frac_digits--; }
    if(frac == 0)
        snprintf(o, cap, "%lld %s", (long long)whole, unit ? unit : "");
    else {
        /* Hand-roll the zero-padding to keep the output buffer bounded
         * (the %0*lld combo confuses -Wformat-truncation). */
        char fbuf[8];
        int  fi = 0;
        int64_t f = frac;
        for(int k = 0; k < frac_digits && fi < (int)sizeof(fbuf) - 1; k++) {
            fbuf[fi++] = (char)('0' + (int)((f / divisor / 10) % 10));
            (void)0; /* unused branch */
            divisor /= 10;
        }
        /* Simpler: just snprintf with explicit padding string. */
        char zeros[8] = "000000";
        int leading = frac_digits;
        int64_t tmp = frac;
        while(tmp > 0) { tmp /= 10; leading--; }
        if(leading < 0) leading = 0;
        if(leading > 6) leading = 6;
        zeros[leading] = 0;
        snprintf(o, cap, "%lld.%s%lld %s",
                 (long long)whole, zeros, (long long)frac, unit ? unit : "");
        (void)fbuf; (void)fi;
    }
}

static bool record_is_useful(const WmbusRecord* r) {
    if(!r->quantity) return false;
    if(strcmp(r->quantity, "Unknown") == 0) return false;
    if(strcmp(r->quantity, "Manufacturer") == 0) return false;
    /* Skip integer/BCD records that are exactly zero — usually filler. */
    if((r->kind == WmbusValueInt || r->kind == WmbusValueBcd) && r->i == 0)
        return false;
    /* Skip empty date strings. */
    if(r->kind == WmbusValueDate && (!r->s_len || r->s[0] == 0)) return false;
    return true;
}

static void render_cb(const WmbusRecord* r, void* c) {
    RenderCtx* x = (RenderCtx*)c;
    if(x->pos >= x->cap || x->emitted >= RENDER_MAX_LINES) return;
    if(!record_is_useful(r)) return;

    char val[48];
    switch(r->kind) {
    case WmbusValueInt:
    case WmbusValueBcd:
        format_scaled_int(r->i, r->exponent, r->unit, val, sizeof(val));
        break;
    case WmbusValueReal: {
        /* IEEE float -> scaled integer (×1000) so we never need to print
         * a double. The toolchain treats fp constants as float, so we do
         * the multiplication via printf which accepts double natively. */
        snprintf(val, sizeof(val), "%.3f %s", r->r, r->unit ? r->unit : "");
        break;
    }
    case WmbusValueString:
    case WmbusValueDate:
        snprintf(val, sizeof(val), "%s", r->s); break;
    case WmbusValueRaw: {
        size_t k = r->raw_len < 6 ? r->raw_len : 6;
        char* p = val; *p = 0;
        for(size_t i = 0; i < k; i++)
            p += snprintf(p, val + sizeof(val) - p, "%02X", r->raw[i]);
        if(r->raw_len > k) snprintf(p, val + sizeof(val) - p, "..");
        break;
    }
    default:
        return; /* WmbusValueNone — skip silently */
    }

    int n = snprintf(x->out + x->pos, x->cap - x->pos, "%s: %s\n",
                     r->quantity, val);
    if(n > 0) {
        x->pos += (size_t)n;
        x->emitted++;
    }
}

size_t wmbus_app_render(const uint8_t* a, size_t len, char* out, size_t out_size) {
    RenderCtx c = { out, out_size, 0, 0 };
    if(out_size) out[0] = 0;
    wmbus_app_parse(a, len, render_cb, &c);
    return c.pos;
}
