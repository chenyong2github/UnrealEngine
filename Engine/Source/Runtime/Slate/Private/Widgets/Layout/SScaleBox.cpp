// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Widgets/Layout/SScaleBox.h"
#include "Layout/LayoutUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SViewport.h"
#include "Misc/CoreDelegates.h"
#include "Widgets/SWidget.h"


/* SScaleBox interface
 *****************************************************************************/

void SScaleBox::Construct(const SScaleBox::FArguments& InArgs)
{
	bHasCustomPrepass = true;

	Stretch = InArgs._Stretch;

	StretchDirection = InArgs._StretchDirection;
	UserSpecifiedScale = InArgs._UserSpecifiedScale;
	IgnoreInheritedScale = InArgs._IgnoreInheritedScale;

	LastFinalOffset = FVector2D(0, 0);

	ChildSlot
	.HAlign(InArgs._HAlign)
	.VAlign(InArgs._VAlign)
	[
		InArgs._Content.Widget
	];

#if WITH_EDITOR
	OverrideScreenSize = InArgs._OverrideScreenSize;
	FSlateApplication::Get().OnDebugSafeZoneChanged.AddSP(this, &SScaleBox::DebugSafeAreaUpdated);
#endif

	RefreshSafeZoneScale();
	OnSafeFrameChangedHandle = FCoreDelegates::OnSafeFrameChangedEvent.AddSP(this, &SScaleBox::RefreshSafeZoneScale);
}

SScaleBox::~SScaleBox()
{
	FCoreDelegates::OnSafeFrameChangedEvent.Remove(OnSafeFrameChangedHandle);
}

bool SScaleBox::CustomPrepass(float LayoutScaleMultiplier)
{
	SWidget& ChildSlotWidget = ChildSlot.GetWidget().Get();

	const bool bLocalGeometryRequired = DoesScaleRequireLocalGeometry();
	const bool bNeedsNormalizingPrepass = DoesScaleRequireNormalizingPrepass();

	//bool bCanRenderThisFrame = true;

	// If we need a normalizing prepass, or we've yet to give the child a chance to generate a desired
	// size, do that now.
	if (bNeedsNormalizingPrepass || !LastAllocatedArea.IsSet())
	{
		ChildSlotWidget.SlatePrepass(LayoutScaleMultiplier);
	}

	ContentDesiredSize = ChildSlotWidget.GetDesiredSize();
	TOptional<float> NewComputedContentScale;

	if (bLocalGeometryRequired)
	{
		if (LastAllocatedArea.IsSet())
		{
			NewComputedContentScale = ComputeContentScale(LastPaintGeometry.GetValue());
		}
	}
	else
	{
		// If we don't need the area, send a false geometry.
		static const FGeometry NullGeometry;
		NewComputedContentScale = ComputeContentScale(NullGeometry);
	}

	if (bNeedsNormalizingPrepass)
	{
		ChildSlotWidget.InvalidatePrepass();
	}

	// Extract the incoming scale out of the layout scale if 
	if (NewComputedContentScale.IsSet())
	{
		if (IgnoreInheritedScale.Get(false) && LayoutScaleMultiplier != 0)
		{
			NewComputedContentScale = NewComputedContentScale.GetValue() / LayoutScaleMultiplier;
		}
	}

	ComputedContentScale = NewComputedContentScale;

	return true;
}

bool SScaleBox::DoesScaleRequireNormalizingPrepass() const
{
	const EStretch::Type CurrentStretch = Stretch.Get();
	switch (CurrentStretch)
	{
	case EStretch::None:
	case EStretch::Fill:
	case EStretch::ScaleBySafeZone:
	case EStretch::UserSpecified:
		return false;
	default:
		return true;
	}
}

bool SScaleBox::DoesScaleRequireLocalGeometry() const
{
	const EStretch::Type CurrentStretch = Stretch.Get();
	switch (CurrentStretch)
	{
	case EStretch::None:
	case EStretch::Fill:
	case EStretch::ScaleBySafeZone:
	case EStretch::UserSpecified:
		return false;
	default:
		return true;
	}
}

