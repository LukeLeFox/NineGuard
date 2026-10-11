#import "NGKeychainStore.h"
#import <UIKit/UIKit.h>
#import <NetworkExtension/NetworkExtension.h>
#import "NGQRScanner.h"
#import "NGKeychainSelfTest.h"
#import "NGPublicIPChecker.h"
#import "NGWGConfigParser.h"
#import "NGProfileStore.h"
#import "NGHomeLayoutController.h"
#import "NGDaemonClient.h"

@interface NineGuardAppDelegate : UIResponder <UIApplicationDelegate>
@property (nonatomic, strong) UIWindow *window;
@property (nonatomic, strong) UILabel *statusLabel;
@property (nonatomic, strong) UILabel *ipLabel;
@property (nonatomic, strong) UILabel *subtitleLabel;
@property (nonatomic, strong) UIButton *scanButton;
@property (nonatomic, strong) UIButton *registerButton;
@property (nonatomic, strong) UIButton *removeButton;
@property (nonatomic, strong) NETunnelProviderManager *manager;
@property (nonatomic, strong) NSTimer *statsTimer;
@property (nonatomic, assign) BOOL statsInFlight;
@end

@implementation NineGuardAppDelegate

#pragma mark - App lifecycle

- (BOOL)application:(UIApplication *)application
didFinishLaunchingWithOptions:(NSDictionary *)options {

    self.window = [[UIWindow alloc] initWithFrame:[UIScreen mainScreen].bounds];

    NGHomeLayoutController *vc = [[NGHomeLayoutController alloc] init];
    vc.view.backgroundColor = [UIColor colorWithRed:0.95 green:0.97 blue:0.99 alpha:1.0];

    UIScrollView *page = [[UIScrollView alloc] initWithFrame:vc.view.bounds];
    page.tag = 100;
    page.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    [vc.view addSubview:page];
    CGFloat width = self.window.bounds.size.width;
    CGFloat cardX = 20.0;
    CGFloat cardW = width - 40.0;

    UIView *card = [[UIView alloc] initWithFrame:CGRectMake(cardX, 85, cardW, 330)];
    card.backgroundColor = [UIColor whiteColor];
    card.layer.cornerRadius = 18.0;
    card.layer.shadowColor = [UIColor blackColor].CGColor;
    card.layer.shadowOpacity = 0.08;
    card.layer.shadowRadius = 12.0;
    card.layer.shadowOffset = CGSizeMake(0, 4);
    card.tag = 101;
    [page addSubview:card];

    UIImage *icon = [UIImage imageNamed:@"Icon-120"];
    if (!icon) {
        icon = [UIImage imageNamed:@"Icon-76"];
    }
    if (icon) {
        UIImageView *iconView = [[UIImageView alloc] initWithFrame:CGRectMake((cardW - 72)/2.0, 20, 72, 72)];
        iconView.image = icon;
        iconView.layer.cornerRadius = 16.0;
        iconView.clipsToBounds = YES;
        iconView.tag = 102;
    [card addSubview:iconView];
    }

    UILabel *title = [[UILabel alloc] initWithFrame:CGRectMake(20, 102, cardW - 40, 42)];
    title.text = @"NineGuard";
    title.font = [UIFont boldSystemFontOfSize:28.0];
    title.textAlignment = NSTextAlignmentCenter;
    title.textColor = [UIColor colorWithRed:0.08 green:0.12 blue:0.20 alpha:1.0];
    title.tag = 103;
    [card addSubview:title];

    self.subtitleLabel = [[UILabel alloc] initWithFrame:CGRectMake(20, 142, cardW - 40, 24)];
    self.subtitleLabel.text = @"WireGuard 2.0 per iOS 9";
    self.subtitleLabel.font = [UIFont systemFontOfSize:15.0];
    self.subtitleLabel.textAlignment = NSTextAlignmentCenter;
    self.subtitleLabel.textColor = [UIColor colorWithRed:0.38 green:0.43 blue:0.50 alpha:1.0];
    self.subtitleLabel.adjustsFontSizeToFitWidth = YES;
    self.subtitleLabel.minimumScaleFactor = 0.7;
    self.subtitleLabel.tag = 104;
    [card addSubview:self.subtitleLabel];

    UIView *statusBox = [[UIView alloc] initWithFrame:CGRectMake(18, 180, cardW - 36, 72)];
    statusBox.backgroundColor = [UIColor colorWithRed:0.96 green:0.98 blue:1.00 alpha:1.0];
    statusBox.layer.cornerRadius = 12.0;
    statusBox.tag = 105;
    [card addSubview:statusBox];

    self.statusLabel = [[UILabel alloc] initWithFrame:CGRectMake(12, 10, statusBox.bounds.size.width - 24, 52)];
    self.statusLabel.text = @"Verifica stato profilo VPN...";
    self.statusLabel.numberOfLines = 0;
    self.statusLabel.font = [UIFont systemFontOfSize:16.0];
    self.statusLabel.textAlignment = NSTextAlignmentCenter;
    self.statusLabel.textColor = [UIColor colorWithRed:0.14 green:0.18 blue:0.24 alpha:1.0];
    self.statusLabel.tag = 106;
    [statusBox addSubview:self.statusLabel];

    self.scanButton = [UIButton buttonWithType:UIButtonTypeSystem];
    self.scanButton.frame = CGRectMake(18, 266, cardW - 36, 44);
    [self.scanButton setTitle:@"Importa QR WireGuard" forState:UIControlStateNormal];
    [self.scanButton addTarget:self action:@selector(scanQR) forControlEvents:UIControlEventTouchUpInside];
    [self stylePrimaryButton:self.scanButton];
    self.scanButton.tag = 107;
    [card addSubview:self.scanButton];

    self.registerButton = [UIButton buttonWithType:UIButtonTypeSystem];
    self.registerButton.frame = CGRectMake(18, 318, cardW - 36, 44);
    [self.registerButton setTitle:@"Verifica daemon 2.0" forState:UIControlStateNormal];
    [self.registerButton addTarget:self action:@selector(checkDaemon) forControlEvents:UIControlEventTouchUpInside];
    [self styleSecondaryButton:self.registerButton];
    self.registerButton.tag = 108;
    [card addSubview:self.registerButton];

    UIButton *editButton = [UIButton buttonWithType:UIButtonTypeSystem];
    editButton.frame = CGRectMake(18, 370, cardW - 36, 44);
    [editButton setTitle:@"Connetti"
               forState:UIControlStateNormal];
    [editButton addTarget:self action:@selector(toggleConnection)
        forControlEvents:UIControlEventTouchUpInside];
    [self styleSecondaryButton:editButton];
    editButton.enabled = NO;
    editButton.alpha = 0.55;
    editButton.tag = 109;
    [card addSubview:editButton];

    self.removeButton = [UIButton buttonWithType:UIButtonTypeSystem];
    self.removeButton.frame = CGRectMake(18, 422, cardW - 36, 44);
    [self.removeButton setTitle:@"Rimuovi profilo WireGuard 2.0" forState:UIControlStateNormal];
    [self.removeButton addTarget:self action:@selector(removeProfile) forControlEvents:UIControlEventTouchUpInside];
    [self styleDangerButton:self.removeButton];
    self.removeButton.tag = 110;
    [card addSubview:self.removeButton];

    UIButton *testButton = [UIButton buttonWithType:UIButtonTypeSystem];
    testButton.frame = CGRectMake(18, 474, cardW - 36, 44);
    [testButton setTitle:@"Test Keychain"
                forState:UIControlStateNormal];
    [testButton addTarget:self action:@selector(testKeychain)
        forControlEvents:UIControlEventTouchUpInside];
    [self styleSecondaryButton:testButton];
    testButton.tag = 111;
    [card addSubview:testButton];

    UIButton *ipButton = [UIButton buttonWithType:UIButtonTypeSystem];
    ipButton.tag = 112;
    [ipButton setTitle:@"Verifica IP pubblico"
             forState:UIControlStateNormal];
    [ipButton addTarget:self action:@selector(checkPublicIP)
        forControlEvents:UIControlEventTouchUpInside];
    [self stylePrimaryButton:ipButton];
    [card addSubview:ipButton];

    self.ipLabel = [[UILabel alloc] init];
    self.ipLabel.tag = 113;
    self.ipLabel.text = @"IP pubblico: non verificato";
    self.ipLabel.textAlignment = NSTextAlignmentCenter;
    self.ipLabel.numberOfLines = 2;
    self.ipLabel.font = [UIFont systemFontOfSize:14];
    [card addSubview:self.ipLabel];



    self.window.rootViewController = vc;
    [self.window makeKeyAndVisible];

    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(refreshProfileState)
                                                 name:UIApplicationDidBecomeActiveNotification
                                               object:nil];
    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(stopStatsTimer)
                                                 name:UIApplicationWillResignActiveNotification
                                               object:nil];

    [self refreshProfileState];
    [NGDaemonClient prepareAndPingWithRetries:12
        completion:^(NSString *reply, NSError *error) {
            NSString *result = [reply isEqualToString:@"PONG\n"]
                ? @"OK" : @"FAILED";
            [[NSUserDefaults standardUserDefaults]
                setObject:result forKey:@"NGDaemonIPCAutoprobeResult"];
            [[NSUserDefaults standardUserDefaults] synchronize];
            if ([result isEqualToString:@"OK"]) {
                self.subtitleLabel.text = @"Daemon 2.0 autenticato";
                [self refreshConnectionButton];
            } else {
                [self showStatusText:[NSString stringWithFormat:
                    @"Demone 2.0: %@", error.localizedDescription
                        ?: @"risposta non valida"]];
            }
            NSLog(@"[NineGuard] DAEMON_IPC_READY=%@", result);
        }];
    return YES;
}

