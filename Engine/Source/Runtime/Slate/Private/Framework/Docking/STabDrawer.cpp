// Copyright Epic Games, Inc. All Rights Reserved.

#include "Framework/Docking/STabDrawer.h"
#include "Styling/AppStyle.h"
#include "Styling/SlateTypes.h"
#include "Layout/ArrangedChildren.h"
#include "Framework/Application/SlateApplication.h"

STabDrawer::~STabDrawer()
{
	FSlateThrottleManager::Get().LeaveResponsiveMode(AnimationThrottle);
	FSlateThrottleManager::Get().LeaveResponsiveMode(ResizeThrottleHandle);
}

void STabDrawer::SetCurrentSize(float InSize)
{
	CurrentSize = FMath::Clamp(InSize, MinDrawerSize, TargetDrawerSize);
}

void STabDrawer::Construct(const FArguments& InArgs, TSharedRef<SDockTab> InTab, ETabDrawerOpenDirection InOpenDirection)
{
	OpenDirection = InOpenDirection;

	ForTab = InTab;
	OpenCloseAnimation = FCurveSequence(0.0f, 0.15f, ECurveEaseFunction::QuadOut);

	CurrentSize = 0;

	ShadowOffset = InArgs._ShadowOffset;
	ExpanderSize = 5.0f;

	SplitterStyle = &FAppStyle::Get().GetWidgetStyle<FSplitterStyle>("Splitter");

	MinDrawerSize = InArgs._MinDrawerSize;

	MaxDrawerSize = InArgs._MaxDrawerSize;

	TargetDrawerSize = FMath::Clamp(InArgs._TargetDrawerSize, MinDrawerSize, MaxDrawerSize);

	OnTargetDrawerSizeChanged = InArgs._OnTargetDrawerSizeChanged;
	OnDrawerFocusLost = InArgs._OnDrawerFocusLost;
	OnDrawerClosed = InArgs._OnDrawerClosed;

	BackgroundBrush = FAppStyle::Get().GetBrush("Docking.Sidebar.DrawerBackground");
	ShadowBrush = FAppStyle::Get().GetBrush("Docking.Sidebar.DrawerShadow");
	BorderBrush = FAppStyle::Get().GetBrush("Docking.Sidebar.Border");

	FSlateApplication::Get().OnFocusChanging().AddSP(this, &STabDrawer::OnGlobalFocusChanging);

	bIsResizeHandleHovered = false;
	bIsResizing = false;

	ChildSlot
	[
		InArgs._Content.Widget
	];
}

void STabDrawer::Open()
{
	OpenCloseAnimation.Play(AsShared(), false, OpenCloseAnimation.IsPlaying() ? OpenCloseAnimation.GetSequenceTime() : 0.0f, false);

	if (!OpenCloseTimer.IsValid())
	{
		AnimationThrottle = FSlateThrottleManager::Get().EnterResponsiveMode();
		OpenCloseTimer = RegisterActiveTimer(0.0f, FWidgetActiveTimerDelegate::CreateSP(this, &STabDrawer::UpdateAnimation));
	}
}

void STabDrawer::Close()
{
	if (OpenCloseAnimation.IsForward())
	{
		OpenCloseAnimation.Reverse();
	}

	if (!OpenCloseTimer.IsValid())
	{
		AnimationThrottle = FSlateThrottleManager::Get().EnterResponsiveMode();
		OpenCloseTimer = RegisterActiveTimer(0.0f, FWidgetActiveTimerDelegate::CreateSP(this, &STabDrawer::UpdateAnimation));
	}
}

bool STabDrawer::IsOpen() const
{
	return !OpenCloseAnimation.IsAtStart();
}

const TSharedRef<SDockTab> STabDrawer::GetTab() const
{
	return ForTab.ToSharedRef();
}

bool STabDrawer::SupportsKeyboardFocus() const 
{
	return true;
}

FVector2D STabDrawer::ComputeDesiredSize(float) const
{
	if (OpenDirection == ETabDrawerOpenDirection::Bottom)
	{
		return FVector2D(1.0f, TargetDrawerSize + ShadowOffset.Y);
	}
	else
	{
		return FVector2D(TargetDrawerSize + ShadowOffset.X, 1.0f);
	}
}

