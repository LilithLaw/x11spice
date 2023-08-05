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
**  display.c
**      This file provides functions to interact with the X11 display.
**  The concept is that the bulk of the connection to the X server is done
**  here, using xcb.
**--------------------------------------------------------------------------*/

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>

#include <glib.h>

#include <sys/types.h>
#include <sys/shm.h>
#include <string.h>

#include <xcb/xcb.h>
#include <xcb/xcb_aux.h>
#include <xcb/xkb.h>
#include <xcb/randr.h>
#include <pixman.h>
#include <errno.h>

#include "x11spice.h"
#include "options.h"
#include "display.h"
#include "session.h"
#include "scan.h"


static xcb_screen_t *screen_of_display(xcb_connection_t *c, int screen)
{
    xcb_screen_iterator_t iter;

    iter = xcb_setup_roots_iterator(xcb_get_setup(c));
    for (; iter.rem; --screen, xcb_screen_next(&iter))
        if (screen == 0)
            return iter.data;

    return NULL;
}

static unsigned int bits_per_pixel(display_t *d)
{
    xcb_format_iterator_t fmt;

    for (fmt = xcb_setup_pixmap_formats_iterator(xcb_get_setup(d->c));
         fmt.rem; xcb_format_next(&fmt))
        if (fmt.data->depth == d->depth)
            return fmt.data->bits_per_pixel;

    return 0;
}


static void handle_cursor_notify(display_t *display,
                                 xcb_xfixes_cursor_notify_event_t *cev G_GNUC_UNUSED)
{
    xcb_xfixes_get_cursor_image_cookie_t icookie;
    xcb_xfixes_get_cursor_image_reply_t *ir;
    xcb_generic_error_t *error;
    int imglen;
    uint32_t *imgdata;

    if (display->session->options.debug_draws >= DEBUG_DRAWS_BASIC) {
        display_debug("Cursor Notify [seq %d|subtype %d|serial %u]\n",
                      cev->sequence, cev->subtype, cev->cursor_serial);
    }

    icookie = xcb_xfixes_get_cursor_image(display->c);

    ir = xcb_xfixes_get_cursor_image_reply(display->c, icookie, &error);
    if (error) {
        g_warning("Could not get cursor_image_reply; type %d; code %d; major %d; minor %d\n",
                  error->response_type, error->error_code, error->major_code, error->minor_code);
        return;
    }

    if (!ir)
        return;

    imglen = xcb_xfixes_get_cursor_image_cursor_image_length(ir);
    imgdata = xcb_xfixes_get_cursor_image_cursor_image(ir);

    session_push_cursor_image(display->session,
                              ir->x, ir->y, ir->width, ir->height, ir->xhot, ir->yhot,
                              imglen * sizeof(*imgdata), (uint8_t *) imgdata);

    free(ir);
}

static void handle_damage_notify(display_t *display, xcb_damage_notify_event_t *dev,
                                 pixman_region16_t * damage_region)
{
    int i, n;
    pixman_box16_t *p;

    pixman_region_union_rect(damage_region, damage_region,
                             dev->area.x, dev->area.y, dev->area.width, dev->area.height);

    /* The MORE flag is 0x80 on the level field; the proto documentation
       is wrong on this point.  Check the xorg server code to see. */
    if (dev->level & 0x80)
        return;

    xcb_damage_subtract(display->c, display->damage,
                        XCB_XFIXES_REGION_NONE, XCB_XFIXES_REGION_NONE);

    p = pixman_region_rectangles(damage_region, &n);

    /* Compositing window managers such as mutter have a bad habit of sending
       whole screen updates, which ends up being harmful to user experience.
       In that case, we want to stop trusting those damage reports. */
    if (dev->area.width == display->width && dev->area.height == display->height) {
        display->fullscreen_damage_count++;
    } else {
        display->fullscreen_damage_count = 0;
    }

    if (display->session->options.debug_draws >= DEBUG_DRAWS_BASIC) {
        display_debug
            ("Damage Notify [seq %d|level %d|more %d|area (%dx%d)@%dx%d|geo (%dx%d)@%dx%d%s\n",
             dev->sequence, dev->level, dev->level & 0x80, dev->area.width, dev->area.height,
             dev->area.x, dev->area.y, dev->geometry.width, dev->geometry.height, dev->geometry.x,
             dev->geometry.y, display_trust_damage(display) ? "" : " SKIPPED");
    }

    if (display_trust_damage(display)) {
        for (i = 0; i < n; i++)
            scanner_push(&display->session->scanner, DAMAGE_SCAN_REPORT,
                         p[i].x1, p[i].y1, p[i].x2 - p[i].x1, p[i].y2 - p[i].y1);
    } else {
        scanner_push(&display->session->scanner, FULLSCREEN_SCAN_REQUEST, 0, 0, 0, 0);
    }

    pixman_region_clear(damage_region);
}