#pragma mark - UI helpers

- (void)stylePrimaryButton:(UIButton *)button {
    button.backgroundColor = [UIColor colorWithRed:0.05 green:0.42 blue:0.95 alpha:1.0];
    [button setTitleColor:[UIColor whiteColor] forState:UIControlStateNormal];
    button.layer.cornerRadius = 12.0;
    button.titleLabel.font = [UIFont boldSystemFontOfSize:16.0];
}

- (void)styleSecondaryButton:(UIButton *)button {
    button.backgroundColor = [UIColor colorWithRed:0.91 green:0.95 blue:1.00 alpha:1.0];
    [button setTitleColor:[UIColor colorWithRed:0.05 green:0.34 blue:0.78 alpha:1.0] forState:UIControlStateNormal];
    button.layer.cornerRadius = 12.0;
    button.titleLabel.font = [UIFont boldSystemFontOfSize:16.0];
}

- (void)styleDangerButton:(UIButton *)button {
    button.backgroundColor = [UIColor colorWithRed:1.00 green:0.93 blue:0.93 alpha:1.0];
    [button setTitleColor:[UIColor colorWithRed:0.76 green:0.15 blue:0.15 alpha:1.0] forState:UIControlStateNormal];
    button.layer.cornerRadius = 12.0;
    button.titleLabel.font = [UIFont boldSystemFontOfSize:16.0];
}

