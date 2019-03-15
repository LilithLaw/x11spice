
/* All drivers should typically include these */
#include "xf86.h"
#include "xf86_OSproc.h"

#include "xf86Cursor.h"

#ifdef XvExtension
#include "xf86xv.h"
#include <X11/extensions/Xv.h>
#endif
#include <string.h>

#include "compat-api.h"

#define GLAMOR_FOR_XORG 1
#include "glamor.h"

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

    dummy_colors colors[1024];
    Bool (*CreateWindow)();    /* wrapped CreateWindow */
    Bool prop;

    Bool glamor;
    int fd;
} DUMMYRec, *DUMMYPtr;

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
