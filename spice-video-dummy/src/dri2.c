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
#include "dri2.h"
#include "xf86drm.h"

struct dummy_dri2_buffer_private {
    PixmapRec *pixmap;
};

static PixmapRec *
dummy_get_drawable_pixmap(DrawableRec * drawable)
{
    const ScreenRec *screen = drawable->pScreen;

    if (drawable->type == DRAWABLE_PIXMAP) {
        return (PixmapRec *) drawable;
    }
    return screen->GetWindowPixmap((WindowRec *) drawable);
}

static DRI2Buffer2Rec *
dummy_dri2_create_buffer2(ScreenRec * screen,
                          DrawableRec * drawable, unsigned int attachment, unsigned int format)
{
    const ScrnInfoRec *scrn = xf86ScreenToScrn(screen);
    struct dummy_dri2_buffer_private *private;
    DRI2Buffer2Rec *buffer;
    PixmapRec *pixmap;
    CARD16 pitch;
    CARD32 size;

    if (!(buffer = calloc(1, sizeof(*buffer)))) {
        return NULL;
    }

    if (!(private = calloc(1, sizeof(*private)))) {
        free(buffer);
        return NULL;
    }

    pixmap = NULL;
    if (attachment == DRI2BufferFrontLeft) {
        pixmap = dummy_get_drawable_pixmap(drawable);
        if (pixmap && pixmap->drawable.pScreen != screen) {
            pixmap = NULL;
        }
        if (pixmap) {
            ++pixmap->refcnt;
        }
    }

    if (!pixmap) {
        switch (attachment) {
        case DRI2BufferAccum:
        case DRI2BufferBackLeft:
        case DRI2BufferBackRight:
        case DRI2BufferFakeFrontLeft:
        case DRI2BufferFakeFrontRight:
        case DRI2BufferFrontLeft:
        case DRI2BufferFrontRight:
            break;

        case DRI2BufferStencil:
        case DRI2BufferDepth:
        case DRI2BufferDepthStencil:
        case DRI2BufferHiz:
        default:
            xf86DrvMsg(scrn->scrnIndex, X_WARNING,
                       "Request for DRI2 buffer attachment %#x unsupported.\n", attachment);
            free(private);
            free(buffer);
            return NULL;
        }

        if (!(pixmap = screen->CreatePixmap(screen, drawable->width,
                                            drawable->height, format ? format : drawable->depth,
                                            0))) {
            free(private);
            free(buffer);
            return NULL;
        }
    }

    buffer->attachment = attachment;
    buffer->cpp = pixmap->drawable.bitsPerPixel / 8;
    buffer->format = format;
    buffer->flags = 0;

    buffer->name = glamor_name_from_pixmap(pixmap, &pitch, &size);
    buffer->pitch = pitch;
    if (buffer->name == -1) {
        xf86DrvMsg(scrn->scrnIndex, X_ERROR, "Failed to get DRI2 name for pixmap.\n");
        screen->DestroyPixmap(pixmap);
        free(private);
        free(buffer);
        return NULL;
    }

    buffer->driverPrivate = private;
    private->pixmap = pixmap;

    return buffer;
}

static DRI2Buffer2Rec *
dummy_dri2_create_buffer(DrawableRec * drawable, unsigned int attachment, unsigned int format)
{
    return dummy_dri2_create_buffer2(drawable->pScreen, drawable, attachment, format);
}

static void
dummy_dri2_destroy_buffer2(ScreenRec * unused, DrawableRec * unused2, DRI2Buffer2Rec * buffer)
{
    struct dummy_dri2_buffer_private *private;
    const ScreenRec *screen;

    if (!buffer) {
        return;
    }

    if (!(private = buffer->driverPrivate)) {
        free(buffer);
        return;
    }

    screen = private->pixmap->drawable.pScreen;
    screen->DestroyPixmap(private->pixmap);
    free(private);
    free(buffer);
}

static void
dummy_dri2_destroy_buffer(DrawableRec * drawable, DRI2Buffer2Rec * buffer)
{
    dummy_dri2_destroy_buffer2(NULL, drawable, buffer);
}

