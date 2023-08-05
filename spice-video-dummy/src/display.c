/*
 * Copyright 2019 Jeremy White
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

#define DUMMY_DEFAULT_WIDTH 1024
#define DUMMY_DEFAULT_HEIGHT 768

static Bool
crtc_resize(ScrnInfoRec * scrn, int width, int height)
{
    PixmapRec *pixmap;
    ScreenRec *screen;

    screen = xf86ScrnToScreen(scrn);
    pixmap = screen->GetScreenPixmap(screen);

    screen->ModifyPixmapHeader(pixmap, width, height, -1, -1, -1, NULL);

    scrn->virtualX = width;
    scrn->virtualY = height;

    return TRUE;
}

static const xf86CrtcConfigFuncsRec crtc_config_funcs = {
    crtc_resize
};

static void
crtc_dpms(xf86CrtcPtr crtc, int mode)
{
    xf86DrvMsg(crtc->scrn->scrnIndex, X_INFO, "%s: STUB dpms %d\n", __func__, mode);
}

static Bool
crtc_set_mode_major(xf86CrtcPtr crtc, DisplayModePtr mode, Rotation rotation, int x, int y)
{
    struct dummy_crtc_state *state;
    uint64_t ust;

    xf86DrvMsg(crtc->scrn->scrnIndex, X_INFO, "%s: mode %s, x %d, y %d\n", __func__, mode->name, x,
               y);

    state = crtc->driver_private;
    ust = dummy_gettime_us();
    if (state->interval)
        state->msc_base += ((ust - state->ust_base) / state->interval);
    state->ust_base = ust;
    state->interval =
        (((uint64_t) 1000 * mode->HTotal * mode->VTotal) + (mode->Clock / 2)) / mode->Clock;

    crtc->mode = *mode;
    crtc->x = x;
    crtc->y = y;
    crtc->rotation = rotation;
#if XORG_VERSION_CURRENT >= XORG_VERSION_NUMERIC (1, 5, 99, 0, 0)
    crtc->transformPresent = FALSE;
#endif

    return TRUE;
}

static void
crtc_destroy(xf86CrtcPtr crtc)
{
    xf86DrvMsg(crtc->scrn->scrnIndex, X_INFO, "%s\n", __func__);

    if (crtc->driver_private) {
        dummy_present_free_vblanks(crtc);
        free(crtc->driver_private);
    }
    crtc->driver_private = NULL;
}

static void
dummy_crtc_set_cursor_colors(xf86CrtcPtr crtc, int bg, int fg)
{
}

static void
dummy_crtc_set_cursor_position(xf86CrtcPtr crtc, int x, int y)
{
}

static void
dummy_crtc_hide_cursor(xf86CrtcPtr crtc)
{
}

#if XF86_CRTC_VERSION > 7
static Bool
#else
static void
#endif
dummy_crtc_show_cursor(xf86CrtcPtr crtc)
{
#if XF86_CRTC_VERSION > 7
    return TRUE;
#endif
}

static Bool
dummy_crtc_load_cursor_argb_check(xf86CrtcPtr crtc, CARD32 * image)
{
    return TRUE;
}

static const xf86CrtcFuncsRec crtc_funcs = {
    .dpms = crtc_dpms,
    .save = NULL,
    .restore = NULL,
    .lock = NULL,
    .unlock = NULL,
    .mode_fixup = NULL,
    .prepare = NULL,
    .mode_set = NULL,
    .commit = NULL,
    .gamma_set = NULL,
    .shadow_allocate = NULL,
    .shadow_create = NULL,
    .shadow_destroy = NULL,
    .set_cursor_colors = dummy_crtc_set_cursor_colors,
    .set_cursor_position = dummy_crtc_set_cursor_position,
#if XF86_CRTC_VERSION > 7
    .show_cursor = NULL,
    .show_cursor_check = dummy_crtc_show_cursor,
#else
    .show_cursor = dummy_crtc_show_cursor,
#endif
    .hide_cursor = dummy_crtc_hide_cursor,
    .load_cursor_image = NULL,
    .load_cursor_image_check = NULL,
    .load_cursor_argb = NULL,
    .load_cursor_argb_check = dummy_crtc_load_cursor_argb_check,
    .destroy = crtc_destroy,
    .set_mode_major = crtc_set_mode_major,
    .set_origin = NULL,
    .set_scanout_pixmap = NULL,
};

void
crtc_config_init(ScrnInfoPtr pScrn)
{
    xf86CrtcConfigInit(pScrn, &crtc_config_funcs);
}

void
crtc_create_multiple(ScrnInfoPtr pScrn, unsigned int num_crtcs)
{
    struct dummy_crtc_state *state;
    xf86CrtcRec *crtc;
    unsigned int i;

    for (i = 0; i < num_crtcs; i++) {
        crtc = xf86CrtcCreate(pScrn, &crtc_funcs);
        if (!crtc) {
            xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "Unable to create crtc %d\n", i);
            break;
        }

        state = malloc(sizeof(*state));
        state->ust_base = 0;
        state->msc_base = 0;
        state->interval = 0;
        xorg_list_init(&state->vblank_queue);
        xorg_list_init(&state->vblank_free);
        state->vblank_timer = NULL;
        crtc->driver_private = state;
    }

    /*
     * Set the CRTC parameters for all of the modes based on the type
     * of mode, and the chipset's interlace requirements.
     *
     * Calling this is required if the mode->Crtc* values are used by the
     * driver and if the driver doesn't provide code to set them.  They
     * are not pre-initialised at all.
     */
    xf86SetCrtcForModes(pScrn, 0);

    xf86CrtcSetSizeRange(pScrn, 320, 200, pScrn->display->virtualX, pScrn->display->virtualY);
}

