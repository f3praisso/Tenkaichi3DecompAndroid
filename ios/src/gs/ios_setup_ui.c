/* iOS: the screen of the preparation of the game's data (the work itself is ios_setup.c).

   Objective-C in a .c file: the build compiles this one as Objective-C with ARC (undefined.py). It runs before the game's
   code, in the program as the system loaded it, on a window of its own that goes away when the data are ready.
   The disc image is chosen with the Files app's picker (read where it is: no copy of 4 GB) or put by hand in the app's
   folder in the Files app (the first .iso there, once its size has stopped changing, starts the work by itself). The
   work runs on a thread of its own, and this screen shows its progress. */
#import <UIKit/UIKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "ios_setup.h"

#define ORANGE [UIColor colorWithRed:1.0 green:0.63 blue:0.12 alpha:1.0]

@interface BT3Setup : UIViewController <UIDocumentPickerDelegate>
@property (nonatomic, copy) NSString *docs;
@property (nonatomic, copy) NSString *gamedata;
@property (nonatomic, readonly) BOOL finished;
- (void)tick;
@end

@implementation BT3Setup {
    UILabel *_title, *_body, *_percent, *_status;
    UIProgressView *_bar;
    UIButton *_pick, *_retry;
    IosProgress _prog;
    BOOL _working, _finished, _failed;
    NSURL *_scoped;
    NSString *_failedPath, *_isoPath;
    uint64_t _lastSize;
    int _stable;
    CFTimeInterval _lastPoll;
}

- (BOOL)finished {
    return _finished;
}

- (UILabel *)label:(NSString *)text size:(CGFloat)size bold:(BOOL)bold color:(UIColor *)color {
    UILabel *l = [[UILabel alloc] init];
    l.text = text;
    l.font = bold ? [UIFont boldSystemFontOfSize:size] : [UIFont systemFontOfSize:size];
    l.textColor = color;
    l.textAlignment = NSTextAlignmentCenter;
    l.numberOfLines = 0;
    return l;
}

- (UIButton *)button:(NSString *)title action:(SEL)action {
    UIButton *b = [UIButton buttonWithType:UIButtonTypeSystem];
    [b setTitle:title forState:UIControlStateNormal];
    b.titleLabel.font = [UIFont boldSystemFontOfSize:17];
    [b setTitleColor:UIColor.blackColor forState:UIControlStateNormal];
    b.backgroundColor = ORANGE;
    b.layer.cornerRadius = 12;
    [b.heightAnchor constraintEqualToConstant:46].active = YES;
    [b addTarget:self action:action forControlEvents:UIControlEventTouchUpInside];
    return b;
}

- (void)viewDidLoad {
    UIStackView *st;

    [super viewDidLoad];
    self.view.backgroundColor = [UIColor colorWithRed:0.05 green:0.06 blue:0.14 alpha:1.0];
    _title = [self label:@"Preparar os dados do jogo" size:25 bold:YES color:ORANGE];
    _body = [self label:@"" size:15 bold:NO color:[UIColor colorWithWhite:0.92 alpha:1.0]];
    _bar = [[UIProgressView alloc] initWithProgressViewStyle:UIProgressViewStyleDefault];
    _bar.progressTintColor = ORANGE;
    _bar.trackTintColor = [UIColor colorWithWhite:1.0 alpha:0.18];
    _percent = [self label:@"" size:17 bold:YES color:UIColor.whiteColor];
    _status = [self label:@"" size:12 bold:NO color:[UIColor colorWithWhite:0.7 alpha:1.0]];
    _pick = [self button:@"Escolher o arquivo .iso…" action:@selector(pickTapped)];
    _retry = [self button:@"Procurar de novo" action:@selector(retryTapped)];
    st = [[UIStackView alloc] initWithArrangedSubviews:@[_title, _body, _bar, _percent, _status, _pick, _retry]];
    st.axis = UILayoutConstraintAxisVertical;
    st.spacing = 12;
    st.alignment = UIStackViewAlignmentFill;
    st.translatesAutoresizingMaskIntoConstraints = NO;
    [self.view addSubview:st];
    [NSLayoutConstraint activateConstraints:@[
        [st.centerXAnchor constraintEqualToAnchor:self.view.centerXAnchor],
        [st.centerYAnchor constraintEqualToAnchor:self.view.centerYAnchor],
        [st.widthAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.widthAnchor multiplier:0.72],
    ]];
    [self showWaiting];
}

/* ---- the states of the screen */
- (void)showWaiting {
    _title.text = @"Preparar os dados do jogo";
    _body.text = @"O jogo precisa do arquivo .iso do seu disco (Dragon Ball Z Budokai Tenkaichi 3, versão USA). Toque no botão para escolher o arquivo. "
                  @"Ou copie o .iso para a pasta BT3, no app Arquivos: aqui ele começa sozinho.";
    _body.textColor = [UIColor colorWithWhite:0.92 alpha:1.0];
    _bar.hidden = _percent.hidden = _status.hidden = _retry.hidden = YES;
    _pick.hidden = NO;
}

- (void)showWorking {
    _title.text = @"Preparando…";
    _body.text = @"Lendo o disco e gravando os arquivos do jogo. Isso leva alguns minutos e só acontece uma vez. Mantenha o app aberto.";
    _body.textColor = [UIColor colorWithWhite:0.92 alpha:1.0];
    _bar.progress = 0;
    _percent.text = @"0%";
    _status.text = @"";
    _bar.hidden = _percent.hidden = _status.hidden = NO;
    _pick.hidden = _retry.hidden = YES;
}