bool SScaleBox::IsDesiredSizeDependentOnAreaAndScale() const
{
	const EStretch::Type CurrentStretch = Stretch.Get();
	switch (CurrentStretch)
	{
	case EStretch::ScaleToFitX:
	case EStretch::ScaleToFitY:
		return true;
	default:
		return false;
	}
}

float SScaleBox::ComputeContentScale(const FGeometry& PaintGeometry) const
{
	const EStretch::Type CurrentStretch = Stretch.Get();
	const EStretchDirection::Type CurrentStretchDirection = StretchDirection.Get();

	switch (CurrentStretch)
	{
	case EStretch::ScaleBySafeZone:
		return SafeZoneScale;
	case EStretch::UserSpecified:
		return UserSpecifiedScale.Get(1.0f);
	}

	float FinalScale = 1;

	const FVector2D ChildDesiredSize = ChildSlot.GetWidget()->GetDesiredSize();

	if (ChildDesiredSize.X != 0 && ChildDesiredSize.Y != 0)
	{
		switch (CurrentStretch)
		{
		case EStretch::ScaleToFit:
		{
			//FVector2D LocalSnappedArea = PaintGeometry.LocalToRoundedLocal(PaintGeometry.GetLocalSize()) - PaintGeometry.LocalToRoundedLocal(FVector2D(0, 0));
			//FinalScale = FMath::Min(LocalSnappedArea.X / ChildDesiredSize.X, LocalSnappedArea.Y / ChildDesiredSize.Y);
			FinalScale = FMath::Min(PaintGeometry.GetLocalSize().X / ChildDesiredSize.X, PaintGeometry.GetLocalSize().Y / ChildDesiredSize.Y);
			break;
		}
		case EStretch::ScaleToFitX:
			FinalScale = PaintGeometry.GetLocalSize().X / ChildDesiredSize.X;
			break;
		case EStretch::ScaleToFitY:
			FinalScale = PaintGeometry.GetLocalSize().Y / ChildDesiredSize.Y;
			break;
		case EStretch::Fill:
			break;
		case EStretch::ScaleToFill:
			FinalScale = FMath::Max(PaintGeometry.GetLocalSize().X / ChildDesiredSize.X, PaintGeometry.GetLocalSize().Y / ChildDesiredSize.Y);
			break;
		}

		switch (CurrentStretchDirection)
		{
		case EStretchDirection::DownOnly:
			FinalScale = FMath::Min(FinalScale, 1.0f);
			break;
		case EStretchDirection::UpOnly:
			FinalScale = FMath::Max(FinalScale, 1.0f);
			break;
		case EStretchDirection::Both:
			break;
		}
	}

	return FinalScale;
}

void SScaleBox::OnArrangeChildren(const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren) const
{
	const EVisibility ChildVisibility = ChildSlot.GetWidget()->GetVisibility();
	if (ArrangedChildren.Accepts(ChildVisibility))
	{
		const FVector2D AreaSize = AllottedGeometry.GetLocalSize();

		const FVector2D CurrentWidgetDesiredSize = ChildSlot.GetWidget()->GetDesiredSize();
		FVector2D SlotWidgetDesiredSize = CurrentWidgetDesiredSize;

		const EStretch::Type CurrentStretch = Stretch.Get();
		const EStretchDirection::Type CurrentStretchDirection = StretchDirection.Get();

		if (CurrentStretch == EStretch::Fill)
		{
			SlotWidgetDesiredSize = AreaSize;
		}

		if (ComputedContentScale.IsSet())
		{
			LastFinalOffset = FVector2D(0, 0);
			float FinalScale = ComputedContentScale.GetValue();

			// If we're just filling, there's no scale applied, we're just filling the area.
			if (CurrentStretch != EStretch::Fill)
			{
				const FMargin SlotPadding(ChildSlot.SlotPadding.Get());
				AlignmentArrangeResult XResult = AlignChild<Orient_Horizontal>(AreaSize.X, ChildSlot, SlotPadding, FinalScale, false);
				AlignmentArrangeResult YResult = AlignChild<Orient_Vertical>(AreaSize.Y, ChildSlot, SlotPadding, FinalScale, false);

				LastFinalOffset = FVector2D(XResult.Offset, YResult.Offset) / FinalScale;

				// If the layout horizontally is fill, then we need the desired size to be the whole size of the widget, 
				// but scale the inverse of the scale we're applying.
				if (ChildSlot.HAlignment == HAlign_Fill)
				{
					SlotWidgetDesiredSize.X = AreaSize.X / FinalScale;
				}

				// If the layout vertically is fill, then we need the desired size to be the whole size of the widget, 
				// but scale the inverse of the scale we're applying.
				if (ChildSlot.VAlignment == VAlign_Fill)
				{
					SlotWidgetDesiredSize.Y = AreaSize.Y / FinalScale;
				}
			}

			ArrangedChildren.AddWidget(ChildVisibility, AllottedGeometry.MakeChild(
				ChildSlot.GetWidget(),
				LastFinalOffset,
				SlotWidgetDesiredSize,
				FinalScale
			));
		}
	}
}