/**
 * Turns the output on/off, or sets intermediate power levels if available.
 *
 * Unsupported intermediate modes drop to the lower power setting.  If the
 * mode is DPMSModeOff, the output must be disabled, as the DPLL may be
 * disabled afterwards.
 */
static void
output_dpms(xf86OutputPtr output, int mode)
{
    xf86DrvMsg(output->scrn->scrnIndex, X_INFO, "%s: STUB dpms %d\n", __func__, mode);
}

/**
 * Callback for testing a video mode for a given output.
 *
 * This function should only check for cases where a mode can't be supported
 * on the output specifically, and not represent generic CRTC limitations.
 *
 * \return MODE_OK if the mode is valid, or another MODE_* otherwise.
 */
static int
output_mode_valid(xf86OutputPtr output, DisplayModePtr pMode)
{
    return MODE_OK;
}

/* This is copied from xf86Crtc.c/GuessRangeFromModes.  Essentially, we want
   all of the default modes to be considered within sync range.  The normal
   xf86InitialConfiguration will use this, but then discard the sync ranges.
*/
static void
output_guess_ranges_from_modes(MonPtr mon, DisplayModePtr mode)
{
    if (!mon || !mode)
        return;

    mon->nHsync = 1;
    mon->hsync[0].lo = 1024.0;
    mon->hsync[0].hi = 0.0;

    mon->nVrefresh = 1;
    mon->vrefresh[0].lo = 1024.0;
    mon->vrefresh[0].hi = 0.0;

    while (mode) {
        if (!mode->HSync)
            mode->HSync = ((float) mode->Clock) / ((float) mode->HTotal);

        if (!mode->VRefresh)
            mode->VRefresh = (1000.0 * ((float) mode->Clock)) /
                ((float) (mode->HTotal * mode->VTotal));

        if (mode->HSync < mon->hsync[0].lo)
            mon->hsync[0].lo = mode->HSync;

        if (mode->HSync > mon->hsync[0].hi)
            mon->hsync[0].hi = mode->HSync;

        if (mode->VRefresh < mon->vrefresh[0].lo)
            mon->vrefresh[0].lo = mode->VRefresh;

        if (mode->VRefresh > mon->vrefresh[0].hi)
            mon->vrefresh[0].hi = mode->VRefresh;

        mode = mode->next;
    }

    /* stretch out the bottom to fit 640x480@60 */
    if (mon->hsync[0].lo > 31.0)
        mon->hsync[0].lo = 31.0;
    if (mon->vrefresh[0].lo > 58.0)
        mon->vrefresh[0].lo = 58.0;
}

/**
 * Probe for a connected output, and return detect_status.
 */
static xf86OutputStatus
output_detect(xf86OutputPtr output)
{
    xf86DrvMsg(output->scrn->scrnIndex, X_INFO, "%s: STUB detect\n", __func__);
    return XF86OutputStatusConnected;
}

