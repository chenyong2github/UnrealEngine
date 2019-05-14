// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#if WITH_ACCESSIBILITY

#include "Widgets/Accessibility/SlateAccessibleMessageHandler.h"
#include "Widgets/Accessibility/SlateAccessibleWidgetCache.h"
#include "Application/SlateApplicationBase.h"
#include "Application/SlateWindowHelper.h"
#include "Widgets/SWidget.h"
#include "Widgets/SWindow.h"
#include "Input/HittestGrid.h"

DECLARE_CYCLE_STAT(TEXT("Slate Accessibility: Parent Updated"), STAT_AccessibilitySlateParentUpdated, STATGROUP_Accessibility);
DECLARE_CYCLE_STAT(TEXT("Slate Accessibility: Children Updated"), STAT_AccessibilitySlateChildrenUpdated, STATGROUP_Accessibility);
DECLARE_CYCLE_STAT(TEXT("Slate Accessibility: Behavior Changed"), STAT_AccessibilitySlateBehaviorChanged, STATGROUP_Accessibility);
DECLARE_CYCLE_STAT(TEXT("Slate Accessibility: Event Raised"), STAT_AccessibilitySlateEventRaised, STATGROUP_Accessibility);

void FSlateAccessibleMessageHandler::OnActivate()
{
	// widgets are initialized when their accessible window is created
}

void FSlateAccessibleMessageHandler::OnDeactivate()
{
	FSlateAccessibleWidgetCache::Get().ClearAll();
}

TSharedPtr<IAccessibleWidget> FSlateAccessibleMessageHandler::GetAccessibleWindow(const TSharedRef<FGenericWindow>& InWindow) const
{
	if (IsActive())
	{
		TSharedPtr<SWindow> SlateWindow = FSlateWindowHelper::FindWindowByPlatformWindow(FSlateApplicationBase::Get().GetTopLevelWindows(), InWindow);
		if (SlateWindow.IsValid())
		{
			return FSlateAccessibleWidgetCache::Get().GetAccessibleWidget(SlateWindow);
		}
	}
	return nullptr;
}

AccessibleWidgetId FSlateAccessibleMessageHandler::GetAccessibleWindowId(const TSharedRef<FGenericWindow>& InWindow) const
{
	TSharedPtr<IAccessibleWidget> AccessibleWindow = GetAccessibleWindow(InWindow);
	if (AccessibleWindow.IsValid())
	{
		return AccessibleWindow->GetId();
	}
	return IAccessibleWidget::InvalidAccessibleWidgetId;
}

TSharedPtr<IAccessibleWidget> FSlateAccessibleMessageHandler::GetAccessibleWidgetFromId(AccessibleWidgetId Id) const
{
	return FSlateAccessibleWidgetCache::Get().GetAccessibleWidgetFromId(Id);
}

void FSlateAccessibleMessageHandler::OnWidgetRemoved(SWidget* Widget)
{
	if (IsActive())
	{
		TSharedPtr<FSlateAccessibleWidget> RemovedWidget = FSlateAccessibleWidgetCache::Get().RemoveWidget(Widget);
		if (RemovedWidget.IsValid())
		{
			RaiseEvent(StaticCastSharedPtr<IAccessibleWidget>(RemovedWidget).ToSharedRef(), EAccessibleEvent::WidgetRemoved);
		}
	}
}

void FSlateAccessibleMessageHandler::OnWidgetParentChanged(TSharedRef<SWidget> Widget)
{
	if (IsActive())
	{
		SCOPE_CYCLE_COUNTER(STAT_AccessibilitySlateParentUpdated);

		TSharedPtr<SWidget> Parent = Widget->GetParentWidget();
		while (Parent.IsValid() && !Parent->IsAccessible())
		{
			Parent = Parent->GetParentWidget();
		}
		
		if (Widget->IsAccessible())
		{
			if (Parent.IsValid())
			{
				FSlateAccessibleWidgetCache::Get().GetAccessibleWidget(Widget)->UpdateParent(FSlateAccessibleWidgetCache::Get().GetAccessibleWidget(Parent));
			}
			else
			{
				FSlateAccessibleWidgetCache::Get().GetAccessibleWidget(Widget)->UpdateParent(nullptr);
			}
		}
		else if (Parent.IsValid() && Parent->CanChildrenBeAccessible())
		{
			TSharedPtr<IAccessibleWidget> AccessibleParent = FSlateAccessibleWidgetCache::Get().GetAccessibleWidget(Parent);
			TArray<TSharedRef<SWidget>> AccessibleChildren = FSlateAccessibleWidget::GetAccessibleChildren(Widget);
			for (int32 i = 0; i < AccessibleChildren.Num(); ++i)
			{
				FSlateAccessibleWidgetCache::Get().GetAccessibleWidget(AccessibleChildren[i])->UpdateParent(AccessibleParent);
			}
		}
	}
}

