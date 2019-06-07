// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#if WITH_ACCESSIBILITY

#include "Widgets/Accessibility/SlateCoreAccessibleWidgets.h"
#include "Widgets/Accessibility/SlateAccessibleWidgetCache.h"
#include "Widgets/Accessibility/SlateAccessibleMessageHandler.h"
#include "Layout/WidgetPath.h"
#include "Widgets/IToolTip.h"
#include "Widgets/SWidget.h"
#include "Widgets/SWindow.h"
#include "Input/HittestGrid.h"
#include "Application/SlateApplicationBase.h"
#include "Application/SlateWindowHelper.h"
#include "Math/NumericLimits.h"

DECLARE_CYCLE_STAT(TEXT("Slate Accessibility: Get Widget At Point"), STAT_AccessibilitySlateGetChildAtPosition, STATGROUP_Accessibility);

FSlateAccessibleWidget::FSlateAccessibleWidget(TWeakPtr<SWidget> InWidget, EAccessibleWidgetType InWidgetType)
	: Widget(InWidget)
	, WidgetType(InWidgetType)
	, SiblingIndex(INDEX_NONE)
	, bChildrenDirty(true)
{
	static AccessibleWidgetId RuntimeIdCounter = 0;
	if (RuntimeIdCounter == TNumericLimits<AccessibleWidgetId>::Max())
	{
		RuntimeIdCounter = TNumericLimits<AccessibleWidgetId>::Min();
	}
	if (RuntimeIdCounter == InvalidAccessibleWidgetId)
	{
		++RuntimeIdCounter;
	}
	Id = RuntimeIdCounter++;
}

FSlateAccessibleWidget::~FSlateAccessibleWidget()
{
}

AccessibleWidgetId FSlateAccessibleWidget::GetId() const
{
	return Id;
}

bool FSlateAccessibleWidget::IsValid() const
{
	return Widget.IsValid();
}

TSharedPtr<SWindow> FSlateAccessibleWidget::GetTopLevelSlateWindow() const
{
	if (Widget.IsValid())
	{
		TSharedPtr<SWidget> WindowWidget = Widget.Pin();
		// todo: fix this for nested windows
		while (WindowWidget.IsValid())
		{
			if (WindowWidget->Advanced_IsWindow())
			{
				return StaticCastSharedPtr<SWindow>(WindowWidget);
			}
			WindowWidget = WindowWidget->GetParentWidget();
		}
	}
	return nullptr;
}

TSharedPtr<IAccessibleWidget> FSlateAccessibleWidget::GetTopLevelWindow() const
{
	return FSlateAccessibleWidgetCache::Get().GetAccessibleWidget(GetTopLevelSlateWindow());
}

FBox2D FSlateAccessibleWidget::GetBounds() const
{
	if (Widget.IsValid())
	{
		const FGeometry& Geometry = Widget.Pin()->GetCachedGeometry();
		return FBox2D(Geometry.GetAbsolutePosition(), Geometry.GetAbsolutePosition() + Geometry.GetAbsoluteSize());
	}
	return FBox2D();
}

FString FSlateAccessibleWidget::GetClassName() const
{
	if (Widget.IsValid())
	{
		// Note: this is technically debug code and not guaranteed to work
		return Widget.Pin()->GetTypeAsString();
	}
	return FString();
}

FString FSlateAccessibleWidget::GetWidgetName() const
{
	if (Widget.IsValid())
	{
		FText AccessibleText = Widget.Pin()->GetAccessibleText();
		if (AccessibleText.IsEmpty())
		{
			TSharedPtr<FTagMetaData> Tag = Widget.Pin()->GetMetaData<FTagMetaData>();
			if (Tag.IsValid())
			{
				return Tag->Tag.ToString();
			}
			else
			{
				return GetClassName();
			}
		}
		else
		{
			return AccessibleText.ToString();
		}
	}
	return FString();
}

FString FSlateAccessibleWidget::GetHelpText() const
{
	if (Widget.IsValid())
	{
		TSharedPtr<IToolTip> ToolTip = Widget.Pin()->GetToolTip();
		if (ToolTip.IsValid())
		{
			return ToolTip->GetContentWidget()->GetAccessibleText().ToString();
		}
	}
	return FString();
}

