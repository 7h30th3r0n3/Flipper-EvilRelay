// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 7h30th3r0n3. Part of Flipper-EvilRelay: https://github.com/7h30th3r0n3/Flipper-EvilRelay
// EvilRelay (Flipper) — ISO-DEP emulator half of the NFC relay.
// The Flipper emulates an ISO14443-4 card to the terminal (TPE) and relays the
// application APDUs over its USART (pins 13=TX / 14=RX) to a companion ESP32.
// The ESP32 bridges that UART to the air side — a phone over Wi-Fi/TCP, or an
// Evil reader unit over ESP-NOW — which reads the real card and returns the
// responses. Same relay frame protocol ([0x5A]...) runs end to end.
#include <furi.h>
#include <gui/gui.h>
#include <input/input.h>
#include "relay_uart.h"
#include "relay_nfc.h"

#define TAG "EvilRelay"
#define EVILRELAY_VERSION "1.1"

// UART link baud to the ESP32. Must match the ESP32 sketch (RELAY_UART_BAUD).
#define RELAY_UART_BAUD 115200

typedef enum {
    ViewStatus,
    ViewAbout,
} UiView;

typedef struct {
    volatile bool started;
    UiView view;
    uint32_t tick; // animation counter (++ each redraw)
    uint32_t start_tick; // for uptime
} RelayState;

static RelayState* g_st;

// --- one status row: a glyph (done / active-blink / pending) + label + value ---
static void
    draw_step(Canvas* c, int y, bool done, bool active, uint32_t tick, const char* label, const char* value) {
    int gx = 5, gy = y - 3;
    if(done) {
        canvas_draw_disc(c, gx, gy, 2); // filled = reached
    } else if(active) {
        canvas_draw_circle(c, gx, gy, 2);
        if((tick / 3) % 2 == 0) canvas_draw_dot(c, gx, gy); // blink: in progress
    } else {
        canvas_draw_circle(c, gx, gy, 2); // empty = pending
    }
    canvas_draw_str(c, 12, y, label);
    canvas_draw_str(c, 52, y, value);
}

static void draw_titlebar(Canvas* c, const char* title, const char* right) {
    canvas_draw_box(c, 0, 0, 128, 13);
    canvas_set_color(c, ColorWhite);
    canvas_set_font(c, FontPrimary);
    canvas_draw_str(c, 3, 10, title);
    if(right) {
        canvas_set_font(c, FontSecondary);
        canvas_draw_str_aligned(c, 125, 9, AlignRight, AlignBottom, right);
    }
    canvas_set_color(c, ColorBlack);
}

static void draw_status_view(Canvas* c, RelayState* st) {
    bool bridge = relay_nfc_bridge_alive() || relay_uart_link_seen();
    uint8_t rs = relay_nfc_reader_state();
    bool emul = relay_nfc_emulating();
    bool reader_ok = (rs == 2) || (rs == 3) || emul;
    const char* uid = relay_nfc_uid_str();
    bool card_ok = emul && uid[0] != 0;
    uint32_t apdu = relay_nfc_apdu_count();

    // Title bar + a blinking heartbeat dot while the bridge is alive.
    draw_titlebar(c, "EvilRelay", NULL);
    if(bridge && (st->tick / 3) % 2 == 0) {
        canvas_set_color(c, ColorWhite);
        canvas_draw_disc(c, 120, 6, 2);
        canvas_set_color(c, ColorBlack);
    }

    // Which step is the current frontier (gets the blinking "in progress" glyph).
    int frontier = !bridge ? 0 : !reader_ok ? 1 : !card_ok ? 2 : 3;

    canvas_set_font(c, FontSecondary);
    const char* rtxt = (rs == 2) ? "Wi-Fi" : (rs == 3) ? "ESP-NOW" : emul ? "linked" : "waiting";
    char uidv[20];
    if(card_ok) {
        snprintf(uidv, sizeof(uidv), "%.14s%s", uid, (uid[14] != 0) ? "~" : "");
    } else {
        snprintf(uidv, sizeof(uidv), "waiting");
    }
    char relv[20];
    if(apdu > 0)
        snprintf(relv, sizeof(relv), "%lu  %lums", (unsigned long)apdu, (unsigned long)relay_nfc_last_rtt());
    else
        snprintf(relv, sizeof(relv), "idle");

    draw_step(c, 22, bridge, frontier == 0, st->tick, "Bridge", bridge ? "online" : "waiting");
    draw_step(c, 32, reader_ok, frontier == 1, st->tick, "Reader", rtxt);
    draw_step(c, 42, card_ok, frontier == 2, st->tick, "Card", uidv);
    draw_step(c, 52, apdu > 0, frontier == 3, st->tick, "Relay", relv);

    // Footer: live counters + nav hint.
    canvas_draw_line(c, 0, 55, 128, 55);
    uint32_t up = (furi_get_tick() - st->start_tick) / 1000;
    char foot[28];
    snprintf(foot, sizeof(foot), "rx%lu  %lu:%02lu", (unsigned long)relay_uart_rx_bytes(), (unsigned long)(up / 60), (unsigned long)(up % 60));
    canvas_draw_str(c, 2, 63, foot);
    canvas_draw_str_aligned(c, 126, 63, AlignRight, AlignBottom, "OK:About");
}

