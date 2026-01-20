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

#ifdef __cplusplus
extern "C" { 
#endif

// Fallback implementations
extern uint32_t FALLBACK_wl_proxy_get_version(struct wl_proxy* proxy);

extern struct wl_proxy* FALLBACK_wl_proxy_marshal_constructor(
    struct wl_proxy* proxy,
    uint32_t opcode,
    const struct wl_interface* interface,
    ...);

extern struct wl_proxy* FALLBACK_wl_proxy_marshal_constructor_versioned(
    struct wl_proxy* proxy,
    uint32_t opcode,
    const struct wl_interface* interface,
    uint32_t version,
    ...);

extern int FALLBACK_wl_display_prepare_read(struct wl_display* display);
extern int FALLBACK_wl_display_read_events(struct wl_display* display);
extern void FALLBACK_wl_display_cancel_read(struct wl_display* display);

// Wrapper implementations that choose between real and fallback
extern uint32_t WRAPPER_wl_proxy_get_version(struct wl_proxy* proxy);

extern struct wl_proxy* WRAPPER_wl_proxy_marshal_constructor(
    struct wl_proxy* proxy,
    uint32_t opcode,
    const struct wl_interface* interface,
    ...);

extern struct wl_proxy* WRAPPER_wl_proxy_marshal_constructor_versioned(
    struct wl_proxy* proxy,
    uint32_t opcode,
    const struct wl_interface* interface,
    uint32_t version,
    ...);

extern int WRAPPER_wl_display_prepare_read(struct wl_display* display);
extern int WRAPPER_wl_display_read_events(struct wl_display* display);
extern void WRAPPER_wl_display_cancel_read(struct wl_display* display);

#ifdef __cplusplus 
}
#endif

#ifndef wl_proxy_get_version
#define wl_proxy_get_version                   WRAPPER_wl_proxy_get_version
#endif

#ifndef wl_proxy_marshal_constructor
#define wl_proxy_marshal_constructor           WRAPPER_wl_proxy_marshal_constructor
#endif

#ifndef wl_proxy_marshal_constructor_versioned
#define wl_proxy_marshal_constructor_versioned WRAPPER_wl_proxy_marshal_constructor_versioned
#endif

#ifndef wl_display_prepare_read
#define wl_display_prepare_read                WRAPPER_wl_display_prepare_read
#endif

#ifndef wl_display_read_events
#define wl_display_read_events                 WRAPPER_wl_display_read_events
#endif

#ifndef wl_display_cancel_read
#define wl_display_cancel_read                 WRAPPER_wl_display_cancel_read
#endif
