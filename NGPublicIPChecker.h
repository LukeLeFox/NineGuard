#import <Foundation/Foundation.h>

@interface NGPublicIPChecker : NSObject

+ (void)fetchIPv4WithCompletion:
    (void (^)(NSString *ip, NSError *error))completion;

@end
