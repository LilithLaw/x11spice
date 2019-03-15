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

static RRCrtcRec *
dummy_present_get_crtc(WindowRec * window)
{
    return NULL;
}

static int
dummy_present_get_ust_msc(RRCrtcRec * crtc, CARD64 * ust, CARD64 * msc)
{
    *ust = dummy_gettime_us();
    *msc = 0;

    return Success;
}

static int
dummy_present_queue_vblank(RRCrtcRec * crtc, uint64_t event_id, uint64_t msc)
{
    present_event_notify(event_id, dummy_gettime_us(), msc);

    return Success;
}

static void
dummy_present_abort_vblank(RRCrtcRec * crtc, uint64_t event_id, uint64_t msc)
{
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
