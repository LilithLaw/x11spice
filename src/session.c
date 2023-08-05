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

/*----------------------------------------------------------------------------
**  session.c
**      A session is meant to tie it all together.  It groups a local system
**  gui along with code to interace with the current X display, and then connects
**  it all to a spice server.  The lines blur at times; but that is the goal.
**--------------------------------------------------------------------------*/


#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sched.h>
#include <errno.h>

#include <xcb/xcb.h>
#include <xcb/xtest.h>
#include <xcb/xcb_aux.h>
#include <xcb/xkb.h>

#include <sys/types.h>
#include <sys/wait.h>

#include "x11spice.h"
#include "session.h"
#include "scan.h"

#if defined(HAVE_LIBAUDIT_H)
#include <libaudit.h>
#endif

/*----------------------------------------------------------------------------
** I fought very hard to avoid global variables, but the spice channel_event
**  callback simply had no way of passing back a data pointer.
** So we use this global variable to enable the use of the session connect
**  and disconnect notices from spice
----------------------------------------------------------------------------*/
session_t *global_session = NULL;


void free_cursor_queue_item(gpointer data)
{
    QXLCursorCmd *ccmd = (QXLCursorCmd *) data;
    spice_free_release((spice_release_t *) (uintptr_t) ccmd->release_info.id);
}

void free_draw_queue_item(gpointer data)
{
    QXLDrawable *drawable = (QXLDrawable *) data;
    spice_free_release((spice_release_t *) (uintptr_t) drawable->release_info.id);
}

void *session_pop_draw(session_t *session)
{
    void *ret = NULL;

    if (!session)
        return ret;

    if (!session->running) {
        session->draw_command_in_progress = FALSE;
        return ret;
    }

    if (!g_mutex_trylock(session->lock))
        return ret;

    ret = g_async_queue_try_pop(session->draw_queue);
    session->draw_command_in_progress = (ret != NULL);
    g_mutex_unlock(session->lock);

    return ret;
}

int session_draw_waiting(session_t *session)
{
    int ret = 0;

    if (!session)
        return ret;

    if (!session->running) {
        session->draw_command_in_progress = FALSE;
        return ret;
    }

    if (!g_mutex_trylock(session->lock))
        return ret;

    ret = g_async_queue_length(session->draw_queue);
    g_mutex_unlock(session->lock);
    return (ret);
}

void *session_pop_cursor(session_t *session)
{
    if (!session || !session->running)
        return NULL;

    return g_async_queue_try_pop(session->cursor_queue);
}

int session_cursor_waiting(session_t *session)
{
    if (!session || !session->running)
        return 0;

    return g_async_queue_length(session->cursor_queue);
}

void session_handle_key(session_t *session, uint8_t keycode, int is_press)
{
    if (!session->options.allow_control)
        return;

    xcb_test_fake_input(session->display.c, is_press ? XCB_KEY_PRESS : XCB_KEY_RELEASE,
                        keycode, XCB_CURRENT_TIME, XCB_NONE, 0, 0, 0);
    g_debug("key 0x%x, press %d", keycode, is_press);
    xcb_flush(session->display.c);
}

void session_handle_mouse_position(session_t *session, int x, int y,
                                   uint32_t buttons_state G_GNUC_UNUSED)
{
    if (!session->options.allow_control)
        return;

    xcb_test_fake_input(session->display.c, XCB_MOTION_NOTIFY, 0, XCB_CURRENT_TIME,
                        session->display.root, x, y, 0);
    xcb_flush(session->display.c);
}

#define BUTTONS 5
static void session_handle_button_change(session_t *s, uint32_t buttons_state)
{
    int i;
    if (!s->options.allow_control)
        return;

    for (i = 0; i < BUTTONS; i++) {
        if ((buttons_state ^ s->spice.buttons_state) & (1 << i)) {
            int action = (buttons_state & (1 << i));
            xcb_test_fake_input(s->display.c, action ? XCB_BUTTON_PRESS : XCB_BUTTON_RELEASE,
                                i + 1, XCB_CURRENT_TIME, s->display.root, 0, 0, 0);
        }
    }
    s->spice.buttons_state = buttons_state;
}

