
/* All drivers should typically include these */
#include "xf86.h"
#include "xf86_OSproc.h"

#include "xf86Cursor.h"
#include "xf86Crtc.h"

#ifdef XvExtension
#include "xf86xv.h"
#include <X11/extensions/Xv.h>
#endif
#include <string.h>
#include <time.h>

#define GLAMOR_FOR_XORG 1
#include "glamor.h"

#include "compat-api.h"

/* Supported chipsets */
typedef enum {
    DUMMY_CHIP
} DUMMYType;

/* function prototypes */

extern Bool DUMMYSwitchMode(SWITCH_MODE_ARGS_DECL);
extern void DUMMYAdjustFrame(ADJUST_FRAME_ARGS_DECL);

/* in dummy_cursor.c */
extern Bool DUMMYCursorInit(ScreenPtr pScrn);
extern void DUMMYShowCursor(ScrnInfoPtr pScrn);
extern void DUMMYHideCursor(ScrnInfoPtr pScrn);

void dummy_dri2_close_screen(ScreenRec * screen);
Bool dummy_dri2_screen_init(ScreenRec * screen);
Bool dummy_present_screen_init(ScreenRec * screen);
void dummy_present_free_vblanks(xf86CrtcPtr crtc);

/* in crtc.c */
void crtc_config_init(ScrnInfoPtr scrn);
void crtc_create_multiple(ScrnInfoPtr scrn, unsigned int num_crtcs);

/* in output.c */
void output_pre_init(ScrnInfoPtr scrn, unsigned int num_outputs);

/* globals */
typedef struct _color {
    int red;
    int green;
    int blue;
} dummy_colors;

typedef struct dummyRec {
    /* options */
    OptionInfoPtr Options;
    Bool swCursor;
    /* proc pointer */
    CloseScreenProcPtr CloseScreen;
    xf86CursorInfoPtr CursorInfo;

    Bool DummyHWCursorShown;
    int cursorX, cursorY;
    int cursorFG, cursorBG;

    int numHeads;

    dummy_colors colors[1024];
    Bool (*CreateWindow)(WindowRec * window);   /* wrapped CreateWindow */
    Bool prop;

    Bool glamor;
    int fd;
} DUMMYRec, *DUMMYPtr;

struct dummy_crtc_state {
    uint64_t ust_base;
    uint64_t msc_base;
    uint64_t interval;

    struct xorg_list vblank_queue;
    struct xorg_list vblank_free;
    OsTimerPtr vblank_timer;
};

/* The privates of the DUMMY driver */
#define DUMMYPTR(p)	((DUMMYPtr)((p)->driverPrivate))

static inline uint64_t
dummy_gettime_us(void)
{
    struct timespec tv;

    if (clock_gettime(CLOCK_MONOTONIC, &tv)) {
        return 0;
    }

    return (uint64_t) tv.tv_sec * 1000000 + tv.tv_nsec / 1000;
}

static inline void
dummy_get_ust_msc(const RRCrtcRec * crtc, uint64_t * ust, uint64_t * msc)
{
    struct dummy_crtc_state *state;
    xf86CrtcRec *xf86_crtc;

    xf86_crtc = crtc->devPrivate;
    state = xf86_crtc->driver_private;
    *ust = dummy_gettime_us();
    *msc = state->msc_base;
    if (state->interval)
        *msc += (*ust - state->ust_base) / state->interval;
}
