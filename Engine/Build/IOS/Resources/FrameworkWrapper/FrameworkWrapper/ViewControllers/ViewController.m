//
//  ViewController.m
//  FrameworkWrapper
//
//  Created by Ryan West on 7/29/19.
//  Copyright © 2019 Epic Games. All rights reserved.
//

#import "ViewController.h"

@implementation ViewController

-(void) viewDidLoad
{
    [super viewDidLoad];

    [UnrealContainerView DelayedCreateView];
    [UnrealContainerView WakeUpUnreal];
}

@end