static uint32_t convert_spice_buttons(int wheel, uint32_t buttons_state)
{
    // For some reason spice switches the second and third button, undo that.
    // basically undo RED_MOUSE_STATE_TO_LOCAL
    buttons_state = (buttons_state & SPICE_MOUSE_BUTTON_MASK_LEFT) |
        ((buttons_state & SPICE_MOUSE_BUTTON_MASK_MIDDLE) << 1) |
        ((buttons_state & SPICE_MOUSE_BUTTON_MASK_RIGHT) >> 1) |
        (buttons_state & ~(SPICE_MOUSE_BUTTON_MASK_LEFT | SPICE_MOUSE_BUTTON_MASK_MIDDLE
                           | SPICE_MOUSE_BUTTON_MASK_RIGHT));
    return buttons_state | (wheel > 0 ? (1 << 4) : 0)
        | (wheel < 0 ? (1 << 3) : 0);
}


void session_handle_mouse_wheel(session_t *session, int wheel_motion, uint32_t buttons_state)
{
    g_debug("mouse wheel: motion %d, buttons 0x%x", wheel_motion, buttons_state);

    session_handle_button_change(session, convert_spice_buttons(wheel_motion, buttons_state));
}

void session_handle_mouse_buttons(session_t *session, uint32_t buttons_state)
{
    g_debug("mouse button: buttons 0x%x", buttons_state);
    session_handle_button_change(session, convert_spice_buttons(0, buttons_state));
}

static void cleanup_process(int pid)
{
    if (waitpid(pid, NULL, WNOHANG) == pid)
        return;

    killpg(pid, SIGKILL);
}

int session_start(session_t *s)
{
    int rc = 0;

    s->spice.session = s;
    s->display.session = s;
    s->scanner.session = s;

    s->running = TRUE;

    rc = scanner_create(&s->scanner);
    if (rc)
        goto end;

    rc = display_start_event_thread(&s->display);
    if (rc)
        return rc;


end:
    global_session = s;
    if (rc)
        session_end(s);
    return rc;
}

static void flush_and_lock(session_t *s)
{
    while (1) {
        g_mutex_lock(s->lock);
        if (!s->draw_command_in_progress)
            break;

        g_mutex_unlock(s->lock);
        sched_yield();
    }
}

void session_end(session_t *s)
{
    session_remote_disconnected();
    s->running = 0;
    global_session = NULL;

    display_stop_event_thread(&s->display);

    scanner_destroy(&s->scanner);

    display_destroy_screen_images(&s->display);

}

static int begin_audit(session_t *s)
{
    int rc = X11SPICE_ERR_NOAUDIT;
#if defined(HAVE_LIBAUDIT) && defined(HAVE_LIBAUDIT_H)
    s->audit_id = audit_open();
    if (s->audit_id != -1) {
        rc = audit_log_user_message(s->audit_id, s->options.audit_message_type,
                                    "x11spice begin", NULL, NULL, NULL, 1);
        if (rc <= 0) {
            perror("audit_log_user_message");
            rc = X11SPICE_ERR_NOAUDIT;
        } else
            rc = 0;
    } else
        perror("audit_open");
#else
    fprintf(stderr, "Error: audit requested, but not libaudit available.\n");
#endif
    return rc;
}

static void end_audit(session_t *s)
{
#if defined(HAVE_LIBAUDIT) && defined(HAVE_LIBAUDIT_H)
    if (s->audit_id != -1) {
        audit_log_user_message(s->audit_id, s->options.audit_message_type,
                               "x11spice close", NULL, NULL, NULL, 1);
        audit_close(s->audit_id);
    }
    s->audit_id = -1;
#endif
}

int session_create(session_t *s)
{
    int rc = 0;

#if ! GLIB_CHECK_VERSION(2, 32, 0)
    g_thread_init(NULL);
#endif

    s->cursor_queue = g_async_queue_new_full(free_cursor_queue_item);
    s->draw_queue = g_async_queue_new_full(free_draw_queue_item);
    s->lock = g_mutex_new();

    s->connected = FALSE;
    s->connect_pid = 0;
    s->disconnect_pid = 0;

    if (s->options.audit)
        rc = begin_audit(s);

    return rc;
}

