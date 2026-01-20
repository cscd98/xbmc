/*
 *  Copyright (C) 2017-2026 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include <stdint.h>

#define WL_CLOSURE_MAX_ARGS 20

struct wl_interface;
struct wl_proxy;
struct wl_display;
struct wl_event_queue;
union wl_argument;
typedef int (*wl_dispatcher_func_t)(const void*, void*, uint32_t, const struct wl_message*, union wl_argument*);

#ifdef __cplusplus
extern "C" { 
#endif

uint32_t wl_proxy_get_version(struct wl_proxy* proxy);

struct wl_proxy* wl_proxy_marshal_constructor(
    struct wl_proxy* proxy,
    uint32_t opcode,
    const struct wl_interface* interface,
    ...);

struct wl_proxy* wl_proxy_marshal_constructor_versioned(
    struct wl_proxy* proxy,
    uint32_t opcode,
    const struct wl_interface* interface,
    uint32_t version,
    ...);

int wl_display_prepare_read(struct wl_display* display);
int wl_display_read_events(struct wl_display* display);
void wl_display_cancel_read(struct wl_display* display);

void *wl_proxy_create_wrapper(void *proxy);
int wl_display_prepare_read_queue(struct wl_display* display, struct wl_event_queue* queue);
int wl_display_roundtrip_queue(struct wl_display* display, struct wl_event_queue* queue);
void wl_proxy_marshal_array(struct wl_proxy* p, uint32_t opcode, union wl_argument* args);

struct wl_proxy* wl_proxy_marshal_array_constructor(
    struct wl_proxy* proxy,
    uint32_t opcode,
    union wl_argument* args,
    const struct wl_interface* interface);

struct wl_proxy* wl_proxy_marshal_array_constructor_versioned(
    struct wl_proxy* proxy,
    uint32_t opcode,
    union wl_argument* args,
    const struct wl_interface* interface,
    uint32_t version);

int wl_proxy_add_dispatcher(
    struct wl_proxy* proxy,
    wl_dispatcher_func_t dispatcher_func,
    const void* dispatcher_data,
    void* data);

void wl_proxy_wrapper_destroy(void *proxy_wrapper);
const char* wl_proxy_get_class(struct wl_proxy* proxy);

#ifdef __cplusplus 
}
#endif
