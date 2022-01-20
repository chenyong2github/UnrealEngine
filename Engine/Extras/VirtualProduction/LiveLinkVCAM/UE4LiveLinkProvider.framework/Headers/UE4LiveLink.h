//  Copyright Epic Games, Inc. All Rights Reserved.

#import <Foundation/Foundation.h>

#import "UE4LiveLinkProvider.h"

__attribute__((visibility("default")))
@protocol UE4LiveLinkLogDelegate

- (void) ue4LogMessage:(NSString *)message;

@end

 
__attribute__((visibility("default")))
@interface UE4LiveLink : NSObject

+ (void) initialize:(id<UE4LiveLinkLogDelegate>)logDelegate;
+ (void) restart;
+ (void) shutdown;
+ (id<UE4LiveLinkProvider> ) createProvider:(NSString *)name;

@end
