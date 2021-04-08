// Copyright Epic Games, Inc. All Rights Reserved.

#include "CommonButtonTypes.h"

//////////////////////////////////////////////////////////////////////////
// SCommonButton
//////////////////////////////////////////////////////////////////////////

FReply SCommonButton::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	return IsInteractable() ? SButton::OnMouseButtonDown(MyGeometry, MouseEvent) : FReply::Handled();
}

FReply SCommonButton::OnMouseButtonDoubleClick(const FGeometry& InMyGeometry, const FPointerEvent& InMouseEvent)
{
	if (!IsInteractable())
	{
		return FReply::Handled();
	}

	if (OnDoubleClicked.IsBound())
	{
		FReply Reply = OnDoubleClicked.Execute();
		if (Reply.IsEventHandled())
		{
			return Reply;
		}
	}

	return SButton::OnMouseButtonDoubleClick(InMyGeometry, InMouseEvent);
}

FReply SCommonButton::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	FReply Reply = FReply::Handled();
	if (!IsInteractable())
	{
		if (HasMouseCapture())
		{
			// It's conceivable that interaction was disabled while this button had mouse capture
			// If that's the case, we want to release it (without acknowledging the click)
			Release();
			Reply.ReleaseMouseCapture();
		}
	}
	else
	{
		Reply = SButton::OnMouseButtonUp(MyGeometry, MouseEvent);
	}

	return Reply;
}

void SCommonButton::OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (!MouseEvent.IsTouchEvent())
	{
		SButton::OnMouseEnter(MyGeometry, MouseEvent);
	}
}

void SCommonButton::OnMouseLeave(const FPointerEvent& MouseEvent)
{
	if (MouseEvent.IsTouchEvent())
	{
		if (HasMouseCapture())
		{
			Release();
		}
	}
	else
	{
		SButton::OnMouseLeave(MouseEvent);
	}
}

FReply SCommonButton::OnTouchMoved(const FGeometry& MyGeometry, const FPointerEvent& InTouchEvent)
{
	FReply Reply = FReply::Handled();

	if (HasMouseCapture())
	{
		if (!MyGeometry.IsUnderLocation(InTouchEvent.GetScreenSpacePosition()))
		{
			Release();
			Reply.ReleaseMouseCapture();
		}
	}
	else
	{
		Reply = SButton::OnTouchMoved(MyGeometry, InTouchEvent);
	}

	return Reply;
}

FReply SCommonButton::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Enter)
	{
		return FReply::Unhandled();
	}
	return SButton::OnKeyDown(MyGeometry, InKeyEvent);
}

FReply SCommonButton::OnKeyUp(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Enter)
	{
		return FReply::Unhandled();
	}
	return SButton::OnKeyUp(MyGeometry, InKeyEvent);
}

bool SCommonButton::IsHovered() const
{
	return bIsInteractionEnabled ? SButton::IsHovered() : false;
}

bool SCommonButton::IsPressed() const
{
	return IsInteractable() ? SButton::IsPressed() : false;
}

void SCommonButton::SetIsButtonEnabled(bool bInIsButtonEnabled)
{
	bIsButtonEnabled = bInIsButtonEnabled;
}

void SCommonButton::SetIsButtonFocusable(bool bInIsButtonFocusable)
{
	bIsFocusable = bInIsButtonFocusable;
}

void SCommonButton::SetIsInteractionEnabled(bool bInIsInteractionEnabled)
{
	if (bIsInteractionEnabled == bInIsInteractionEnabled)
	{
		return;
	}

	const bool bWasHovered = IsHovered();

	bIsInteractionEnabled = bInIsInteractionEnabled;

	// If the hover state changed due to an interactability change, trigger external logic accordingly.
	const bool bIsHoveredNow = IsHovered();
	if (bWasHovered != bIsHoveredNow)
	{
		if (bIsHoveredNow)
		{
			OnHovered.ExecuteIfBound();
		}
		else
		{
			OnUnhovered.ExecuteIfBound();
		}
	}
}

bool SCommonButton::IsInteractable() const
{
	return bIsButtonEnabled && bIsInteractionEnabled;
}

/** Overridden to fire delegate for external listener */
FReply SCommonButton::OnFocusReceived(const FGeometry& MyGeometry, const FFocusEvent& InFocusEvent)
{
	FReply ReturnReply = SButton::OnFocusReceived(MyGeometry, InFocusEvent);
	OnReceivedFocus.ExecuteIfBound();

	return ReturnReply;
}

int32 SCommonButton::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyClippingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	bool bEnabled = bParentEnabled && bIsButtonEnabled;
	return SButton::OnPaint(Args, AllottedGeometry, MyClippingRect, OutDrawElements, LayerId, InWidgetStyle, bEnabled);
}