- (void)showFailure:(NSString *)why {
    _title.text = @"Não deu certo";
    _body.text = why;
    _body.textColor = [UIColor colorWithRed:1.0 green:0.55 blue:0.5 alpha:1.0];
    _bar.hidden = _percent.hidden = _status.hidden = YES;
    _pick.hidden = _retry.hidden = NO;
}

/* ---- the work */
- (void)startWithPath:(NSString *)path scoped:(NSURL *)url {
    NSString *iso = [path copy], *gd = [self.gamedata copy];
    IosProgress *p = &_prog;

    _working = YES;
    _failed = NO;
    _isoPath = iso;
    _scoped = url;
    UIApplication.sharedApplication.idleTimerDisabled = YES;
    [self showWorking];
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        IosData_Extract(iso.fileSystemRepresentation, gd.fileSystemRepresentation, p);
    });
}

- (void)pickTapped {
    UIDocumentPickerViewController *pk = [[UIDocumentPickerViewController alloc] initForOpeningContentTypes:@[UTTypeData] asCopy:NO];
    pk.delegate = self;
    pk.allowsMultipleSelection = NO;
    [self presentViewController:pk animated:YES completion:nil];
}

- (void)documentPicker:(UIDocumentPickerViewController *)controller didPickDocumentsAtURLs:(NSArray<NSURL *> *)urls {
    NSURL *u = urls.firstObject;
    if (u != nil && !_working) {
        BOOL access = [u startAccessingSecurityScopedResource];
        [self startWithPath:u.path scoped:access ? u : nil];
    }
}

- (void)retryTapped {
    _failedPath = nil;
    _failed = NO;
    _stable = 0;
    _lastSize = 0;
    [self showWaiting];
}

/* called about twenty times a second by the loop that serves the window */
- (void)tick {
    if (_working) {
        uint64_t d = _prog.done, t = _prog.total;
        char what[96];

        memcpy(what, _prog.what, sizeof(what));
        what[sizeof(what) - 1] = 0;
        if (t > 0) {
            _bar.progress = (float)((double)d / (double)t);
            _percent.text = [NSString stringWithFormat:@"%d%%  ·  %llu de %llu MB", (int)(d * 100 / t), (unsigned long long)(d >> 20), (unsigned long long)(t >> 20)];
        }
        _status.text = [NSString stringWithUTF8String:what] ?: @"";
        if (_prog.finished) {
            _working = NO;
            UIApplication.sharedApplication.idleTimerDisabled = NO;
            if (_scoped != nil) {
                [_scoped stopAccessingSecurityScopedResource];
                _scoped = nil;
            }
            if (_prog.ok) {
                IosData_Translations(self.gamedata.fileSystemRepresentation);
                _finished = YES;
            } else {
                _failed = YES;
                _failedPath = _isoPath;
                [self showFailure:[NSString stringWithUTF8String:_prog.fail] ?: @"Erro desconhecido."];
            }
        }
        return;
    }
    if (_failed || self.presentedViewController != nil || CACurrentMediaTime() - _lastPoll < 1.0) {
        return;
    }
    _lastPoll = CACurrentMediaTime();
    {   /* an .iso put in the app's folder: started once its size has stopped changing (the Files app may still be copying) */
        char path[1024];
        struct stat st;
        if (IosData_FindIso(self.docs.fileSystemRepresentation, path, sizeof(path)) && stat(path, &st) == 0 && ![_failedPath isEqualToString:@(path)]) {
            if ((uint64_t)st.st_size == _lastSize) {
                _stable++;
            } else {
                _stable = 0;
                _lastSize = (uint64_t)st.st_size;
            }
            _body.text = [NSString stringWithFormat:@"Disco encontrado: %s (%llu MB).\n%@", strrchr(path, '/') + 1, (unsigned long long)(_lastSize >> 20),
                                                    _stable >= 2 ? @"Começando…" : @"Esperando a cópia terminar…"];
            if (_stable >= 2) {
                [self startWithPath:@(path) scoped:nil];
            }
        } else {
            _stable = 0;
            _lastSize = 0;
        }
    }
}
@end

static UIWindowScene *first_scene(void) {
    for (UIScene *sc in UIApplication.sharedApplication.connectedScenes) {
        if ([sc isKindOfClass:UIWindowScene.class]) {
            return (UIWindowScene *)sc;
        }
    }
    return nil;
}

/* 1 when the data are there (the screen has come and gone if they were not); 0 if there is nowhere to put them. */
int Ios_Setup(void) {
    const char *home = getenv("HOME");
    char docs[512], gamedata[600];

    if (home == NULL) {
        return 0;
    }
    snprintf(docs, sizeof(docs), "%s/Documents", home);
    snprintf(gamedata, sizeof(gamedata), "%s/gamedata", docs);
    if (IosData_Ready(gamedata)) {
        IosData_Translations(gamedata);
        return 1;
    }
    @autoreleasepool {
        BT3Setup *vc = [[BT3Setup alloc] init];
        UIWindowScene *scene = first_scene();
        UIWindow *win = scene != nil ? [[UIWindow alloc] initWithWindowScene:scene] : [[UIWindow alloc] initWithFrame:UIScreen.mainScreen.bounds];

        vc.docs = @(docs);
        vc.gamedata = @(gamedata);
        win.rootViewController = vc;
        [win makeKeyAndVisible];
        while (!vc.finished) {
            [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];
            [vc tick];
        }
        win.hidden = YES;
        win.rootViewController = nil;
    }
    return 1;
}
