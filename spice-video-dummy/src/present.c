/*
 * Copyright 2019 Henri Verbeet
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "dummy.h"
#include "present.h"

#include <stdbool.h>

struct dummy_vblank {
    struct xorg_list list;
    uint64_t event_id;
    uint64_t target_msc;
};

static RRCrtcRec *
dummy_present_get_crtc(WindowRec * window)
{
    ScrnInfoRec *scrn;

    if (!(scrn = xf86ScreenToScrn(window->drawable.pScreen)))
        return NULL;
    return xf86CompatRRCrtc(scrn);
}

static int
dummy_present_get_ust_msc(RRCrtcRec * crtc, CARD64 * ust, CARD64 * msc)
{
    dummy_get_ust_msc(crtc, ust, msc);

    return Success;
}

static uint64_t
dummy_present_ust_for_msc(RRCrtcRec * crtc, uint64_t msc)
{
    struct dummy_crtc_state *state;
    xf86CrtcRec *xf86_crtc;

    xf86_crtc = crtc->devPrivate;
    state = xf86_crtc->driver_private;

    return state->ust_base + (state->interval * (msc - state->msc_base));
}

static uint32_t
dummy_do_timer(OsTimerPtr timer, uint32_t time, void *arg)
{
    struct dummy_vblank *vblank, *tmp;
    struct dummy_crtc_state *state;
    uint64_t ust, msc, next_ust;
    xf86CrtcRec *xf86_crtc;
    RRCrtcRec *crtc = arg;

    xf86_crtc = crtc->devPrivate;
    state = xf86_crtc->driver_private;

    dummy_get_ust_msc(crtc, &ust, &msc);
    xorg_list_for_each_entry_safe(vblank, tmp, &state->vblank_queue, list) {
        if (msc < vblank->target_msc)
            continue;

        present_event_notify(vblank->event_id, ust, msc);
        xorg_list_del(&vblank->list);
        xorg_list_add(&vblank->list, &state->vblank_free);
    }

    if (!xorg_list_is_empty(&state->vblank_queue)) {
        next_ust = dummy_present_ust_for_msc(crtc, msc + 1);
        TimerSet(state->vblank_timer, TimerAbsolute, (next_ust / 1000) + 1, dummy_do_timer, arg);
    }

    return 0;
}

static int
dummy_present_queue_vblank(RRCrtcRec * crtc, uint64_t event_id, uint64_t target_msc)
{
    struct dummy_crtc_state *state;
    uint64_t ust, target_ust, msc;
    struct dummy_vblank *vblank;
    xf86CrtcRec *xf86_crtc;
    bool start = false;

    xf86_crtc = crtc->devPrivate;
    state = xf86_crtc->driver_private;

    dummy_get_ust_msc(crtc, &ust, &msc);
    if ((target_ust = dummy_present_ust_for_msc(crtc, target_msc)) <= ust)
        target_ust = dummy_present_ust_for_msc(crtc, msc + 1);

    if (!xorg_list_is_empty(&state->vblank_free)) {
        vblank = xorg_list_first_entry(&state->vblank_free, struct dummy_vblank, list);
        xorg_list_del(&vblank->list);
    } else if (!(vblank = malloc(sizeof(*vblank)))) {
        return BadAlloc;
    }

    vblank->event_id = event_id;
    vblank->target_msc = target_msc;
    if (xorg_list_is_empty(&state->vblank_queue))
        start = true;
    xorg_list_append(&vblank->list, &state->vblank_queue);
    if (start
        && !(state->vblank_timer =
             TimerSet(NULL, TimerAbsolute, (target_ust / 1000) + 1, dummy_do_timer, crtc))) {
        xorg_list_del(&vblank->list);
        free(vblank);
        return BadAlloc;
    }

    return Success;
}

static void
dummy_present_abort_vblank(RRCrtcRec * crtc, uint64_t event_id, uint64_t msc)
{
    struct dummy_vblank *vblank, *tmp;
    struct dummy_crtc_state *state;
    xf86CrtcRec *xf86_crtc;

    xf86_crtc = crtc->devPrivate;
    state = xf86_crtc->driver_private;

    xorg_list_for_each_entry_safe(vblank, tmp, &state->vblank_queue, list) {
        if (vblank->event_id == event_id) {
            xorg_list_del(&vblank->list);
            free(vblank);
            break;
        }
    }
}

static void
dummy_present_flush(WindowRec * window)
{
    glamor_block_handler(window->drawable.pScreen);
}

static Bool
dummy_present_check_flip(RRCrtcRec * crtc, WindowRec * window, PixmapRec * pixmap, Bool sync_flip)
{
    return FALSE;
}

static Bool
dummy_present_flip(RRCrtcRec * crtc, uint64_t event_id,
                   uint64_t target_msc, PixmapRec * pixmap, Bool sync_flip)
{
    return FALSE;
}

static void
dummy_present_unflip(ScreenRec * screen, uint64_t event_id)
{
    present_event_notify(event_id, 0, 0);
}

Bool
dummy_present_screen_init(ScreenRec * screen)
{
    static struct present_screen_info present_screen_info = {
        .version = PRESENT_SCREEN_INFO_VERSION,

        .get_crtc = dummy_present_get_crtc,
        .get_ust_msc = dummy_present_get_ust_msc,
        .queue_vblank = dummy_present_queue_vblank,
        .abort_vblank = dummy_present_abort_vblank,
        .flush = dummy_present_flush,

        .capabilities = PresentCapabilityNone,
        .check_flip = dummy_present_check_flip,
        .flip = dummy_present_flip,
        .unflip = dummy_present_unflip,
    };

    return present_screen_init(screen, &present_screen_info);
}

void
dummy_present_free_vblanks(xf86CrtcPtr crtc)
{
    struct dummy_crtc_state *state;
    struct dummy_vblank *vblank, *tmp;
    state = crtc->driver_private;
    xorg_list_for_each_entry_safe(vblank, tmp, &state->vblank_queue, list) {
        xorg_list_del(&vblank->list);
        free(vblank);
    }

    xorg_list_for_each_entry_safe(vblank, tmp, &state->vblank_free, list) {
        xorg_list_del(&vblank->list);
        free(vblank);
    }
}
