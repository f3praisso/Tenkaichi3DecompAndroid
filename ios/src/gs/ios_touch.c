/* iOS: the on-screen controller, in the look and with the controls of the Android one (TouchPadView): round buttons
   with a dark radial fill, a lavender ring, golden glyphs and a small badge with the pad's name for the button
   (X A B Y, LB LT RB RT, L3 R3); the left stick as an orb with three stars.
   The engine takes it through Port_TouchPad (port/src/gs/gs_input.c): player 1's buttons and sticks, added to the
   keyboard's and the controllers'.

   Two layouts, as on Android: FIGHT (the stick, the four attack buttons, the shoulders, the specials: L2 with another
   button, TROCA, TRANSFORMAR, the member chooser) and MENU (the D-pad, the four buttons, START, SELECT). They change by
   themselves with the game (gPortInMenus) and the "layout" button changes them by hand.
   Top centre: a gear (the controller's menu: size, opacity, floating stick, automatic layout, edit the positions,
   reset) and an eye (hide/show). A touch outside every control goes on to the game's window.

   Objective-C in a .c file: the build compiles this one as Objective-C with ARC (undefined.py). */
#import <UIKit/UIKit.h>
#import <QuartzCore/QuartzCore.h>
#import <Metal/Metal.h>
#include <math.h>
#include <stdio.h>

extern void Port_TouchPad(unsigned buttons, int lx, int ly, int rx, int ry);
extern int gPortOverlayOpen;                        /* gs_input.c: the engine's settings window is open */
extern int gPortInMenus;                            /* headless.c: the game is in its menus (0 in a fight) */
extern int Port_Setting(const char *name, int def); /* plat_settings.c: bt3_settings.txt */
extern int Port_LateInput(void);                    /* plat_stub.c: "Low input lag" */

enum {
    B_SELECT = 0x0001, B_L3 = 0x0002, B_R3 = 0x0004, B_START = 0x0008, B_UP = 0x0010, B_RIGHT = 0x0020, B_DOWN = 0x0040, B_LEFT = 0x0080,
    B_L2 = 0x0100, B_R2 = 0x0200, B_L1 = 0x0400, B_R1 = 0x0800,
    B_TRIANGLE = 0x1000, B_CIRCLE = 0x2000, B_CROSS = 0x4000, B_SQUARE = 0x8000
};

enum {
    I_NONE, I_UP, I_DOWN, I_LEFT, I_RIGHT, I_FIST, I_DASH, I_SHIELD, I_BURST, I_LOCK, I_FLAME, I_ASCEND, I_DESCEND, I_SWAP, I_BOLT,
    I_SPECIAL1, I_SPECIAL2, I_ULTIMATE, I_MEMBERS, I_PAUSE, I_MOVE, I_PLAY, I_MENU
};
enum { C_BLUE, C_GREEN, C_RED, C_YELLOW, C_WHITE };
enum { K_BUTTON, K_TEAM, K_MENU, K_NAV }; /* a button; the right stick's push down (the next member); START+SELECT (the engine's window); the layout */
enum { M_FIGHT = 1, M_NAV = 2, M_BOTH = 3 };

/* the movable pieces */
enum { E_DPAD, E_STICK, E_SQUARE, E_CIRCLE, E_TRIANGLE, E_CROSS, E_BLAST1, E_BLAST2, E_ULT, E_L1, E_L2, E_R1, E_R2, E_L3, E_R3, E_TEAM,
       E_SELECT, E_START, E_MENU, E_NAV, E_COUNT };

typedef struct {
    float fx, fy, size; /* where it is (a fraction of the screen) and how big (points; the stick's is its diameter) */
} Elem;

/* the places and sizes of the Android layout (TouchConfig.kt) */
static const Elem kElem[E_COUNT] = {
    [E_DPAD] = {0.14f, 0.70f, 54},     [E_STICK] = {0.14f, 0.70f, 140},   [E_SQUARE] = {0.845f, 0.805f, 82}, [E_CIRCLE] = {0.745f, 0.735f, 66},
    [E_TRIANGLE] = {0.855f, 0.605f, 66}, [E_CROSS] = {0.945f, 0.735f, 60}, [E_BLAST1] = {0.745f, 0.555f, 52}, [E_BLAST2] = {0.945f, 0.555f, 52},
    [E_ULT] = {0.955f, 0.905f, 52},    [E_L1] = {0.075f, 0.155f, 58},     [E_L2] = {0.075f, 0.275f, 58},     [E_R1] = {0.875f, 0.155f, 58},
    [E_R2] = {0.875f, 0.275f, 58},     [E_L3] = {0.31f, 0.58f, 50},       [E_R3] = {0.965f, 0.415f, 48},     [E_TEAM] = {0.22f, 0.435f, 48},
    [E_SELECT] = {0.30f, 0.075f, 40},  [E_START] = {0.40f, 0.075f, 40},  [E_MENU] = {0.60f, 0.075f, 40},    [E_NAV] = {0.70f, 0.075f, 40},
};

typedef struct {
    unsigned bits;
    int kind, elem, icon;
    const char *badge; /* the pad's name for the button, in the corner (fight layout) */
    int color, modes;
    float ox, oy;      /* from the piece's centre, in buttons (only the D-pad has more than one key in a piece) */
    const char *letter; /* a face button in the menu layout: just the letter, big */
} Key;

static const Key kKeys[] = {
    {B_UP, K_BUTTON, E_DPAD, I_UP, NULL, 0, M_NAV, 0, -1, NULL},      {B_DOWN, K_BUTTON, E_DPAD, I_DOWN, NULL, 0, M_NAV, 0, 1, NULL},
    {B_LEFT, K_BUTTON, E_DPAD, I_LEFT, NULL, 0, M_NAV, -1, 0, NULL},  {B_RIGHT, K_BUTTON, E_DPAD, I_RIGHT, NULL, 0, M_NAV, 1, 0, NULL},
    {B_SQUARE, K_BUTTON, E_SQUARE, I_FIST, "X", C_BLUE, M_BOTH, 0, 0, "X"},        {B_CROSS, K_BUTTON, E_CROSS, I_DASH, "A", C_GREEN, M_BOTH, 0, 0, "A"},
    {B_CIRCLE, K_BUTTON, E_CIRCLE, I_SHIELD, "B", C_RED, M_BOTH, 0, 0, "B"},       {B_TRIANGLE, K_BUTTON, E_TRIANGLE, I_BURST, "Y", C_YELLOW, M_BOTH, 0, 0, "Y"},
    {B_L1, K_BUTTON, E_L1, I_LOCK, "LB", C_WHITE, M_FIGHT, 0, 0, NULL},            {B_L2, K_BUTTON, E_L2, I_FLAME, "LT", C_WHITE, M_FIGHT, 0, 0, NULL},
    {B_R1, K_BUTTON, E_R1, I_ASCEND, "RB", C_WHITE, M_FIGHT, 0, 0, NULL},          {B_R2, K_BUTTON, E_R2, I_DESCEND, "RT", C_WHITE, M_FIGHT, 0, 0, NULL},
    {B_L3, K_BUTTON, E_L3, I_SWAP, "L3", C_WHITE, M_FIGHT, 0, 0, NULL},            {B_R3, K_BUTTON, E_R3, I_BOLT, "R3", C_WHITE, M_FIGHT, 0, 0, NULL},
    {B_L2 | B_CIRCLE, K_BUTTON, E_BLAST1, I_SPECIAL1, "B", C_RED, M_FIGHT, 0, 0, NULL},
    {B_L2 | B_TRIANGLE, K_BUTTON, E_BLAST2, I_SPECIAL2, "Y", C_YELLOW, M_FIGHT, 0, 0, NULL},
    {B_L2 | B_DOWN | B_TRIANGLE, K_BUTTON, E_ULT, I_ULTIMATE, "Y", C_YELLOW, M_FIGHT, 0, 0, NULL},
    {0, K_TEAM, E_TEAM, I_MEMBERS, NULL, 0, M_FIGHT, 0, 0, NULL},
    {B_SELECT, K_BUTTON, E_SELECT, I_MENU, NULL, 0, M_BOTH, 0, 0, NULL},           {B_START, K_BUTTON, E_START, I_PLAY, NULL, 0, M_BOTH, 0, 0, NULL},
    {0, K_MENU, E_MENU, I_PAUSE, NULL, 0, M_BOTH, 0, 0, NULL},                     {0, K_NAV, E_NAV, I_MOVE, NULL, 0, M_BOTH, 0, 0, NULL},
};
#define N_KEYS ((int)(sizeof(kKeys) / sizeof(kKeys[0])))

