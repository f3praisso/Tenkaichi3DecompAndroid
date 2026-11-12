/* iOS: the on-screen controller. (The engine takes it through Port_TouchPad, port/src/gs/gs_input.c: player 1's buttons
   and sticks, added to the keyboard's and the controllers'. Android draws it in Kotlin: TouchPadView.)

   Objective-C in a .c file: the build compiles this one as Objective-C with ARC (undefined.py). A transparent view over
   the game's window: the left stick (fixed or floating) or the D-pad, the PlayStation buttons, L1/L2/R1/R2, SELECT and
   START, TROCA (L3: swaps the fighter) and PRÓX. (a short push of the right stick down: the next target).
   Top centre: a gear (menu: size, opacity, floating stick, edit the positions, reset) and an eye (hide/show).
   A touch outside every control goes on to the game's window. START and SELECT together open the engine's settings. */
#import <UIKit/UIKit.h>
#import <QuartzCore/QuartzCore.h>
#import <Metal/Metal.h>
#include <stdio.h>

extern void Port_TouchPad(unsigned buttons, int lx, int ly, int rx, int ry);
extern int gPortOverlayOpen; /* gs_input.c: the engine's settings window is open */
extern int Port_Setting(const char *name, int def);  /* plat_settings.c: bt3_settings.txt */
extern int Port_LateInput(void);                     /* plat_stub.c: "Low input lag" */

enum {
    B_SELECT = 0x0001, B_L3 = 0x0002, B_START = 0x0008, B_UP = 0x0010, B_RIGHT = 0x0020, B_DOWN = 0x0040, B_LEFT = 0x0080,
    B_L2 = 0x0100, B_R2 = 0x0200, B_L1 = 0x0400, B_R1 = 0x0800,
    B_TRIANGLE = 0x1000, B_CIRCLE = 0x2000, B_CROSS = 0x4000, B_SQUARE = 0x8000
};

/* the movable pieces; each key and the stick belong to one */
enum { E_DPAD, E_FACE, E_STICK, E_L1, E_L2, E_R1, E_R2, E_SELECT, E_START, E_TROCA, E_PROX, E_MODE, E_COUNT };
enum { K_BUTTON, K_PULSE, K_MODE };

typedef struct {
    unsigned bit;
    int kind, elem;
    float ox, oy, w, h; /* in units of u, from the piece's centre */
    const char *label;
} Key;

static const Key kKeys[] = {
    {B_UP, K_BUTTON, E_DPAD, 0, -1, 1, 1, "▲"},       {B_DOWN, K_BUTTON, E_DPAD, 0, 1, 1, 1, "▼"},
    {B_LEFT, K_BUTTON, E_DPAD, -1, 0, 1, 1, "◀"},     {B_RIGHT, K_BUTTON, E_DPAD, 1, 0, 1, 1, "▶"},
    {B_TRIANGLE, K_BUTTON, E_FACE, 0, -1, 1, 1, "△"}, {B_CROSS, K_BUTTON, E_FACE, 0, 1, 1, 1, "✕"},
    {B_SQUARE, K_BUTTON, E_FACE, -1, 0, 1, 1, "□"},   {B_CIRCLE, K_BUTTON, E_FACE, 1, 0, 1, 1, "○"},
    {B_L1, K_BUTTON, E_L1, 0, 0, 1.5f, 0.8f, "L1"},   {B_L2, K_BUTTON, E_L2, 0, 0, 1.5f, 0.8f, "L2"},
    {B_R1, K_BUTTON, E_R1, 0, 0, 1.5f, 0.8f, "R1"},   {B_R2, K_BUTTON, E_R2, 0, 0, 1.5f, 0.8f, "R2"},
    {B_SELECT, K_BUTTON, E_SELECT, 0, 0, 1.5f, 0.7f, "SELECT"}, {B_START, K_BUTTON, E_START, 0, 0, 1.5f, 0.7f, "START"},
    {B_L3, K_BUTTON, E_TROCA, 0, 0, 1.4f, 0.7f, "TROCA"},       {0, K_PULSE, E_PROX, 0, 0, 1.4f, 0.7f, "PRÓX."},
    {0, K_MODE, E_MODE, 0, 0, 1.6f, 0.6f, "MODO"},
};
#define N_KEYS ((int)(sizeof(kKeys) / sizeof(kKeys[0])))

