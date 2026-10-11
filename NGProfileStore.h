#import <Foundation/Foundation.h>
#import <Security/Security.h>

@interface NGProfileStore : NSObject

+ (NSString *)saveConfigurationText:(NSString *)text
                               error:(NSError **)error;
+ (NSString *)activeProfileID;
+ (NSData *)activeConfigurationDataWithStatus:(OSStatus *)status;
+ (OSStatus)removeActiveProfile;

@end
