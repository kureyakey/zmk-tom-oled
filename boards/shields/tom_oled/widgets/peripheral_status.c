/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/input/input.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/display.h>
#include <zmk/event_manager.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/events/split_peripheral_status_changed.h>
#include <zmk/split/bluetooth/peripheral.h>

#include "assets/peripheral_cat_images.h"
#include "peripheral_status.h"

static sys_slist_t widgets = SYS_SLIST_STATIC_INIT(&widgets);
static atomic_t trackball_activity;

#define TOM_OLED_PERIPHERAL_WIDTH 128
#define TOM_OLED_PERIPHERAL_HEIGHT 32
#define TOM_OLED_ICON_WIDTH 64
#define TOM_OLED_ICON_HEIGHT 32
#define TOM_OLED_MOVE_HOLD_MS 700
#define TOM_OLED_TYPE_HOLD_MS 500
#define TOM_OLED_ANIM_CONNECTED_MS 450
#define TOM_OLED_ANIM_MOVING_MS 120
#define TOM_OLED_ANIM_DISCONNECTED_MS 850

struct peripheral_status_state {
    bool connected;
};

struct peripheral_key_state {
    bool pressed;
};

static void set_px(lv_obj_t *canvas, int16_t x, int16_t y) {
    if (x >= 0 && x < TOM_OLED_ICON_WIDTH && y >= 0 && y < TOM_OLED_ICON_HEIGHT) {
        lv_canvas_set_px(canvas, x, y, lv_color_black());
    }
}

static void draw_bitmap(lv_obj_t *canvas, const uint32_t rows[TOM_OLED_ICON_HEIGHT][2]) {
    for (uint8_t y = 0; y < TOM_OLED_ICON_HEIGHT; y++) {
        for (uint8_t x = 0; x < TOM_OLED_ICON_WIDTH; x++) {
            uint32_t word = rows[y][x < 32 ? 0 : 1];
            uint8_t bit = x < 32 ? x : x - 32;
            if ((word & BIT(31 - bit)) != 0) {
                set_px(canvas, x, y);
            }
        }
    }
}

static void draw_icon(struct zmk_widget_peripheral_status *widget) {
    lv_canvas_fill_bg(widget->canvas, lv_color_white(), LV_OPA_COVER);

    const uint32_t (*frames[2])[2] = {
        !widget->connected  ? tom_oled_cat_disconnect_1
        : widget->moving    ? tom_oled_cat_move_1
                             : tom_oled_cat_idle_1,
        !widget->connected  ? tom_oled_cat_disconnect_2
        : widget->moving    ? tom_oled_cat_move_2
                             : tom_oled_cat_idle_2,
    };

    draw_bitmap(widget->canvas, frames[widget->frame % 2]);
}

static void update_labels(struct zmk_widget_peripheral_status *widget) {
    lv_label_set_text_fmt(widget->connection_label, "R %s", widget->connected ? "OK" : "--");

    lv_label_set_text(widget->mode_label, widget->moving ? "MOVE" : widget->typing ? "KEY" : "PTR");
}

static void refresh_widget(struct zmk_widget_peripheral_status *widget) {
    if (widget->anim_timer != NULL) {
        lv_timer_set_period(widget->anim_timer,
                            widget->moving       ? TOM_OLED_ANIM_MOVING_MS
                            : widget->connected ? TOM_OLED_ANIM_CONNECTED_MS
                                                : TOM_OLED_ANIM_DISCONNECTED_MS);
    }

    update_labels(widget);
    draw_icon(widget);
}

static void anim_timer_cb(lv_timer_t *timer) {
    struct zmk_widget_peripheral_status *widget = timer->user_data;
    int64_t now = k_uptime_get();
    bool refresh = false;

    if (atomic_cas(&trackball_activity, 1, 0)) {
        widget->moving = true;
        widget->moving_until = now + TOM_OLED_MOVE_HOLD_MS;
        refresh = true;
    }

    if (widget->moving && now >= widget->moving_until) {
        widget->moving = false;
        refresh = true;
    }

    if (widget->typing && now >= widget->typing_until) {
        widget->typing = false;
        refresh = true;
    }

    if (refresh) {
        refresh_widget(widget);
    }

    widget->frame++;
    draw_icon(widget);
}