static UIColor *rgb(CGFloat r, CGFloat g, CGFloat b, CGFloat a) {
    return [UIColor colorWithRed:r green:g blue:b alpha:a];
}

static UIColor *badge_color(int c) {
    switch (c) {
    case C_BLUE: return rgb(0.36, 0.55, 0.94, 1);
    case C_GREEN: return rgb(0.49, 0.75, 0.29, 1);
    case C_RED: return rgb(0.88, 0.35, 0.31, 1);
    case C_YELLOW: return rgb(0.96, 0.78, 0.26, 1);
    }
    return rgb(1, 1, 1, 1);
}

/* a regular star, as a closed path */
static UIBezierPath *star_path(CGPoint c, CGFloat r1, CGFloat r2) {
    UIBezierPath *p = [UIBezierPath bezierPath];
    int i;
    for (i = 0; i < 10; i++) {
        CGFloat a = -M_PI / 2 + i * M_PI / 5, r = i % 2 == 0 ? r1 : r2;
        CGPoint q = CGPointMake(c.x + r * cos(a), c.y + r * sin(a));
        if (i == 0) {
            [p moveToPoint:q];
        } else {
            [p addLineToPoint:q];
        }
    }
    [p closePath];
    return p;
}

/* The glyph of an icon in a button of radius s (the unit square of icons.py scaled by s), centred on (0, 0):
   what is stroked goes in `stroke`, what is solid in `fill`. (Generated from one description, with the preview.) */