@interface BT3Pad : UIView
- (void)releaseAll;
@end

@implementation BT3Pad {
    /* settings (saved) */
    float _scale, _alpha;
    BOOL _floating, _hidden, _dpadMode, _editing;
    CGPoint _custom[E_COUNT]; /* where the user put a piece: as fractions of the view (valid when _isCustom) */
    BOOL _isCustom[E_COUNT];
    /* geometry */
    CGFloat _u, _m;
    CGPoint _ctr[E_COUNT];
    CGRect _rect[N_KEYS], _gear, _eye, _done;
    /* layers */
    CAShapeLayer *_shape[N_KEYS];
    CATextLayer *_text[N_KEYS];
    CAShapeLayer *_stickBase, *_stickThumb, *_gearShape, *_eyeShape, *_doneShape, *_veil;
    CATextLayer *_gearText, *_eyeText, *_doneText, *_banner;
    /* state */
    BOOL _menuUp; /* the gear's menu is on the screen */
    BOOL _down[N_KEYS];
    BOOL _shown[N_KEYS]; /* what the layers show of _down */
    BOOL _stickShown;
    __weak UITouch *_stickTouch;
    __weak UITouch *_dragTouch;
    __weak UITouch *_gearTouch;
    __weak UITouch *_eyeTouch;
    __weak UITouch *_doneTouch;
    CGPoint _stickCtr, _stickPos, _dragOff;
    int _dragElem;
    CFTimeInterval _pulseUntil;
    unsigned _bits;
    int _lx, _ly;
}

/* ---------------------------------------------------------------------------------------------- settings */
- (void)loadSettings {
    NSUserDefaults *d = NSUserDefaults.standardUserDefaults;
    NSArray *pos = [d arrayForKey:@"bt3pad.pos"];
    int e;

    _scale = [d objectForKey:@"bt3pad.scale"] ? [d floatForKey:@"bt3pad.scale"] : 1.0f;
    _alpha = [d objectForKey:@"bt3pad.alpha"] ? [d floatForKey:@"bt3pad.alpha"] : 0.8f;
    _floating = [d boolForKey:@"bt3pad.floating"];
    _hidden = [d boolForKey:@"bt3pad.hidden"];
    _dpadMode = [d boolForKey:@"bt3pad.dpad"];
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
    [d setBool:_dpadMode forKey:@"bt3pad.dpad"];
}

/* ---------------------------------------------------------------------------------------------- layers */
- (CAShapeLayer *)newShape {
    CAShapeLayer *s = [CAShapeLayer layer];
    s.strokeColor = [UIColor colorWithWhite:1.0 alpha:0.7].CGColor;
    s.lineWidth = 2.0;
    s.fillColor = [UIColor colorWithWhite:1.0 alpha:0.08].CGColor;
    [self.layer addSublayer:s];
    return s;
}