static void get_monitors(display_t *display)
{
    xcb_randr_get_monitors_cookie_t cookie;
    xcb_generic_error_t *error;
    xcb_randr_get_monitors_reply_t *reply;
    xcb_randr_monitor_info_iterator_t iter;
    int i;

    cookie = xcb_randr_get_monitors(display->c, display->root, 1);
    reply = xcb_randr_get_monitors_reply(display->c, cookie, &error);
    if (error) {
        fprintf(stderr,
                "Error: could not get monitor reply; type %d; code %d; major %d; minor %d\n",
                error->response_type, error->error_code, error->major_code, error->minor_code);
        return;
    }
    display->monitor_count = xcb_randr_get_monitors_monitors_length(reply);
    free(display->monitors);
    display->monitors = calloc(display->monitor_count, sizeof(*(display->monitors)));
    if (!display->monitors) {
        fprintf(stderr, "Error: could not allocate %d monitors\n", display->monitor_count);
        free(reply);
        return;
    }
    iter = xcb_randr_get_monitors_monitors_iterator(reply);
    for (i = 0; i < display->monitor_count; i++) {
        display->monitors[i].x = iter.data->x;
        display->monitors[i].y = iter.data->y;
        display->monitors[i].width = iter.data->width;
        display->monitors[i].height = iter.data->height;
        xcb_randr_monitor_info_next(&iter);
    }
    free(reply);

}

static void handle_configure_notify(display_t *display, xcb_configure_notify_event_t *cev)
{
    if (display->session->options.debug_draws >= DEBUG_DRAWS_BASIC) {
        display_debug
            ("%s:[event %u|window %u|above_sibling %u|x %d|y %d|width %d|height %d|border_width %d|override_redirect %d]\n",
             __func__, cev->event, cev->window, cev->above_sibling, cev->x, cev->y, cev->width,
             cev->height, cev->border_width, cev->override_redirect);
    }

    if (cev->window != display->root) {
        g_debug("not main window; skipping.");
        return;
    }

    display->width = cev->width;
    display->height = cev->height;
    get_monitors(display);
    session_handle_resize(display->session);
}

static void *handle_xevents(void *opaque)
{
    display_t *display = (display_t *) opaque;
    xcb_generic_event_t *ev = NULL;
    pixman_region16_t damage_region;

    pixman_region_init(&damage_region);

    while ((ev = xcb_wait_for_event(display->c))) {
        if (ev->response_type == display->xfixes_ext->first_event + XCB_XFIXES_CURSOR_NOTIFY)
            handle_cursor_notify(display, (xcb_xfixes_cursor_notify_event_t *) ev);

        else if (ev->response_type == display->damage_ext->first_event + XCB_DAMAGE_NOTIFY)
            handle_damage_notify(display, (xcb_damage_notify_event_t *) ev, &damage_region);

        else if (ev->response_type == XCB_CONFIGURE_NOTIFY)
            handle_configure_notify(display, (xcb_configure_notify_event_t *) ev);

        else
            g_debug("Unexpected X event %d", ev->response_type);

        free(ev);

        if (display->session && !session_alive(display->session))
            break;
    }

    while ((ev = xcb_poll_for_event(display->c)))
        free(ev);

    pixman_region_clear(&damage_region);

    return NULL;
}