static void icon_paths(int icon, CGFloat s, UIBezierPath *stroke, UIBezierPath *fill) {
    switch (icon) {
    case I_UP: {
        [fill moveToPoint:CGPointMake(0 * s, -0.55 * s)];
        [fill addLineToPoint:CGPointMake(0.5 * s, 0.35 * s)];
        [fill addLineToPoint:CGPointMake(-0.5 * s, 0.35 * s)];
        [fill closePath];
        break;
    }
    case I_DOWN: {
        [fill moveToPoint:CGPointMake(0 * s, 0.55 * s)];
        [fill addLineToPoint:CGPointMake(0.5 * s, -0.35 * s)];
        [fill addLineToPoint:CGPointMake(-0.5 * s, -0.35 * s)];
        [fill closePath];
        break;
    }
    case I_LEFT: {
        [fill moveToPoint:CGPointMake(-0.55 * s, 0 * s)];
        [fill addLineToPoint:CGPointMake(0.35 * s, 0.5 * s)];
        [fill addLineToPoint:CGPointMake(0.35 * s, -0.5 * s)];
        [fill closePath];
        break;
    }
    case I_RIGHT: {
        [fill moveToPoint:CGPointMake(0.55 * s, 0 * s)];
        [fill addLineToPoint:CGPointMake(-0.35 * s, 0.5 * s)];
        [fill addLineToPoint:CGPointMake(-0.35 * s, -0.5 * s)];
        [fill closePath];
        break;
    }
    case I_FIST: {
        [stroke appendPath:[UIBezierPath bezierPathWithRoundedRect:CGRectMake(-0.72 * s, -0.02 * s, 1.44 * s, 0.86 * s) cornerRadius:0.2 * s]];
        [stroke appendPath:[UIBezierPath bezierPathWithRoundedRect:CGRectMake(-0.62 * s, -0.6 * s, 0.3 * s, 0.55 * s) cornerRadius:0.14 * s]];
        [stroke appendPath:[UIBezierPath bezierPathWithRoundedRect:CGRectMake(-0.27 * s, -0.66 * s, 0.3 * s, 0.6 * s) cornerRadius:0.14 * s]];
        [stroke appendPath:[UIBezierPath bezierPathWithRoundedRect:CGRectMake(0.08 * s, -0.62 * s, 0.3 * s, 0.57 * s) cornerRadius:0.14 * s]];
        [stroke appendPath:[UIBezierPath bezierPathWithRoundedRect:CGRectMake(0.42 * s, -0.55 * s, 0.28 * s, 0.5 * s) cornerRadius:0.14 * s]];
        break;
    }
    case I_DASH: {
        [stroke appendPath:[UIBezierPath bezierPathWithOvalInRect:CGRectMake(0.22 * s, -0.3 * s, 0.6 * s, 0.6 * s)]];
        [stroke moveToPoint:CGPointMake(-0.95 * s, -0.3 * s)];
        [stroke addLineToPoint:CGPointMake(0.15 * s, -0.12 * s)];
        [stroke moveToPoint:CGPointMake(-0.95 * s, 0 * s)];
        [stroke addLineToPoint:CGPointMake(0.2 * s, 0 * s)];
        [stroke moveToPoint:CGPointMake(-0.95 * s, 0.3 * s)];
        [stroke addLineToPoint:CGPointMake(0.15 * s, 0.12 * s)];
        break;
    }
    case I_SHIELD: {
        [stroke moveToPoint:CGPointMake(-0.7 * s, -0.78 * s)];
        [stroke addLineToPoint:CGPointMake(0 * s, -0.98 * s)];
        [stroke addLineToPoint:CGPointMake(0.7 * s, -0.78 * s)];
        [stroke addLineToPoint:CGPointMake(0.7 * s, -0.1 * s)];
        [stroke addCurveToPoint:CGPointMake(0 * s, 0.98 * s) controlPoint1:CGPointMake(0.7 * s, 0.45 * s) controlPoint2:CGPointMake(0.25 * s, 0.78 * s)];
        [stroke addCurveToPoint:CGPointMake(-0.7 * s, -0.1 * s) controlPoint1:CGPointMake(-0.25 * s, 0.78 * s) controlPoint2:CGPointMake(-0.7 * s, 0.45 * s)];
        [stroke closePath];
        [fill appendPath:[UIBezierPath bezierPathWithOvalInRect:CGRectMake(-0.2 * s, -0.28 * s, 0.4 * s, 0.4 * s)]];
        break;
    }
    case I_BURST: {
        [stroke moveToPoint:CGPointMake(0 * s, -0.98 * s)];
        [stroke addLineToPoint:CGPointMake(0.1607 * s, -0.388 * s)];
        [stroke addLineToPoint:CGPointMake(0.693 * s, -0.693 * s)];
        [stroke addLineToPoint:CGPointMake(0.388 * s, -0.1607 * s)];
        [stroke addLineToPoint:CGPointMake(0.98 * s, 0 * s)];
        [stroke addLineToPoint:CGPointMake(0.388 * s, 0.1607 * s)];
        [stroke addLineToPoint:CGPointMake(0.693 * s, 0.693 * s)];
        [stroke addLineToPoint:CGPointMake(0.1607 * s, 0.388 * s)];
        [stroke addLineToPoint:CGPointMake(0 * s, 0.98 * s)];
        [stroke addLineToPoint:CGPointMake(-0.1607 * s, 0.388 * s)];
        [stroke addLineToPoint:CGPointMake(-0.693 * s, 0.693 * s)];
        [stroke addLineToPoint:CGPointMake(-0.388 * s, 0.1607 * s)];
        [stroke addLineToPoint:CGPointMake(-0.98 * s, 0 * s)];
        [stroke addLineToPoint:CGPointMake(-0.388 * s, -0.1607 * s)];
        [stroke addLineToPoint:CGPointMake(-0.693 * s, -0.693 * s)];
        [stroke addLineToPoint:CGPointMake(-0.1607 * s, -0.388 * s)];
        [stroke closePath];
        [fill appendPath:[UIBezierPath bezierPathWithOvalInRect:CGRectMake(-0.17 * s, -0.17 * s, 0.34 * s, 0.34 * s)]];
        break;
    }
    case I_LOCK: {
        [stroke appendPath:[UIBezierPath bezierPathWithOvalInRect:CGRectMake(-0.52 * s, -0.52 * s, 1.04 * s, 1.04 * s)]];
        [stroke appendPath:[UIBezierPath bezierPathWithOvalInRect:CGRectMake(-0.1 * s, -0.1 * s, 0.2 * s, 0.2 * s)]];
        [stroke moveToPoint:CGPointMake(0 * s, -0.98 * s)];
        [stroke addLineToPoint:CGPointMake(0 * s, -0.5 * s)];
        [stroke moveToPoint:CGPointMake(0 * s, 0.5 * s)];
        [stroke addLineToPoint:CGPointMake(0 * s, 0.98 * s)];
        [stroke moveToPoint:CGPointMake(-0.98 * s, 0 * s)];
        [stroke addLineToPoint:CGPointMake(-0.5 * s, 0 * s)];
        [stroke moveToPoint:CGPointMake(0.5 * s, 0 * s)];
        [stroke addLineToPoint:CGPointMake(0.98 * s, 0 * s)];
        break;
    }
    case I_FLAME: {
        [fill moveToPoint:CGPointMake(0.05 * s, -0.98 * s)];
        [fill addCurveToPoint:CGPointMake(0.62 * s, 0.4 * s) controlPoint1:CGPointMake(0.2 * s, -0.5 * s) controlPoint2:CGPointMake(0.78 * s, -0.15 * s)];
        [fill addCurveToPoint:CGPointMake(0 * s, 0.98 * s) controlPoint1:CGPointMake(0.52 * s, 0.78 * s) controlPoint2:CGPointMake(0.22 * s, 0.98 * s)];
        [fill addCurveToPoint:CGPointMake(-0.62 * s, 0.3 * s) controlPoint1:CGPointMake(-0.3 * s, 0.98 * s) controlPoint2:CGPointMake(-0.65 * s, 0.75 * s)];
        [fill addCurveToPoint:CGPointMake(-0.28 * s, -0.5 * s) controlPoint1:CGPointMake(-0.6 * s, -0.05 * s) controlPoint2:CGPointMake(-0.3 * s, -0.2 * s)];
        [fill addCurveToPoint:CGPointMake(0.05 * s, -0.98 * s) controlPoint1:CGPointMake(-0.15 * s, -0.3 * s) controlPoint2:CGPointMake(-0.05 * s, -0.5 * s)];
        [fill closePath];
        break;
    }
    case I_ASCEND: {
        [stroke moveToPoint:CGPointMake(-0.72 * s, 0 * s)];
        [stroke addLineToPoint:CGPointMake(0 * s, -0.62 * s)];
        [stroke addLineToPoint:CGPointMake(0.72 * s, 0 * s)];
        [stroke moveToPoint:CGPointMake(-0.72 * s, 0.62 * s)];
        [stroke addLineToPoint:CGPointMake(0 * s, 0 * s)];
        [stroke addLineToPoint:CGPointMake(0.72 * s, 0.62 * s)];
        break;
    }
    case I_DESCEND: {
        [stroke moveToPoint:CGPointMake(-0.72 * s, 0 * s)];
        [stroke addLineToPoint:CGPointMake(0 * s, 0.62 * s)];
        [stroke addLineToPoint:CGPointMake(0.72 * s, 0 * s)];
        [stroke moveToPoint:CGPointMake(-0.72 * s, -0.62 * s)];
        [stroke addLineToPoint:CGPointMake(0 * s, 0 * s)];
        [stroke addLineToPoint:CGPointMake(0.72 * s, -0.62 * s)];
        break;
    }
    case I_SWAP: {
        [stroke moveToPoint:CGPointMake(-0.85 * s, -0.32 * s)];
        [stroke addLineToPoint:CGPointMake(0.8 * s, -0.32 * s)];
        [stroke moveToPoint:CGPointMake(0.85 * s, -0.32 * s)];
        [stroke addLineToPoint:CGPointMake(0.51 * s, 0.02 * s)];
        [stroke moveToPoint:CGPointMake(0.85 * s, -0.32 * s)];
        [stroke addLineToPoint:CGPointMake(0.51 * s, -0.66 * s)];
        [stroke moveToPoint:CGPointMake(0.85 * s, 0.32 * s)];
        [stroke addLineToPoint:CGPointMake(-0.8 * s, 0.32 * s)];
        [stroke moveToPoint:CGPointMake(-0.85 * s, 0.32 * s)];
        [stroke addLineToPoint:CGPointMake(-0.51 * s, -0.02 * s)];
        [stroke moveToPoint:CGPointMake(-0.85 * s, 0.32 * s)];
        [stroke addLineToPoint:CGPointMake(-0.51 * s, 0.66 * s)];
        break;
    }
    case I_BOLT: {
        [fill moveToPoint:CGPointMake(0.28 * s, -0.98 * s)];
        [fill addLineToPoint:CGPointMake(-0.52 * s, 0.12 * s)];
        [fill addLineToPoint:CGPointMake(-0.06 * s, 0.12 * s)];
        [fill addLineToPoint:CGPointMake(-0.28 * s, 0.98 * s)];
        [fill addLineToPoint:CGPointMake(0.52 * s, -0.16 * s)];
        [fill addLineToPoint:CGPointMake(0.06 * s, -0.16 * s)];
        [fill closePath];
        break;
    }
    case I_SPECIAL1: {
        [stroke appendPath:[UIBezierPath bezierPathWithOvalInRect:CGRectMake(-0.3 * s, -0.3 * s, 0.6 * s, 0.6 * s)]];
        [stroke appendPath:[UIBezierPath bezierPathWithOvalInRect:CGRectMake(-0.62 * s, -0.62 * s, 1.24 * s, 1.24 * s)]];
        [stroke moveToPoint:CGPointMake(0.495 * s, 0.495 * s)];
        [stroke addLineToPoint:CGPointMake(0.693 * s, 0.693 * s)];
        [stroke moveToPoint:CGPointMake(-0.495 * s, 0.495 * s)];
        [stroke addLineToPoint:CGPointMake(-0.693 * s, 0.693 * s)];
        [stroke moveToPoint:CGPointMake(-0.495 * s, -0.495 * s)];
        [stroke addLineToPoint:CGPointMake(-0.693 * s, -0.693 * s)];
        [stroke moveToPoint:CGPointMake(0.495 * s, -0.495 * s)];
        [stroke addLineToPoint:CGPointMake(0.693 * s, -0.693 * s)];
        [fill appendPath:[UIBezierPath bezierPathWithOvalInRect:CGRectMake(-0.1 * s, -0.1 * s, 0.2 * s, 0.2 * s)]];
        break;
    }
    case I_SPECIAL2: {
        [stroke appendPath:[UIBezierPath bezierPathWithOvalInRect:CGRectMake(-0.42 * s, -0.42 * s, 0.84 * s, 0.84 * s)]];
        {
            UIBezierPath *e = [UIBezierPath bezierPathWithOvalInRect:CGRectMake(-0.98 * s, -0.26 * s, 1.96 * s, 0.52 * s)];
            [e applyTransform:CGAffineTransformMakeRotation(-0.384)];
            [e applyTransform:CGAffineTransformMakeTranslation(0 * s, 0 * s)];
            [stroke appendPath:e];
        }
        break;
    }
    case I_ULTIMATE: {
        [stroke moveToPoint:CGPointMake(0.3 * s, 0 * s)];
        [stroke addLineToPoint:CGPointMake(0.98 * s, 0 * s)];
        [stroke moveToPoint:CGPointMake(0.2121 * s, 0.2121 * s)];
        [stroke addLineToPoint:CGPointMake(0.693 * s, 0.693 * s)];
        [stroke moveToPoint:CGPointMake(0 * s, 0.3 * s)];
        [stroke addLineToPoint:CGPointMake(0 * s, 0.98 * s)];
        [stroke moveToPoint:CGPointMake(-0.2121 * s, 0.2121 * s)];
        [stroke addLineToPoint:CGPointMake(-0.693 * s, 0.693 * s)];
        [stroke moveToPoint:CGPointMake(-0.3 * s, 0 * s)];
        [stroke addLineToPoint:CGPointMake(-0.98 * s, 0 * s)];
        [stroke moveToPoint:CGPointMake(-0.2121 * s, -0.2121 * s)];
        [stroke addLineToPoint:CGPointMake(-0.693 * s, -0.693 * s)];
        [stroke moveToPoint:CGPointMake(0 * s, -0.3 * s)];
        [stroke addLineToPoint:CGPointMake(0 * s, -0.98 * s)];
        [stroke moveToPoint:CGPointMake(0.2121 * s, -0.2121 * s)];
        [stroke addLineToPoint:CGPointMake(0.693 * s, -0.693 * s)];
        [fill appendPath:[UIBezierPath bezierPathWithOvalInRect:CGRectMake(-0.16 * s, -0.16 * s, 0.32 * s, 0.32 * s)]];
        break;
    }
    case I_MEMBERS: {
        [stroke appendPath:[UIBezierPath bezierPathWithRoundedRect:CGRectMake(-0.9 * s, -0.46 * s, 0.46 * s, 0.92 * s) cornerRadius:0.1 * s]];
        [stroke appendPath:[UIBezierPath bezierPathWithRoundedRect:CGRectMake(0.44 * s, -0.46 * s, 0.46 * s, 0.92 * s) cornerRadius:0.1 * s]];
        [fill appendPath:[UIBezierPath bezierPathWithRoundedRect:CGRectMake(-0.22 * s, -0.58 * s, 0.44 * s, 1.16 * s) cornerRadius:0.1 * s]];
        break;
    }
    case I_PAUSE: {
        [fill appendPath:[UIBezierPath bezierPathWithRoundedRect:CGRectMake(-0.46 * s, -0.58 * s, 0.3 * s, 1.16 * s) cornerRadius:0.08 * s]];
        [fill appendPath:[UIBezierPath bezierPathWithRoundedRect:CGRectMake(0.16 * s, -0.58 * s, 0.3 * s, 1.16 * s) cornerRadius:0.08 * s]];
        break;
    }
    case I_MOVE: {
        [stroke moveToPoint:CGPointMake(0 * s, -0.92 * s)];
        [stroke addLineToPoint:CGPointMake(0 * s, 0.92 * s)];
        [stroke moveToPoint:CGPointMake(-0.92 * s, 0 * s)];
        [stroke addLineToPoint:CGPointMake(0.92 * s, 0 * s)];
        [stroke moveToPoint:CGPointMake(0 * s, -0.95 * s)];
        [stroke addLineToPoint:CGPointMake(0.3 * s, -0.65 * s)];
        [stroke moveToPoint:CGPointMake(0 * s, -0.95 * s)];
        [stroke addLineToPoint:CGPointMake(-0.3 * s, -0.65 * s)];
        [stroke moveToPoint:CGPointMake(0 * s, 0.95 * s)];
        [stroke addLineToPoint:CGPointMake(-0.3 * s, 0.65 * s)];
        [stroke moveToPoint:CGPointMake(0 * s, 0.95 * s)];
        [stroke addLineToPoint:CGPointMake(0.3 * s, 0.65 * s)];
        [stroke moveToPoint:CGPointMake(-0.95 * s, 0 * s)];
        [stroke addLineToPoint:CGPointMake(-0.65 * s, -0.3 * s)];
        [stroke moveToPoint:CGPointMake(-0.95 * s, 0 * s)];
        [stroke addLineToPoint:CGPointMake(-0.65 * s, 0.3 * s)];
        [stroke moveToPoint:CGPointMake(0.95 * s, 0 * s)];
        [stroke addLineToPoint:CGPointMake(0.65 * s, 0.3 * s)];
        [stroke moveToPoint:CGPointMake(0.95 * s, 0 * s)];
        [stroke addLineToPoint:CGPointMake(0.65 * s, -0.3 * s)];
        break;
    }
    case I_PLAY: {
        [stroke moveToPoint:CGPointMake(-0.38 * s, -0.62 * s)];
        [stroke addLineToPoint:CGPointMake(0.66 * s, 0 * s)];
        [stroke addLineToPoint:CGPointMake(-0.38 * s, 0.62 * s)];
        [stroke closePath];
        break;
    }
    case I_MENU: {
        [stroke moveToPoint:CGPointMake(-0.66 * s, -0.42 * s)];
        [stroke addLineToPoint:CGPointMake(0.66 * s, -0.42 * s)];
        [stroke moveToPoint:CGPointMake(-0.66 * s, 0 * s)];
        [stroke addLineToPoint:CGPointMake(0.66 * s, 0 * s)];
        [stroke moveToPoint:CGPointMake(-0.66 * s, 0.42 * s)];
        [stroke addLineToPoint:CGPointMake(0.66 * s, 0.42 * s)];
        break;
    }
    default: break;
    }
}