static void
dummy_dri2_copy_region2(ScreenRec * screen, DrawableRec * drawable,
                        RegionRec * region, DRI2BufferRec * dst_buffer, DRI2BufferRec * src_buffer)
{
    const struct dummy_dri2_buffer_private *src_priv = src_buffer->driverPrivate;
    const struct dummy_dri2_buffer_private *dst_priv = dst_buffer->driverPrivate;
    int off_x = 0, off_y = 0;
    Bool translate = FALSE;
    DrawableRec *src, *dst;
    RegionRec *clip_region;
    GC *gc;

    src = (src_buffer->attachment == DRI2BufferFrontLeft) ? drawable : &src_priv->pixmap->drawable;
    dst = (dst_buffer->attachment == DRI2BufferFrontLeft) ? drawable : &dst_priv->pixmap->drawable;

    if (dst_buffer->attachment == DRI2BufferFrontLeft && drawable->pScreen != screen) {
        if (!(dst = DRI2UpdatePrime(drawable, dst_buffer))) {
            return;
        }
        if (dst != drawable) {
            translate = TRUE;
        }
    }

    if (translate && drawable->type == DRAWABLE_WINDOW) {
        const PixmapRec *pixmap = dummy_get_drawable_pixmap(drawable);
        off_x = -pixmap->screen_x;
        off_y = -pixmap->screen_y;
        off_x += drawable->x;
        off_y += drawable->y;
    }

    if (!(gc = GetScratchGC(dst->depth, screen))) {
        return;
    }

    clip_region = REGION_CREATE(screen, NULL, 0);
    REGION_COPY(screen, clip_region, region);
    if (translate) {
        REGION_TRANSLATE(screen, clip_region, off_x, off_y);
    }
    gc->funcs->ChangeClip(gc, CT_REGION, clip_region, 0);
    ValidateGC(dst, gc);

    gc->ops->CopyArea(src, dst, gc, 0, 0, drawable->width, drawable->height, off_x, off_y);

    FreeScratchGC(gc);
}

static void
dummy_dri2_copy_region(DrawableRec * drawable, RegionRec * region,
                       DRI2BufferRec * dst_buffer, DRI2BufferRec * src_buffer)
{
    dummy_dri2_copy_region2(drawable->pScreen, drawable, region, dst_buffer, src_buffer);
}

static int
dummy_dri2_get_msc(DrawableRec * drawable, CARD64 * ust, CARD64 * msc)
{
    ScrnInfoRec *scrn = xf86ScreenToScrn(drawable->pScreen);

    dummy_get_ust_msc(xf86CompatRRCrtc(scrn), ust, msc);

    return TRUE;
}

static int
dummy_dri2_schedule_wait_msc(ClientRec * client, DrawableRec * drawable,
                             CARD64 target_msc, CARD64 divisor, CARD64 remainder)
{
    DRI2WaitMSCComplete(client, drawable, target_msc, 0, 0);

    return TRUE;
}

static int
dummy_dri2_schedule_swap(ClientRec * client, DrawableRec * drawable,
                         DRI2BufferRec * front, DRI2BufferRec * back,
                         CARD64 * target_msc, CARD64 divisor, CARD64 remainder,
                         DRI2SwapEventPtr func, void *data)
{
    RegionRec region;
    BoxRec box;

    box.x1 = 0;
    box.y1 = 0;
    box.x2 = drawable->width;
    box.y2 = drawable->height;
    RegionInit(&region, &box, 0);

    dummy_dri2_copy_region(drawable, &region, front, back);
    DRI2SwapComplete(client, drawable, 0, 0, 0, DRI2_BLIT_COMPLETE, func, data);
    *target_msc = 0;

    return TRUE;
}

Bool
dummy_dri2_screen_init(ScreenRec * screen)
{
    const ScrnInfoRec *scrn = xf86ScreenToScrn(screen);
    const DUMMYRec *dummy = scrn->driverPrivate;
    DRI2InfoRec info;

    if (!glamor_supports_pixmap_import_export(screen)) {
        xf86DrvMsg(scrn->scrnIndex, X_WARNING,
                   "DRI2: glamor lacks support for pixmap import/export\n");
    }

    if (!xf86LoaderCheckSymbol("DRI2Version")) {
        return FALSE;
    }

    memset(&info, 0, sizeof(info));
    info.fd = dummy->fd;
    info.driverName = NULL;
    info.deviceName = drmGetDeviceNameFromFd2(dummy->fd);

    info.version = 9;
    info.CreateBuffer = dummy_dri2_create_buffer;
    info.DestroyBuffer = dummy_dri2_destroy_buffer;
    info.CopyRegion = dummy_dri2_copy_region;
    info.ScheduleSwap = dummy_dri2_schedule_swap;
    info.GetMSC = dummy_dri2_get_msc;
    info.ScheduleWaitMSC = dummy_dri2_schedule_wait_msc;
    info.CreateBuffer2 = dummy_dri2_create_buffer2;
    info.DestroyBuffer2 = dummy_dri2_destroy_buffer2;
    info.CopyRegion2 = dummy_dri2_copy_region2;

    info.numDrivers = 0;
    info.driverNames = NULL;

    return DRI2ScreenInit(screen, &info);
}

void
dummy_dri2_close_screen(ScreenRec * screen)
{
    DRI2CloseScreen(screen);
}
