// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/OverlaySlot.h"
#include "Components/Widget.h"

/////////////////////////////////////////////////////
// UOverlaySlot

UOverlaySlot::UOverlaySlot(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	HorizontalAlignment = HAlign_Left;
	VerticalAlignment = VAlign_Top;
	Slot = nullptr;
}

void UOverlaySlot::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);

	Slot = nullptr;
}

void UOverlaySlot::BuildSlot(TSharedRef<SOverlay> Overlay)
{
	Overlay->AddSlot()
		.Expose(Slot)
		.Padding(Padding)
		.HAlign(HorizontalAlignment)
		.VAlign(VerticalAlignment)
		[
			Content == nullptr ? SNullWidget::NullWidget : Content->TakeWidget()
		];
}

void UOverlaySlot::SetPadding(FMargin InPadding)
{
	Padding = InPadding;
	if ( Slot )
	{
		Slot->SetPadding(InPadding);
	}
}

void UOverlaySlot::SetHorizontalAlignment(EHorizontalAlignment InHorizontalAlignment)
{
	HorizontalAlignment = InHorizontalAlignment;
	if ( Slot )
	{
		Slot->SetHorizontalAlignment(InHorizontalAlignment);
	}
}

void UOverlaySlot::SetVerticalAlignment(EVerticalAlignment InVerticalAlignment)
{
	VerticalAlignment = InVerticalAlignment;
	if ( Slot )
	{
		Slot->SetVerticalAlignment(InVerticalAlignment);
	}
}

void UOverlaySlot::SynchronizeProperties()
{
	SetPadding(Padding);
	SetHorizontalAlignment(HorizontalAlignment);
	SetVerticalAlignment(VerticalAlignment);
}
