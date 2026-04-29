/* Meter list scene: snapshot app->meters[] under the lock, sort and
 * filter, push the result to ScanCanvas. The widget owns its own
 * cursor/scroll state; we re-render row contents in place. */

#include "../wmbus_app_i.h"
#include "../protocol/wmbus_manuf.h"
#include "../protocol/wmbus_medium.h"
#include "../protocol/wmbus_models.h"
#include "scan_canvas.h"
#include <furi_hal_light.h>
#include <furi_hal_resources.h>
#include <stdio.h>
#include <string.h>

/* Compact per-row snapshot used outside the lock. */

typedef struct {
    uint16_t manuf;
    uint32_t id;
    uint8_t  version;
    uint8_t  medium;
    int8_t   rssi;
    bool     encrypted;
    bool     have_key;
    bool     decrypted_ok;
    uint32_t last_seen_tick;
    uint32_t telegram_count;
    char     last_value[32];
} MeterRow;

static bool snapshot(WmbusApp* app, MeterRow* snap, size_t* total_out,
                     WmbusFilter* fout, WmbusSort* sout) {
    /* Short timeout — never block the GUI thread on the worker. */
    if(furi_mutex_acquire(app->lock, 25) != FuriStatusOk) return false;
    size_t n = app->meter_count;
    *total_out = n;
    *fout = app->settings.filter;
    *sout = app->settings.sort;
    for(size_t i = 0; i < n; i++) {
        const WmbusMeter* m = &app->meters[i];
        snap[i].manuf   = m->manuf;
        snap[i].id      = m->id;
        snap[i].version = m->version;
        snap[i].medium  = m->medium;
        snap[i].rssi    = m->rssi;
        snap[i].encrypted = m->encrypted;
        snap[i].have_key = m->have_key;
        snap[i].decrypted_ok = m->decrypted_ok;
        snap[i].last_seen_tick = m->last_seen_tick;
        snap[i].telegram_count = m->telegram_count;
        memcpy(snap[i].last_value, m->last_value, sizeof(snap[i].last_value));
    }
    furi_mutex_release(app->lock);
    return true;
}

/* ---- Filter + sort ---- */

static int row_cmp(const MeterRow* a, const MeterRow* b, WmbusSort sort) {
    switch(sort) {
    case WmbusSortRecent:
        return (int32_t)(a->last_seen_tick - b->last_seen_tick) > 0 ? 1 :
               (int32_t)(a->last_seen_tick - b->last_seen_tick) < 0 ? -1 : 0;
    case WmbusSortId:
        if(a->id < b->id) return 1;
        if(a->id > b->id) return -1;
        return 0;
    case WmbusSortPackets:
        if(a->telegram_count > b->telegram_count) return 1;
        if(a->telegram_count < b->telegram_count) return -1;
        return 0;
    case WmbusSortRssi:
    default:
        return (a->rssi > b->rssi) ? 1 : (a->rssi < b->rssi ? -1 : 0);
    }
}

static size_t build_order(const MeterRow* snap, size_t total,
                          WmbusFilter filter, WmbusSort sort,
                          uint16_t* order) {
    for(size_t i = 0; i < total; i++) order[i] = (uint16_t)i;
    size_t n = total;
    /* Top-N by RSSI before applying the user-chosen sort. */
    size_t cap = total;
    if(filter == WmbusFilterTop10) cap = 10;
    else if(filter == WmbusFilterTop5) cap = 5;
    else if(filter == WmbusFilterTop3) cap = 3;
    if(cap < n) {
        for(size_t i = 1; i < n; i++) {
            uint16_t v = order[i]; int8_t r = snap[v].rssi;
            size_t j = i;
            while(j > 0 && snap[order[j-1]].rssi < r) {
                order[j] = order[j-1]; j--;
            }
            order[j] = v;
        }
        n = cap;
    }
    /* Now apply user-selected sort. */
    for(size_t i = 1; i < n; i++) {
        uint16_t v = order[i];
        size_t j = i;
        while(j > 0 && row_cmp(&snap[v], &snap[order[j-1]], sort) > 0) {
            order[j] = order[j-1]; j--;
        }
        order[j] = v;
    }
    return n;
}

/* ---- Friendly head string ----
 *
 * Format chosen to be compact AND uniquely identify each meter at a glance:
 *
 *   "TCH 03776014"
 *   "KAM 12345678"
 *
 * The full ID is the only piece of info that's always definitive (multiple
 * neighbours can have the same model/medium combination). On a 128-px-wide
 * screen the 12-character form leaves room for a "Now 1234u"-style value
 * suffix on the right.
 *
 * If the medium is recognised we add a tiny one-letter hint between the
 * manufacturer code and the ID:
 *   H = heat-cost allocator    W = water    E = electricity
 *   T = heat / cooling         G = gas      .. otherwise no hint
 */
