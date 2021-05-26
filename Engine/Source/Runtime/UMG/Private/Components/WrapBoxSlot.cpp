// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/WrapBoxSlot.h"
#include "Components/Widget.h"

/////////////////////////////////////////////////////
// UWrapBoxSlot

UWrapBoxSlot::UWrapBoxSlot(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, Slot(nullptr)
{
	HorizontalAlignment = HAlign_Fill;
	VerticalAlignment = VAlign_Fill;

	bFillEmptySpace = false;
	FillSpanWhenLessThan = 0;
	bForceNewLine = false;
}

void UWrapBoxSlot::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);

	Slot = nullptr;
}

void UWrapBoxSlot::BuildSlot(TSharedRef<SWrapBox> WrapBox)
{
	WrapBox->AddSlot()
		.Expose(Slot)
		.Padding(Padding)
		.HAlign(HorizontalAlignment)
		.VAlign(VerticalAlignment)
		.FillEmptySpace(bFillEmptySpace)
		.FillLineWhenSizeLessThan(FillSpanWhenLessThan == 0 ? TOptional<float>() : TOptional<float>(FillSpanWhenLessThan))
		.ForceNewLine(false)
		.Expose(Slot)
		[
			Content == nullptr ? SNullWidget::NullWidget : Content->TakeWidget()
		];
}

void UWrapBoxSlot::SetPadding(FMargin InPadding)
{
	Padding = InPadding;
	if ( Slot )
	{
		Slot->SetPadding(InPadding);
	}
}

void UWrapBoxSlot::SetFillEmptySpace(bool InbFillEmptySpace)
{
	bFillEmptySpace = InbFillEmptySpace;
	if ( Slot )
	{
		Slot->SetFillEmptySpace(InbFillEmptySpace);
	}
}

void UWrapBoxSlot::SetFillSpanWhenLessThan(float InFillSpanWhenLessThan)
{
	FillSpanWhenLessThan = InFillSpanWhenLessThan;
	if ( Slot )
	{
		Slot->SetFillLineWhenSizeLessThan(InFillSpanWhenLessThan == 0 ? TOptional<float>() : TOptional<float>(InFillSpanWhenLessThan));
	}
}

void UWrapBoxSlot::SetHorizontalAlignment(EHorizontalAlignment InHorizontalAlignment)
{
	HorizontalAlignment = InHorizontalAlignment;
	if ( Slot )
	{
		Slot->SetHorizontalAlignment(InHorizontalAlignment);
	}
}

void UWrapBoxSlot::SetVerticalAlignment(EVerticalAlignment InVerticalAlignment)
{
	VerticalAlignment = InVerticalAlignment;
	if ( Slot )
	{
		Slot->SetVerticalAlignment(InVerticalAlignment);
	}
}

void UWrapBoxSlot::SetNewLine(bool InForceNewLine)
{
	bForceNewLine = InForceNewLine;
	if (Slot)
	{
		Slot->SetForceNewLine(InForceNewLine);
	}
}

void UWrapBoxSlot::SynchronizeProperties()
{
	SetPadding(Padding);
	SetFillEmptySpace(bFillEmptySpace);
	SetFillSpanWhenLessThan(FillSpanWhenLessThan);
	SetHorizontalAlignment(HorizontalAlignment);
	SetVerticalAlignment(VerticalAlignment);
	SetNewLine(bForceNewLine);
}