- (void)showStatusText:(NSString *)text {
    dispatch_async(dispatch_get_main_queue(), ^{
        self.statusLabel.text = text;
    });
}

- (void)showError:(NSError *)error phase:(NSString *)phase {
    NSLog(@"[NineGuard] %@: %@", phase, error);
    [self showStatusText:[NSString stringWithFormat:@"%@: %@ (%ld)",
                          phase, error.domain, (long)error.code]];
}

#pragma mark - VPN profile state

- (void)loadManagerWithCompletion:(void (^)(NETunnelProviderManager *mgr, NSError *error))completion {
    [NETunnelProviderManager loadAllFromPreferencesWithCompletionHandler:
     ^(NSArray *managers, NSError *error) {
        if (error) {
            completion(nil, error);
            return;
        }

        NETunnelProviderManager *selected = nil;

        for (NETunnelProviderManager *candidate in managers) {
            NETunnelProviderProtocol *proto =
                (NETunnelProviderProtocol *)candidate.protocolConfiguration;

            if (![proto isKindOfClass:
                  [NETunnelProviderProtocol class]])
                continue;

            if (![proto.providerBundleIdentifier
                  isEqualToString:@"dev.luke.nineguard.tunnel"])
                continue;

            if (selected) {
                NSError *multiple = [NSError errorWithDomain:
                    @"dev.luke.nineguard.profile"
                    code:1002
                    userInfo:@{
                        NSLocalizedDescriptionKey:
                        @"Trovati piu profili NineGuard: selezione necessaria"
                    }];

                completion(nil, multiple);
                return;
            }

            selected = candidate;
        }

        completion(selected, nil);
    }];
}

- (void)refreshProfileState {
    [self showStatusText:@"Verifica stato profilo VPN..."];
    [self refreshConnectionButton];

    NSString *profileID = [NGProfileStore activeProfileID];
    if (profileID) {
        OSStatus status = errSecParam;
        NSData *data = [NGProfileStore
            activeConfigurationDataWithStatus:&status];
        if (data.length > 0 && status == errSecSuccess) {
            [self showStatusText:
                @"Profilo WireGuard 2.0 salvato nel Keychain."];
            return;
        }
        [self showStatusText:[NSString stringWithFormat:
            @"Profilo 2.0 non leggibile dal Keychain (%ld)",
            (long)status]];
        return;
    }

    [self loadManagerWithCompletion:^(NETunnelProviderManager *mgr, NSError *error) {
        if (error) {
            [self showError:error phase:@"Lettura profilo"];
            return;
        }

        self.manager = mgr;

        if (!mgr) {
            [self showStatusText:@"Nessun profilo VPN configurato."];
            return;
        }

        NETunnelProviderProtocol *proto = (NETunnelProviderProtocol *)mgr.protocolConfiguration;
        NSString *bundleID = proto.providerBundleIdentifier ?: @"(vuoto)";
        NSString *server = mgr.localizedDescription ?: @"(senza nome)";

        NSString *text = [NSString stringWithFormat:
                          @"Profilo VPN presente.\nProvider: %@\nNome: %@",
                          bundleID, server];

        [self showStatusText:text];
    }];
}