bool FSlateAccessibleWidget::IsEnabled() const
{
	if (Widget.IsValid())
	{
		return Widget.Pin()->IsEnabled();
	}
	return false;
}

bool FSlateAccessibleWidget::IsHidden() const
{
	if (Widget.IsValid())
	{
		return !Widget.Pin()->GetVisibility().IsVisible();
	}
	return true;
}

bool FSlateAccessibleWidget::SupportsFocus() const
{
	if (Widget.IsValid())
	{
		return Widget.Pin()->SupportsKeyboardFocus();
	}
	return false;
}

bool FSlateAccessibleWidget::HasFocus() const
{
	if (Widget.IsValid())
	{
		return Widget.Pin()->HasKeyboardFocus();
	}
	return false;
}

void FSlateAccessibleWidget::SetFocus()
{
	if (SupportsFocus())
	{
		TSharedPtr<SWindow> WidgetWindow = GetTopLevelSlateWindow();
		if (WidgetWindow.IsValid())
		{
			TArray<TSharedRef<SWindow>> WindowArray;
			WindowArray.Add(WidgetWindow.ToSharedRef());
			FWidgetPath WidgetPath;
			if (FSlateWindowHelper::FindPathToWidget(WindowArray, Widget.Pin().ToSharedRef(), WidgetPath))
			{
				FSlateApplicationBase::Get().SetKeyboardFocus(WidgetPath, EFocusCause::SetDirectly);
			}
		}
	}
}

void FSlateAccessibleWidget::MarkChildrenDirty()
{
	for (int32 i = 0; i < Children.Num(); ++i)
	{
		if (Children[i].IsValid())
		{
			Children[i].Pin()->SiblingIndex = INDEX_NONE;
		}
	}

	Children.Reset();
	bChildrenDirty = true;
}

void FSlateAccessibleWidget::UpdateAllChildren(bool bUpdateRecursively)
{
	if (bChildrenDirty)
	{
		bChildrenDirty = false;
		if (Widget.IsValid())
		{
			TArray<TSharedRef<SWidget>> AccessibleChildren = GetAccessibleChildren(Widget.Pin().ToSharedRef());
			Children.Reset(AccessibleChildren.Num());
			for (int32 i = 0; i < AccessibleChildren.Num(); ++i)
			{
				TSharedPtr<FSlateAccessibleWidget> Child = FSlateAccessibleWidgetCache::Get().GetAccessibleWidget(AccessibleChildren[i]);
				Children.Add(Child);
				Child->Parent = StaticCastSharedRef<FSlateAccessibleWidget>(AsShared());
				Child->SiblingIndex = i;

				if (bUpdateRecursively)
				{
					Child->UpdateAllChildren(true);
				}
			}
		}
	}
}

void FSlateAccessibleWidget::UpdateParent(TSharedPtr<IAccessibleWidget> NewParent)
{
	if (Parent == NewParent)
	{
		return;
	}

	if (Parent.IsValid())
	{
		// Even though we're storing SiblingIndex, we have no guarantee
		// that GetChildren will return the same order after a widget is
		// added or removed, so we have to re-update all indices.
		Parent.Pin()->MarkChildrenDirty();
		FSlateApplicationBase::Get().GetAccessibleMessageHandler()->RaiseEvent(AsShared(), EAccessibleEvent::BeforeRemoveFromParent);
	}

	Parent = StaticCastSharedPtr<FSlateAccessibleWidget>(NewParent);

	if (Parent.IsValid())
	{
		Parent.Pin()->MarkChildrenDirty();
		FSlateApplicationBase::Get().GetAccessibleMessageHandler()->RaiseEvent(AsShared(), EAccessibleEvent::AfterAddToParent);
	}
	else
	{
		SiblingIndex = INDEX_NONE;
	}
}

TSharedPtr<IAccessibleWidget> FSlateAccessibleWidget::GetParent()
{
	if (Parent.IsValid())
	{
		return Parent.Pin();
	}
	return nullptr;
}

