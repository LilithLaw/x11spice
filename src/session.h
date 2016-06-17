/*
    Copyright (C) 2016  Jeremy White <jwhite@codeweavers.com>
    All rights reserved.

    This file is part of x11spice

    x11spice is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    x11spice is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with x11spice.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef SESSION_H_
#define SESSION_H_

#include "options.h"
#include "display.h"
#include "local_spice.h"
#include "gui.h"
#include "scan.h"

/*----------------------------------------------------------------------------
**  Structure definitions
**--------------------------------------------------------------------------*/
typedef struct session_struct
{
    options_t   options;
    display_t   display;
    spice_t     spice;
    gui_t       gui;
    scanner_t   scanner;

    GAsyncQueue *cursor_queue;
    GAsyncQueue *draw_queue;
} session_t;

/*----------------------------------------------------------------------------
**  Prototypes
**--------------------------------------------------------------------------*/
int session_create(session_t *s);
void session_destroy(session_t *s);
int session_start(session_t *s);
void session_end(session_t *s);

void *session_pop_draw(session_t *session);
int session_draw_waiting(session_t *session);
void session_handle_key(session_t *session, uint8_t keycode, int is_press);
void session_handle_mouse_position(session_t *session, int x, int y, uint32_t buttons_state);
void session_handle_mouse_buttons(session_t *session, uint32_t buttons_state);
void session_handle_mouse_wheel(session_t *session, int wheel_motion, uint32_t buttons_state);
#endif