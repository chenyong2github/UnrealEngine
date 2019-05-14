// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#if WITH_ACCESSIBILITY

#include "IOS/Accessibility/IOSAccessibilityElement.h"

#include "Async/TaskGraphInterfaces.h"
#include "GenericPlatform/GenericAccessibleInterfaces.h"
#include "IOS/Accessibility/IOSAccessibilityCache.h"
#include "IOS/IOSApplication.h"
#include "IOS/IOSAppDelegate.h"
#include "IOS/IOSView.h"

@implementation FIOSAccessibilityContainer

@synthesize Id;
@synthesize ChildIds;
@synthesize Bounds;
@synthesize bIsVisible;

-(id)initWithId:(AccessibleWidgetId)InId AndParentId:(AccessibleWidgetId)InParentId
{
	id UIParent = nil;
	if (InParentId != IAccessibleWidget::InvalidAccessibleWidgetId)
	{
		UIParent = [[FIOSAccessibilityCache AccessibilityElementCache] GetAccessibilityElement:InParentId];
	}
	else
	{
		UIParent = [IOSAppDelegate GetDelegate].IOSView;
	}

	if (self = [super initWithAccessibilityContainer:UIParent])
	{
		self.Id = InId;
		self.bIsVisible = true;
		Leaf = [[FIOSAccessibilityLeaf alloc] initWithParent:self];
	}

	return self;
}

-(void)dealloc
{
	[Leaf release];
	[super dealloc];
}

-(void)SetParent:(AccessibleWidgetId)InParentId
{
	if (InParentId != IAccessibleWidget::InvalidAccessibleWidgetId)
	{
		self.accessibilityContainer = [[FIOSAccessibilityCache AccessibilityElementCache] GetAccessibilityElement:InParentId];
	}
	else
	{
		self.accessibilityContainer = [IOSAppDelegate GetDelegate].IOSView;
	}
}

-(FIOSAccessibilityLeaf*)GetLeaf
{
	return Leaf;
}

-(BOOL)isAccessibilityElement
{
	// Containers are never accessible
	return NO;
}

-(CGRect)accessibilityFrame
{
	return Bounds;
}

-(NSInteger)accessibilityElementCount
{
	// The extra +1 is from the Leaf element
	return self.ChildIds.Num() + 1;
}

-(id)accessibilityElementAtIndex:(NSInteger)index
{
	if (index == self.ChildIds.Num())
	{
		return Leaf;
	}
	else
	{
		return [[FIOSAccessibilityCache AccessibilityElementCache] GetAccessibilityElement:self.ChildIds[index]];
	}
}

-(NSInteger)indexOfAccessibilityElement:(id)element
{
	if ([element isKindOfClass : [FIOSAccessibilityLeaf class]])
	{
		// If it's a Leaf, it is the last child of the parent
		return [[element accessibilityContainer] accessibilityElementCount] - 1;
	}

	const AccessibleWidgetId OtherId = ((FIOSAccessibilityContainer*)element).Id;
	for (int32 i = 0; i < self.ChildIds.Num(); ++i)
	{
		if (self.Id == OtherId)
		{
			return i;
		}
	}

	return NSNotFound;
}

@end

@implementation FIOSAccessibilityLeaf

@synthesize Label;
@synthesize Hint;
@synthesize Value;
@synthesize Traits;

-(id)initWithParent:(FIOSAccessibilityContainer*)Parent
{
	if (self = [super initWithAccessibilityContainer : Parent])
	{
		const AccessibleWidgetId ParentId = Parent.Id;
		// All IAccessibleWidget functions must be run on Game Thread
		FFunctionGraphTask::CreateAndDispatchWhenReady([ParentId]()
		{
			TSharedPtr<IAccessibleWidget> Widget = [IOSAppDelegate GetDelegate].IOSApplication->GetAccessibleMessageHandler()->GetAccessibleWidgetFromId(ParentId);
			if (Widget.IsValid())
			{
				// Most accessibility traits cannot be changed after setting, so initialize them here
				UIAccessibilityTraits InitialTraits = UIAccessibilityTraitNone;
				if (Widget->AsProperty() && !FMath::IsNearlyZero(Widget->AsProperty()->GetStepSize()))
				{
					InitialTraits |= UIAccessibilityTraitAdjustable;
				}
				if (Widget->AsActivatable())
				{
					InitialTraits |= UIAccessibilityTraitButton;
				}
				if (Widget->GetWidgetType() == EAccessibleWidgetType::Image)
				{
					InitialTraits |= UIAccessibilityTraitImage;
				}
				if (Widget->GetWidgetType() == EAccessibleWidgetType::Hyperlink)
				{
					InitialTraits |= UIAccessibilityTraitLink;
				}
				if (!Widget->IsEnabled())
				{
					InitialTraits |= UIAccessibilityTraitNotEnabled;
				}

				// All UIKit functions must be run on Main Thread
				dispatch_async(dispatch_get_main_queue(), ^
				{
					FIOSAccessibilityContainer* Element = [[FIOSAccessibilityCache AccessibilityElementCache] GetAccessibilityElement:ParentId];
					[Element GetLeaf].Traits = InitialTraits;
				});
			}
		}, TStatId(), NULL, ENamedThreads::GameThread);
	}
	return self;
}