static void set_connection_status(struct zmk_widget_peripheral_status *widget,
                                  struct peripheral_status_state state) {
    widget->connected = state.connected;
    refresh_widget(widget);
}

static struct peripheral_status_state peripheral_status_get_state(const zmk_event_t *_eh) {
    return (struct peripheral_status_state){.connected = zmk_split_bt_peripheral_is_connected()};
}

static void peripheral_status_update_cb(struct peripheral_status_state state) {
    struct zmk_widget_peripheral_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) { set_connection_status(widget, state); }
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_tom_oled_peripheral_status, struct peripheral_status_state,
                            peripheral_status_update_cb, peripheral_status_get_state)
ZMK_SUBSCRIPTION(widget_tom_oled_peripheral_status, zmk_split_peripheral_status_changed);

static void set_key_status(struct zmk_widget_peripheral_status *widget,
                           struct peripheral_key_state state) {
    if (!state.pressed) {
        return;
    }

    widget->typing = true;
    widget->typing_until = k_uptime_get() + TOM_OLED_TYPE_HOLD_MS;
    refresh_widget(widget);
}

static struct peripheral_key_state peripheral_key_get_state(const zmk_event_t *eh) {
    const struct zmk_position_state_changed *ev = as_zmk_position_state_changed(eh);

    return (struct peripheral_key_state){.pressed = ev != NULL && ev->state};
}

static void peripheral_key_update_cb(struct peripheral_key_state state) {
    struct zmk_widget_peripheral_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) { set_key_status(widget, state); }
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_tom_oled_peripheral_key, struct peripheral_key_state,
                            peripheral_key_update_cb, peripheral_key_get_state)
ZMK_SUBSCRIPTION(widget_tom_oled_peripheral_key, zmk_position_state_changed);

static void peripheral_input_listener(struct input_event *ev) {
    if (ev->type == INPUT_EV_REL && ev->value != 0) {
        atomic_set(&trackball_activity, 1);
    }
}

INPUT_CALLBACK_DEFINE(NULL, peripheral_input_listener);

int zmk_widget_peripheral_status_init(struct zmk_widget_peripheral_status *widget,
                                      lv_obj_t *parent) {
    widget->obj = lv_obj_create(parent);
    lv_obj_set_size(widget->obj, TOM_OLED_PERIPHERAL_WIDTH, TOM_OLED_PERIPHERAL_HEIGHT);

    widget->canvas = lv_canvas_create(widget->obj);
    lv_canvas_set_buffer(widget->canvas, widget->cbuf, TOM_OLED_ICON_WIDTH, TOM_OLED_ICON_HEIGHT,
                         LV_IMG_CF_TRUE_COLOR);
    lv_obj_set_size(widget->canvas, TOM_OLED_ICON_WIDTH, TOM_OLED_ICON_HEIGHT);
    lv_obj_align(widget->canvas, LV_ALIGN_LEFT_MID, 0, 0);

    widget->connection_label = lv_label_create(widget->obj);
    lv_obj_set_width(widget->connection_label, 34);
    lv_label_set_long_mode(widget->connection_label, LV_LABEL_LONG_CLIP);
    lv_obj_align(widget->connection_label, LV_ALIGN_TOP_LEFT, 68, 0);

    widget->mode_label = lv_label_create(widget->obj);
    lv_obj_set_width(widget->mode_label, 60);
    lv_label_set_long_mode(widget->mode_label, LV_LABEL_LONG_CLIP);
    lv_obj_align(widget->mode_label, LV_ALIGN_BOTTOM_LEFT, 68, 0);

    sys_slist_append(&widgets, &widget->node);

    widget->connected = zmk_split_bt_peripheral_is_connected();
    widget->anim_timer = lv_timer_create(anim_timer_cb, TOM_OLED_ANIM_CONNECTED_MS, widget);
    refresh_widget(widget);

    widget_tom_oled_peripheral_status_init();
    widget_tom_oled_peripheral_key_init();

    return 0;
}

lv_obj_t *zmk_widget_peripheral_status_obj(struct zmk_widget_peripheral_status *widget) {
    return widget->obj;
}