#pragma mark - Connessione (daemon 2.0)

- (UIButton *)connectionButton {
    return (UIButton *)
        [self.window.rootViewController.view viewWithTag:109];
}

- (void)setConnectionButtonTitle:(NSString *)title enabled:(BOOL)enabled {
    UIButton *button = [self connectionButton];
    [button setTitle:title forState:UIControlStateNormal];
    button.enabled = enabled;
    button.alpha = enabled ? 1.0 : 0.55;
}

- (void)refreshConnectionButton {
    [NGDaemonClient sendCommand:@"STATUS" retries:1
        completion:^(NSString *reply, NSError *error) {
            if (error || !reply) {
                [self setConnectionButtonTitle:@"Connetti" enabled:NO];
                return;
            }
            if ([reply rangeOfString:@"PROFILE=YES"].location != NSNotFound) {
                [self setConnectionButtonTitle:@"Disconnetti" enabled:YES];
                [self startStatsTimer];
            } else {
                [self setConnectionButtonTitle:@"Connetti"
                    enabled:[NGProfileStore activeProfileID] != nil];
                [self stopStatsTimer];
            }
        }];
}

#pragma mark - Statistiche periodiche (STATUS + STATS ogni 5 s)

- (void)startStatsTimer {
    if (self.statsTimer)
        return;
    self.statsTimer = [NSTimer scheduledTimerWithTimeInterval:5.0
        target:self selector:@selector(pollStats) userInfo:nil repeats:YES];
    [self pollStats];
}

- (void)stopStatsTimer {
    [self.statsTimer invalidate];
    self.statsTimer = nil;
    self.subtitleLabel.text = @"Daemon 2.0 autenticato";
}

/* "KEY=valore" da una risposta del daemon, nil se assente. */
- (NSString *)field:(NSString *)key inReply:(NSString *)reply {
    for (NSString *item in [reply componentsSeparatedByCharactersInSet:
             [NSCharacterSet whitespaceAndNewlineCharacterSet]]) {
        NSString *prefix = [key stringByAppendingString:@"="];
        if ([item hasPrefix:prefix])
            return [item substringFromIndex:prefix.length];
    }
    return nil;
}

- (NSString *)statsSummary:(NSString *)stats watch:(NSString *)watch {
    NSString *age = [self field:@"HANDSHAKE_AGE" inReply:stats];
    NSString *handshake;
    if (!age || [age isEqualToString:@"NEVER"]) {
        handshake = @"In attesa di handshake";
    } else {
        long long seconds = age.longLongValue;
        handshake = seconds < 120
            ? [NSString stringWithFormat:@"Handshake %lld s fa", seconds]
            : [NSString stringWithFormat:@"Handshake %lld min fa",
                  seconds / 60];
    }
    long long rx = [self field:@"RX" inReply:stats].longLongValue;
    long long tx = [self field:@"TX" inReply:stats].longLongValue;
    NSString *traffic = [NSString stringWithFormat:@"\u2193%@ \u2191%@",
        [NSByteCountFormatter stringFromByteCount:rx
            countStyle:NSByteCountFormatterCountStyleFile],
        [NSByteCountFormatter stringFromByteCount:tx
            countStyle:NSByteCountFormatterCountStyleFile]];
    NSString *text = [NSString stringWithFormat:@"%@ \u00b7 %@",
        handshake, traffic];
    if ([watch isEqualToString:@"WAITING"])
        text = [text stringByAppendingString:@" \u00b7 rete assente"];
    else if ([watch isEqualToString:@"FAILING"])
        text = [text stringByAppendingString:@" \u00b7 cambio rete..."];
    return text;
}