static int register_for_events(display_t *d)
{
    uint32_t events = XCB_EVENT_MASK_STRUCTURE_NOTIFY;
    xcb_void_cookie_t cookie;
    xcb_generic_error_t *error;

    cookie = xcb_change_window_attributes_checked(d->c, d->root, XCB_CW_EVENT_MASK, &events);
    error = xcb_request_check(d->c, cookie);
    if (error) {
        fprintf(stderr,
                "Error:  Could not register normal events; type %d; code %d; major %d; minor %d\n",
                error->response_type, error->error_code, error->major_code, error->minor_code);
        return X11SPICE_ERR_NOEVENTS;
    }

    return 0;
}

static void shm_segment_destroy(display_t *d, shm_segment_t *segment)
{
    if (segment->shmid == -1) {
        return;
    }

    xcb_shm_detach(d->c, segment->shmseg);
    segment->shmseg = -1;

    shmdt(segment->shmaddr);
    segment->shmaddr = NULL;

    shmctl(segment->shmid, IPC_RMID, NULL);
    segment->shmid = -1;
}


static int shm_cache_get(display_t *d, size_t size, shm_segment_t *segment)
{
    int i, ret;
    shm_segment_t *bigger_entry = NULL;
    shm_segment_t *entry_to_use = NULL;

#if defined(DEBUG_SHM_CACHE)
    static guint cache_hits = 0;
    static guint cache_total = 0;

    cache_total++;
#endif

    g_mutex_lock(&d->shm_cache_mutex);

    /* Check the cache for a segment of size 'size' or bigger.
       Use an exact-size segment if found.
       If not, use the smallest entry that is big enough.  */
    for (i = 0; i < G_N_ELEMENTS(d->shm_cache); i++) {
        shm_segment_t *entry = &d->shm_cache[i];

        if (entry->shmid != -1) {
            /* If a cache entry of the exact size being requested is found, use that */
            if (size == entry->size) {
                entry_to_use = entry;
                break;
            }

            /* Keep track of the next-biggest entry in case an exact-size match isn't found */
            if (size < entry->size) {
                if (!bigger_entry || entry->size < bigger_entry->size) {
                    bigger_entry = entry;
                }
            }
        }
    }

    /* An exact-size entry wasn't found, use the next biggest entry that was found */
    if (!entry_to_use) {
        entry_to_use = bigger_entry;
    }

    if (entry_to_use) {
        *segment = *entry_to_use;
        entry_to_use->shmid = -1;

        ret = 1;

#if defined(DEBUG_SHM_CACHE)
        cache_hits++;
        if ((cache_hits % 100 == 0)) {
            g_debug("SHM cache hitrate: %u/%u (%.2f%%)", cache_hits, cache_total,
                    (float) ((float) cache_hits / (float) cache_total) * 100);
        }
#endif
    } else {
        /* No usable entry found in the cache */
        ret = 0;
    }

    g_mutex_unlock(&d->shm_cache_mutex);
    return ret;
}

static int shm_cache_add(display_t *d, shm_segment_t *segment)
{
    int i, ret;
    shm_segment_t *smallest_entry = NULL;
    shm_segment_t *entry_to_use = NULL;

    g_mutex_lock(&d->shm_cache_mutex);

    /* 'segment' is now unused, try to add it to the cache.
       Use an empty slot in the cache if available.
       If not, evict the smallest entry (which also must be smaller than 'segment') from the cache.  */
    for (i = 0; i < G_N_ELEMENTS(d->shm_cache); i++) {
        shm_segment_t *entry = &d->shm_cache[i];

        if (entry->shmid == -1) {
            /* Use an empty slot if found */
            entry_to_use = entry;
            break;
        }

        /* Keep track of the smallest entry that's smaller than 'segment' */
        if (entry->size < segment->size) {
            if (!smallest_entry || entry->size < smallest_entry->size) {
                smallest_entry = entry;
            }
        }
    }

    /* If no empty entries were found, evict 'smallest_entry' and reuse it */
    if (!entry_to_use && smallest_entry) {
        shm_segment_destroy(d, smallest_entry);
        entry_to_use = smallest_entry;
    }

    if (entry_to_use) {
        *entry_to_use = *segment;
        ret = 1;
    } else {
        /* Cache is full, and contained no entries smaller than the one being added */
        ret = 0;
    }

    g_mutex_unlock(&d->shm_cache_mutex);
    return ret;
}

