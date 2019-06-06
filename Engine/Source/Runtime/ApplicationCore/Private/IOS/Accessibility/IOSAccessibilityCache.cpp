// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#if WITH_ACCESSIBILITY

#include "IOS/Accessibility/IOSAccessibilityCache.h"
#include "IOS/Accessibility/IOSAccessibilityElement.h"
#include "IOS/IOSApplication.h"
#include "IOS/IOSAppDelegate.h"
#include "IOS/IOSAsyncTask.h"
#include "IOS/IOSView.h"

@implementation FIOSAccessibilityCache

- (id)init
{
	Cache = [[NSMutableDictionary alloc] init];
	return self;
}

-(void)dealloc
{
	[Cache release];
	[super dealloc];
}

-(FIOSAccessibilityContainer*)GetAccessibilityElement:(AccessibleWidgetId)Id
{
	if (Id == IAccessibleWidget::InvalidAccessibleWidgetId)
	{
		return nil;
	}

	FIOSAccessibilityContainer* ExistingElement = [Cache objectForKey:[NSNumber numberWithInt:Id]];
	if (ExistingElement == nil)
	{
		AccessibleWidgetId ParentId = IAccessibleWidget::InvalidAccessibleWidgetId;
		// All IAccessibleWidget functions must be run on Game Thread
		[IOSAppDelegate WaitAndRunOnGameThread : [Id, &ParentId]()
		{
			TSharedPtr<IAccessibleWidget> Widget = [IOSAppDelegate GetDelegate].IOSApplication->GetAccessibleMessageHandler()->GetAccessibleWidgetFromId(Id);
			if (Widget.IsValid())
			{
				TSharedPtr<IAccessibleWidget> ParentWidget = Widget->GetParent();
				if (ParentWidget.IsValid())
				{
					ParentId = ParentWidget->GetId();
				}
			}
		}];

		ExistingElement = [[[FIOSAccessibilityContainer alloc] initWithId:Id AndParentId:ParentId] autorelease];
		[Cache setObject:ExistingElement forKey:[NSNumber numberWithInt:Id]];
	}
	return ExistingElement;
}

-(bool)AccessibilityElementExists:(AccessibleWidgetId)Id
{
	return [Cache objectForKey:[NSNumber numberWithInt:Id]] != nil;
}

-(void)RemoveAccessibilityElement:(AccessibleWidgetId)Id
{
	[Cache removeObjectForKey:[NSNumber numberWithInt:Id]];
}

-(void)Clear
{
	[Cache removeAllObjects];
}

+(id)AccessibilityElementCache
{
	static FIOSAccessibilityCache* Cache = nil;
	if (Cache == nil)
	{
		Cache = [[self alloc] init];
	}
	return Cache;
}

-(void)UpdateAllCachedProperties
{
	TArray<AccessibleWidgetId> Ids;
	for (NSString* Key in Cache)
	{
		Ids.Add(Key.intValue);
	}

	if (Ids.Num() > 0)
	{
		// All IAccessibleWidget functions must be run on Game Thread
		FFunctionGraphTask::CreateAndDispatchWhenReady([Ids]()
		{
			for (const AccessibleWidgetId Id : Ids)
			{
				TSharedPtr<IAccessibleWidget> Widget = [IOSAppDelegate GetDelegate].IOSApplication->GetAccessibleMessageHandler()->GetAccessibleWidgetFromId(Id);
				if (Widget.IsValid())
				{
					// Children
					TArray<int32> ChildIds;
					for (int32 i = 0; i < Widget->GetNumberOfChildren(); ++i)
					{
						TSharedPtr<IAccessibleWidget> Child = Widget->GetChildAt(i);
						if (Child.IsValid())
						{
							ChildIds.Add(Child->GetId());
						}
					}

					// Bounding rect
					FBox2D Bounds = Widget->GetBounds();
					const float Scale = [IOSAppDelegate GetDelegate].IOSView.contentScaleFactor;
					Bounds.Min /= Scale;
					Bounds.Max /= Scale;

					// Visibility
					const bool bIsEnabled = Widget->IsEnabled();
					const bool bIsVisible = !Widget->IsHidden();

					// All UIKit functions must be run on Main Thread
					dispatch_async(dispatch_get_main_queue(), ^
					{
						FIOSAccessibilityContainer* Element = [[FIOSAccessibilityCache AccessibilityElementCache] GetAccessibilityElement:Id];
						Element.ChildIds = ChildIds;
						Element.Bounds = Bounds;

						[[Element GetLeaf] SetAccessibilityTrait:UIAccessibilityTraitNotEnabled Set:!bIsEnabled];
						Element.bIsVisible = bIsVisible;
					});
				}
			}
		}, TStatId(), NULL, ENamedThreads::GameThread);
	}
}

@end

#endif