void session_destroy(session_t *s)
{
    flush_and_lock(s);

    if (s->cursor_queue)
        g_async_queue_unref(s->cursor_queue);
    if (s->draw_queue)
        g_async_queue_unref(s->draw_queue);
    s->cursor_queue = NULL;
    s->draw_queue = NULL;

    g_mutex_unlock(s->lock);
    g_mutex_free(s->lock);
    s->lock = NULL;

    if (s->connect_pid)
        cleanup_process(s->connect_pid);
    s->connect_pid = 0;

    if (s->disconnect_pid)
        cleanup_process(s->disconnect_pid);
    s->disconnect_pid = 0;

    if (s->options.audit)
        end_audit(s);
}

/* Important note - this is meant to be called from
    a thread context *other* than the spice worker thread */
int session_recreate_primary(session_t *s)
{
    int rc;

    flush_and_lock(s);
    spice_destroy_primary(&s->spice);
    display_destroy_screen_images(&s->display);

    rc = display_create_screen_images(&s->display);
    if (rc == 0) {
        shm_image_t *f = s->display.primary;
        rc = spice_create_primary(&s->spice, f->w, f->h, f->bytes_per_line, f->segment.shmaddr,
                                  &s->display);
    }

    g_mutex_unlock(s->lock);
    return rc;
}

void session_handle_resize(session_t *s)
{
    if (s->display.width == s->spice.width && s->display.height == s->spice.height &&
        s->display.monitor_count == s->spice.monitor_count)
        return;

    g_debug("resizing from %dx%d to %dx%d; monitor_count now %d",
            s->spice.width, s->spice.height, s->display.width, s->display.height,
            s->display.monitor_count);
    session_recreate_primary(s);
}

int session_alive(session_t *s)
{
    return s->running;
}

int session_push_cursor_image(session_t *s,
                              int x, int y, int w, int h, int xhot, int yhot,
                              int imglen, uint8_t *imgdata)
{
    QXLCursorCmd *ccmd;
    QXLCursor *cursor;

    ccmd = calloc(1, sizeof(*ccmd) + sizeof(*cursor) + imglen);
    if (!ccmd)
        return X11SPICE_ERR_MALLOC;;

    cursor = (QXLCursor *) (ccmd + 1);

    cursor->header.unique = 0;
    cursor->header.type = SPICE_CURSOR_TYPE_ALPHA;
    cursor->header.width = w;
    cursor->header.height = h;

    cursor->header.hot_spot_x = xhot;
    cursor->header.hot_spot_y = yhot;

    cursor->data_size = imglen;

    cursor->chunk.next_chunk = 0;
    cursor->chunk.prev_chunk = 0;
    cursor->chunk.data_size = imglen;

    memcpy(cursor->chunk.data, imgdata, imglen);

    ccmd->type = QXL_CURSOR_SET;
    ccmd->u.set.position.x = x + xhot;
    ccmd->u.set.position.y = y + yhot;
    ccmd->u.set.shape = (uintptr_t) cursor;
    ccmd->u.set.visible = TRUE;

    ccmd->release_info.id = (uintptr_t) spice_create_release(&s->spice, RELEASE_MEMORY, ccmd);

    g_async_queue_push(s->cursor_queue, ccmd);
    spice_qxl_wakeup(&s->spice.display_sin);

    return 0;
}