static void shm_cache_destroy(display_t *d)
{
    int i;

    g_mutex_lock(&d->shm_cache_mutex);
    for (i = 0; i < G_N_ELEMENTS(d->shm_cache); i++) {
        shm_segment_t *entry = &d->shm_cache[i];

        shm_segment_destroy(d, entry);
    }
    g_mutex_unlock(&d->shm_cache_mutex);
}

int display_open(display_t *d, session_t *session)
{
    int scr;
    int rc;
    int i;
    xcb_damage_query_version_cookie_t dcookie;
    xcb_damage_query_version_reply_t *damage_version;
    xcb_xkb_use_extension_cookie_t use_cookie;
    xcb_xkb_use_extension_reply_t *use_reply;

    xcb_void_cookie_t cookie;
    xcb_generic_error_t *error;
    xcb_screen_t *screen;

    d->session = session;

    d->c = xcb_connect(session->options.display, &scr);
    if (!d->c || xcb_connection_has_error(d->c)) {
        fprintf(stderr, "Error:  could not open display %s\n",
                session->options.display ? session->options.display : "");
        return X11SPICE_ERR_NODISPLAY;
    }

    screen = screen_of_display(d->c, scr);
    if (!screen) {
        fprintf(stderr, "Error:  could not get screen for display %s\n",
                session->options.display ? session->options.display : "");
        return X11SPICE_ERR_NODISPLAY;
    }
    d->root = screen->root;
    d->width = screen->width_in_pixels;
    d->height = screen->height_in_pixels;
    d->depth = screen->root_depth;

    d->damage_ext = xcb_get_extension_data(d->c, &xcb_damage_id);
    if (!d->damage_ext) {
        fprintf(stderr, "Error:  XDAMAGE not found on display %s\n",
                session->options.display ? session->options.display : "");
        return X11SPICE_ERR_NODAMAGE;
    }

    if (session->options.full_screen_fps <= 0) {
        dcookie =
            xcb_damage_query_version(d->c, XCB_DAMAGE_MAJOR_VERSION, XCB_DAMAGE_MINOR_VERSION);
        damage_version = xcb_damage_query_version_reply(d->c, dcookie, &error);
        if (error) {
            fprintf(stderr,
                    "Error:  Could not query damage; type %d; code %d; major %d; minor %d\n",
                    error->response_type, error->error_code, error->major_code, error->minor_code);
            return X11SPICE_ERR_NODAMAGE;
        }
        free(damage_version);

        d->damage = xcb_generate_id(d->c);
        cookie =
            xcb_damage_create_checked(d->c, d->damage, d->root,
                                      XCB_DAMAGE_REPORT_LEVEL_RAW_RECTANGLES);
        error = xcb_request_check(d->c, cookie);
        if (error) {
            fprintf(stderr,
                    "Error:  Could not create damage; type %d; code %d; major %d; minor %d\n",
                    error->response_type, error->error_code, error->major_code, error->minor_code);
            return X11SPICE_ERR_NODAMAGE;
        }
    }

    d->shm_ext = xcb_get_extension_data(d->c, &xcb_shm_id);
    if (!d->shm_ext) {
        fprintf(stderr, "Error:  XSHM not found on display %s\n",
                session->options.display ? session->options.display : "");
        return X11SPICE_ERR_NOSHM;
    }

    d->xfixes_ext = xcb_get_extension_data(d->c, &xcb_xfixes_id);
    if (!d->xfixes_ext) {
        fprintf(stderr, "Error:  XFIXES not found on display %s\n",
                session->options.display ? session->options.display : "");
        return X11SPICE_ERR_NOXFIXES;
    }

    xcb_xfixes_query_version(d->c, XCB_XFIXES_MAJOR_VERSION, XCB_XFIXES_MINOR_VERSION);

    cookie =
        xcb_xfixes_select_cursor_input_checked(d->c, d->root,
                                               XCB_XFIXES_CURSOR_NOTIFY_MASK_DISPLAY_CURSOR);
    error = xcb_request_check(d->c, cookie);
    if (error) {
        fprintf(stderr,
                "Error:  Could not select cursor input; type %d; code %d; major %d; minor %d\n",
                error->response_type, error->error_code, error->major_code, error->minor_code);
        return X11SPICE_ERR_NOXFIXES;
    }

    use_cookie = xcb_xkb_use_extension(d->c, XCB_XKB_MAJOR_VERSION, XCB_XKB_MINOR_VERSION);
    use_reply = xcb_xkb_use_extension_reply(d->c, use_cookie, &error);
    if (error) {
        fprintf(stderr, "Error: could not get use reply; type %d; code %d; major %d; minor %d\n",
                error->response_type, error->error_code, error->major_code, error->minor_code);
        return X11SPICE_ERR_NO_XKB;
    }
    free(use_reply);


    rc = register_for_events(d);
    if (rc)
        return rc;

    g_mutex_init(&d->shm_cache_mutex);
    for (i = 0; i < G_N_ELEMENTS(d->shm_cache); i++) {
        d->shm_cache[i].shmid = -1;
    }

    get_monitors(d);

    rc = display_create_screen_images(d);

    g_message("Display %s opened", session->options.display ? session->options.display : "");

    return rc;
}