static DisplayModePtr
output_create_mode(ScrnInfoPtr pScrn, int width, int height, int type)
{
    DisplayModePtr mode;

    mode = xnfcalloc(1, sizeof(DisplayModeRec));

    mode->status = MODE_OK;
    mode->type = type;
    mode->HDisplay = width;
    mode->HSyncStart = (width * 105 / 100 + 7) & ~7;
    mode->HSyncEnd = (width * 115 / 100 + 7) & ~7;
    mode->HTotal = (width * 130 / 100 + 7) & ~7;
    mode->VDisplay = height;
    mode->VSyncStart = height + 1;
    mode->VSyncEnd = height + 4;
    mode->VTotal = height * 1035 / 1000;
    mode->Clock = mode->HTotal * mode->VTotal * 60 / 1000;
    mode->Flags = V_NHSYNC | V_PVSYNC;

    xf86SetModeDefaultName(mode);
    xf86SetModeCrtc(mode, pScrn->adjustFlags);  /* needed? xf86-video-modesetting does this */

    return mode;
}

static void
output_check_modes(ScrnInfoPtr pScrn, DisplayModePtr modes)
{
    int preferred_found = 0;
    int virtual_found = 0;
    DisplayModePtr mode = modes;

    while (mode) {
        if (mode->HDisplay == DUMMY_DEFAULT_WIDTH && mode->VDisplay == DUMMY_DEFAULT_HEIGHT) {
            mode->type |= M_T_PREFERRED;
            preferred_found++;
        }
        if (mode->HDisplay == pScrn->display->virtualX &&
            mode->VDisplay == pScrn->display->virtualY) {
            virtual_found = 0;
        }

        mode->type |= M_T_DRIVER;
        mode = mode->next;
    }

    if (preferred_found == 0) {
        mode = output_create_mode(pScrn, DUMMY_DEFAULT_WIDTH,
                                  DUMMY_DEFAULT_HEIGHT, M_T_DRIVER | M_T_PREFERRED);
        xf86ModesAdd(modes, mode);
    }

    if (DUMMY_DEFAULT_WIDTH == pScrn->display->virtualX &&
        DUMMY_DEFAULT_HEIGHT == pScrn->display->virtualY) {
        virtual_found++;
    }

    if (virtual_found == 0) {
        mode = output_create_mode(pScrn, pScrn->display->virtualX,
                                  pScrn->display->virtualY, M_T_DRIVER);
        xf86ModesAdd(modes, mode);
    }
}

/**
 * Query the device for the modes it provides.
 *
 * This function may also update MonInfo, mm_width, and mm_height.
 *
 * \return singly-linked list of modes or NULL if no modes found.
 */
static DisplayModePtr
output_get_modes(xf86OutputPtr output)
{
    DisplayModePtr modes = output->driver_private;

    xf86DrvMsg(output->scrn->scrnIndex, X_INFO, "%s: STUB %p\n", __func__, modes);

    return xf86DuplicateModes(output->scrn, modes);
}

/**
 * Callback to get current CRTC for a given output
 */
static xf86CrtcPtr
output_get_crtc(xf86OutputPtr output)
{
    return output->crtc;
}

/**
 * Clean up driver-specific bits of the output
 */
static void
output_destroy(xf86OutputPtr output)
{
    DisplayModePtr modes = output->driver_private;

    output->driver_private = NULL;
    while (modes)
        xf86DeleteMode(&modes, modes);
}

xf86OutputFuncsRec output_funcs = {
    .create_resources = NULL,
    .dpms = output_dpms,
    .save = NULL,
    .restore = NULL,
    .mode_valid = output_mode_valid,
    .mode_fixup = NULL,         /* Not required if crtc provides set_major_mode */
    .prepare = NULL,
    .commit = NULL,
    .mode_set = NULL,
    .detect = output_detect,
    .get_modes = output_get_modes,

#ifdef RANDR_12_INTERFACE
    .set_property = NULL,
#endif
#ifdef RANDR_13_INTERFACE
    .get_property = NULL,
#endif
#ifdef RANDR_GET_CRTC_INTERFACE
    .get_crtc = output_get_crtc,
#endif
    .destroy = output_destroy,
};

void
output_pre_init(ScrnInfoPtr pScrn, unsigned int num_outputs)
{
    DisplayModePtr modes;
    unsigned int i;
    char name[32];
    xf86OutputPtr output;

    for (i = 0; i < num_outputs; i++) {
        snprintf(name, sizeof(name), "SPICE-%d", i);
        output = xf86OutputCreate(pScrn, &output_funcs, name);
        if (!output) {
            xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "Unable to create output %s\n", name);
            break;
        }
        modes = xf86GetDefaultModes();
        output->possible_crtcs = (1u << i);
        output_check_modes(pScrn, modes);
        output_guess_ranges_from_modes(output->scrn->monitor, modes);
        output->driver_private = modes;
    }
}