- (void)pollStats {
    if (self.statsInFlight)
        return;
    self.statsInFlight = YES;
    [NGDaemonClient sendCommand:@"STATUS" retries:0
        completion:^(NSString *status, NSError *error) {
            if (error || !status) {
                self.statsInFlight = NO;
                return;
            }
            NSString *watch = [self field:@"NETWATCH" inReply:status];
            if ([status rangeOfString:@"PROFILE=YES"].location ==
                NSNotFound) {
                self.statsInFlight = NO;
                [self stopStatsTimer];
                [self showStatusText:[watch isEqualToString:@"STOPPED"]
                    ? @"Tunnel fermato dal daemon: dopo il cambio rete "
                      @"il server VPN non era raggiungibile in modo sicuro."
                    : @"Tunnel non piu' attivo."];
                [self refreshConnectionButton];
                return;
            }
            [NGDaemonClient sendCommand:@"STATS" retries:0
                completion:^(NSString *stats, NSError *statsError) {
                    self.statsInFlight = NO;
                    if (!statsError && [stats hasPrefix:@"STATS=OK"])
                        self.subtitleLabel.text =
                            [self statsSummary:stats watch:watch];
                }];
        }];
}

- (void)showDaemonReply:(NSString *)reply prefix:(NSString *)prefix {
    NSString *text = [reply stringByTrimmingCharactersInSet:
        [NSCharacterSet whitespaceAndNewlineCharacterSet]];
    [self showStatusText:[NSString stringWithFormat:@"%@\n%@",
        prefix, text ?: @""]];
}

/* "NET_UP=OK ROUTES=2 FULL=YES BYPASS=1 DNS=YES ..." -> testo leggibile. */
- (NSString *)connectedSummary:(NSString *)reply {
    BOOL full = [reply rangeOfString:@" FULL=YES"].location != NSNotFound;
    NSString *dns = @"DNS di sistema";
    if ([reply rangeOfString:@" DNS=YES"].location != NSNotFound)
        dns = @"DNS della VPN";
    else if ([reply rangeOfString:@" DNS=SKIPPED"].location != NSNotFound)
        dns = @"DNS di sistema (profilo non full)";
    return [NSString stringWithFormat:@"Connesso: %@.\n%@.",
        full ? @"tutto il traffico IPv4 nel tunnel"
             : @"solo le reti del profilo", dns];
}

- (void)toggleConnection {
    [self setConnectionButtonTitle:@"Attendere..." enabled:NO];
    [NGDaemonClient sendCommand:@"STATUS" retries:1
        completion:^(NSString *reply, NSError *error) {
            if (error) {
                [self showStatusText:[NSString stringWithFormat:
                    @"Daemon 2.0: %@", error.localizedDescription]];
                [self refreshConnectionButton];
                return;
            }
            if ([reply rangeOfString:@"PROFILE=YES"].location != NSNotFound)
                [self disconnectTunnel];
            else
                [self connectTunnel];
        }];
}

- (void)connectTunnel {
    NSString *profileID = [NGProfileStore activeProfileID];
    if (!profileID) {
        [self showStatusText:@"Nessun profilo: importa prima un QR."];
        [self refreshConnectionButton];
        return;
    }
    [self showStatusText:@"Avvio tunnel..."];
    [NGDaemonClient startProfileID:profileID
        completion:^(NSString *reply, NSError *error) {
            if (error || ![reply hasPrefix:@"START_PROFILE=OK"]) {
                [self showDaemonReply:reply ?: error.localizedDescription
                               prefix:@"Avvio fallito."];
                [self refreshConnectionButton];
                return;
            }
            [NGDaemonClient sendCommand:@"NET_UP" retries:1
                completion:^(NSString *netReply, NSError *netError) {
                    if (netError || ![netReply hasPrefix:@"NET_UP=OK"]) {
                        /* Rollback: STOP esegue anche NET_DOWN. */
                        [NGDaemonClient sendCommand:@"STOP" retries:1
                            completion:^(NSString *r, NSError *e) {
                                (void)r; (void)e;
                                [self refreshConnectionButton];
                            }];
                        [self showDaemonReply:netReply ?:
                                netError.localizedDescription
                                       prefix:@"Rete non configurata, tunnel fermato."];
                        return;
                    }
                    [self showStatusText:[self connectedSummary:netReply]];
                    [self refreshConnectionButton];
                }];
        }];
}

- (void)disconnectTunnel {
    [self showStatusText:@"Disconnessione..."];
    [NGDaemonClient sendCommand:@"STOP" retries:1
        completion:^(NSString *reply, NSError *error) {
            [self showDaemonReply:reply ?: error.localizedDescription
                           prefix:@"Disconnesso."];
            [self refreshConnectionButton];
        }];
}

