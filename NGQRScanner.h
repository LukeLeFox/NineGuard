#import <UIKit/UIKit.h>

@interface NGQRScanner : UIViewController
@property (nonatomic, copy) void (^onCode)(NSString *code);
@end
