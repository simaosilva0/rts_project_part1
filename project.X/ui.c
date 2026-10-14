#include <stdint.h>
#include "ui.h"
#include "system.h"
#include "clock.h"
#include "lcd.h"

/* The clock tick has 1 s granularity, so a view of N s needs N+1 ticks to
 * last at least N s (the first tick lands 0-1 s after the press). */
#define THRESH_TICKS 3   /* threshold view: >= 2 s */
#define REC_TICKS    2   /* each record view: >= 1 s */

typedef enum { UI_NORMAL, UI_THRESH, UI_CONFIG, UI_REC_T, UI_REC_L } ui_state_t;
typedef enum { F_H, F_M, F_S, F_C, F_T, F_L, F_A, F_R, F_COUNT } field_t;

static ui_state_t state;
static field_t    field;
static uint8_t    sub;        /* 0 = selecting; >0 = editing a threshold (C: 1=h 2=m 3=s) */
static uint8_t    timer;      /* ticks left in a timed view */
static uint8_t    dirty;      /* screen needs a redraw */
static uint8_t    prev_s;
static uint8_t    cursor_on;

/* ---------- drawing ---------- */

static void put_time(uint8_t h, uint8_t m, uint8_t s)
{
    lcd_put2(h);
    lcd_putc(':');
    lcd_put2(m);
    lcd_putc(':');
    lcd_put2(s);
}

static void put_val2(uint8_t v)
{
    if (v > 99) {
        lcd_puts("--");           /* empty record after reset */
    } else {
        lcd_put2(v);
    }
}

static void put_lvl(uint8_t l)
{
    lcd_putc(l <= 7 ? (char)('0' + l) : '-');
}

/* "hh:mm:ss CTL AR" */
static void draw_row0(uint8_t h, uint8_t m, uint8_t s,
                      uint8_t all_letters, uint8_t show_r)
{
    lcd_goto(0, 0);
    put_time(h, m, s);
    lcd_putc(' ');
    lcd_putc((all_letters || (alarms & ALARM_C)) ? 'C' : ' ');
    lcd_putc((all_letters || (alarms & ALARM_T)) ? 'T' : ' ');
    lcd_putc((all_letters || (alarms & ALARM_L)) ? 'L' : ' ');
    lcd_putc(' ');
    lcd_putc(cfg.alaf ? 'A' : 'a');
    lcd_putc(show_r ? 'R' : ' ');
}

/* "tt C L l" */
static void draw_row1(uint8_t t, uint8_t l)
{
    lcd_goto(1, 0);
    put_val2(t);
    lcd_puts(" C L ");
    put_lvl(l);
}

/* "hh:mm:ss ttC Ll" (16 columns do not fit the spec's "tt C L l") */
static void draw_rec(uint8_t row, const record_t *r)
{
    lcd_goto(row, 0);
    put_time(r->h, r->m, r->s);
    lcd_putc(' ');
    put_val2(r->t);
    lcd_putc('C');
    lcd_putc(' ');
    lcd_putc('L');
    put_lvl(r->l);
}

static void place_cursor(void)
{
    static const uint8_t col0[F_COUNT] = { 0, 3, 6, 9, 10, 11, 13, 14 };
    uint8_t row = 0;
    uint8_t col = col0[field];

    if (sub) {
        if (field == F_C) {
            col = (uint8_t)((sub - 1) * 3);
        } else if (field == F_T) {
            row = 1;
            col = 0;
        } else {
            row = 1;
            col = 7;
        }
    }
    lcd_goto(row, col);
    if (!cursor_on) {
        lcd_cursor(1, 1);
        cursor_on = 1;
    }
}

static void draw(void)
{
    switch (state) {
    case UI_NORMAL:
        draw_row0(now.h, now.m, now.s, 0, 0);
        draw_row1(last_t, last_l);
        break;

    case UI_THRESH:
        draw_row0(cfg.alah, cfg.alam, cfg.alas, 1, 0);
        draw_row1(cfg.alat, cfg.alal);
        break;

    case UI_CONFIG:
        if (field == F_C) {
            draw_row0(cfg.alah, cfg.alam, cfg.alas, 1, 1);
        } else {
            draw_row0(now.h, now.m, now.s, 1, 1);
        }
        draw_row1(field == F_T ? cfg.alat : last_t,
                  field == F_L ? cfg.alal : last_l);
        break;

    case UI_REC_T:
        draw_rec(0, &records[REC_TMAX]);
        draw_rec(1, &records[REC_TMIN]);
        break;

    case UI_REC_L:
        draw_rec(0, &records[REC_LMAX]);
        draw_rec(1, &records[REC_LMIN]);
        break;
    }

    if (state == UI_CONFIG) {
        place_cursor();
    } else if (cursor_on) {
        lcd_cursor(0, 0);
        cursor_on = 0;
    }
}

