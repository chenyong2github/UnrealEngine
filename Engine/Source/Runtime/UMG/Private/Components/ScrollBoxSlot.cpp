// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/ScrollBoxSlot.h"
#include "Components/Widget.h"

/////////////////////////////////////////////////////
// UScrollBoxSlot

UScrollBoxSlot::UScrollBoxSlot(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, Slot(nullptr)
{
	HorizontalAlignment = HAlign_Fill;
	VerticalAlignment = VAlign_Fill;
}

void UScrollBoxSlot::BuildSlot(TSharedRef<SScrollBox> ScrollBox)
{
	ScrollBox->AddSlot()
		.Padding(Padding)
		.HAlign(HorizontalAlignment)
		.VAlign(VerticalAlignment)
		.Expose(Slot)
		[
			Content == nullptr ? SNullWidget::NullWidget : Content->TakeWidget()
		];
}

void UScrollBoxSlot::SetPadding(FMargin InPadding)
{
	Padding = InPadding;
	if ( Slot )
	{
		Slot->SetPadding(InPadding);
	}
}

void UScrollBoxSlot::SetHorizontalAlignment(EHorizontalAlignment InHorizontalAlignment)
{
	HorizontalAlignment = InHorizontalAlignment;
	if ( Slot )
	{
		Slot->SetHorizontalAlignment(InHorizontalAlignment);
	}
}

void UScrollBoxSlot::SetVerticalAlignment(EVerticalAlignment InVerticalAlignment)
{
	VerticalAlignment = InVerticalAlignment;
	if (Slot)
	{
		Slot->SetVerticalAlignment(InVerticalAlignment);
	}
}

void UScrollBoxSlot::SynchronizeProperties()
{
	SetPadding(Padding);
	SetHorizontalAlignment(HorizontalAlignment);
	SetVerticalAlignment(VerticalAlignment);
}

void UScrollBoxSlot::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	Slot = nullptr;
}