#pragma mark - Actions

- (void)checkDaemon {
    [self showStatusText:@"Verifica daemon 2.0..."];
    [NGDaemonClient sendCommand:@"STATUS" retries:2
        completion:^(NSString *reply, NSError *error) {
            if (error) {
                [self showStatusText:[NSString stringWithFormat:
                    @"Daemon 2.0: %@", error.localizedDescription]];
                return;
            }
            NSString *status = [reply
                stringByTrimmingCharactersInSet:
                    [NSCharacterSet whitespaceAndNewlineCharacterSet]];
            [self showStatusText:[NSString stringWithFormat:
                @"Daemon 2.0 autenticato.\n%@", status]];
        }];
}

- (void)checkPublicIP {
    UIButton *button = (UIButton *)
        [self.window.rootViewController.view viewWithTag:112];

    button.enabled = NO;
    self.ipLabel.text = @"Verifica IP pubblico in corso...";

    __weak NineGuardAppDelegate *weakSelf = self;

    [NGPublicIPChecker fetchIPv4WithCompletion:
     ^(NSString *ip, NSError *error) {

        NineGuardAppDelegate *owner = weakSelf;
        if (!owner) return;

        button.enabled = YES;

        if (ip) {
            owner.ipLabel.text = [NSString stringWithFormat:
                @"IPv4 pubblico: %@", ip];
        } else {
            owner.ipLabel.text = [NSString stringWithFormat:
                @"Test IP: %@", error.localizedDescription
                    ?: @"errore sconosciuto"];
        }
    }];
}

- (void)testNativeStart {
    [self showStatusText:@"Test avvio NetworkExtension..."];

    [self loadManagerWithCompletion:
     ^(NETunnelProviderManager *mgr, NSError *error) {

        if (error || !mgr) {
            [self showStatusText:
                @"Profilo NineGuard assente o non leggibile"];
            return;
        }

        [mgr loadFromPreferencesWithCompletionHandler:
         ^(NSError *loadError) {

            if (loadError) {
                [self showError:loadError phase:@"Ricaricamento"];
                return;
            }

            NEVPNConnection *connection = mgr.connection;

            if (connection.status != NEVPNStatusDisconnected) {
                [self showStatusText:
                    @"Test annullato: VPN non disconnessa"];
                return;
            }

            NSError *startError = nil;
            BOOL accepted =
                [connection startVPNTunnelAndReturnError:&startError];

            if (!accepted) {
                [self showError:startError phase:@"Avvio VPN"];
                return;
            }

            NSLog(@"[NineGuard] Richiesta START accettata");

            [self showStatusText:
                @"Richiesta START accettata. Verificare syslog."];
        }];
    }];
}

- (void)testProviderPing {
    [self showStatusText:@"PING Network Extension in corso..."];

    [self loadManagerWithCompletion:
     ^(NETunnelProviderManager *mgr, NSError *error) {

        if (error) {
            [self showError:error phase:@"Caricamento VPN"];
            return;
        }

        if (!mgr) {
            [self showStatusText:@"Profilo NineGuard non presente"];
            return;
        }

        NETunnelProviderSession *session =
            (NETunnelProviderSession *)mgr.connection;

        if (![session isKindOfClass:[NETunnelProviderSession class]]) {
            [self showStatusText:@"Sessione VPN non valida"];
            return;
        }

        NSData *ping = [@"NG-PING-V1"
            dataUsingEncoding:NSUTF8StringEncoding];

        NSError *sendError = nil;

        NSLog(@"[NineGuard] IPC PING, VPN status=%ld",
              (long)session.status);

        BOOL sent = [session
            sendProviderMessage:ping
            returnError:&sendError
            responseHandler:^(NSData *response) {

                if (!response) {
                    [self showStatusText:
                        [NSString stringWithFormat:
                         @"IPC: risposta NIL (VPN status %ld)",
                         (long)session.status]];
                    return;
                }

                NSString *reply = [[NSString alloc]
                    initWithData:response
                    encoding:NSUTF8StringEncoding];

                if ([reply isEqualToString:@"NG-PONG-V1"]) {
                    [self showStatusText:
                        @"IPC PING/PONG: TEST SUPERATO!"];
                } else {
                    [self showStatusText:
                        [NSString stringWithFormat:
                         @"IPC: risposta inattesa (%lu byte)",
                         (unsigned long)response.length]];
                }
            }];

        if (!sent) {
            [self showStatusText:
                [NSString stringWithFormat:
                 @"IPC invio fallito: %@ (%ld)",
                 sendError.domain ?: @"sconosciuto",
                 (long)sendError.code]];
        }
    }];
}