int32 SScaleBox::OnPaint( const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled ) const
{
	// We need another layout pass if the incoming allocated geometry is different from last frames.
	if (!LastAllocatedArea.IsSet() || !AllottedGeometry.GetLocalSize().Equals(LastAllocatedArea.GetValue()))
	{
		LastAllocatedArea = AllottedGeometry.GetLocalSize();
		LastPaintGeometry = AllottedGeometry;
		const_cast<SScaleBox*>(this)->Invalidate(EInvalidateWidgetReason::Layout);
		const_cast<SScaleBox*>(this)->InvalidatePrepass();
		if (Stretch.Get() == EStretch::UserSpecified)
		{
			const_cast<SScaleBox*>(this)->SlatePrepass(GetPrepassLayoutScaleMultiplier());
		}
	}

	bool bClippingNeeded = false;

	if (GetClipping() == EWidgetClipping::Inherit)
	{
		const EStretch::Type CurrentStretch = Stretch.Get();

		// There are a few stretch modes that require we clip, even if the user didn't set the property.
		switch (CurrentStretch)
		{
		case EStretch::ScaleToFitX:
		case EStretch::ScaleToFitY:
		case EStretch::ScaleToFill:
			bClippingNeeded = true;
			break;
		}
	}

	if (bClippingNeeded)
	{
		OutDrawElements.PushClip(FSlateClippingZone(AllottedGeometry));
		FGeometry HitTestGeometry = AllottedGeometry;
		HitTestGeometry.AppendTransform(FSlateLayoutTransform(Args.GetWindowToDesktopTransform()));
	}

	int32 MaxLayerId = SCompoundWidget::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);

	if (bClippingNeeded)
	{
		OutDrawElements.PopClip();
	}

	return MaxLayerId;
}

void SScaleBox::SetContent(TSharedRef<SWidget> InContent)
{
	ChildSlot
	[
		InContent
	];
}

void SScaleBox::SetHAlign(EHorizontalAlignment HAlign)
{
	if(ChildSlot.HAlignment != HAlign)
	{
		ChildSlot.HAlignment = HAlign;
		Invalidate(EInvalidateWidget::Layout);
	}
}

void SScaleBox::SetVAlign(EVerticalAlignment VAlign)
{
	if(ChildSlot.VAlignment != VAlign)
	{
		ChildSlot.VAlignment = VAlign;
		Invalidate(EInvalidateWidget::Layout);
	}
}

void SScaleBox::SetStretchDirection(EStretchDirection::Type InStretchDirection)
{
	SetAttribute(StretchDirection, TAttribute<EStretchDirection::Type>(InStretchDirection), EInvalidateWidgetReason::Layout);
}

void SScaleBox::SetStretch(EStretch::Type InStretch)
{
	if (SetAttribute(Stretch, TAttribute<EStretch::Type>(InStretch), EInvalidateWidgetReason::Layout))
	{
		RefreshSafeZoneScale();
	}
}

void SScaleBox::SetUserSpecifiedScale(float InUserSpecifiedScale)
{
	SetAttribute(UserSpecifiedScale, TAttribute<float>(InUserSpecifiedScale), EInvalidateWidgetReason::Layout);
}