static void draw_about_view(Canvas* c, RelayState* st) {
    UNUSED(st);
    draw_titlebar(c, "About", "v" EVILRELAY_VERSION);
    canvas_set_font(c, FontSecondary);
    canvas_draw_str(c, 3, 23, "ISO-DEP NFC relay");
    canvas_draw_str(c, 3, 33, "Flipper + ESP32 + phone");
    canvas_draw_str(c, 3, 43, "github.com/7h30th3r0n3");
    canvas_draw_str(c, 3, 52, " /Flipper-EvilRelay");
    canvas_draw_line(c, 0, 55, 128, 55);
    canvas_draw_str(c, 2, 63, "GPL-3.0  7h30th3r0n3");
    canvas_draw_str_aligned(c, 126, 63, AlignRight, AlignBottom, "OK:back");
}

static void relay_draw(Canvas* c, void* ctx) {
    RelayState* st = (RelayState*)ctx;
    canvas_clear(c);

    if(!st->started) {
        draw_titlebar(c, "EvilRelay", NULL);
        canvas_set_font(c, FontSecondary);
        canvas_draw_str(c, 3, 30, "UART start FAILED");
        canvas_draw_str(c, 3, 42, "ESP32 on pins 13/14?");
        canvas_draw_str_aligned(c, 126, 63, AlignRight, AlignBottom, "Back:exit");
        return;
    }

    if(st->view == ViewAbout)
        draw_about_view(c, st);
    else
        draw_status_view(c, st);
}

static void relay_input(InputEvent* e, void* ctx) {
    FuriMessageQueue* q = (FuriMessageQueue*)ctx;
    furi_message_queue_put(q, e, FuriWaitForever);
}

int32_t evil_relay_app(void* p) {
    UNUSED(p);
    g_st = malloc(sizeof(RelayState));
    memset(g_st, 0, sizeof(RelayState));
    g_st->view = ViewStatus;
    g_st->start_tick = furi_get_tick();
    FuriMessageQueue* queue = furi_message_queue_alloc(8, sizeof(InputEvent));

    ViewPort* vp = view_port_alloc();
    view_port_draw_callback_set(vp, relay_draw, g_st);
    view_port_input_callback_set(vp, relay_input, queue);
    Gui* gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(gui, vp, GuiLayerFullscreen);

    // Bring up the NFC emulator first (it waits for the IDENT), then the UART
    // transport that will feed it frames from the ESP32.
    relay_nfc_start();
    g_st->started = relay_uart_start(RELAY_UART_BAUD, relay_nfc_on_frame, NULL);
    if(g_st->started) {
        FURI_LOG_I(TAG, "relay up: UART @ %d, NFC ready", RELAY_UART_BAUD);
    } else {
        FURI_LOG_E(TAG, "relay_uart_start FAILED");
    }

    InputEvent e;
    bool run = true; // stay on screen even if the UART failed, so we can see why
    uint32_t tick = 0;
    while(run) {
        if(furi_message_queue_get(queue, &e, 100) == FuriStatusOk) {
            if(e.type == InputTypeShort) {
                switch(e.key) {
                case InputKeyBack:
                    run = false;
                    break;
                case InputKeyOk:
                    g_st->view = (g_st->view == ViewStatus) ? ViewAbout : ViewStatus;
                    break;
                case InputKeyRight:
                    g_st->view = ViewAbout;
                    break;
                case InputKeyLeft:
                    g_st->view = ViewStatus;
                    break;
                default:
                    break;
                }
            }
        }
        g_st->tick = ++tick;
        if(tick % 20 == 0) {
            FURI_LOG_I(
                TAG,
                "alive started=%d rx=%lu apdu=%lu",
                g_st->started,
                (unsigned long)relay_uart_rx_bytes(),
                (unsigned long)relay_nfc_apdu_count());
        }
        view_port_update(vp);
    }

    relay_uart_stop();
    relay_nfc_stop();

    gui_remove_view_port(gui, vp);
    furi_record_close(RECORD_GUI);
    view_port_free(vp);
    furi_message_queue_free(queue);
    free(g_st);
    return 0;
}