static NSArray *grad_colors(BOOL pressed) {
    if (pressed) {
        return @[(__bridge id)rgb(0.62, 0.52, 0.85, 1).CGColor, (__bridge id)rgb(0.26, 0.20, 0.45, 1).CGColor];
    }
    return @[(__bridge id)rgb(0.31, 0.28, 0.46, 1).CGColor, (__bridge id)rgb(0.07, 0.07, 0.16, 1).CGColor];
}

@interface BT3Pad : UIView
- (void)releaseAll;
- (void)autoLayout:(int)inMenus;
@end

@implementation BT3Pad {
    /* settings (saved) */
    float _scale, _alpha;
    BOOL _floating, _hidden, _auto;
    CGPoint _custom[E_COUNT]; /* where the user put a piece: as fractions of the view (valid when _isCustom) */
    BOOL _isCustom[E_COUNT];
    /* the moment */
    BOOL _editing, _nav, _menuUp;
    int _lastMenus;
    /* geometry */
    CGFloat _u;
    CGPoint _ctr[E_COUNT];
    CGFloat _sz[E_COUNT];
    CGPoint _kc[N_KEYS];
    CGFloat _kr[N_KEYS];
    CGRect _gear, _eye, _done;
    /* layers: a button is a radial fill, a ring, two glyph layers (stroked, solid), a badge and its letters */
    CAGradientLayer *_grad[N_KEYS];
    CAShapeLayer *_gmask[N_KEYS];
    CAShapeLayer *_ring[N_KEYS];
    CAShapeLayer *_ifill[N_KEYS];
    CAShapeLayer *_istroke[N_KEYS];
    CAShapeLayer *_badge[N_KEYS];
    CATextLayer *_btxt[N_KEYS];
    CATextLayer *_letter[N_KEYS];
    CALayer *_sbox;
    CAGradientLayer *_sgrad;
    CAShapeLayer *_sgmask;
    CAShapeLayer *_sring;
    CAShapeLayer *_sarrows;
    CALayer *_thumb;
    CAGradientLayer *_tgrad;
    CAShapeLayer *_tmask;
    CAShapeLayer *_tring;
    CAShapeLayer *_tstars;
    CAShapeLayer *_gearShape;
    CAShapeLayer *_eyeShape;
    CAShapeLayer *_doneShape;
    CAShapeLayer *_veil;
    CATextLayer *_gearText;
    CATextLayer *_eyeText;
    CATextLayer *_doneText;
    CATextLayer *_banner;
    /* touches */
    BOOL _down[N_KEYS];
    BOOL _shown[N_KEYS]; /* what the layers show of _down */
    __weak UITouch *_stickTouch;
    __weak UITouch *_dragTouch;
    __weak UITouch *_gearTouch;
    __weak UITouch *_eyeTouch;
    __weak UITouch *_doneTouch;
    CGPoint _stickCtr, _stickPos, _dragOff, _dragStart;
    int _dragElem;
    BOOL _dragMoved;
    CFTimeInterval _pulseUntil, _menuUntil;
    unsigned _bits;
    int _lx, _ly;
}

/* ---------------------------------------------------------------------------------------------- settings */
- (void)loadSettings {
    NSUserDefaults *d = NSUserDefaults.standardUserDefaults;
    NSArray *pos = [d arrayForKey:@"bt3pad.pos"];
    int e;

    _scale = [d objectForKey:@"bt3pad.scale"] ? [d floatForKey:@"bt3pad.scale"] : 1.0f;
    _alpha = [d objectForKey:@"bt3pad.alpha"] ? [d floatForKey:@"bt3pad.alpha"] : 0.85f;
    _floating = [d boolForKey:@"bt3pad.floating"];
    _hidden = [d boolForKey:@"bt3pad.hidden"];
    _auto = [d objectForKey:@"bt3pad.auto"] ? [d boolForKey:@"bt3pad.auto"] : YES;
    for (e = 0; e < E_COUNT; e++) {
        _isCustom[e] = NO;
        if (pos.count == (NSUInteger)E_COUNT * 2) {
            float x = [pos[e * 2] floatValue], y = [pos[e * 2 + 1] floatValue];
            if (x >= 0 && y >= 0) {
                _custom[e] = CGPointMake(x, y);
                _isCustom[e] = YES;
            }
        }
    }
}