void FSlateAccessibleMessageHandler::OnWidgetChildrenChanged(TSharedRef<SWidget> Widget)
{
	if (IsActive())
	{
		SCOPE_CYCLE_COUNTER(STAT_AccessibilitySlateChildrenUpdated);

		TSharedPtr<SWidget> Parent = Widget;
		while (Parent.IsValid() && !Parent->IsAccessible())
		{
			Parent = Parent->GetParentWidget();
		}
		if (Parent.IsValid())
		{
			TSharedPtr<IAccessibleWidget> AccessibleParent = FSlateAccessibleWidgetCache::Get().GetAccessibleWidget(Parent);
			StaticCastSharedPtr<FSlateAccessibleWidget>(AccessibleParent)->MarkChildrenDirty();
		}
	}
}

void FSlateAccessibleMessageHandler::OnWidgetAccessibleBehaviorChanged(TSharedRef<SWidget> Widget)
{
	if (IsActive())
	{
		SCOPE_CYCLE_COUNTER(STAT_AccessibilitySlateBehaviorChanged);

		TSharedPtr<SWidget> Parent = Widget->GetParentWidget();
		while (Parent.IsValid())
		{
			if (Parent->IsAccessible())
			{
				break;
			}

			Parent = Parent->GetParentWidget();
		}

		if (Parent.IsValid())
		{
			TSharedPtr<IAccessibleWidget> AccessibleParent = FSlateAccessibleWidgetCache::Get().GetAccessibleWidget(Parent);
			TArray<TSharedRef<SWidget>> AccessibleChildren = FSlateAccessibleWidget::GetAccessibleChildren(Widget);
			if (Widget->IsAccessible())
			{
				TSharedPtr<FSlateAccessibleWidget> AccessibleWidget = StaticCastSharedPtr<FSlateAccessibleWidget>(FSlateAccessibleWidgetCache::Get().GetAccessibleWidget(Widget));
				for (int32 i = 0; i < AccessibleChildren.Num(); ++i)
				{
					FSlateAccessibleWidgetCache::Get().GetAccessibleWidget(AccessibleChildren[i])->UpdateParent(AccessibleWidget);
				}
				AccessibleWidget->UpdateParent(AccessibleParent);
			}
			else
			{
				for (int32 i = 0; i < AccessibleChildren.Num(); ++i)
				{
					FSlateAccessibleWidgetCache::Get().GetAccessibleWidget(AccessibleChildren[i])->UpdateParent(AccessibleParent);
				}
			}
		}
	}
}

void FSlateAccessibleMessageHandler::OnWidgetEventRaised(TSharedRef<SWidget> Widget, EAccessibleEvent Event)
{
	OnWidgetEventRaised(Widget, Event, FVariant(), FVariant());
}

void FSlateAccessibleMessageHandler::OnWidgetEventRaised(TSharedRef<SWidget> Widget, EAccessibleEvent Event, FVariant OldValue, FVariant NewValue)
{
	if (IsActive())
	{
		SCOPE_CYCLE_COUNTER(STAT_AccessibilitySlateEventRaised);
		// todo: not sure what to do for a case like focus changed to not-accessible widget. maybe pass through a nullptr?
		if (Widget->IsAccessible())
		{
			FSlateAccessibleMessageHandler::RaiseEvent(FSlateAccessibleWidgetCache::Get().GetAccessibleWidget(Widget).ToSharedRef(), Event, OldValue, NewValue);
		}
	}
}

#endif