shm_image_t *create_shm_image(display_t *d, unsigned int w, unsigned int h)
{
    shm_image_t *shmi;
    size_t imgsize;
    xcb_void_cookie_t cookie;
    xcb_generic_error_t *error;

    shmi = calloc(1, sizeof(*shmi));
    if (!shmi)
        return shmi;

    shmi->w = w ? w : d->width;
    shmi->h = h ? h : d->height;

    shmi->bytes_per_line = (bits_per_pixel(d) / 8) * shmi->w;
    imgsize = shmi->bytes_per_line * shmi->h;

    if (shm_cache_get(d, imgsize, &shmi->segment)) {
        return shmi;
    }

    /* No usable shared memory segment found in cache, allocate a new one */
    shmi->segment.shmid = shmget(IPC_PRIVATE, imgsize, IPC_CREAT | 0700);
    if (shmi->segment.shmid != -1)
        shmi->segment.shmaddr = shmat(shmi->segment.shmid, 0, 0);
    if (shmi->segment.shmid == -1 || shmi->segment.shmaddr == (void *) -1) {
        g_warning("Cannot get shared memory of size %" G_GSIZE_FORMAT "; errno %d", imgsize, errno);
        free(shmi);
        return NULL;
    }
    /* We tell shmctl to detach now; that prevents us from holding this
       shared memory segment forever in case of abnormal process exit. */
    shmctl(shmi->segment.shmid, IPC_RMID, NULL);
    shmi->segment.size = imgsize;

    shmi->segment.shmseg = xcb_generate_id(d->c);
    cookie = xcb_shm_attach_checked(d->c, shmi->segment.shmseg, shmi->segment.shmid, 0);
    error = xcb_request_check(d->c, cookie);
    if (error) {
        g_warning("Could not attach; type %d; code %d; major %d; minor %d\n",
                  error->response_type, error->error_code, error->major_code, error->minor_code);
        return NULL;
    }

    return shmi;
}

int read_shm_image(display_t *d, shm_image_t *shmi, int x, int y)
{
    xcb_shm_get_image_cookie_t cookie;
    xcb_generic_error_t *e;
    xcb_shm_get_image_reply_t *reply;

    cookie = xcb_shm_get_image(d->c, d->root, x, y, shmi->w, shmi->h,
                               ~0, XCB_IMAGE_FORMAT_Z_PIXMAP, shmi->segment.shmseg, 0);

    reply = xcb_shm_get_image_reply(d->c, cookie, &e);
    if (e) {
        g_warning("xcb_shm_get_image from %dx%d into size %dx%d failed", x, y, shmi->w, shmi->h);
        return -1;
    }
    free(reply);

    return 0;
}

