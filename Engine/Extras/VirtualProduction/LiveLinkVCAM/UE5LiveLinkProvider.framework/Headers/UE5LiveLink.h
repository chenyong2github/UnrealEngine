//  Copyright Epic Games, Inc. All Rights Reserved.

#import <Foundation/Foundation.h>

#import "UE5LiveLinkProvider.h"

__attribute__((visibility("default")))
@protocol UE5LiveLinkLogDelegate

- (void) ue5LogMessage:(NSString *)message;

@end

 
__attribute__((visibility("default")))
@interface UE5LiveLink : NSObject

+ (void) initialize:(id<UE5LiveLinkLogDelegate>)logDelegate;
+ (void) restart;
+ (void) shutdown;
+ (id<UE5LiveLinkProvider> ) createProvider:(NSString *)name;

@end
