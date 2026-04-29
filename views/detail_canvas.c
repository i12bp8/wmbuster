/* Detail canvas: inverted 18px header (id, RSSI, subtitle, badge, pkt count)
 * + a scrollable list of label/value rows. Up/Down scrolls. */

#include "detail_canvas.h"
#include "../wmbus_app_i.h"

#include <gui/canvas.h>
#include <gui/elements.h>
#include <input/input.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define DETAIL_HEADER_H   18
#define DETAIL_ROW_H      9
#define DETAIL_LIST_TOP   (DETAIL_HEADER_H + 1)
#define DETAIL_VISIBLE    ((64 - DETAIL_LIST_TOP) / DETAIL_ROW_H)
#define DETAIL_MAX_ROWS   16

typedef struct {
    char     id_line[24];
    char     subtitle[28];
    int8_t   rssi;
    uint32_t telegram_count;
    bool     encrypted;
    char     badge[5];           /* "ENC", "DEC", "BAD" or empty           */
    DetailRow rows[DETAIL_MAX_ROWS];
    size_t   row_count;
    size_t   scroll;
} DetailModel;

struct DetailCanvas {
    View* view;
};

/* ---------- Drawing ---------- */

static void draw_header(Canvas* c, const DetailModel* m) {
    canvas_set_color(c, ColorBlack);
    canvas_draw_box(c, 0, 0, 128, DETAIL_HEADER_H);
    canvas_set_color(c, ColorWhite);

    canvas_set_font(c, FontPrimary);
    canvas_draw_str(c, 2, 9, m->id_line);

    /* RSSI on the right of the first line. */
    char rssi[10];
    snprintf(rssi, sizeof(rssi), "%ddBm", (int)m->rssi);
    canvas_set_font(c, FontSecondary);
    int rw = canvas_string_width(c, rssi);
    canvas_draw_str(c, 128 - rw - 2, 9, rssi);

    /* Second line: subtitle + optional badge + packet count. */
    char tail[24];
    if(m->badge[0])
        snprintf(tail, sizeof(tail), "%s %lu pkt",
                 m->badge, (unsigned long)m->telegram_count);
    else
        snprintf(tail, sizeof(tail), "%lu pkt",
                 (unsigned long)m->telegram_count);
    int tw = canvas_string_width(c, tail);

    /* Truncate subtitle to fit alongside the tail. */
    char sub[28];
    strncpy(sub, m->subtitle, sizeof(sub) - 1); sub[sizeof(sub)-1] = 0;
    int max_sub_w = 128 - 4 - tw - 2;
    while(canvas_string_width(c, sub) > max_sub_w && sub[0]) sub[strlen(sub) - 1] = 0;

    canvas_draw_str(c, 2, 17, sub);
    canvas_draw_str(c, 128 - tw - 2, 17, tail);

    canvas_set_color(c, ColorBlack);
}

static void draw_rows(Canvas* c, const DetailModel* m) {
    canvas_set_color(c, ColorBlack);
    canvas_set_font(c, FontSecondary);

    if(m->row_count == 0) {
        canvas_draw_str_aligned(c, 64, 38, AlignCenter, AlignCenter,
                                "(no data yet)");
        return;
    }

    size_t end = m->scroll + DETAIL_VISIBLE;
    if(end > m->row_count) end = m->row_count;

    for(size_t i = m->scroll; i < end; i++) {
        int y = DETAIL_LIST_TOP + (int)(i - m->scroll) * DETAIL_ROW_H + DETAIL_ROW_H - 2;
        const DetailRow* r = &m->rows[i];
        canvas_draw_str(c, 2, y, r->label);
        /* Right-aligned value. */
        int vw = canvas_string_width(c, r->value);
        canvas_draw_str(c, 128 - vw - (m->row_count > DETAIL_VISIBLE ? 6 : 2), y, r->value);
    }

    if(m->row_count > DETAIL_VISIBLE) {
        elements_scrollbar_pos(
            c, 127, DETAIL_LIST_TOP, 64 - DETAIL_LIST_TOP,
            m->scroll, m->row_count - DETAIL_VISIBLE + 1);
    }
}

static void detail_view_draw(Canvas* c, void* m_) {
    const DetailModel* m = m_;
    canvas_clear(c);
    draw_header(c, m);
    draw_rows(c, m);
}

static bool detail_view_input(InputEvent* ev, void* ctx) {
    DetailCanvas* dc = ctx;
    if(ev->type != InputTypeShort && ev->type != InputTypeRepeat) return false;
    if(ev->key != InputKeyUp && ev->key != InputKeyDown) return false;
    bool need = false;
    with_view_model(dc->view, DetailModel * m,
        {
            if(m->row_count > DETAIL_VISIBLE) {
                if(ev->key == InputKeyUp && m->scroll > 0) { m->scroll--; need = true; }
                else if(ev->key == InputKeyDown &&
                        m->scroll + DETAIL_VISIBLE < m->row_count) { m->scroll++; need = true; }
            }
        },
        need);
    return true;
}

/* ---------- Public API ---------- */

DetailCanvas* detail_canvas_alloc(void) {
    DetailCanvas* dc = malloc(sizeof(DetailCanvas));
    memset(dc, 0, sizeof(*dc));
    dc->view = view_alloc();
    view_allocate_model(dc->view, ViewModelTypeLocking, sizeof(DetailModel));
    view_set_context(dc->view, dc);
    view_set_draw_callback(dc->view, detail_view_draw);
    view_set_input_callback(dc->view, detail_view_input);
    return dc;
}

void detail_canvas_free(DetailCanvas* dc) {
    if(!dc) return;
    view_free(dc->view);
    free(dc);
}

View* detail_canvas_get_view(DetailCanvas* dc) { return dc->view; }

void detail_canvas_set_header(DetailCanvas* dc,
                              const char* line1, const char* line2,
                              int8_t rssi, uint32_t pkt, bool encrypted,
                              const char* badge) {
    with_view_model(dc->view, DetailModel * m,
        {
            strncpy(m->id_line,  line1 ? line1 : "", sizeof(m->id_line)  - 1);
            strncpy(m->subtitle, line2 ? line2 : "", sizeof(m->subtitle) - 1);
            m->id_line[sizeof(m->id_line) - 1] = 0;
            m->subtitle[sizeof(m->subtitle) - 1] = 0;
            m->rssi = rssi;
            m->telegram_count = pkt;
            m->encrypted = encrypted;
            if(badge) {
                strncpy(m->badge, badge, sizeof(m->badge) - 1);
                m->badge[sizeof(m->badge) - 1] = 0;
            } else {
                m->badge[0] = 0;
            }
        },
        true);
}

void detail_canvas_set_rows(DetailCanvas* dc, const DetailRow* rows, size_t n) {
    if(n > DETAIL_MAX_ROWS) n = DETAIL_MAX_ROWS;
    with_view_model(dc->view, DetailModel * m,
        {
            for(size_t i = 0; i < n; i++) m->rows[i] = rows[i];
            m->row_count = n;
            if(m->scroll > n) m->scroll = 0;
        },
        true);
}