void STabDrawer::OnArrangeChildren(const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren) const 
{
	const EVisibility ChildVisibility = ChildSlot.GetWidget()->GetVisibility();
	if (ArrangedChildren.Accepts(ChildVisibility))
	{
		if (OpenDirection == ETabDrawerOpenDirection::Left)
		{
			ArrangedChildren.AddWidget(
				AllottedGeometry.MakeChild(
					ChildSlot.GetWidget(),
					FVector2D(0.0f, ShadowOffset.Y),
					FVector2D(TargetDrawerSize, AllottedGeometry.GetLocalSize().Y - (ShadowOffset.Y * 2))
				)
			);
		}
		else if (OpenDirection == ETabDrawerOpenDirection::Right)
		{
			ArrangedChildren.AddWidget(
				AllottedGeometry.MakeChild(
					ChildSlot.GetWidget(),
					FVector2D(ShadowOffset),
					FVector2D(TargetDrawerSize, AllottedGeometry.GetLocalSize().Y - (ShadowOffset.Y * 2))
				)
			);
		}
		else
		{
			ArrangedChildren.AddWidget(
				AllottedGeometry.MakeChild(
					ChildSlot.GetWidget(),
					ShadowOffset,
					FVector2D(AllottedGeometry.GetLocalSize().X - (ShadowOffset.X * 2), TargetDrawerSize)
				)
			);
		}
		
	}
}

FReply STabDrawer::OnMouseButtonDown(const FGeometry& AllottedGeometry, const FPointerEvent& MouseEvent) 
{
	FReply Reply = FReply::Unhandled();
	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		const FGeometry RenderTransformedChildGeometry = GetRenderTransformedGeometry(AllottedGeometry);
		const FGeometry ResizeHandleGeometry = GetResizeHandleGeometry(AllottedGeometry);

		if (ResizeHandleGeometry.IsUnderLocation(MouseEvent.GetScreenSpacePosition()))
		{
			bIsResizing = true;
			InitialResizeGeometry = ResizeHandleGeometry;
			InitialSizeAtResize = CurrentSize;
			ResizeThrottleHandle = FSlateThrottleManager::Get().EnterResponsiveMode();

			Reply = FReply::Handled().CaptureMouse(SharedThis(this));
		}
	}

	return Reply;

}

FReply STabDrawer::OnMouseButtonUp(const FGeometry& AllottedGeometry, const FPointerEvent& MouseEvent) 
{
	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && bIsResizing == true)
	{
		bIsResizing = false;
		FSlateThrottleManager::Get().LeaveResponsiveMode(ResizeThrottleHandle);

		OnTargetDrawerSizeChanged.ExecuteIfBound(SharedThis(this), TargetDrawerSize);
		return FReply::Handled().ReleaseMouseCapture();
	}
	return FReply::Unhandled();
}

FReply STabDrawer::OnMouseMove(const FGeometry& AllottedGeometry, const FPointerEvent& MouseEvent)
{
	const FGeometry ResizeHandleGeometry = GetResizeHandleGeometry(AllottedGeometry);

	bIsResizeHandleHovered = ResizeHandleGeometry.IsUnderLocation(MouseEvent.GetScreenSpacePosition());

	if (bIsResizing && this->HasMouseCapture() && !MouseEvent.GetCursorDelta().IsZero())
	{
		const FVector2D MousePos = MouseEvent.GetScreenSpacePosition();
		float DeltaSize = 0.0f;

		if (OpenDirection == ETabDrawerOpenDirection::Left)
		{
			DeltaSize = (MousePos - InitialResizeGeometry.GetAbsolutePositionAtCoordinates(FVector2D::ZeroVector)).X;
		}
		else if (OpenDirection == ETabDrawerOpenDirection::Right)
		{
			DeltaSize = (InitialResizeGeometry.GetAbsolutePositionAtCoordinates(FVector2D::ZeroVector) - MousePos).X;
		}
		else
		{
			DeltaSize = (InitialResizeGeometry.GetAbsolutePositionAtCoordinates(FVector2D::ZeroVector) - MousePos).Y;
		}



		TargetDrawerSize = FMath::Clamp(InitialSizeAtResize + DeltaSize, MinDrawerSize, MaxDrawerSize);
		SetCurrentSize(InitialSizeAtResize + DeltaSize);


		return FReply::Handled();
	}
	else
	{
		return FReply::Unhandled();
	}
}