- (void)saveSettings {
    NSUserDefaults *d = NSUserDefaults.standardUserDefaults;
    NSMutableArray *pos = [NSMutableArray array];
    int e;

    for (e = 0; e < E_COUNT; e++) {
        [pos addObject:@(_isCustom[e] ? _custom[e].x : -1.0f)];
        [pos addObject:@(_isCustom[e] ? _custom[e].y : -1.0f)];
    }
    [d setObject:pos forKey:@"bt3pad.pos"];
    [d setFloat:_scale forKey:@"bt3pad.scale"];
    [d setFloat:_alpha forKey:@"bt3pad.alpha"];
    [d setBool:_floating forKey:@"bt3pad.floating"];
    [d setBool:_hidden forKey:@"bt3pad.hidden"];
    [d setBool:_auto forKey:@"bt3pad.auto"];
}

/* ---------------------------------------------------------------------------------------------- layers */
- (CAShapeLayer *)newShape {
    CAShapeLayer *s = [CAShapeLayer layer];
    s.fillColor = nil;
    s.strokeColor = nil;
    s.lineCap = kCALineCapRound;
    s.lineJoin = kCALineJoinRound;
    [self.layer addSublayer:s];
    return s;
}

- (CATextLayer *)newText {
    CATextLayer *t = [CATextLayer layer];
    t.alignmentMode = kCAAlignmentCenter;
    t.foregroundColor = rgb(1, 1, 1, 0.92).CGColor;
    t.contentsScale = UIScreen.mainScreen.scale;
    [self.layer addSublayer:t];
    return t;
}

- (CAGradientLayer *)newGradIn:(CALayer *)parent colors:(NSArray *)colors {
    CAGradientLayer *g = [CAGradientLayer layer];
    g.type = kCAGradientLayerRadial;
    g.startPoint = CGPointMake(0.5, 0.5);
    g.endPoint = CGPointMake(1.0, 1.0);
    g.colors = colors;
    [parent addSublayer:g];
    return g;
}

- (instancetype)initWithFrame:(CGRect)frame {
    int i;

    self = [super initWithFrame:frame];
    if (self == nil) {
        return nil;
    }
    self.multipleTouchEnabled = YES;
    self.backgroundColor = UIColor.clearColor;
    self.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    [self loadSettings];
    _lx = _ly = 0x80;
    _lastMenus = -1;
    _veil = [self newShape];
    _veil.fillColor = rgb(0, 0, 0, 0.55).CGColor;
    _veil.hidden = YES;
    for (i = 0; i < N_KEYS; i++) {
        _grad[i] = [self newGradIn:self.layer colors:grad_colors(NO)];
        _gmask[i] = [CAShapeLayer layer];
        _gmask[i].fillColor = UIColor.blackColor.CGColor;
        _grad[i].mask = _gmask[i];
        _ring[i] = [self newShape];
        _ring[i].lineWidth = 2.0;
        _ifill[i] = [self newShape];
        _istroke[i] = [self newShape];
        _badge[i] = [self newShape];
        _badge[i].fillColor = rgb(0.055, 0.047, 0.09, 1).CGColor;
        _btxt[i] = [self newText];
        _letter[i] = [self newText];
        _btxt[i].foregroundColor = badge_color(kKeys[i].color).CGColor;
        _btxt[i].string = kKeys[i].badge != NULL ? [NSString stringWithUTF8String:kKeys[i].badge] : @"";
        _letter[i].foregroundColor = badge_color(kKeys[i].color).CGColor;
        _letter[i].string = kKeys[i].letter != NULL ? [NSString stringWithUTF8String:kKeys[i].letter] : @"";
        [self applyPressed:i];
    }
    /* the stick: a base (a button's look, with four arrows) and an orb with three stars */
    _sbox = [CALayer layer];
    [self.layer addSublayer:_sbox];
    _sgrad = [self newGradIn:_sbox colors:grad_colors(NO)];
    _sgmask = [CAShapeLayer layer];
    _sgmask.fillColor = UIColor.blackColor.CGColor;
    _sgrad.mask = _sgmask;
    _sring = [CAShapeLayer layer];
    _sring.fillColor = nil;
    _sring.strokeColor = rgb(0.80, 0.76, 0.93, 0.95).CGColor;
    _sring.lineWidth = 2.0;
    [_sbox addSublayer:_sring];
    _sarrows = [CAShapeLayer layer];
    _sarrows.fillColor = rgb(0.96, 0.77, 0.40, 1).CGColor;
    [_sbox addSublayer:_sarrows];
    _thumb = [CALayer layer];
    [_sbox addSublayer:_thumb];
    _tgrad = [self newGradIn:_thumb colors:@[(__bridge id)rgb(1.0, 0.82, 0.42, 1).CGColor, (__bridge id)rgb(0.85, 0.44, 0.10, 1).CGColor]];
    _tmask = [CAShapeLayer layer];
    _tmask.fillColor = UIColor.blackColor.CGColor;
    _tgrad.mask = _tmask;
    _tring = [CAShapeLayer layer];
    _tring.fillColor = nil;
    _tring.strokeColor = rgb(1.0, 0.85, 0.5, 0.9).CGColor;
    _tring.lineWidth = 1.5;
    [_thumb addSublayer:_tring];
    _tstars = [CAShapeLayer layer];
    _tstars.fillColor = rgb(0.75, 0.17, 0.15, 1).CGColor;
    [_thumb addSublayer:_tstars];
    /* gear, eye, and what the editing shows */
    _gearShape = [self newShape];
    _gearText = [self newText];
    _gearText.string = @"⚙";
    _eyeShape = [self newShape];
    _eyeText = [self newText];
    _eyeText.string = @"◉";
    _doneShape = [self newShape];
    _doneText = [self newText];
    _doneText.string = @"PRONTO";
    _banner = [self newText];
    _banner.string = @"Arraste os controles para onde quiser. Toque em PRONTO para terminar.";
    _banner.wrapped = YES;
    return self;
}

/* ---------------------------------------------------------------------------------------------- what is shown */
- (BOOL)keyShown:(int)k {
    return (kKeys[k].modes & (_nav ? M_NAV : M_FIGHT)) != 0;
}

- (CGFloat)stickRadius {
    return _sz[E_STICK] / 2;
}

- (void)applyPressed:(int)k {
    BOOL p = _down[k];
    UIColor *gold = p ? rgb(1.0, 0.93, 0.72, 1) : rgb(0.96, 0.77, 0.40, 1);

    _grad[k].colors = grad_colors(p);
    _ring[k].strokeColor = (p ? rgb(1, 1, 1, 1) : rgb(0.80, 0.76, 0.93, 0.95)).CGColor;
    _ifill[k].fillColor = gold.CGColor;
    _istroke[k].strokeColor = gold.CGColor;
}

/* ---------------------------------------------------------------------------------------------- geometry */
- (void)computeGeometry {
    CGFloat w = self.bounds.size.width, h = self.bounds.size.height;
    UIEdgeInsets si = self.safeAreaInsets;
    int e, k;

    _u = MIN(w, h) * 0.165 * _scale;
    for (e = 0; e < E_COUNT; e++) {
        CGFloat sz = kElem[e].size * _scale;
        CGPoint c;
        _sz[e] = sz;
        if (_isCustom[e]) {
            c = CGPointMake(_custom[e].x * w, _custom[e].y * h);
        } else {   /* the default places stay inside the safe area */
            CGFloat r = e == E_DPAD ? sz * 1.65 : sz / 2;
            c = CGPointMake(kElem[e].fx * w, kElem[e].fy * h);
            c.x = MAX(MAX(si.left, 0) + r, MIN(w - MAX(si.right, 0) - r, c.x));
            c.y = MAX(r + 4, MIN(h - MAX(si.bottom, 0) - r, c.y));
        }
        _ctr[e] = c;
    }
    for (k = 0; k < N_KEYS; k++) {
        const Key *K = &kKeys[k];
        CGFloat sz = _sz[K->elem];
        _kc[k] = CGPointMake(_ctr[K->elem].x + K->ox * sz * 1.1, _ctr[K->elem].y + K->oy * sz * 1.1);
        _kr[k] = sz / 2;
    }
    {
        CGFloat gs = MAX(MIN(w, h) * 0.1, 32);
        _gear = CGRectMake(w / 2 - gs - 4, 6, gs, gs);
        _eye = CGRectMake(w / 2 + 4, 6, gs, gs);
        _done = CGRectMake(w / 2 - 1.3 * _u, h / 2 - 0.45 * _u, 2.6 * _u, 0.9 * _u);
    }
}