int session_get_one_led(session_t *session, const char *name)
{
    int ret;
    xcb_intern_atom_cookie_t atom_cookie;
    xcb_intern_atom_reply_t *atom_reply;
    xcb_xkb_get_named_indicator_cookie_t indicator_cookie;
    xcb_xkb_get_named_indicator_reply_t *indicator_reply;
    xcb_generic_error_t *error;

    atom_cookie = xcb_intern_atom(session->display.c, 0, strlen(name), name);
    atom_reply = xcb_intern_atom_reply(session->display.c, atom_cookie, &error);
    if (error) {
        g_warning("Could not get atom; type %d; code %d; major %d; minor %d",
                  error->response_type, error->error_code, error->major_code, error->minor_code);
        return 0;
    }

    indicator_cookie = xcb_xkb_get_named_indicator(session->display.c,
                                                   XCB_XKB_ID_USE_CORE_KBD,
                                                   XCB_XKB_LED_CLASS_DFLT_XI_CLASS,
                                                   XCB_XKB_ID_DFLT_XI_ID, atom_reply->atom);
    free(atom_reply);

    indicator_reply =
        xcb_xkb_get_named_indicator_reply(session->display.c, indicator_cookie, &error);
    if (error) {
        g_warning("Could not get indicator; type %d; code %d; major %d; minor %d",
                  error->response_type, error->error_code, error->major_code, error->minor_code);
        return 0;
    }

    ret = indicator_reply->on;
    free(indicator_reply);
    return ret;
}

void session_disconnect_client(session_t *session)
{
    /* TODO: This is using a side effect of set_ticket that is not intentional.
       It would be better to ask for a deliberate method of achieving this result.  */
    g_debug("client disconnect");
    spice_server_set_ticket(session->spice.server, session->options.spice_password, 0, 0, TRUE);
    if (!session->options.spice_password || session->options.disable_ticketing)
        spice_server_set_noauth(session->spice.server);
}

static void invoke_on_connect(session_t *session, const char *from)
{
    if (session->connect_pid)
        cleanup_process(session->connect_pid);
    session->connect_pid = fork();
    if (session->connect_pid == 0) {
        /* TODO:  If would be nice to either close the spice socket after
           the fork, or to get CLOEXEC set on the socket after open. */
        setsid();
        execl(session->options.on_connect, session->options.on_connect, from, NULL);
        g_error("Exec of connect command [%s %s] failed", session->options.on_connect, from);
        exit(-1);
    }
}

static void invoke_on_disconnect(session_t *session)
{
    if (session->connect_pid)
        cleanup_process(session->connect_pid);
    session->connect_pid = 0;

    if (session->disconnect_pid)
        cleanup_process(session->disconnect_pid);

    session->disconnect_pid = fork();
    if (session->disconnect_pid == 0) {
        /* TODO:  If would be nice to either close the spice socket after
           the fork, or to get CLOEXEC set on the socket after open. */
        setsid();
        execl(session->options.on_disconnect, session->options.on_disconnect, NULL);
        g_error("Exec of disconnect command [%s] failed", session->options.on_disconnect);
        exit(-1);
    }
}

void session_remote_connected(const char *from)
{
#define GUI_FROM_PREFIX "Connection from "
    char *from_string;
    if (!global_session || global_session->connected)
        return;

    global_session->connected = TRUE;

    from_string = calloc(1, strlen(from) + strlen(GUI_FROM_PREFIX) + 1);
    if (from_string) {
        strcpy(from_string, GUI_FROM_PREFIX);
        strcat(from_string, from);
        gui_remote_connected(&global_session->gui, from_string);
        free(from_string);
    }
    if (global_session->options.on_connect)
        invoke_on_connect(global_session, from);

#if defined(HAVE_LIBAUDIT)
    if (global_session->options.audit && global_session->audit_id != -1)
        audit_log_user_message(global_session->audit_id, global_session->options.audit_message_type,
                               "x11spice connect", NULL, NULL, NULL, 1);
#endif
}

void session_remote_disconnected(void)
{
    if (!global_session || !global_session->connected)
        return;

    global_session->connected = FALSE;
    if (global_session->options.on_disconnect)
        invoke_on_disconnect(global_session);
    gui_remote_disconnected(&global_session->gui);

#if defined(HAVE_LIBAUDIT)
    if (global_session->options.audit && global_session->audit_id != -1)
        audit_log_user_message(global_session->audit_id, global_session->options.audit_message_type,
                               "x11spice disconnect", NULL, NULL, NULL, 1);
#endif
}