void SScaleBox::SetIgnoreInheritedScale(bool InIgnoreInheritedScale)
{
	SetAttribute(IgnoreInheritedScale, TAttribute<bool>(InIgnoreInheritedScale), EInvalidateWidgetReason::Layout);
}

FVector2D SScaleBox::ComputeDesiredSize(float InScale) const
{
	if (DoesScaleRequireNormalizingPrepass())
	{
		if (ContentDesiredSize.IsSet())
		{
			FVector2D ContentDesiredSizeValue = ContentDesiredSize.GetValue();

			if (IsDesiredSizeDependentOnAreaAndScale())
			{
				// SUPER SPECIAL CASE - 
				// In the special case that we're only fitting one dimension, we can have the opposite dimension desire the growth of the
				// expected scale, if we can get that extra space, awesome.
				if (ComputedContentScale.IsSet() && ComputedContentScale.GetValue() != 0)
				{
					const EStretch::Type CurrentStretch = Stretch.Get();

					switch (CurrentStretch)
					{
					case EStretch::ScaleToFitX:
						ContentDesiredSizeValue.Y = ContentDesiredSizeValue.Y * ComputedContentScale.GetValue();
						break;
					case EStretch::ScaleToFitY:
						ContentDesiredSizeValue.X = ContentDesiredSizeValue.X * ComputedContentScale.GetValue();
						break;
					}
				}
			}

			// If we require a normalizing pre-pass, we can never allow the scaled content's desired size to affect
			// the area we return that we need, otherwise, we'll be introducing hysteresis.
			return ContentDesiredSizeValue;
		}
	}
	else
	{
		if (ContentDesiredSize.IsSet() && ComputedContentScale.IsSet())
		{
			return ContentDesiredSize.GetValue() * ComputedContentScale.GetValue();
		}
	}

	return SCompoundWidget::ComputeDesiredSize(InScale);
}

float SScaleBox::GetRelativeLayoutScale(const FSlotBase& Child, float LayoutScaleMultiplier) const
{
	return ComputedContentScale.IsSet() ? ComputedContentScale.GetValue() : 1.0f;
}

void SScaleBox::RefreshSafeZoneScale()
{
	float ScaleDownBy = 0.f;
	FMargin SafeMargin;
	FVector2D ScaleBy;
#if WITH_EDITOR
	if (OverrideScreenSize.IsSet() && !OverrideScreenSize.GetValue().IsZero())
	{
		FSlateApplication::Get().GetSafeZoneSize(SafeMargin, OverrideScreenSize.GetValue());
		ScaleBy = OverrideScreenSize.GetValue();
	}
	else
#endif
	{
		if (Stretch.Get() == EStretch::ScaleBySafeZone)
		{
			TSharedPtr<SViewport> GameViewport = FSlateApplication::Get().GetGameViewport();
			if (GameViewport.IsValid())
			{
				TSharedPtr<ISlateViewport> ViewportInterface = GameViewport->GetViewportInterface().Pin();
				if (ViewportInterface.IsValid())
				{
					const FIntPoint ViewportSize = ViewportInterface->GetSize();

					FSlateApplication::Get().GetSafeZoneSize(SafeMargin, ViewportSize);
					ScaleBy = ViewportSize;
				}
			}
		}
	}
	const float SafeZoneScaleX = FMath::Max(SafeMargin.Left, SafeMargin.Right)/ (float)ScaleBy.X;
	const float SafeZoneScaleY = FMath::Max(SafeMargin.Top, SafeMargin.Bottom) / (float)ScaleBy.Y;

	// In order to deal with non-uniform safe-zones we take the largest scale as the amount to scale down by.
	ScaleDownBy = FMath::Max(SafeZoneScaleX, SafeZoneScaleY);

	SafeZoneScale = 1.f - ScaleDownBy;
}

#if WITH_EDITOR

void SScaleBox::DebugSafeAreaUpdated(const FMargin& NewSafeZone, bool bShouldRecacheMetrics)
{
	RefreshSafeZoneScale();
}

void SScaleBox::SetOverrideScreenInformation(TOptional<FVector2D> InScreenSize)
{
	OverrideScreenSize = InScreenSize;
	RefreshSafeZoneScale();
}

#endif