- (void)layoutKey:(int)k show:(BOOL)show {
    const Key *K = &kKeys[k];
    BOOL vis = show && [self keyShown:k];
    BOOL faceMenu = _nav && K->letter != NULL;
    CGFloat r = _kr[k], d = 2 * r;
    CGPoint c = _kc[k];
    CGRect box = CGRectMake(c.x - r, c.y - r, d, d), local = CGRectMake(0, 0, d, d);

    _grad[k].hidden = _ring[k].hidden = !vis;
    _ifill[k].hidden = _istroke[k].hidden = !vis || faceMenu || K->icon == I_NONE;
    _badge[k].hidden = _btxt[k].hidden = !vis || faceMenu || K->badge == NULL;
    _letter[k].hidden = !vis || !faceMenu;
    if (!vis) {
        return;
    }
    _grad[k].opacity = _ring[k].opacity = _ifill[k].opacity = _istroke[k].opacity = _badge[k].opacity = _btxt[k].opacity = _letter[k].opacity = _alpha;
    _grad[k].frame = box;
    _gmask[k].frame = local;
    _gmask[k].path = [UIBezierPath bezierPathWithOvalInRect:local].CGPath;
    _ring[k].path = [UIBezierPath bezierPathWithOvalInRect:CGRectInset(box, 1, 1)].CGPath;
    if (!_ifill[k].hidden) {
        UIBezierPath *sp = [UIBezierPath bezierPath], *fp = [UIBezierPath bezierPath];
        CGAffineTransform t = CGAffineTransformMakeTranslation(c.x, c.y);

        icon_paths(K->icon, d * 0.27, sp, fp);
        [sp applyTransform:t];
        [fp applyTransform:t];
        _istroke[k].path = sp.CGPath;
        _istroke[k].lineWidth = MAX(1.6, d * 0.052);
        _ifill[k].path = fp.CGPath;
    }
    if (!_badge[k].hidden) {
        CGFloat br = d * 0.16, bx = c.x + d * 0.345, by = c.y - d * 0.345;
        CGFloat fs = d * 0.16 * (strlen(K->badge) > 1 ? 0.78 : 1.0);

        _badge[k].path = [UIBezierPath bezierPathWithOvalInRect:CGRectMake(bx - br, by - br, 2 * br, 2 * br)].CGPath;
        _btxt[k].fontSize = fs;
        _btxt[k].frame = CGRectMake(bx - br * 1.3, by - fs * 0.62, br * 2.6, fs * 1.3);
    }
    if (!_letter[k].hidden) {
        CGFloat fs = d * 0.55;

        _letter[k].fontSize = fs;
        _letter[k].frame = CGRectMake(box.origin.x, c.y - fs * 0.62, d, fs * 1.3);
    }
    [self applyPressed:k];
    _shown[k] = _down[k];
}

/* the stick's ring, its arrows and its thumb */
- (void)layoutStick:(BOOL)show {
    CGFloat Rs = [self stickRadius], Rt = Rs * 0.42, T = Rs * 0.62;
    BOOL active = _stickTouch != nil, vis = show && !_nav;
    CGPoint c = active ? _stickCtr : _ctr[E_STICK];
    CGFloat dx = active ? _stickPos.x - c.x : 0, dy = active ? _stickPos.y - c.y : 0, mag = hypot(dx, dy);

    _sbox.hidden = !vis;
    if (!vis) {
        return;
    }
    if (mag > T) {
        dx *= T / mag;
        dy *= T / mag;
    }
    _sbox.opacity = _floating && !active && !_editing ? _alpha * 0.5 : _alpha;
    _sbox.bounds = CGRectMake(0, 0, 2 * Rs, 2 * Rs);
    _sbox.position = c;
    _sgrad.frame = _sbox.bounds;
    _sgmask.frame = _sbox.bounds;
    _sgmask.path = [UIBezierPath bezierPathWithOvalInRect:_sbox.bounds].CGPath;
    _sring.path = [UIBezierPath bezierPathWithOvalInRect:CGRectInset(_sbox.bounds, 1, 1)].CGPath;
    {   /* four small arrows at the edge, pointing out */
        UIBezierPath *p = [UIBezierPath bezierPath];
        CGFloat q = Rs * 0.09, e = Rs * 0.84;
        int i;
        for (i = 0; i < 4; i++) {
            CGFloat ux = (i == 0) - (i == 1) + 0.0, uy = (i == 2) - (i == 3) + 0.0;  /* right, left, down, up */
            CGPoint m = CGPointMake(Rs + ux * e, Rs + uy * e);
            [p moveToPoint:CGPointMake(m.x + ux * q, m.y + uy * q)];
            [p addLineToPoint:CGPointMake(m.x - ux * q - uy * q, m.y - uy * q + ux * q)];
            [p addLineToPoint:CGPointMake(m.x - ux * q + uy * q, m.y - uy * q - ux * q)];
            [p closePath];
        }
        _sarrows.path = p.CGPath;
    }
    _thumb.bounds = CGRectMake(0, 0, 2 * Rt, 2 * Rt);
    _thumb.position = CGPointMake(Rs + dx, Rs + dy);
    _tgrad.frame = _thumb.bounds;
    _tmask.frame = _thumb.bounds;
    _tmask.path = [UIBezierPath bezierPathWithOvalInRect:_thumb.bounds].CGPath;
    _tring.path = [UIBezierPath bezierPathWithOvalInRect:CGRectInset(_thumb.bounds, 1, 1)].CGPath;
    {
        UIBezierPath *p = [UIBezierPath bezierPath];
        CGPoint o = CGPointMake(Rt, Rt);
        [p appendPath:star_path(CGPointMake(o.x, o.y - Rt * 0.3), Rt * 0.27, Rt * 0.11)];
        [p appendPath:star_path(CGPointMake(o.x - Rt * 0.3, o.y + Rt * 0.22), Rt * 0.27, Rt * 0.11)];
        [p appendPath:star_path(CGPointMake(o.x + Rt * 0.3, o.y + Rt * 0.22), Rt * 0.27, Rt * 0.11)];
        _tstars.path = p.CGPath;
    }
}

- (void)refresh {
    CGFloat w = self.bounds.size.width, h = self.bounds.size.height;
    BOOL show = !_hidden || _editing;
    int k;

    [self computeGeometry];
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    _veil.hidden = !_editing;
    _veil.path = [UIBezierPath bezierPathWithRect:self.bounds].CGPath;
    for (k = 0; k < N_KEYS; k++) {
        [self layoutKey:k show:show];
    }
    [self layoutStick:show];
    /* gear and eye: always there, a little fainter */
    _gearShape.path = [UIBezierPath bezierPathWithOvalInRect:_gear].CGPath;
    _eyeShape.path = [UIBezierPath bezierPathWithOvalInRect:_eye].CGPath;
    _gearShape.strokeColor = _eyeShape.strokeColor = rgb(0.80, 0.76, 0.93, 0.9).CGColor;
    _gearShape.lineWidth = _eyeShape.lineWidth = 1.5;
    _gearShape.fillColor = rgb(0.10, 0.09, 0.20, 0.8).CGColor;
    _eyeShape.fillColor = (_hidden ? rgb(0.55, 0.45, 0.8, 0.9) : rgb(0.10, 0.09, 0.20, 0.8)).CGColor;
    _gearShape.opacity = _eyeShape.opacity = 0.75;
    _gearText.foregroundColor = _eyeText.foregroundColor = rgb(0.96, 0.77, 0.40, 1).CGColor;
    _gearText.fontSize = _eyeText.fontSize = _gear.size.width * 0.55;
    _gearText.frame = CGRectMake(_gear.origin.x, _gear.origin.y + _gear.size.height * 0.16, _gear.size.width, _gear.size.height);
    _eyeText.frame = CGRectMake(_eye.origin.x, _eye.origin.y + _eye.size.height * 0.16, _eye.size.width, _eye.size.height);
    /* editing */
    _doneShape.hidden = _doneText.hidden = _banner.hidden = !_editing;
    _doneShape.path = [UIBezierPath bezierPathWithRoundedRect:_done cornerRadius:12].CGPath;
    _doneShape.fillColor = rgb(0.9, 0.5, 0.1, 0.92).CGColor;
    _doneText.fontSize = _u * 0.34;
    _doneText.frame = CGRectMake(_done.origin.x, _done.origin.y + (_done.size.height - _u * 0.45) / 2, _done.size.width, _u * 0.5);
    _banner.fontSize = _u * 0.3;
    _banner.frame = CGRectMake(w * 0.2, h / 2 - 1.4 * _u, w * 0.6, _u * 0.9);
    [CATransaction commit];
}