TSharedPtr<IAccessibleWidget> FSlateAccessibleWidget::GetNextSibling()
{
	if (Parent.IsValid())
	{
		TSharedPtr<FSlateAccessibleWidget> SharedParent = Parent.Pin();
		SharedParent->UpdateAllChildren();
		if (SiblingIndex >= 0 && SiblingIndex < SharedParent->Children.Num() - 1)
		{
			const TWeakPtr<FSlateAccessibleWidget>& Child = SharedParent->Children[SiblingIndex + 1];
			if (Child.IsValid())
			{
				return Child.Pin();
			}
		}
	}
	return nullptr;
}

TSharedPtr<IAccessibleWidget> FSlateAccessibleWidget::GetPreviousSibling()
{
	if (Parent.IsValid())
	{
		TSharedPtr<FSlateAccessibleWidget> SharedParent = Parent.Pin();
		SharedParent->UpdateAllChildren();
		if (SiblingIndex >= 1 && SiblingIndex < SharedParent->Children.Num())
		{
			const TWeakPtr<FSlateAccessibleWidget>& Child = SharedParent->Children[SiblingIndex - 1];
			if (Child.IsValid())
			{
				return Child.Pin();
			}
		}
	}
	return nullptr;
}

TSharedPtr<IAccessibleWidget> FSlateAccessibleWidget::GetChildAt(int32 Index)
{
	UpdateAllChildren();
	if (Index >= 0 && Index < Children.Num() && Widget.IsValid() && Widget.Pin()->CanChildrenBeAccessible())
	{
		const TWeakPtr<FSlateAccessibleWidget>& Child = Children[Index];
		if (Child.IsValid())
		{
			return Child.Pin();
		}

	}
	return nullptr;
}

int32 FSlateAccessibleWidget::GetNumberOfChildren()
{
	UpdateAllChildren();
	if (Widget.IsValid() && Widget.Pin()->CanChildrenBeAccessible())
	{
		return Children.Num();
	}
	return 0;
}

TArray<TSharedRef<SWidget>> FSlateAccessibleWidget::GetAccessibleChildren(TSharedRef<SWidget> Widget)
{
	TArray<TSharedRef<SWidget>> AccessibleChildren;
	if (!Widget->CanChildrenBeAccessible())
	{
		return AccessibleChildren;
	}

	FChildren* Children = Widget->GetChildren();
	if (Children)
	{
		for (int32 i = 0; i < Children->Num(); ++i)
		{
			TSharedRef<SWidget> Child = Children->GetChildAt(i);
			if (Child->GetAccessibleBehavior() != EAccessibleBehavior::NotAccessible)
			{
				AccessibleChildren.Add(Child);
			}
			else
			{
				AccessibleChildren.Append(GetAccessibleChildren(Child));
			}
		}
	}
	return AccessibleChildren;
}

TSharedPtr<IAccessibleWidget> FSlateAccessibleWidget::GetChildAtUsingGeometry(int32 X, int32 Y)
{
	// This is slow and we should use the HitTest grid when possible.
	if (!IsHidden() && GetBounds().IsInside(FVector2D(X, Y)))
	{
		UpdateAllChildren();
		// Traverse the hierarchy backwards in order to handle the case where widgets are overlaid on top of each other.
		for (int32 i = Children.Num() - 1; i >= 0; --i)
		{
			if (Children[i].IsValid())
			{
				TSharedPtr<IAccessibleWidget> Child = Children[i].Pin()->GetChildAtUsingGeometry(X, Y);
				if (Child.IsValid())
				{
					return Child;
				}
			}
		}
		return AsShared();
	}
	return nullptr;
}

// SWindow
TSharedPtr<FGenericWindow> FSlateAccessibleWindow::GetNativeWindow() const
{
	if (Widget.IsValid())
	{
		return StaticCastSharedPtr<SWindow>(Widget.Pin())->GetNativeWindow();
	}
	return nullptr;
}