int display_find_changed_tiles(display_t *d, int row, bool *tiles, int tiles_across)
{
    int ret;
    int len;
    int i;

    memset(tiles, 0, sizeof(*tiles) * tiles_across);
    ret = read_shm_image(d, d->scanline, 0, row);
    if (ret == 0) {
        uint32_t *old = ((uint32_t *) d->fullscreen->segment.shmaddr) + row * d->fullscreen->w;
        uint32_t *new = ((uint32_t *) d->scanline->segment.shmaddr);
        if (memcmp(old, new, sizeof(*old) * d->scanline->w) == 0)
            return 0;

        len = d->scanline->w / tiles_across;
        for (i = 0; i < tiles_across; i++, old += len, new += len) {
            if (i == tiles_across - 1)
                len = d->scanline->w - (i * len);
            if (memcmp(old, new, sizeof(*old) * len)) {
                ret++;
                tiles[i] = true;
            }
        }
    }
    if (d->session->options.debug_draws >= DEBUG_DRAWS_DETAIL) {
        fprintf(stderr, "%d: ", row);
        for (i = 0; i < tiles_across; i++)
            fprintf(stderr, "%c", tiles[i] ? 'X' : '-');
        fprintf(stderr, "\n");
        fflush(stderr);
    }

    return ret;
}

void display_copy_image_into_fullscreen(display_t *d, shm_image_t *shmi, int x, int y)
{
    uint32_t *to = ((uint32_t *) d->fullscreen->segment.shmaddr) + (y * d->fullscreen->w) + x;
    uint32_t *from = ((uint32_t *) shmi->segment.shmaddr);
    int i;

    /* Ignore invalid draws.  This can happen if the screen is resized after a scan
       has been qeueued */
    if (x + shmi->w > d->fullscreen->w)
        return;
    if (y + shmi->h > d->fullscreen->h)
        return;

    for (i = 0; i < shmi->h; i++) {
        memcpy(to, from, sizeof(*to) * shmi->w);
        from += shmi->w;
        to += d->fullscreen->w;
    }
}

int display_scan_whole_screen(display_t *d, int num_vertical_tiles, int num_horizontal_tiles,
                              bool tiles[][num_horizontal_tiles], int *tiles_changed_in_row)
{
    int ret;
    int len;
    int h_tile, v_tile, y;
    shm_image_t *fullscreen_new;

    memset(tiles, 0, sizeof(**tiles) * num_vertical_tiles * num_horizontal_tiles);
    memset(tiles_changed_in_row, 0, sizeof(*tiles_changed_in_row) * num_vertical_tiles);

    fullscreen_new = create_shm_image(d, 0, 0);
    if (!fullscreen_new)
        return 0;

    ret = read_shm_image(d, fullscreen_new, 0, 0);
    if (ret == 0) {
        if (d->fullscreen->h != fullscreen_new->h || d->fullscreen->w != fullscreen_new->w) {
            /* If we're in the middle of a screen resize, just bail */
            destroy_shm_image(d, fullscreen_new);
            return 0;
        }

        for (v_tile = 0; v_tile < num_vertical_tiles; v_tile++) {
            /* Note that integer math and multiplying first is important;
               especially in the case where our screen height is not a
               multiple of 32 */
            int ystart = (v_tile * d->fullscreen->h) / num_vertical_tiles;
            int yend = ((v_tile + 1) * d->fullscreen->h) / num_vertical_tiles;
            for (y = ystart; y < yend && y < d->fullscreen->h; y++) {
                uint32_t *old = ((uint32_t *) d->fullscreen->segment.shmaddr) +
                    (y * d->fullscreen->w);
                uint32_t *new = ((uint32_t *) fullscreen_new->segment.shmaddr) +
                    (y * fullscreen_new->w);
                if (memcmp(old, new, sizeof(*old) * d->fullscreen->w) == 0)
                    continue;

                len = d->fullscreen->w / num_horizontal_tiles;
                for (h_tile = 0; h_tile < num_horizontal_tiles; h_tile++, old += len, new += len) {
                    if (h_tile == num_horizontal_tiles - 1)
                        len = d->fullscreen->w - (h_tile * len);
                    if (memcmp(old, new, sizeof(*old) * len)) {
                        ret++;
                        tiles[v_tile][h_tile] = true;
                        tiles_changed_in_row[v_tile]++;
                    }
                }
            }
            if (d->session->options.debug_draws >= DEBUG_DRAWS_DETAIL) {
                fprintf(stderr, "%d: ", v_tile);
                for (h_tile = 0; h_tile < num_horizontal_tiles; h_tile++)
                    fprintf(stderr, "%c", tiles[v_tile][h_tile] ? 'X' : '-');
                fprintf(stderr, "\n");
                fflush(stderr);
            }
        }
    }

    /* Note:  it is tempting to replace d->fullscreen now, but that causes
       display glitches.  The issue is the optimization in scanner_push.
       That will cause us to discard a SCANLINE_SCAN_REPORT if there
       is a whole screen SCANLINE_DAMAGE_REPORT right behind it.  That logic is
       reasonable, if the scan will continue to find a problem. But replacing
       d->fullscreen now will cause that damage report to fail to find any
       problems, and we'll have discarded a valid scan report.
       You can modify scan.c to drop that optimization for DAMAGE reports,
       but a naive perf analysis suggests that actually costs you.
       This is partly because call to display_copy_image_into_fullscreen
       in handle_scan_report() still occurs, so you haven't saved that time. */
    destroy_shm_image(d, fullscreen_new);

    return ret;
}