/* What a touch changes, and nothing else: the pressed buttons' look and the stick's thumb. (A full refresh makes every
   path again: on the game's own thread, every touch event, that is time taken from the frame.) */
- (void)refreshState {
    int k;

    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    for (k = 0; k < N_KEYS; k++) {
        if (_down[k] != _shown[k]) {
            _shown[k] = _down[k];
            [self applyPressed:k];
        }
    }
    if (!_nav && (!_sbox.hidden)) {
        BOOL active = _stickTouch != nil;
        CGFloat Rs = [self stickRadius], T = Rs * 0.62;
        CGPoint c = active ? _stickCtr : _ctr[E_STICK];
        CGFloat dx = active ? _stickPos.x - c.x : 0, dy = active ? _stickPos.y - c.y : 0, mag = hypot(dx, dy);

        if (mag > T) {
            dx *= T / mag;
            dy *= T / mag;
        }
        _sbox.position = c;
        _sbox.opacity = _floating && !active && !_editing ? _alpha * 0.5 : _alpha;
        _thumb.position = CGPointMake(Rs + dx, Rs + dy);
    }
    [CATransaction commit];
}

- (void)layoutSubviews {
    [super layoutSubviews];
    [self refresh];
}

/* ---------------------------------------------------------------------------------------------- what is where */
- (BOOL)startsStick:(CGPoint)p {
    CGFloat w = self.bounds.size.width, h = self.bounds.size.height;

    if (_nav) {
        return NO;
    }
    if (_floating) {
        return p.x < w * 0.45 && p.y > h * 0.3;
    }
    return hypot(p.x - _ctr[E_STICK].x, p.y - _ctr[E_STICK].y) < [self stickRadius] * 1.4;
}

- (int)keyAt:(CGPoint)p {
    int k;
    for (k = 0; k < N_KEYS; k++) {
        if ([self keyShown:k] && hypot(p.x - _kc[k].x, p.y - _kc[k].y) <= _kr[k] * 1.15) {
            return k;
        }
    }
    return -1;
}

/* the piece under a point, for moving it */
- (int)elementAt:(CGPoint)p {
    int k = [self keyAt:p];

    if (k >= 0) {
        return kKeys[k].elem;
    }
    if (!_nav && hypot(p.x - _ctr[E_STICK].x, p.y - _ctr[E_STICK].y) < [self stickRadius] * 1.2) {
        return E_STICK;
    }
    return -1;
}

/* only a touch on a control is ours; the rest goes on to the game's window */
- (UIView *)hitTest:(CGPoint)p withEvent:(UIEvent *)e {
    if (_editing || CGRectContainsPoint(_gear, p) || CGRectContainsPoint(_eye, p)) {
        return self;
    }
    if (!_hidden && ([self keyAt:p] >= 0 || [self startsStick:p])) {
        return self;
    }
    return nil;
}

/* ---------------------------------------------------------------------------------------------- output */
- (void)publish {
    CFTimeInterval t = CACurrentMediaTime();
    unsigned bits = _bits;

    if (t < _menuUntil) {
        bits |= B_START | B_SELECT; /* the engine's settings window opens with both */
    }
    Port_TouchPad(bits, _lx, _ly, 0x80, t < _pulseUntil ? 0xFF : 0x80);
}

- (void)publishLater:(double)seconds {
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(seconds * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{ [self publish]; });
}

- (void)releaseAll {
    int k;
    _stickTouch = nil;
    _dragTouch = nil;
    for (k = 0; k < N_KEYS; k++) {
        _down[k] = NO;
    }
    _bits = 0;
    _lx = _ly = 0x80;
    [self publish];
    [self refresh];
}

/* the layout of the game's own moment: the menus' (the D-pad) or the fight's (the stick), unless the player chose by hand */
- (void)autoLayout:(int)inMenus {
    if (!_auto || _editing || inMenus == _lastMenus) {
        return;
    }
    _lastMenus = inMenus;
    if ((inMenus != 0) != _nav) {
        _nav = inMenus != 0;
        [self releaseAll];
    }
}

- (void)update:(UIEvent *)event {
    BOOL now[N_KEYS] = {0};
    BOOL layoutChanged = NO;
    unsigned bits = 0;
    int k, lx = 0x80, ly = 0x80;

    if (!_hidden && !_editing) {
        for (UITouch *t in event.allTouches) {
            if (t.phase == UITouchPhaseEnded || t.phase == UITouchPhaseCancelled || t == _stickTouch || t == _gearTouch || t == _eyeTouch) {
                continue;
            }
            CGPoint p = [t locationInView:self];
            for (k = 0; k < N_KEYS; k++) {
                if ([self keyShown:k] && hypot(p.x - _kc[k].x, p.y - _kc[k].y) <= _kr[k] * 1.15) {
                    now[k] = YES;
                }
            }
        }
        if (_stickTouch != nil) {
            CGFloat T = [self stickRadius] * 0.62, dx = _stickPos.x - _stickCtr.x, dy = _stickPos.y - _stickCtr.y, mag = hypot(dx, dy);
            if (mag > T) {
                dx *= T / mag;
                dy *= T / mag;
            }
            lx = (int)lroundf((float)(128 + dx / T * 127));
            ly = (int)lroundf((float)(128 + dy / T * 127));
        }
    }
    for (k = 0; k < N_KEYS; k++) {
        if (now[k] && !_down[k]) { /* just pressed */
            if (kKeys[k].kind == K_TEAM) {
                _pulseUntil = CACurrentMediaTime() + 0.12;
                [self publishLater:0.14];
            } else if (kKeys[k].kind == K_MENU) {
                _menuUntil = CACurrentMediaTime() + 0.16;
                [self publishLater:0.18];
            } else if (kKeys[k].kind == K_NAV) {
                _nav = !_nav;
                _stickTouch = nil;
                layoutChanged = YES;
            }
        }
        if (now[k] && kKeys[k].kind == K_BUTTON) {
            bits |= kKeys[k].bit;
        }
        _down[k] = now[k];
    }
    _bits = bits;
    _lx = MAX(0, MIN(255, lx));
    _ly = MAX(0, MIN(255, ly));
    [self publish];
    if (layoutChanged) {
        [self refresh];
    } else {
        [self refreshState];
    }
}


/* ---------------------------------------------------------------------------------------------- touches */
/* How long a press waited before this view saw it (the touch's own time against now): it waits for the game's thread to
   take the events. Logged every ten seconds, "touch: N presses, ...", to see where a delay is. */
static void note_delivery(UITouch *t) {
    static double sum, max, since;
    static int n;
    double now = CACurrentMediaTime(), lag = (now - t.timestamp) * 1000.0;

    sum += lag;
    n++;
    if (lag > max) {
        max = lag;
    }
    if (since == 0) {
        since = now;
    }
    if (now - since > 10.0) {
        fprintf(stderr, "bt3: touch: %d presses in %.0f s, seen %.1f ms after the touch on average (worst %.1f ms)\n", n, now - since, sum / n, max);
        sum = max = 0;
        n = 0;
        since = now;
    }
}

- (void)touchesBegan:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    for (UITouch *t in touches) {
        CGPoint p = [t locationInView:self];
        note_delivery(t);
        if (CGRectContainsPoint(_gear, p)) {
            _gearTouch = t;
        } else if (CGRectContainsPoint(_eye, p)) {
            _eyeTouch = t;
        } else if (_editing) {
            if (CGRectContainsPoint(_done, p)) {
                _doneTouch = t;
            } else if (_dragTouch == nil) {
                int e = [self elementAt:p];
                if (e >= 0) {
                    _dragTouch = t;
                    _dragElem = e;
                    _dragOff = CGPointMake(p.x - _ctr[e].x, p.y - _ctr[e].y);
                    _dragStart = p;
                    _dragMoved = NO;
                }
            }
        } else if (!_hidden && _stickTouch == nil && [self keyAt:p] < 0 && [self startsStick:p]) {
            _stickTouch = t;
            _stickCtr = _floating ? p : _ctr[E_STICK];
            _stickPos = p;
        }
    }
    [self update:event];
}

