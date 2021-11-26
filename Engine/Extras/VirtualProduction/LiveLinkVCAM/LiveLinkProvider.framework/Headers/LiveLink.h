//  Copyright Epic Games, Inc. All Rights Reserved.

#import <Foundation/Foundation.h>

#import "LiveLinkProvider.h"

__attribute__((visibility("default")))
@protocol UELogDelegate

- (void) ueLogMessage:(NSString *)message;

@end

 
__attribute__((visibility("default")))
@interface LiveLink : NSObject

+ (void) initialize:(id<UELogDelegate>)logDelegate;
+ (void) shutdown;
+ (id<LiveLinkProvider> ) createProvider:(NSString *)name;

@end