void STabDrawer::OnMouseLeave(const FPointerEvent& MouseEvent)
{
	SCompoundWidget::OnMouseLeave(MouseEvent);

	bIsResizeHandleHovered = false;
}

FCursorReply STabDrawer::OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const
{
	return bIsResizing || bIsResizeHandleHovered ? FCursorReply::Cursor(OpenDirection == ETabDrawerOpenDirection::Bottom ? EMouseCursor::ResizeUpDown : EMouseCursor::ResizeLeftRight) : FCursorReply::Unhandled();
}

int32 STabDrawer::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	static FSlateColor ShadowColor = FAppStyle::Get().GetSlateColor("Colors.Foldout");

	const FGeometry RenderTransformedChildGeometry = GetRenderTransformedGeometry(AllottedGeometry);
	const FGeometry ResizeHandleGeometry = GetResizeHandleGeometry(AllottedGeometry);

	FPaintGeometry OffsetPaintGeom;
	if (OpenDirection == ETabDrawerOpenDirection::Left)
	{
		OffsetPaintGeom = RenderTransformedChildGeometry.ToPaintGeometry(FVector2D(0.0f, ShadowOffset.Y), FVector2D(TargetDrawerSize, AllottedGeometry.GetLocalSize().Y - (ShadowOffset.Y * 2)));
	}
	else if (OpenDirection == ETabDrawerOpenDirection::Right)
	{
		OffsetPaintGeom = RenderTransformedChildGeometry.ToPaintGeometry(ShadowOffset, FVector2D(TargetDrawerSize, AllottedGeometry.GetLocalSize().Y - (ShadowOffset.Y * 2)));
	}
	else
	{
		OffsetPaintGeom = RenderTransformedChildGeometry.ToPaintGeometry(ShadowOffset, FVector2D(AllottedGeometry.GetLocalSize().X - (ShadowOffset.X * 2), TargetDrawerSize));
	}

	// Draw the resize handle
	if (bIsResizing || bIsResizeHandleHovered)
	{
		const FSlateBrush* SplitterBrush = &SplitterStyle->HandleHighlightBrush;
		FSlateDrawElement::MakeBox(
			OutDrawElements,
			LayerId,
			ResizeHandleGeometry.ToPaintGeometry(),
			SplitterBrush,
			ESlateDrawEffect::None,
			SplitterBrush->GetTint(InWidgetStyle));
	}

	// Main Shadow
	FSlateDrawElement::MakeBox(
		OutDrawElements,
		LayerId,
		RenderTransformedChildGeometry.ToPaintGeometry(),
		ShadowBrush,
		ESlateDrawEffect::None,
		ShadowBrush->GetTint(InWidgetStyle));


	// Background
	FSlateDrawElement::MakeBox(
		OutDrawElements,
		LayerId,
		OffsetPaintGeom,
		BackgroundBrush,
		ESlateDrawEffect::None,
		BackgroundBrush->GetTint(InWidgetStyle));

	int32 OutLayerId = SCompoundWidget::OnPaint(Args, RenderTransformedChildGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);

	// Top border
	FSlateDrawElement::MakeBox(
		OutDrawElements,
		OutLayerId,
		OffsetPaintGeom,
		BorderBrush,
		ESlateDrawEffect::None,
		BorderBrush->GetTint(InWidgetStyle));

	return OutLayerId+1;

}

FGeometry STabDrawer::GetRenderTransformedGeometry(const FGeometry& AllottedGeometry) const
{
	if(OpenDirection == ETabDrawerOpenDirection::Left)
	{
		return AllottedGeometry.MakeChild(FSlateRenderTransform(FVector2D(CurrentSize - TargetDrawerSize, 0.0f)));
	}
	else if (OpenDirection == ETabDrawerOpenDirection::Right)
	{
		return AllottedGeometry.MakeChild(FSlateRenderTransform(FVector2D(TargetDrawerSize - CurrentSize, 0.0f)));
	}
	else
	{
		return AllottedGeometry.MakeChild(FSlateRenderTransform(FVector2D(0.0f, TargetDrawerSize - CurrentSize)));
	}
}