- (void)testSharedKeychain {
    NSString *probeID =
        @"c0ffeedd-0000-4000-8000-000000000042";

    NSString *value = [NSString stringWithFormat:
        @"NGPROBE-v1:%@", [[NSUUID UUID] UUIDString]];

    NSData *challenge =
        [value dataUsingEncoding:NSUTF8StringEncoding];

    OSStatus rc = [NGKeychainStore
        saveConfigData:challenge profileID:probeID];

    if (rc != errSecSuccess) {
        [self showStatusText:[NSString stringWithFormat:
            @"Probe: scrittura fallita (%ld)", (long)rc]];
        return;
    }

    [self showStatusText:@"Keychain locale OK. Contatto extension..."];

    [self loadManagerWithCompletion:
     ^(NETunnelProviderManager *mgr, NSError *error) {

        if (error || !mgr) {
            [NGKeychainStore removeConfigForProfileID:probeID];
            [self showStatusText:
                error ? @"Errore lettura profilo VPN"
                      : @"Registra prima il profilo NineGuard PoC"];
            return;
        }

        NETunnelProviderSession *session =
            (NETunnelProviderSession *)mgr.connection;

        __block BOOL finished = NO;
        NSError *sendError = nil;

        NSData *command = [@"NG-KEYCHAIN-PROBE-V1"
            dataUsingEncoding:NSUTF8StringEncoding];

        BOOL sent = [session
            sendProviderMessage:command
            returnError:&sendError
            responseHandler:^(NSData *response) {

                dispatch_async(dispatch_get_main_queue(), ^{
                    if (finished) return;
                    finished = YES;

                    [NGKeychainStore
                        removeConfigForProfileID:probeID];

                    if ([response isEqualToData:challenge]) {
                        [self showStatusText:
                            @"Keychain condiviso: TEST SUPERATO!"];
                    } else {
                        NSString *reply = [[NSString alloc]
                            initWithData:response
                            encoding:NSUTF8StringEncoding];

                        [self showStatusText:
                            reply.length
                            ? [NSString stringWithFormat:
                               @"Extension: %@", reply]
                            : @"Extension: risposta assente o diversa"];
                    }
                });
            }];

        if (!sent) {
            finished = YES;
            [NGKeychainStore removeConfigForProfileID:probeID];
            [self showError:sendError phase:@"Invio IPC"];
            return;
        }

        dispatch_after(dispatch_time(DISPATCH_TIME_NOW,
            (int64_t)(15 * NSEC_PER_SEC)),
            dispatch_get_main_queue(), ^{
                if (finished) return;
                finished = YES;

                [NGKeychainStore
                    removeConfigForProfileID:probeID];

                [self showStatusText:
                    @"Timeout: extension non risponde (15s)"];
            });
    }];
}

- (void)testKeychain {
    [self showStatusText:@"Verifica Keychain..."];

    NSString *result = [NGKeychainSelfTest runTest];
    [self showStatusText:result];
}

- (void)scanQR {
    NGQRScanner *scanner = [[NGQRScanner alloc] init];
    scanner.modalPresentationStyle = UIModalPresentationFullScreen;
    __weak NineGuardAppDelegate *weakSelf = self;

    scanner.onCode = ^(NSString *code) {
        NSError *error = nil;
        NSDictionary *config =
            [NGWGConfigParser parseText:code error:&error];

        if (!config) {
            [weakSelf showStatusText:
                [NSString stringWithFormat:@"QR non valido: %@",
                 error.localizedDescription]];
            return;
        }

        NSString *profileID = [NGProfileStore
            saveConfigurationText:code error:&error];
        if (!profileID) {
            [weakSelf showStatusText:
                [NSString stringWithFormat:@"Salvataggio fallito: %@",
                 error.localizedDescription ?: @"errore sconosciuto"]];
            return;
        }

        NSArray *peers = config[@"peers"];
        [weakSelf showStatusText:
            [NSString stringWithFormat:
             @"Profilo WireGuard 2.0 salvato (%lu peer).",
             (unsigned long)peers.count]];
        [weakSelf refreshConnectionButton];
    };

    [self.window.rootViewController presentViewController:scanner animated:YES completion:nil];
}

