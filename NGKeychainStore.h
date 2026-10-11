#import <Foundation/Foundation.h>
#import <Security/Security.h>

@interface NGKeychainStore : NSObject

+ (OSStatus)saveConfigData:(NSData *)data
                profileID:(NSString *)profileID;

+ (NSData *)configDataForProfileID:(NSString *)profileID
                            status:(OSStatus *)status;

+ (OSStatus)removeConfigForProfileID:(NSString *)profileID;

@end
