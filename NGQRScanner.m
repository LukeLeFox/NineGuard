#import "NGQRScanner.h"
#import <AVFoundation/AVFoundation.h>

@interface NGQRScanner () <AVCaptureMetadataOutputObjectsDelegate>
@property (nonatomic, strong) AVCaptureSession *session;
@property (nonatomic, strong) AVCaptureVideoPreviewLayer *preview;
@property (nonatomic, assign) BOOL captured;
@end

@implementation NGQRScanner

- (void)viewDidLoad {
    [super viewDidLoad];
    self.view.backgroundColor = [UIColor blackColor];

    UIButton *cancel = [UIButton buttonWithType:UIButtonTypeSystem];
    cancel.frame = CGRectMake(20, 35, 100, 44);
    [cancel setTitle:@"Annulla" forState:UIControlStateNormal];
    [cancel setTitleColor:[UIColor whiteColor] forState:UIControlStateNormal];
    [cancel addTarget:self action:@selector(cancelScan)
      forControlEvents:UIControlEventTouchUpInside];
    [self.view addSubview:cancel];

    AVAuthorizationStatus status =
        [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeVideo];

    if (status == AVAuthorizationStatusNotDetermined) {
        [AVCaptureDevice requestAccessForMediaType:AVMediaTypeVideo
            completionHandler:^(BOOL granted) {
                dispatch_async(dispatch_get_main_queue(), ^{
                    if (granted) [self startCamera];
                    else [self cameraError];
                });
            }];
    } else if (status == AVAuthorizationStatusAuthorized) {
        [self startCamera];
    } else {
        [self cameraError];
    }
}

- (void)startCamera {
    AVCaptureDevice *device =
        [AVCaptureDevice defaultDeviceWithMediaType:AVMediaTypeVideo];

    NSError *error = nil;
    AVCaptureDeviceInput *input =
        [AVCaptureDeviceInput deviceInputWithDevice:device error:&error];

    if (!input) {
        [self cameraError];
        return;
    }

    self.session = [[AVCaptureSession alloc] init];
    AVCaptureMetadataOutput *output = [[AVCaptureMetadataOutput alloc] init];

    if (![self.session canAddInput:input] ||
        ![self.session canAddOutput:output]) {
        [self cameraError];
        return;
    }

    [self.session addInput:input];
    [self.session addOutput:output];

    [output setMetadataObjectsDelegate:self
        queue:dispatch_get_main_queue()];

    if (![output.availableMetadataObjectTypes
          containsObject:AVMetadataObjectTypeQRCode]) {
        [self cameraError];
        return;
    }

    output.metadataObjectTypes = @[AVMetadataObjectTypeQRCode];

    self.preview = [AVCaptureVideoPreviewLayer layerWithSession:self.session];
    self.preview.videoGravity = AVLayerVideoGravityResizeAspectFill;
    self.preview.frame = self.view.bounds;
    [self.view.layer insertSublayer:self.preview atIndex:0];
    [self updatePreviewOrientation];

    [self.session startRunning];
}

- (void)captureOutput:(AVCaptureOutput *)captureOutput
didOutputMetadataObjects:(NSArray *)objects
       fromConnection:(AVCaptureConnection *)connection {

    if (self.captured) return;

    for (AVMetadataObject *object in objects) {
        if (![object isKindOfClass:
              [AVMetadataMachineReadableCodeObject class]]) continue;

        AVMetadataMachineReadableCodeObject *qr =
            (AVMetadataMachineReadableCodeObject *)object;

        if (![qr.type isEqualToString:AVMetadataObjectTypeQRCode])
            continue;

        if (qr.stringValue.length == 0) continue;

        self.captured = YES;
        NSString *value = qr.stringValue;
        [self.session stopRunning];

        [self dismissViewControllerAnimated:YES completion:^{
            if (self.onCode) self.onCode(value);
        }];
        break;
    }
}

- (void)cancelScan {
    [self dismissViewControllerAnimated:YES completion:nil];
}

- (void)cameraError {
    UIAlertController *alert = [UIAlertController
        alertControllerWithTitle:@"Fotocamera non disponibile"
        message:@"Controlla i permessi fotocamera di NineGuard."
        preferredStyle:UIAlertControllerStyleAlert];

    [alert addAction:[UIAlertAction
        actionWithTitle:@"OK"
        style:UIAlertActionStyleDefault
        handler:^(UIAlertAction *action) {
            [self cancelScan];
        }]];

    [self presentViewController:alert animated:YES completion:nil];
}

- (void)updatePreviewOrientation {
    AVCaptureConnection *conn = self.preview.connection;
    if (!conn || ![conn isVideoOrientationSupported]) return;

    UIInterfaceOrientation ui =
        [UIApplication sharedApplication].statusBarOrientation;

    AVCaptureVideoOrientation video;
    switch (ui) {
        case UIInterfaceOrientationPortrait:
            video = AVCaptureVideoOrientationPortrait; break;
        case UIInterfaceOrientationPortraitUpsideDown:
            video = AVCaptureVideoOrientationPortraitUpsideDown; break;
        case UIInterfaceOrientationLandscapeLeft:
            video = AVCaptureVideoOrientationLandscapeLeft; break;
        case UIInterfaceOrientationLandscapeRight:
            video = AVCaptureVideoOrientationLandscapeRight; break;
        default:
            return;
    }
    conn.videoOrientation = video;
}

- (BOOL)shouldAutorotate {
    return YES;
}

- (UIInterfaceOrientationMask)supportedInterfaceOrientations {
    return UI_USER_INTERFACE_IDIOM() == UIUserInterfaceIdiomPad
        ? UIInterfaceOrientationMaskAll
        : UIInterfaceOrientationMaskAllButUpsideDown;
}

- (void)viewDidLayoutSubviews {
    [super viewDidLayoutSubviews];
    self.preview.frame = self.view.bounds;
    [self updatePreviewOrientation];
}

- (void)viewWillTransitionToSize:(CGSize)size
      withTransitionCoordinator:(id<UIViewControllerTransitionCoordinator>)coordinator {
    [super viewWillTransitionToSize:size withTransitionCoordinator:coordinator];
    [coordinator animateAlongsideTransition:nil completion:
     ^(id<UIViewControllerTransitionCoordinatorContext> context) {
        [self updatePreviewOrientation];
    }];
}

- (void)viewWillDisappear:(BOOL)animated {
    [super viewWillDisappear:animated];
    [self.session stopRunning];
}

@end