- (void)registerProfile {
    [self showStatusText:@"Registrazione profilo VPN..."];

    [self loadManagerWithCompletion:^(NETunnelProviderManager *mgr, NSError *error) {
        if (error) {
            [self showError:error phase:@"Lettura profili"];
            return;
        }

        if (!mgr) {
            mgr = [[NETunnelProviderManager alloc] init];
        }

        self.manager = mgr;

        NETunnelProviderProtocol *proto = [[NETunnelProviderProtocol alloc] init];
        proto.providerBundleIdentifier = @"dev.luke.nineguard.tunnel";
        proto.serverAddress = @"NineGuard PoC";

        mgr.protocolConfiguration = proto;
        mgr.localizedDescription = @"NineGuard PoC";
        mgr.enabled = YES;

        [mgr saveToPreferencesWithCompletionHandler:^(NSError *saveError) {
            if (saveError) {
                [self showError:saveError phase:@"Salvataggio"];
                return;
            }

            [self showStatusText:@"Profilo VPN salvato."];
            [self refreshProfileState];
        }];
    }];
}

- (void)renameProfile {
    [self loadManagerWithCompletion:
     ^(NETunnelProviderManager *mgr, NSError *error) {

        if (error) {
            [self showError:error phase:@"Lettura"];
            return;
        }

        if (!mgr) {
            [self showStatusText:@"Nessun profilo da modificare."];
            return;
        }

        NETunnelProviderProtocol *proto =
            (NETunnelProviderProtocol *)mgr.protocolConfiguration;

        if (![proto isKindOfClass:[NETunnelProviderProtocol class]] ||
            ![proto.providerBundleIdentifier
              isEqualToString:@"dev.luke.nineguard.tunnel"]) {
            [self showStatusText:@"Profilo non appartenente a NineGuard."];
            return;
        }

        dispatch_async(dispatch_get_main_queue(), ^{
            UIAlertController *alert = [UIAlertController
                alertControllerWithTitle:@"Rinomina profilo"
                message:@"Modifica il nome visualizzato nelle VPN di iOS."
                preferredStyle:UIAlertControllerStyleAlert];

            [alert addTextFieldWithConfigurationHandler:
             ^(UITextField *field) {
                field.text = mgr.localizedDescription ?: @"NineGuard";
            }];

            [alert addAction:[UIAlertAction
                actionWithTitle:@"Annulla"
                style:UIAlertActionStyleCancel
                handler:nil]];

            __weak UIAlertController *weakAlert = alert;

            [alert addAction:[UIAlertAction
                actionWithTitle:@"Salva"
                style:UIAlertActionStyleDefault
                handler:^(UIAlertAction *action) {

                NSString *name = [weakAlert.textFields.firstObject.text
                    stringByTrimmingCharactersInSet:
                    [NSCharacterSet whitespaceAndNewlineCharacterSet]];

                if (name.length == 0 || name.length > 64) {
                    [self showStatusText:@"Nome non valido (1-64 caratteri)."];
                    return;
                }

                [mgr loadFromPreferencesWithCompletionHandler:
                 ^(NSError *loadError) {
                    if (loadError) {
                        [self showError:loadError phase:@"Ricaricamento"];
                        return;
                    }

                    mgr.localizedDescription = name;

                    [mgr saveToPreferencesWithCompletionHandler:
                     ^(NSError *saveError) {
                        if (saveError)
                            [self showError:saveError phase:@"Modifica"];
                        else
                            [self refreshProfileState];
                    }];
                }];
            }]];

            [self.window.rootViewController
                presentViewController:alert animated:YES completion:nil];
        });
    }];
}

- (void)removeProfile {
    /* Prima ferma il tunnel (STOP esegue anche NET_DOWN), poi rimuove. */
    [self setConnectionButtonTitle:@"Attendere..." enabled:NO];
    [self showStatusText:@"Arresto tunnel e rimozione profilo..."];
    [NGDaemonClient sendCommand:@"STOP" retries:2
        completion:^(NSString *reply, NSError *error) {
            BOOL stopped = !error && [reply hasPrefix:@"STOP=OK"];
            OSStatus status = [NGProfileStore removeActiveProfile];
            if (status == errSecSuccess || status == errSecItemNotFound) {
                [self showStatusText:stopped
                    ? @"Tunnel fermato, profilo WireGuard 2.0 rimosso."
                    : @"Profilo rimosso. Attenzione: arresto tunnel non confermato dal daemon."];
                [self refreshProfileState];
            } else {
                [self showStatusText:[NSString stringWithFormat:
                    @"Rimozione profilo fallita (%ld)", (long)status]];
                [self refreshConnectionButton];
            }
        }];
}

@end

int main(int argc, char *argv[]) {
    @autoreleasepool {
        return UIApplicationMain(argc, argv, nil,
                                 NSStringFromClass([NineGuardAppDelegate class]));
    }
}