FGeometry STabDrawer::GetResizeHandleGeometry(const FGeometry& AllottedGeometry) const
{
	FGeometry RenderTransformedGeometry = GetRenderTransformedGeometry(AllottedGeometry);

	if (OpenDirection == ETabDrawerOpenDirection::Left)
	{
		return RenderTransformedGeometry.MakeChild((FVector2D(RenderTransformedGeometry.GetLocalSize().X-ShadowOffset.X, ShadowOffset.Y)), FVector2D(ExpanderSize, AllottedGeometry.GetLocalSize().Y - ShadowOffset.Y * 2));
	}
	else if (OpenDirection == ETabDrawerOpenDirection::Right)
	{
		return RenderTransformedGeometry.MakeChild(ShadowOffset - FVector2D(ExpanderSize, 0.0f), FVector2D(ExpanderSize, AllottedGeometry.GetLocalSize().Y - ShadowOffset.Y * 2));
	}
	else
	{
		return RenderTransformedGeometry.MakeChild(ShadowOffset - FVector2D(0.0f, ExpanderSize), FVector2D(AllottedGeometry.GetLocalSize().X - ShadowOffset.X * 2, ExpanderSize));
	}
}

EActiveTimerReturnType STabDrawer::UpdateAnimation(double CurrentTime, float DeltaTime)
{
	SetCurrentSize(FMath::Lerp(0.0f, TargetDrawerSize, OpenCloseAnimation.GetLerp()));

	if (!OpenCloseAnimation.IsPlaying())
	{
		if (OpenCloseAnimation.IsAtStart())
		{
			OnDrawerClosed.ExecuteIfBound(SharedThis(this));
		}

		FSlateThrottleManager::Get().LeaveResponsiveMode(AnimationThrottle);
		OpenCloseTimer.Reset();
		return EActiveTimerReturnType::Stop;
	}

	return EActiveTimerReturnType::Continue;
}

static bool IsLegalWidgetFocused(const FWidgetPath& FocusPath, const TArrayView<TSharedRef<SWidget>> LegalFocusWidgets)
{
	for (const TSharedRef<SWidget>& Widget : LegalFocusWidgets)
	{
		if (FocusPath.ContainsWidget(Widget))
		{
			return true;
		}
	}

	return false;
}

void STabDrawer::OnGlobalFocusChanging(const FFocusEvent& FocusEvent, const FWeakWidgetPath& OldFocusedWidgetPath, const TSharedPtr<SWidget>& OldFocusedWidget, const FWidgetPath& NewFocusedWidgetPath, const TSharedPtr<SWidget>& NewFocusedWidget)
{
	// Sometimes when dismissing focus can change which will trigger this again
	static bool bIsRentrant = false;

	if (!bIsRentrant)
	{
		TGuardValue<bool> RentrancyGuard(bIsRentrant, true);

		TSharedRef<STabDrawer> ThisWidget = SharedThis(this);
		TArray<TSharedRef<SWidget>, TInlineAllocator<4>> LegalFocusWidgets;
		LegalFocusWidgets.Add(ThisWidget);
		LegalFocusWidgets.Add(ChildSlot.GetWidget());

		bool bShouldLoseFocus = false;
		// Do not close due to slow tasks as those opening send window activation events
		if (!GIsSlowTask && !FSlateApplication::Get().GetActiveModalWindow().IsValid() && !IsLegalWidgetFocused(NewFocusedWidgetPath, MakeArrayView(LegalFocusWidgets)))
		{
			if(NewFocusedWidgetPath.IsValid())
			{
				TSharedRef<SWindow> NewWindow = NewFocusedWidgetPath.GetWindow();
				TSharedPtr<SWindow> MyWindow = FSlateApplication::Get().FindWidgetWindow(ThisWidget);

				if (!NewWindow->IsDescendantOf(MyWindow))
				{
					if (TSharedPtr<SWidget> MenuHost = FSlateApplication::Get().GetMenuHostWidget())
					{
						FWidgetPath MenuHostPath;

						// See if the menu being opened is owned by the drawer contents and if so the menu should not be dismissed
						FSlateApplication::Get().GeneratePathToWidgetUnchecked(MenuHost.ToSharedRef(), MenuHostPath);
						if (!MenuHostPath.ContainsWidget(ChildSlot.GetWidget()))
						{
							bShouldLoseFocus = true;
						}
					}
					else
					{
						bShouldLoseFocus = true;
					}
				}
			}
			else
			{
				bShouldLoseFocus = true;
			}
		}

		if (bShouldLoseFocus)
		{
			OnDrawerFocusLost.ExecuteIfBound(ThisWidget);
		}
	}
}
