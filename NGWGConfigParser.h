#import <Foundation/Foundation.h>

@interface NGWGConfigParser : NSObject

+ (NSDictionary *)parseText:(NSString *)text
                      error:(NSError **)error;

@end