- (void)touchesMoved:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    for (UITouch *t in touches) {
        CGPoint p = [t locationInView:self];
        if (t == _stickTouch) {
            _stickPos = p;
        } else if (t == _dragTouch) {
            CGFloat w = self.bounds.size.width, h = self.bounds.size.height;
            CGFloat x = MAX(0, MIN(w, p.x - _dragOff.x)), y = MAX(0, MIN(h, p.y - _dragOff.y));
            _custom[_dragElem] = CGPointMake(x / w, y / h);
            _isCustom[_dragElem] = YES;
            if (hypot(p.x - _dragStart.x, p.y - _dragStart.y) > 8) {
                _dragMoved = YES;
            }
        }
    }
    [self update:event];
    if (_dragTouch != nil) {
        [self refresh]; /* a piece being moved: its places change */
    }
}

- (void)finish:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event cancelled:(BOOL)cancelled {
    for (UITouch *t in touches) {
        CGPoint p = [t locationInView:self];
        if (t == _stickTouch) {
            _stickTouch = nil;
        } else if (t == _dragTouch) {
            BOOL tap = !_dragMoved && !cancelled && _dragElem == E_NAV; /* a tap on the layout button, while editing, changes the layout to edit the other one */
            if (tap) {
                _isCustom[E_NAV] = NO;
                _nav = !_nav;
            }
            _dragTouch = nil;
            [self saveSettings];
            [self refresh];
        } else if (t == _gearTouch) {
            _gearTouch = nil;
            if (!cancelled && CGRectContainsPoint(_gear, p)) {
                [self showMenu];
            }
        } else if (t == _eyeTouch) {
            _eyeTouch = nil;
            if (!cancelled && CGRectContainsPoint(_eye, p)) {
                _hidden = !_hidden;
                [self saveSettings];
                [self releaseAll];
            }
        } else if (t == _doneTouch) {
            _doneTouch = nil;
            if (!cancelled && CGRectContainsPoint(_done, p)) {
                _editing = NO;
                [self saveSettings];
                [self releaseAll];
            }
        }
    }
    [self update:event];
}

- (void)touchesEnded:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event { [self finish:touches withEvent:event cancelled:NO]; }
- (void)touchesCancelled:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event { [self finish:touches withEvent:event cancelled:YES]; }

/* ---------------------------------------------------------------------------------------------- the gear's menu */
/* an action of the menu: it says the menu is gone, then does its work */
- (UIAlertAction *)act:(NSString *)title style:(UIAlertActionStyle)style do:(void (^)(void))work {
    return [UIAlertAction actionWithTitle:title style:style handler:^(UIAlertAction *a) {
        self->_menuUp = NO;
        if (work != nil) {
            work();
        }
    }];
}

- (void)showMenu {
    UIViewController *vc = self.window.rootViewController;
    UIAlertController *ac;
    UIAlertActionStyle normal = UIAlertActionStyleDefault;

    if (_menuUp || vc == nil || vc.presentedViewController != nil) { /* one menu at a time */
        return;
    }
    [self releaseAll];
    ac = [UIAlertController alertControllerWithTitle:@"Controles na tela"
                                             message:[NSString stringWithFormat:@"Tamanho %d%%   Opacidade %d%%", (int)lroundf(_scale * 100), (int)lroundf(_alpha * 100)]
                                      preferredStyle:UIAlertControllerStyleAlert];
    [ac addAction:[self act:@"Editar posições" style:normal do:^{
        self->_editing = YES;
        self->_hidden = NO;
        [self refresh];
    }]];
    [ac addAction:[self act:@"Botões maiores" style:normal do:^{
        self->_scale = MIN(1.8f, self->_scale + 0.1f);
        [self saveSettings];
        [self refresh];
    }]];
    [ac addAction:[self act:@"Botões menores" style:normal do:^{
        self->_scale = MAX(0.5f, self->_scale - 0.1f);
        [self saveSettings];
        [self refresh];
    }]];
    [ac addAction:[self act:@"Mais opacos" style:normal do:^{
        self->_alpha = MIN(1.0f, self->_alpha + 0.1f);
        [self saveSettings];
        [self refresh];
    }]];
    [ac addAction:[self act:@"Mais transparentes" style:normal do:^{
        self->_alpha = MAX(0.2f, self->_alpha - 0.1f);
        [self saveSettings];
        [self refresh];
    }]];
    [ac addAction:[self act:_floating ? @"Analógico fixo" : @"Analógico flutuante" style:normal do:^{
        self->_floating = !self->_floating;
        [self saveSettings];
        [self refresh];
    }]];
    [ac addAction:[self act:_auto ? @"Trocar o layout só à mão" : @"Trocar o layout sozinho (menu e luta)" style:normal do:^{
        self->_auto = !self->_auto;
        self->_lastMenus = -1; /* the next look at the game decides */
        [self saveSettings];
    }]];
    [ac addAction:[self act:@"Restaurar o padrão" style:UIAlertActionStyleDestructive do:^{
        int e;
        for (e = 0; e < E_COUNT; e++) {
            self->_isCustom[e] = NO;
        }
        self->_scale = 1.0f;
        self->_alpha = 0.85f;
        self->_floating = NO;
        self->_auto = YES;
        self->_lastMenus = -1;
        [self saveSettings];
        [self refresh];
    }]];
    [ac addAction:[self act:@"Fechar" style:UIAlertActionStyleCancel do:nil]];
    _menuUp = YES;
    [vc presentViewController:ac animated:YES completion:nil];
}
@end

static BT3Pad *sPad;

static UIWindow *game_window(void) {
    UIWindow *win = nil;
    for (UIScene *sc in UIApplication.sharedApplication.connectedScenes) {
        if ([sc isKindOfClass:UIWindowScene.class]) {
            for (UIWindow *w in ((UIWindowScene *)sc).windows) {
                if (w.isKeyWindow || win == nil) {
                    win = w;
                }
            }
        }
    }
    return win;
}

/* The game's picture reaches the screen through a CAMetalLayer that lets up to three pictures wait for the display's
   refresh (its default): up to two refreshes of delay between a press and what it does, on top of the game's own.
   Two is the layer's least. `ios_drawables` in bt3_settings.txt (2 or 3) puts it back if the game stutters. */
static void tune_metal(UIWindow *win) {
    NSMutableArray<CALayer *> *todo = [NSMutableArray arrayWithObject:win.layer];
    int want = Port_Setting("ios_drawables", 2);

    want = want < 2 ? 2 : want > 3 ? 3 : want;
    while (todo.count > 0) {
        CALayer *l = todo.lastObject;
        [todo removeLastObject];
        if ([l isKindOfClass:CAMetalLayer.class]) {
            CAMetalLayer *m = (CAMetalLayer *)l;
            if ((int)m.maximumDrawableCount != want) {
                fprintf(stderr, "bt3: Metal layer: %d pictures may wait for the display (was %d)\n", want, (int)m.maximumDrawableCount);
                m.maximumDrawableCount = (NSUInteger)want;
            }
        }
        if (l.sublayers != nil) {
            [todo addObjectsFromArray:l.sublayers];
        }
    }
}

/* Called by plat_ios.c when the game starts: the game's window comes a little later, so a timer puts the controller on
   it as soon as there is one, keeps it on top, and hides it while the engine's settings window is open. */
void Ios_TouchStart(void) {
    dispatch_async(dispatch_get_main_queue(), ^{
        [NSTimer scheduledTimerWithTimeInterval:0.4 repeats:YES block:^(NSTimer *timer) {
            UIWindow *win = game_window();
            BOOL overlay, modal;
            if (win == nil) {
                return;
            }
            if (sPad == nil) {
                sPad = [[BT3Pad alloc] initWithFrame:win.bounds];
                fprintf(stderr, "bt3: low input lag: %s\n", Port_LateInput() ? "on" : "off");
            }
            tune_metal(win);
            modal = win.rootViewController.presentedViewController != nil; /* the gear's menu, the Files picker... */
            if (sPad.superview != win) {
                [win addSubview:sPad];
                sPad.frame = win.bounds;
            } else if (!modal) {
                [win bringSubviewToFront:sPad]; /* (not above a menu: its buttons would go on being touched) */
            }
            overlay = gPortOverlayOpen != 0 || modal;
            if (overlay && !sPad.hidden) {
                [sPad releaseAll];
            }
            sPad.hidden = overlay;
            if (!overlay) {
                [sPad autoLayout:gPortInMenus];
            }
        }];
    });
}
