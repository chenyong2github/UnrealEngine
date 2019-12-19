// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_ACCESSIBILITY

#include "CoreMinimal.h"
#include "GenericPlatform/Accessibility/GenericAccessibleInterfaces.h"
#import <UIKit/UIKit.h>

@class FIOSAccessibilityLeaf;

/**
 * UIAccessibilityElements cannot be both accessible and have children. While
 * the same class can be used in both cases, the value they return for
 * isAccessibilityElement determines what type of widget they are. If false,
 * the widget is a container and only functions regarding children will be called.
 * If true, only functions regarding the value of the wigdet will be called.
 *
 * Because of this, all IAccessibleWidgets have both a corresponding container
 * and leaf. The leaf is always reported as the last child of the container. This
 * is our workaround for a widget being accessible and having children at the same time.
 */
@interface FIOSAccessibilityContainer : UIAccessibilityElement
{
@private
	/** A matching Leaf element that shares the same AccessibleWidgetId at this container. */
	FIOSAccessibilityLeaf* Leaf;
}

/** This must be used instead of initWithAccessibilityContainer in order to work properly. */
-(id)initWithId:(AccessibleWidgetId)InId;
/** Updates the accessibilityContainer property from UIAccessibilityElement. */
-(void)SetParent:(AccessibleWidgetId)InParentId;
/** Get the accessible version of this widget. */
-(FIOSAccessibilityLeaf*)GetLeaf;

/** The identifier used to access this widget through the accessible API. */
@property (nonatomic) AccessibleWidgetId Id;
/** A list of identifiers for all current children of this container. */
@property (nonatomic) TArray<AccessibleWidgetId> ChildIds;
/** The bounding rect of the container. */
@property (nonatomic) FBox2D Bounds;
/** Whether or not the widget is currently visible. */
@property (nonatomic) bool bIsVisible;

@end

/**
 * The accessible version of a widget for a given AccessibleWidgetId. A Leaf is
 * guaranteed to have an FIOSAccessibilityContainer as its container, and can be
 * accessed with [self accessibilityContainer] (in order to get things like bounds).
 */
@interface FIOSAccessibilityLeaf : UIAccessibilityElement
{
}

/** This must be used instead of initWithAccessibilityContainer in order to work properly. */
-(id)initWithParent:(FIOSAccessibilityContainer*)Parent;
/** Check if LastCachedStringTime was updated recently. */
-(bool)ShouldCacheStrings;
/**  Toggle an individual trait on or off */
-(void)SetAccessibilityTrait:(UIAccessibilityTraits)Trait Set:(bool)IsEnabled;

/** A cached version of the name of the widget. */
@property (nonatomic) FString Label;
/** A cached version of the help text of the widget. */
@property (nonatomic) FString Hint;
/** A cached version of the value of property widgets. */
@property (nonatomic) FString Value;
/** Bitflag of traits that describe the widget. Most are set once on initialization. */
@property (nonatomic) UIAccessibilityTraits Traits;
/** Timestamp for when Label, Hint, and Value were last cached. */
@property (nonatomic) double LastCachedStringTime;

@end

#endif
