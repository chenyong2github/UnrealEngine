// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#if WITH_ACCESSIBILITY

#include "IOS/Accessibility/IOSAccessibilityElement.h"

#include "Async/TaskGraphInterfaces.h"
#include "GenericPlatform/Accessibility/GenericAccessibleInterfaces.h"
#include "IOS/Accessibility/IOSAccessibilityCache.h"
#include "IOS/IOSApplication.h"
#include "IOS/IOSAppDelegate.h"
#include "IOS/IOSView.h"

@implementation FIOSAccessibilityContainer

@synthesize Id;
@synthesize ChildIds;
@synthesize Bounds;
@synthesize bIsVisible;

-(id)initWithId:(AccessibleWidgetId)InId
{
	if (self = [super initWithAccessibilityContainer:[IOSAppDelegate GetDelegate].IOSView])
	{
		self.Id = InId;
		self.bIsVisible = true;
		Leaf = [[FIOSAccessibilityLeaf alloc] initWithParent:self];

		// Retrieve parent ID in the background. Things probably won't work quite right until
		// this finishes but it's better than locking up the application for a while.
		FFunctionGraphTask::CreateAndDispatchWhenReady([InId]()
		{
			TSharedPtr<IAccessibleWidget> Widget = [IOSAppDelegate GetDelegate].IOSApplication->GetAccessibleMessageHandler()->GetAccessibleWidgetFromId(InId);
			if (Widget.IsValid())
			{
				TSharedPtr<IAccessibleWidget> Parent = Widget->GetParent();
				if (Parent.IsValid())
				{
					const AccessibleWidgetId ParentId = Parent->GetId();
					// All UIKit functions must be run on Main Thread
					dispatch_async(dispatch_get_main_queue(), ^
					{
						[[[FIOSAccessibilityCache AccessibilityElementCache] GetAccessibilityElement:InId] SetParent:ParentId];
					});
				}
			}
		}, TStatId(), NULL, ENamedThreads::GameThread);
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
	// This function will be called less than the function to cache the bounds, so make
	// the IOS rect here. If we refactor the code to not using a polling-based cache,
	// it may make more sense to change the Bounds property itself to a CGRect.
	return CGRectMake(Bounds.Min.X, Bounds.Min.Y, Bounds.Max.X - Bounds.Min.X, Bounds.Max.Y - Bounds.Min.Y);
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

-(id)accessibilityHitTest:(CGPoint)point
{
	const AccessibleWidgetId TempId = self.Id;
	AccessibleWidgetId FoundId = IAccessibleWidget::InvalidAccessibleWidgetId;

	const float Scale = [IOSAppDelegate GetDelegate].IOSView.contentScaleFactor;
	const int32 X = point.x * Scale;
	const int32 Y = point.y * Scale;
	// Update the labels while we're on the game thread, since IOS is going to request them immediately.
	FString TempLabel, TempHint, TempValue;

	[IOSAppDelegate WaitAndRunOnGameThread:[TempId, X, Y, &FoundId, &TempLabel, &TempHint, &TempValue]()
	{
		const TSharedPtr<IAccessibleWidget> Widget = [IOSAppDelegate GetDelegate].IOSApplication->GetAccessibleMessageHandler()->GetAccessibleWidgetFromId(TempId);
		if (Widget.IsValid())
		{
			const TSharedPtr<IAccessibleWidget> HitWidget = Widget->GetWindow()->AsWindow()->GetChildAtPosition(X, Y);
			if (HitWidget.IsValid())
			{
				FoundId = HitWidget->GetId();
				TempLabel = HitWidget->GetWidgetName();
				TempHint = HitWidget->GetHelpText();
				if (HitWidget->AsProperty())
				{
					TempValue = HitWidget->AsProperty()->GetValue();
				}
			}
		}
	}];

	if (FoundId != IAccessibleWidget::InvalidAccessibleWidgetId)
	{
		FIOSAccessibilityLeaf* FoundLeaf = [[[FIOSAccessibilityCache AccessibilityElementCache] GetAccessibilityElement:FoundId] GetLeaf];
		if ([FoundLeaf ShouldCacheStrings])
		{
			FoundLeaf.Label = MoveTemp(TempLabel);
			FoundLeaf.Hint = MoveTemp(TempHint);
			FoundLeaf.Value = MoveTemp(TempValue);
			FoundLeaf.LastCachedStringTime = FPlatformTime::Seconds();
		}
		return FoundLeaf;
	}
	else
	{
		return Leaf;
	}
}

@end

@implementation FIOSAccessibilityLeaf

@synthesize Label;
@synthesize Hint;
@synthesize Value;
@synthesize Traits;
@synthesize LastCachedStringTime;

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

				FString InitialLabel = Widget->GetWidgetName();
				FString InitialHint = Widget->GetHelpText();
				FString InitialValue;
				if (Widget->AsProperty())
				{
					InitialValue = Widget->AsProperty()->GetValue();
				}

				// All UIKit functions must be run on Main Thread
				dispatch_async(dispatch_get_main_queue(), ^
				{
					FIOSAccessibilityLeaf* Self = [[[FIOSAccessibilityCache AccessibilityElementCache] GetAccessibilityElement:ParentId] GetLeaf];
					Self.Traits = InitialTraits;
					Self.Label = InitialLabel;
					Self.Hint = InitialHint;
					Self.Value = InitialValue;
					Self.LastCachedStringTime = FPlatformTime::Seconds();
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

-(bool)ShouldCacheStrings
{
	return FPlatformTime::Seconds() - LastCachedStringTime > 1.0;
}

-(NSString*)accessibilityLabel
{
	if (!self.Label.IsEmpty())
	{
		return [NSString stringWithFString:self.Label];
	}
	return nil;
}

-(NSString*)accessibilityHint
{
	if (!self.Hint.IsEmpty())
	{
		return [NSString stringWithFString:self.Hint];
	}
	return nil;
}

-(NSString*)accessibilityValue
{
	if (!self.Value.IsEmpty())
	{
		return [NSString stringWithFString:self.Value];
	}
	return nil;
}

-(void)SetAccessibilityTrait:(UIAccessibilityTraits)Trait Set:(bool)IsEnabled
{
	if (IsEnabled)
	{
		self.Traits |= Trait;
	}
	else
	{
		self.Traits &= ~Trait;
	}
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
