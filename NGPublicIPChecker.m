#import "NGPublicIPChecker.h"
#import <arpa/inet.h>

static NSError *NGIPError(NSInteger code, NSString *message) {
    return [NSError errorWithDomain:@"dev.luke.nineguard.ip"
        code:code
        userInfo:@{NSLocalizedDescriptionKey:message}];
}

@implementation NGPublicIPChecker

+ (void)fetchIPv4WithCompletion:
    (void (^)(NSString *ip, NSError *error))completion {

    NSURLSessionConfiguration *config =
        [NSURLSessionConfiguration ephemeralSessionConfiguration];

    config.timeoutIntervalForRequest = 12.0;
    config.timeoutIntervalForResource = 15.0;
    config.requestCachePolicy =
        NSURLRequestReloadIgnoringLocalCacheData;

    NSURLSession *session =
        [NSURLSession sessionWithConfiguration:config];

    NSURLRequest *request = [NSURLRequest
        requestWithURL:[NSURL URLWithString:@"https://4.ident.me/"]
        cachePolicy:NSURLRequestReloadIgnoringLocalCacheData
        timeoutInterval:12.0];

    NSURLSessionDataTask *task =
        [session dataTaskWithRequest:request
        completionHandler:^(NSData *data,
                            NSURLResponse *response,
                            NSError *networkError) {
        NSString *ip = nil;
        NSError *problem = networkError;

        if (!problem) {
            NSHTTPURLResponse *http =
                [response isKindOfClass:[NSHTTPURLResponse class]]
                ? (NSHTTPURLResponse *)response : nil;

            if (!http || http.statusCode != 200) {
                problem = NGIPError(1, @"Risposta HTTP non valida");
            } else if (data.length == 0 || data.length > 64) {
                problem = NGIPError(2, @"Risposta IP non valida");
            } else {
                NSString *raw = [[NSString alloc]
                    initWithData:data encoding:NSUTF8StringEncoding];

                NSString *candidate = [raw
                    stringByTrimmingCharactersInSet:
                    [NSCharacterSet whitespaceAndNewlineCharacterSet]];

                struct in_addr address;

                if (!candidate ||
                    inet_pton(AF_INET, candidate.UTF8String,
                              &address) != 1) {
                    problem = NGIPError(3, @"IPv4 non riconosciuto");
                } else {
                    ip = candidate;
                }
            }
        }

        [session finishTasksAndInvalidate];

        if (completion) {
            dispatch_async(dispatch_get_main_queue(), ^{
                completion(ip, problem);
            });
        }
    }];

    [task resume];
}

@end