-(BOOL)isAccessibilityElement
{
	return YES;
}

-(CGRect)accessibilityFrame
{
	return [self.accessibilityContainer accessibilityFrame];
}

/**
 * Since Strings are expensive to copy over and over, we don't update them with polling.
 * When any one of the three are requested, we cache all three in order to restrict it to
 * a single cross-thread call. This will add a slight delay when the user taps on a widget,
 * but at the moment it isn't noticeable. The slower the framerate of the game, the more
 * noticeable this delay may become.
 */
-(void)CacheStrings
{
	const double StartTime = FPlatformTime::Seconds();
	if (StartTime - LastCachedStringTime > 1.0)
	{
		const AccessibleWidgetId TempId = ((FIOSAccessibilityContainer*)self.accessibilityContainer).Id;
		FString TempLabel, TempHint, TempValue;
		// All IAccessibleWidget functions must be run on Game Thread
		[IOSAppDelegate WaitAndRunOnGameThread : [TempId, &TempLabel, &TempHint, &TempValue]()
		{
			TSharedPtr<IAccessibleWidget> Widget = [IOSAppDelegate GetDelegate].IOSApplication->GetAccessibleMessageHandler()->GetAccessibleWidgetFromId(TempId);
			if (Widget.IsValid())
			{
				TempLabel = Widget->GetWidgetName();
				TempHint = Widget->GetHelpText();
				if (Widget->AsProperty())
				{
					TempValue = Widget->AsProperty()->GetValue();
				}
			}
		}];
		self.Label = MoveTemp(TempLabel);
		self.Hint = MoveTemp(TempHint);
		self.Value = MoveTemp(TempValue);
		LastCachedStringTime = FPlatformTime::Seconds();
	}
}

-(NSString*)accessibilityLabel
{
	[self CacheStrings];
	if (!self.Label.IsEmpty())
	{
		return [NSString stringWithFString:self.Label];
	}
	return nil;
}

-(NSString*)accessibilityHint
{
	[self CacheStrings];
	if (!self.Hint.IsEmpty())
	{
		return [NSString stringWithFString:self.Hint];
	}
	return nil;
}

-(NSString*)accessibilityValue
{
	[self CacheStrings];
	if (!self.Value.IsEmpty())
	{
		return [NSString stringWithFString:self.Value];
	}
	return nil;
}

-(UIAccessibilityTraits)accessibilityTraits
{
	return self.Traits;
}

-(void)accessibilityIncrement
{
	const AccessibleWidgetId TempId = ((FIOSAccessibilityContainer*)self.accessibilityContainer).Id;
	// All IAccessibleWidget functions must be run on Game Thread
	FFunctionGraphTask::CreateAndDispatchWhenReady([TempId]()
	{
		TSharedPtr<IAccessibleWidget> Widget = [IOSAppDelegate GetDelegate].IOSApplication->GetAccessibleMessageHandler()->GetAccessibleWidgetFromId(TempId);
		if (Widget.IsValid())
		{
			IAccessibleProperty* Property = Widget->AsProperty();
			if (Property && !FMath::IsNearlyZero(Property->GetStepSize()))
			{
				const float CurrentValue = FCString::Atof(*Property->GetValue());
				Property->SetValue(FString::SanitizeFloat(CurrentValue + Property->GetStepSize()));
			}
		}
	}, TStatId(), NULL, ENamedThreads::GameThread);
}

-(void)accessibilityDecrement
{
	const AccessibleWidgetId TempId = ((FIOSAccessibilityContainer*)self.accessibilityContainer).Id;
	// All IAccessibleWidget functions must be run on Game Thread
	FFunctionGraphTask::CreateAndDispatchWhenReady([TempId]()
	{
		TSharedPtr<IAccessibleWidget> Widget = [IOSAppDelegate GetDelegate].IOSApplication->GetAccessibleMessageHandler()->GetAccessibleWidgetFromId(TempId);
		if (Widget.IsValid())
		{
			IAccessibleProperty* Property = Widget->AsProperty();
			if (Property && !FMath::IsNearlyZero(Property->GetStepSize()))
			{
				const float CurrentValue = FCString::Atof(*Property->GetValue());
				Property->SetValue(FString::SanitizeFloat(CurrentValue - Property->GetStepSize()));
			}
		}
	}, TStatId(), NULL, ENamedThreads::GameThread);
}

@end

#endif