TSharedPtr<IAccessibleWidget> FSlateAccessibleWindow::GetChildAtPosition(int32 X, int32 Y)
{
	TSharedPtr<IAccessibleWidget> HitWidget;
	if (Widget.IsValid())
	{
		static const bool UseHitTestGrid = false;

		SCOPE_CYCLE_COUNTER(STAT_AccessibilitySlateGetChildAtPosition);
		if (UseHitTestGrid)
		{
			TSharedPtr<SWindow> SlateWindow = StaticCastSharedPtr<SWindow>(Widget.Pin());
			TArray<FWidgetAndPointer> Hits = SlateWindow->GetHittestGrid()->GetBubblePath(FVector2D(X, Y), 0.0f, false);
			TSharedPtr<SWidget> LastAccessibleWidget = nullptr;
			for (int32 i = 0; i < Hits.Num(); ++i)
			{
				if (Hits[i].Widget->GetAccessibleBehavior() != EAccessibleBehavior::NotAccessible)
				{
					LastAccessibleWidget = Hits[i].Widget;
				}
				if (!Hits[i].Widget->CanChildrenBeAccessible())
				{
					break;
				}
			}
			HitWidget = FSlateAccessibleWidgetCache::Get().GetAccessibleWidget(LastAccessibleWidget);
		}
		else
		{
			HitWidget = GetChildAtUsingGeometry(X, Y);
		}
	}

	return HitWidget;
}

TSharedPtr<IAccessibleWidget> FSlateAccessibleWindow::GetFocusedWidget() const
{
	return FSlateAccessibleWidgetCache::Get().GetAccessibleWidget(FSlateApplicationBase::Get().GetKeyboardFocusedWidget());
}

FString FSlateAccessibleWindow::GetWidgetName() const
{
	if (Widget.IsValid())
	{
		return StaticCastSharedPtr<SWindow>(Widget.Pin())->GetTitle().ToString();
	}
	else
	{
		return FSlateAccessibleWidget::GetWidgetName();
	}
}

void FSlateAccessibleWindow::Close()
{
	if (Widget.IsValid())
	{
		StaticCastSharedPtr<SWindow>(Widget.Pin())->RequestDestroyWindow();
	}
}

bool FSlateAccessibleWindow::SupportsDisplayState(EWindowDisplayState State) const
{
	if (Widget.IsValid())
	{
		switch (State)
		{
		case IAccessibleWindow::EWindowDisplayState::Normal:
			return true;
		case IAccessibleWindow::EWindowDisplayState::Minimize:
			return StaticCastSharedPtr<SWindow>(Widget.Pin())->HasMinimizeBox();
		case IAccessibleWindow::EWindowDisplayState::Maximize:
			return StaticCastSharedPtr<SWindow>(Widget.Pin())->HasMaximizeBox();
		}
	}
	return false;
}

IAccessibleWindow::EWindowDisplayState FSlateAccessibleWindow::GetDisplayState() const
{
	if (Widget.IsValid())
	{
		TSharedPtr<SWindow> Window = StaticCastSharedPtr<SWindow>(Widget.Pin());
		if (Window->IsWindowMaximized())
		{
			return IAccessibleWindow::EWindowDisplayState::Maximize;
		}
		else if (Window->IsWindowMinimized())
		{
			return IAccessibleWindow::EWindowDisplayState::Minimize;
		}
		else
		{
			return IAccessibleWindow::EWindowDisplayState::Normal;
		}
	}
	return IAccessibleWindow::EWindowDisplayState::Normal;
}

void FSlateAccessibleWindow::SetDisplayState(EWindowDisplayState State)
{
	if (Widget.IsValid() && GetDisplayState() != State)
	{
		switch (State)
		{
		case IAccessibleWindow::EWindowDisplayState::Normal:
			StaticCastSharedPtr<SWindow>(Widget.Pin())->Restore();
			break;
		case IAccessibleWindow::EWindowDisplayState::Minimize:
			StaticCastSharedPtr<SWindow>(Widget.Pin())->Minimize();
			break;
		case IAccessibleWindow::EWindowDisplayState::Maximize:
			StaticCastSharedPtr<SWindow>(Widget.Pin())->Maximize();
			break;
		}
	}
}

bool FSlateAccessibleWindow::IsModal() const
{
	if (Widget.IsValid())
	{
		return StaticCastSharedPtr<SWindow>(Widget.Pin())->IsModalWindow();
	}
	return false;
}

// ~

// SImage
FString FSlateAccessibleImage::GetHelpText() const
{
	// todo: See UIA_HelpTextPropertyId on https://docs.microsoft.com/en-us/windows/desktop/winauto/uiauto-supportimagecontroltype
	return FString();
}
// ~

#endif