- (CATextLayer *)newText {
    CATextLayer *t = [CATextLayer layer];
    t.alignmentMode = kCAAlignmentCenter;
    t.foregroundColor = [UIColor colorWithWhite:1.0 alpha:0.9].CGColor;
    t.contentsScale = UIScreen.mainScreen.scale;
    [self.layer addSublayer:t];
    return t;
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
    _veil = [self newShape];
    _veil.fillColor = [UIColor colorWithWhite:0.0 alpha:0.55].CGColor;
    _veil.strokeColor = nil;
    _veil.hidden = YES;
    _stickBase = [self newShape];
    _stickThumb = [self newShape];
    for (i = 0; i < N_KEYS; i++) {
        _shape[i] = [self newShape];
        _text[i] = [self newText];
        _text[i].string = [NSString stringWithUTF8String:kKeys[i].label];
    }
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

/* ---------------------------------------------------------------------------------------------- geometry */
- (CGPoint)defaultCenter:(int)e {
    CGFloat w = self.bounds.size.width, h = self.bounds.size.height;
    UIEdgeInsets si = self.safeAreaInsets;
    CGFloat L = MAX(si.left, 0) + _m, R = w - MAX(si.right, 0) - _m, T = _m, B = h - MAX(si.bottom, 0) - _m;
    CGFloat u = _u;

    switch (e) {
    case E_DPAD:
    case E_STICK: return CGPointMake(L + 1.8 * u, B - 1.8 * u);
    case E_FACE: return CGPointMake(R - 1.8 * u, B - 1.8 * u);
    case E_L1: return CGPointMake(L + 0.8 * u, T + 0.45 * u);
    case E_L2: return CGPointMake(L + 0.8 * u, T + 1.4 * u);
    case E_R1: return CGPointMake(R - 0.8 * u, T + 0.45 * u);
    case E_R2: return CGPointMake(R - 0.8 * u, T + 1.4 * u);
    case E_SELECT: return CGPointMake(w / 2 - 1.0 * u, B - 0.4 * u);
    case E_START: return CGPointMake(w / 2 + 1.0 * u, B - 0.4 * u);
    case E_TROCA: return CGPointMake(L + 0.8 * u, T + 2.35 * u);
    case E_PROX: return CGPointMake(R - 0.8 * u, T + 2.35 * u);
    case E_MODE: return CGPointMake(L + 1.8 * u, B - 4.0 * u);
    }
    return CGPointZero;
}

- (BOOL)keyVisible:(int)k {
    return kKeys[k].elem != E_DPAD || _dpadMode;
}

- (CGFloat)stickRadius {
    return 1.35 * _u;
}

- (void)computeGeometry {
    CGFloat w = self.bounds.size.width, h = self.bounds.size.height;
    int e, k;

    _u = MIN(w, h) * 0.165 * _scale;
    _m = MIN(w, h) * 0.05;
    for (e = 0; e < E_COUNT; e++) {
        _ctr[e] = _isCustom[e] ? CGPointMake(_custom[e].x * w, _custom[e].y * h) : [self defaultCenter:e];
    }
    for (k = 0; k < N_KEYS; k++) {
        const Key *K = &kKeys[k];
        _rect[k] = CGRectMake(_ctr[K->elem].x + (K->ox - K->w / 2) * _u, _ctr[K->elem].y + (K->oy - K->h / 2) * _u, K->w * _u, K->h * _u);
    }
    {
        CGFloat gs = MAX(MIN(w, h) * 0.11, 34);
        _gear = CGRectMake(w / 2 - gs - 4, 6, gs, gs);
        _eye = CGRectMake(w / 2 + 4, 6, gs, gs);
        _done = CGRectMake(w / 2 - 1.3 * _u, h / 2 - 0.45 * _u, 2.6 * _u, 0.9 * _u);
    }
}

- (BOOL)stickVisible {
    return !_dpadMode && (!_hidden || _editing);
}

- (void)refresh {
    CGFloat w = self.bounds.size.width, h = self.bounds.size.height;
    BOOL show = !_hidden || _editing;
    CGFloat Rs = [self stickRadius];
    int k;

    [self computeGeometry];
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    _veil.hidden = !_editing;
    _veil.path = [UIBezierPath bezierPathWithRect:self.bounds].CGPath;
    for (k = 0; k < N_KEYS; k++) {
        const Key *K = &kKeys[k];
        BOOL vis = show && [self keyVisible:k];
        BOOL isRound = K->elem == E_DPAD || K->elem == E_FACE;
        CGRect r = _rect[k];
        CGFloat fs = K->elem == E_DPAD || K->elem == E_FACE ? _u * 0.42 : (K->w > 1.45 ? _u * 0.27 : _u * 0.24);

        _shape[k].hidden = !vis;
        _text[k].hidden = !vis;
        _shape[k].opacity = _alpha;
        _text[k].opacity = _alpha;
        _shape[k].path = [UIBezierPath bezierPathWithRoundedRect:r cornerRadius:isRound ? r.size.width / 2 : 10].CGPath;
        _shape[k].fillColor = [UIColor colorWithWhite:1.0 alpha:_down[k] ? 0.4 : 0.08].CGColor;
        _shown[k] = _down[k];
        _text[k].fontSize = fs;
        _text[k].frame = CGRectMake(r.origin.x, r.origin.y + (r.size.height - fs * 1.25) / 2, r.size.width, fs * 1.3);
        if (K->kind == K_MODE) {
            _text[k].string = _dpadMode ? @"D-PAD" : @"ANALÓG.";
        }
    }
    {   /* the stick: its ring where it was put down (floating) or where it lives, and the thumb */
        BOOL active = _stickTouch != nil;
        CGPoint c = active ? _stickCtr : _ctr[E_STICK];
        CGFloat dx = active ? _stickPos.x - c.x : 0, dy = active ? _stickPos.y - c.y : 0, mag = hypot(dx, dy);

        if (mag > Rs) {
            dx *= Rs / mag;
            dy *= Rs / mag;
        }
        _stickBase.hidden = _stickThumb.hidden = ![self stickVisible];
        _stickBase.opacity = _stickThumb.opacity = _floating && !active && !_editing ? _alpha * 0.45 : _alpha;
        _stickBase.path = [UIBezierPath bezierPathWithOvalInRect:CGRectMake(c.x - Rs, c.y - Rs, 2 * Rs, 2 * Rs)].CGPath;
        _stickThumb.path = [UIBezierPath bezierPathWithOvalInRect:CGRectMake(c.x + dx - Rs * 0.5, c.y + dy - Rs * 0.5, Rs, Rs)].CGPath;
        _stickThumb.fillColor = [UIColor colorWithWhite:1.0 alpha:active ? 0.35 : 0.15].CGColor;
        _stickShown = active;
    }
    /* gear and eye: always there, a little fainter */
    _gearShape.path = [UIBezierPath bezierPathWithRoundedRect:_gear cornerRadius:_gear.size.width / 2].CGPath;
    _eyeShape.path = [UIBezierPath bezierPathWithRoundedRect:_eye cornerRadius:_eye.size.width / 2].CGPath;
    _gearShape.opacity = _eyeShape.opacity = 0.7;
    _gearText.fontSize = _eyeText.fontSize = _gear.size.width * 0.6;
    _gearText.frame = CGRectMake(_gear.origin.x, _gear.origin.y + _gear.size.height * 0.12, _gear.size.width, _gear.size.height);
    _eyeText.frame = CGRectMake(_eye.origin.x, _eye.origin.y + _eye.size.height * 0.12, _eye.size.width, _eye.size.height);
    _gearText.opacity = _eyeText.opacity = 0.9;
    _eyeShape.fillColor = [UIColor colorWithWhite:1.0 alpha:_hidden ? 0.4 : 0.08].CGColor;
    /* editing */
    _doneShape.hidden = _doneText.hidden = _banner.hidden = !_editing;
    _doneShape.path = [UIBezierPath bezierPathWithRoundedRect:_done cornerRadius:12].CGPath;
    _doneShape.fillColor = [UIColor colorWithRed:0.9 green:0.5 blue:0.1 alpha:0.9].CGColor;
    _doneText.fontSize = _u * 0.34;
    _doneText.frame = CGRectMake(_done.origin.x, _done.origin.y + (_done.size.height - _u * 0.45) / 2, _done.size.width, _u * 0.5);
    _banner.fontSize = _u * 0.3;
    _banner.frame = CGRectMake(w * 0.2, h / 2 - 1.4 * _u, w * 0.6, _u * 0.9);
    [CATransaction commit];
}

/* What a touch changes, and nothing else: the pressed keys' fill and the stick's ring and thumb. (A full refresh makes
   every path again: on the game's own thread, every touch event, that is time taken from the frame.) */
- (void)refreshState {
    BOOL active = _stickTouch != nil;
    int k;

    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    for (k = 0; k < N_KEYS; k++) {
        if (_down[k] != _shown[k]) {
            _shown[k] = _down[k];
            _shape[k].fillColor = [UIColor colorWithWhite:1.0 alpha:_down[k] ? 0.4 : 0.08].CGColor;
        }
    }
    if (active || _stickShown) {
        CGFloat Rs = [self stickRadius];
        CGPoint c = active ? _stickCtr : _ctr[E_STICK];
        CGFloat dx = active ? _stickPos.x - c.x : 0, dy = active ? _stickPos.y - c.y : 0, mag = hypot(dx, dy);

        if (mag > Rs) {
            dx *= Rs / mag;
            dy *= Rs / mag;
        }
        _stickBase.opacity = _stickThumb.opacity = _floating && !active && !_editing ? _alpha * 0.45 : _alpha;
        _stickBase.path = [UIBezierPath bezierPathWithOvalInRect:CGRectMake(c.x - Rs, c.y - Rs, 2 * Rs, 2 * Rs)].CGPath;
        _stickThumb.path = [UIBezierPath bezierPathWithOvalInRect:CGRectMake(c.x + dx - Rs * 0.5, c.y + dy - Rs * 0.5, Rs, Rs)].CGPath;
        _stickThumb.fillColor = [UIColor colorWithWhite:1.0 alpha:active ? 0.35 : 0.15].CGColor;
        _stickShown = active;
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

    if (_dpadMode) {
        return NO;
    }
    if (_floating) {
        return p.x < w * 0.45 && p.y > h * 0.3;
    }
    return hypot(p.x - _ctr[E_STICK].x, p.y - _ctr[E_STICK].y) < [self stickRadius] * 1.5;
}

- (int)keyAt:(CGPoint)p {
    int k;
    for (k = 0; k < N_KEYS; k++) {
        if ([self keyVisible:k] && CGRectContainsPoint(CGRectInset(_rect[k], -6, -6), p)) {
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
    if (!_dpadMode && hypot(p.x - _ctr[E_STICK].x, p.y - _ctr[E_STICK].y) < [self stickRadius] * 1.2) {
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
    Port_TouchPad(_bits, _lx, _ly, 0x80, CACurrentMediaTime() < _pulseUntil ? 0xFF : 0x80);
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

- (void)update:(UIEvent *)event {
    BOOL now[N_KEYS] = {0};
    BOOL modeChanged = NO;
    unsigned bits = 0;
    int k, lx = 0x80, ly = 0x80;

    if (!_hidden && !_editing) {
        for (UITouch *t in event.allTouches) {
            if (t.phase == UITouchPhaseEnded || t.phase == UITouchPhaseCancelled || t == _stickTouch || t == _gearTouch || t == _eyeTouch) {
                continue;
            }
            CGPoint p = [t locationInView:self];
            for (k = 0; k < N_KEYS; k++) {
                if ([self keyVisible:k] && CGRectContainsPoint(CGRectInset(_rect[k], -6, -6), p)) {
                    now[k] = YES;
                }
            }
        }
        if (_stickTouch != nil) {
            CGFloat Rs = [self stickRadius], dx = _stickPos.x - _stickCtr.x, dy = _stickPos.y - _stickCtr.y, mag = hypot(dx, dy);
            if (mag > Rs) {
                dx *= Rs / mag;
                dy *= Rs / mag;
            }
            lx = (int)lroundf((float)(128 + dx / Rs * 127));
            ly = (int)lroundf((float)(128 + dy / Rs * 127));
        }
    }
    for (k = 0; k < N_KEYS; k++) {
        if (now[k] && !_down[k]) { /* just pressed */
            if (kKeys[k].kind == K_PULSE) {
                _pulseUntil = CACurrentMediaTime() + 0.12;
                dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(0.14 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{ [self publish]; });
            } else if (kKeys[k].kind == K_MODE) {
                _dpadMode = !_dpadMode;
                _stickTouch = nil;
                [self saveSettings];
                modeChanged = YES;
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
    if (modeChanged) {
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
        }
    }
    [self update:event];
}

- (void)finish:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event cancelled:(BOOL)cancelled {
    for (UITouch *t in touches) {
        CGPoint p = [t locationInView:self];
        if (t == _stickTouch) {
            _stickTouch = nil;
        } else if (t == _dragTouch) {
            _dragTouch = nil;
            [self saveSettings];
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
    [ac addAction:[self act:@"Restaurar o padrão" style:UIAlertActionStyleDestructive do:^{
        int e;
        for (e = 0; e < E_COUNT; e++) {
            self->_isCustom[e] = NO;
        }
        self->_scale = 1.0f;
        self->_alpha = 0.8f;
        self->_floating = NO;
        self->_dpadMode = NO;
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
        }];
    });
}
