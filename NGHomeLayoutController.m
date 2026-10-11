#import "NGHomeLayoutController.h"

@implementation NGHomeLayoutController

- (BOOL)shouldAutorotate {
    return YES;
}

- (UIInterfaceOrientationMask)supportedInterfaceOrientations {
    if (UI_USER_INTERFACE_IDIOM() == UIUserInterfaceIdiomPad)
        return UIInterfaceOrientationMaskAll;
    return UIInterfaceOrientationMaskAllButUpsideDown;
}

- (void)viewDidLayoutSubviews {
    [super viewDidLayoutSubviews];

    UIScrollView *page = (id)[self.view viewWithTag:100];
    UIView *card = [page viewWithTag:101];
    if (!page || !card) return;

    CGFloat w = CGRectGetWidth(self.view.bounds);
    CGFloat h = CGRectGetHeight(self.view.bounds);

    CGFloat cw = MIN(w - 32.0, 560.0);
    CGFloat ch = 680.0;
    CGFloat top = MAX(24.0, (h - ch) / 2.0);

    page.frame = self.view.bounds;
    page.contentSize = CGSizeMake(w, MAX(h, top + ch + 24));

    card.frame = CGRectMake((w - cw) / 2.0, top, cw, ch);

    [card viewWithTag:102].frame =
        CGRectMake((cw - 72) / 2, 20, 72, 72);

    [card viewWithTag:103].frame =
        CGRectMake(20, 103, cw - 40, 42);

    [card viewWithTag:104].frame =
        CGRectMake(20, 145, cw - 40, 28);

    UIView *status = [card viewWithTag:105];
    status.frame = CGRectMake(18, 184, cw - 36, 85);

    [status viewWithTag:106].frame =
        CGRectMake(12, 7, cw - 60, 71);

    for (NSInteger i = 0; i < 6; i++) {
        [card viewWithTag:107 + i].frame =
            CGRectMake(18, 282 + i * 54, cw - 36, 44);
    }

    [card viewWithTag:113].frame =
        CGRectMake(18, 606, cw - 36, 50);
}

@end