void destroy_shm_image(display_t *d, shm_image_t *shmi)
{
    if (!shm_cache_add(d, &shmi->segment)) {
        /* Could not add to cache, destroy this segment */
        shm_segment_destroy(d, &shmi->segment);
    }
    if (shmi->drawable_ptr)
        free(shmi->drawable_ptr);
    free(shmi);
}

int display_create_screen_images(display_t *d)
{
    /* 'primary' and 'fullscreen' don't need to be SHM, normal buffers would work
       fine. Using SHM for all buffers is simpler though, and has no real downsides.  */
    d->primary = create_shm_image(d, 0, 0);
    if (!d->primary) {
        return X11SPICE_ERR_NOSHM;
    }

    d->fullscreen = create_shm_image(d, 0, 0);
    if (!d->fullscreen) {
        destroy_shm_image(d, d->primary);
        d->primary = NULL;
        return X11SPICE_ERR_NOSHM;
    }

    d->scanline = create_shm_image(d, 0, 1);
    if (!d->scanline) {
        destroy_shm_image(d, d->primary);
        d->primary = NULL;
        destroy_shm_image(d, d->fullscreen);
        d->fullscreen = NULL;
        return X11SPICE_ERR_NOSHM;
    }

    return 0;
}

void display_destroy_screen_images(display_t *d)
{
    if (d->primary) {
        destroy_shm_image(d, d->primary);
        d->primary = NULL;
    }

    if (d->fullscreen) {
        destroy_shm_image(d, d->fullscreen);
        d->fullscreen = NULL;
    }

    if (d->scanline) {
        destroy_shm_image(d, d->scanline);
        d->scanline = NULL;
    }
}

int display_start_event_thread(display_t *d)
{
    return pthread_create(&d->event_thread, NULL, handle_xevents, d);
}

void display_stop_event_thread(display_t *d)
{
    void *err;
    shutdown(xcb_get_file_descriptor(d->c), SHUT_RD);
    pthread_join(d->event_thread, &err);
}


void display_close(display_t *d)
{
    shm_cache_destroy(d);
    g_mutex_clear(&d->shm_cache_mutex);
    if (d->session->options.full_screen_fps <= 0) {
        xcb_damage_destroy(d->c, d->damage);
    }
    display_destroy_screen_images(d);
    xcb_disconnect(d->c);
    free(d->monitors);
    d->monitor_count = 0;
}

int display_trust_damage(display_t *d)
{
    if (d->session->options.trust_damage == ALWAYS_TRUST)
        return 1;
    if (d->session->options.trust_damage == NEVER_TRUST)
        return 0;
    return d->fullscreen_damage_count <= 2;
}

void display_debug(const char *fmt, ...)
{
    va_list ap;
    struct timeval tv;

    gettimeofday(&tv, NULL);
    va_start(ap, fmt);
    fprintf(stderr, "draw:%04ld.%06ld:", tv.tv_sec, tv.tv_usec);
    vfprintf(stderr, fmt, ap);
    fflush(stderr);
    va_end(ap);
}