static char medium_letter(uint8_t med) {
    switch(med) {
        case 0x08: return 'H';   /* heat-cost allocator       */
        case 0x07: return 'W';   /* water                     */
        case 0x06: return 'W';   /* warm water                */
        case 0x04: return 'T';   /* heat                      */
        case 0x0A: return 'C';   /* cooling out               */
        case 0x0B: return 'C';   /* cooling in                */
        case 0x02: return 'E';   /* electricity               */
        case 0x03: return 'G';   /* gas                       */
        default:   return ' ';
    }
}

static void format_head(const MeterRow* r, char* head, size_t cap) {
    char m[4]; wmbus_manuf_decode(r->manuf, m);
    char letter = medium_letter(r->medium);
    if(letter == ' ') {
        snprintf(head, cap, "%s %08lX", m, (unsigned long)r->id);
    } else {
        snprintf(head, cap, "%s%c %08lX", m, letter, (unsigned long)r->id);
    }
}

/* ---- Header / footer label helpers ---- */

static void format_band(WmbusApp* app, char* out, size_t cap) {
    const char* mn = "T1";
    switch(app->settings.mode) {
        case WmbusModeT1:  mn = "T1";  break;
        case WmbusModeC1:  mn = "C1";  break;
        case WmbusModeCT:  mn = "T+C"; break;
        case WmbusModeS1:  mn = "S1";  break;
        default: break;
    }
    unsigned f = (unsigned)(app->settings.freq_hz / 1000);
    snprintf(out, cap, "%u.%02u %s", f / 1000, (f / 10) % 100, mn);
}

static const char* filter_label(WmbusFilter f) {
    switch(f) {
        case WmbusFilterTop10: return "Top10";
        case WmbusFilterTop5:  return "Top5";
        case WmbusFilterTop3:  return "Top3";
        default: return "All";
    }
}
static const char* sort_label(WmbusSort s) {
    switch(s) {
        case WmbusSortRecent:  return "Recent";
        case WmbusSortId:      return "ID";
        case WmbusSortPackets: return "Packets";
        default: return "Signal";
    }
}

/* ---- Tap callback bridge ---- */

static void on_pick(void* ctx, uint16_t meter_idx) {
    WmbusApp* app = ctx;
    app->selected = (int)meter_idx;
    scene_manager_next_scene(app->scene_manager, WmbusSceneDetail);
}

/* ---- Repopulate the canvas ---- */

static void rebuild(WmbusApp* app) {
    MeterRow snap[WMBUS_MAX_METERS];
    uint16_t order[WMBUS_MAX_METERS];
    WmbusFilter filter;
    WmbusSort   sort;
    size_t total = 0;
    if(!snapshot(app, snap, &total, &filter, &sort)) return;
    size_t n = build_order(snap, total, filter, sort, order);

    /* Translate ordered MeterRow → ScanRow for the widget. */
    static ScanRow rows[WMBUS_MAX_METERS];   /* GUI thread only */
    for(size_t i = 0; i < n; i++) {
        const MeterRow* r = &snap[order[i]];
        rows[i].meter_idx = order[i];
        rows[i].rssi = r->rssi;
        rows[i].encrypted = r->encrypted;
        format_head(r, rows[i].head, sizeof(rows[i].head));
        if(r->encrypted && !r->decrypted_ok) {
            /* Distinguish ENC (no key) vs BAD (key wrong/decrypt failed)
             * so the user can tell at a glance which meters need key
             * fixes vs which ones are not yet keyed at all. */
            const char* tag = r->have_key ? "BAD" : "ENC";
            strncpy(rows[i].value, tag, sizeof(rows[i].value) - 1);
            rows[i].value[sizeof(rows[i].value) - 1] = 0;
        } else {
            strncpy(rows[i].value, r->last_value, sizeof(rows[i].value) - 1);
            rows[i].value[sizeof(rows[i].value) - 1] = 0;
        }
    }
    scan_canvas_set_rows(app->scan_canvas, rows, n);

    char band[16];
    format_band(app, band, sizeof(band));
    scan_canvas_set_meta(app->scan_canvas, band,
                         filter_label(filter), sort_label(sort),
                         app->total_telegrams, "");
}

/* ---- Scene plumbing ---- */

void wmbus_view_meters_enter(void* ctx) {
    WmbusApp* app = ctx;
    scan_canvas_set_tap_callback(app->scan_canvas, on_pick, app);
    rebuild(app);
    view_dispatcher_switch_to_view(app->view_dispatcher, WmbusViewScan);
    wmbus_scanning_start(app);
    app->on_meters_view = true;
    app->last_shown_seq = app->telegram_seq;
}

void wmbus_view_meters_refresh(WmbusApp* app) {
    if(!app->on_meters_view) return;
    rebuild(app);
}

bool wmbus_view_meters_event(void* ctx, SceneManagerEvent ev) {
    (void)ctx; (void)ev;
    return false;
}

void wmbus_view_meters_exit(void* ctx) {
    WmbusApp* app = ctx;
    app->on_meters_view = false;
    /* Worker keeps running across navigation; only the LED/notification
     * cue is gated by the led_active flag in wmbus_app.c. */
}
