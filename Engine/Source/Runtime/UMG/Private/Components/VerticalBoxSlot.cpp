// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/VerticalBoxSlot.h"
#include "Components/Widget.h"

/////////////////////////////////////////////////////
// UVerticalBoxSlot

UVerticalBoxSlot::UVerticalBoxSlot(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, Slot(nullptr)
{
	HorizontalAlignment = HAlign_Fill;
	VerticalAlignment = VAlign_Fill;
	Size = FSlateChildSize(ESlateSizeRule::Automatic);
}

void UVerticalBoxSlot::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);

	Slot = nullptr;
}

void UVerticalBoxSlot::BuildSlot(TSharedRef<SVerticalBox> VerticalBox)
{
	VerticalBox->AddSlot()
		.Expose(Slot)
		.Padding(Padding)
		.HAlign(HorizontalAlignment)
		.VAlign(VerticalAlignment)
		.SizeParam(UWidget::ConvertSerializedSizeParamToRuntime(Size))
		.Expose(Slot)
		[
			Content == nullptr ? SNullWidget::NullWidget : Content->TakeWidget()
		];
}

void UVerticalBoxSlot::SetPadding(FMargin InPadding)
{
	Padding = InPadding;
	if ( Slot )
	{
		Slot->SetPadding(InPadding);
	}
}

void UVerticalBoxSlot::SetSize(FSlateChildSize InSize)
{
	Size = InSize;
	if ( Slot )
	{
		Slot->SetSizeParam(UWidget::ConvertSerializedSizeParamToRuntime(InSize));
	}
}

void UVerticalBoxSlot::SetHorizontalAlignment(EHorizontalAlignment InHorizontalAlignment)
{
	HorizontalAlignment = InHorizontalAlignment;
	if ( Slot )
	{
		Slot->SetHorizontalAlignment(InHorizontalAlignment);
	}
}

void UVerticalBoxSlot::SetVerticalAlignment(EVerticalAlignment InVerticalAlignment)
{
	VerticalAlignment = InVerticalAlignment;
	if ( Slot )
	{
		Slot->SetVerticalAlignment(InVerticalAlignment);
	}
}

void UVerticalBoxSlot::SynchronizeProperties()
{
	SetPadding(Padding);
	SetSize(Size);
	SetHorizontalAlignment(HorizontalAlignment);
	SetVerticalAlignment(VerticalAlignment);
}