/* ---------- state handling ---------- */

static void set_state(ui_state_t s, uint8_t ticks_left)
{
    state = s;
    timer = ticks_left;
    dirty = 1;
    lcd_clear();
}

static void reset_records(void)
{
    uint8_t i;

    for (i = 0; i < REC_COUNT; i++) {
        records[i].h = 0;
        records[i].m = 0;
        records[i].s = 0;
        records[i].t = 0;
        records[i].l = 0;
    }
    records[REC_TMIN].t = 255;
    records[REC_TMIN].l = 255;
    records[REC_LMIN].t = 255;
    records[REC_LMIN].l = 255;
    cfg_dirty = 1;
}

static void config_s1(void)
{
    if (sub) {
        if (field == F_C && sub < 3) {
            sub++;
        } else {
            sub = 0;
        }
    } else if (++field >= F_COUNT) {
        sub = 0;
        set_state(UI_NORMAL, 0);
        return;
    }
    dirty = 1;
}

static void config_s2(void)
{
    switch (field) {
    case F_H:
        clock_set((uint8_t)((now.h + 1) % 24), now.m, now.s);
        cfg_dirty = 1;
        break;
    case F_M:
        clock_set(now.h, (uint8_t)((now.m + 1) % 60), now.s);
        cfg_dirty = 1;
        break;
    case F_S:
        clock_set(now.h, now.m, (uint8_t)((now.s + 1) % 60));
        cfg_dirty = 1;
        break;
    case F_C:
        if (!sub) {
            sub = 1;
        } else {
            if (sub == 1) {
                cfg.alah = (uint8_t)((cfg.alah + 1) % 24);
            } else if (sub == 2) {
                cfg.alam = (uint8_t)((cfg.alam + 1) % 60);
            } else {
                cfg.alas = (uint8_t)((cfg.alas + 1) % 60);
            }
            cfg_dirty = 1;
        }
        break;
    case F_T:
        if (!sub) {
            sub = 1;
        } else {
            cfg.alat = (uint8_t)((cfg.alat + 1) % 51);
            cfg_dirty = 1;
        }
        break;
    case F_L:
        if (!sub) {
            sub = 1;
        } else {
            cfg.alal = (uint8_t)((cfg.alal + 1) % 8);
            cfg_dirty = 1;
        }
        break;
    case F_A:
        cfg.alaf ^= 1;
        cfg_dirty = 1;
        break;
    case F_R:
        reset_records();
        break;
    default:
        break;
    }
    dirty = 1;
}

static void on_s1(void)
{
    switch (state) {
    case UI_NORMAL:
    case UI_REC_T:
    case UI_REC_L:
        alarms = 0;                         /* clear C/T/L notifications */
        set_state(UI_THRESH, THRESH_TICKS);
        break;
    case UI_THRESH:
        field = F_H;
        sub = 0;
        set_state(UI_CONFIG, 0);
        break;
    case UI_CONFIG:
        config_s1();
        break;
    }
}

static void on_s2(void)
{
    if (state == UI_NORMAL) {
        set_state(UI_REC_T, REC_TICKS);
    } else if (state == UI_CONFIG) {
        config_s2();
    }
}

/* ---------- public ---------- */

void ui_init(void)
{
    lcd_init();
    state = UI_NORMAL;
    field = F_H;
    sub = 0;
    timer = 0;
    cursor_on = 0;
    prev_s = now.s;
    dirty = 1;
}

void ui_task(void)
{
    uint8_t sec = (uint8_t)(now.s != prev_s);
    prev_s = now.s;

    if (sec) {
        dirty = 1;                          /* time/readings may have changed */
        if (timer && --timer == 0) {
            if (state == UI_THRESH) {
                set_state(UI_NORMAL, 0);
            } else if (state == UI_REC_T) {
                set_state(UI_REC_L, REC_TICKS);
            } else if (state == UI_REC_L) {
                set_state(UI_NORMAL, 0);
            }
        }
    }

    if (s1_pressed) {
        s1_pressed = 0;
        on_s1();
    }
    if (s2_pressed) {
        s2_pressed = 0;
        on_s2();
    }

    if (dirty) {
        dirty = 0;
        draw();
    }
}
