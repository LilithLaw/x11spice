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

#ifndef DISPLAY_H_
#define DISPLAY_H_

#include <glib.h>
#include <xcb/xcb.h>
#include <xcb/damage.h>
#include <xcb/shm.h>

struct session_struct;

/*----------------------------------------------------------------------------
**  Structure definitions
**--------------------------------------------------------------------------*/
typedef struct {
    int shmid;  /* if shmid is -1: the shm_segment_t is "empty", other members are undefined */
    size_t size;
    xcb_shm_seg_t shmseg;
    void *shmaddr;
} shm_segment_t;

typedef struct {
    shm_segment_t segment;
    unsigned int w;
    unsigned int h;
    unsigned int bytes_per_line;
    void *drawable_ptr;
} shm_image_t;

typedef struct {
    xcb_connection_t *c;
    xcb_window_t root;
    unsigned int width;
    unsigned int height;
    unsigned int depth;

    const xcb_query_extension_reply_t *damage_ext;
    xcb_damage_damage_t damage;
    unsigned int fullscreen_damage_count;

    const xcb_query_extension_reply_t *shm_ext;

    const xcb_query_extension_reply_t *xfixes_ext;

    shm_image_t *primary;
    shm_image_t *fullscreen;
    shm_image_t *scanline;

    /* The SHM cache holds up to 10 segments, this provides a good cache
       hit rate while keeping memory usage reasonable.  */
    shm_segment_t shm_cache[10];
    GMutex shm_cache_mutex;

    pthread_t event_thread;
    struct session_struct *session;
} display_t;


/*----------------------------------------------------------------------------
**  Prototypes
**--------------------------------------------------------------------------*/
int display_open(display_t *d, struct session_struct *session);
void display_close(display_t *display);
int display_create_screen_images(display_t *d);
void display_destroy_screen_images(display_t *d);
int display_start_event_thread(display_t *d);
void display_stop_event_thread(display_t *d);
int display_find_changed_tiles(display_t *d, int row, int *tiles, int tiles_across);
void display_copy_image_into_fullscreen(display_t *d, shm_image_t *shmi, int x, int y);
int display_scan_whole_screen(display_t *d, int num_vertical_tiles, int num_horizontal_tiles,
                              int tiles[][num_horizontal_tiles], int *tiles_changed_in_row);

shm_image_t *create_shm_image(display_t *d, unsigned int w, unsigned int h);
int read_shm_image(display_t *d, shm_image_t *shmi, int x, int y);
void destroy_shm_image(display_t *d, shm_image_t *shmi);

int display_trust_damage(display_t *d);

#endif
