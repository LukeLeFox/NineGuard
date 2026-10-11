#import <Foundation/Foundation.h>

@interface NGDaemonClient : NSObject

+ (void)sendCommand:(NSString *)command
             retries:(NSUInteger)retries
          completion:(void (^)(NSString *reply,
                                NSError *error))completion;
/* Invia "START_PROFILE <UUID>": solo l'UUID attraversa l'IPC, mai il
 * testo della configurazione o le chiavi. */
+ (void)startProfileID:(NSString *)profileID
            completion:(void (^)(NSString *reply,
                                  NSError *error))completion;
+ (void)prepareAndPingWithRetries:(NSUInteger)retries
                       completion:(void (^)(NSString *reply,
                                             NSError *error))completion;

@end
