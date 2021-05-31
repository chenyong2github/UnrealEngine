// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/GridSlot.h"
#include "Components/Widget.h"

/////////////////////////////////////////////////////
// UGridSlot

UGridSlot::UGridSlot(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, Slot(nullptr)
{
	HorizontalAlignment = HAlign_Fill;
	VerticalAlignment = VAlign_Fill;

	Layer = 0;
	Nudge = FVector2D(0, 0);
}

void UGridSlot::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);

	Slot = nullptr;
}

void UGridSlot::BuildSlot(TSharedRef<SGridPanel> GridPanel)
{
	GridPanel->AddSlot(Column, Row, SGridPanel::Layer(Layer))
		.Expose(Slot)
		.Padding(Padding)
		.HAlign(HorizontalAlignment)
		.VAlign(VerticalAlignment)
		.RowSpan(RowSpan)
		.ColumnSpan(ColumnSpan)
		.Nudge(Nudge)
		[
			Content == nullptr ? SNullWidget::NullWidget : Content->TakeWidget()
		];
}

void UGridSlot::SetPadding(FMargin InPadding)
{
	Padding = InPadding;
	if ( Slot )
	{
		Slot->SetPadding(InPadding);
	}
}

void UGridSlot::SetRow(int32 InRow)
{
	Row = InRow;
	if ( Slot )
	{
		Slot->SetRow(InRow);
	}
}

void UGridSlot::SetRowSpan(int32 InRowSpan)
{
	RowSpan = InRowSpan;
	if ( Slot )
	{
		Slot->SetRowSpan(InRowSpan);
	}
}

void UGridSlot::SetColumn(int32 InColumn)
{
	Column = InColumn;
	if ( Slot )
	{
		Slot->SetColumn(InColumn);
	}
}

void UGridSlot::SetColumnSpan(int32 InColumnSpan)
{
	ColumnSpan = InColumnSpan;
	if ( Slot )
	{
		Slot->SetColumnSpan(InColumnSpan);
	}
}

void UGridSlot::SetLayer(int32 InLayer)
{
	Layer = InLayer;
	if (Slot)
	{
		Slot->SetLayer(InLayer);
	}
}

void UGridSlot::SetNudge(FVector2D InNudge)
{
	Nudge = InNudge;
	if ( Slot )
	{
		Slot->SetNudge(InNudge);
	}
}

void UGridSlot::SetHorizontalAlignment(EHorizontalAlignment InHorizontalAlignment)
{
	HorizontalAlignment = InHorizontalAlignment;
	if ( Slot )
	{
		Slot->SetHorizontalAlignment(InHorizontalAlignment);
	}
}

void UGridSlot::SetVerticalAlignment(EVerticalAlignment InVerticalAlignment)
{
	VerticalAlignment = InVerticalAlignment;
	if ( Slot )
	{
		Slot->SetVerticalAlignment(InVerticalAlignment);
	}
}

void UGridSlot::SynchronizeProperties()
{
	SetHorizontalAlignment(HorizontalAlignment);
	SetVerticalAlignment(VerticalAlignment);
	SetPadding(Padding);

	SetRow(Row);
	SetRowSpan(RowSpan);
	SetColumn(Column);
	SetColumnSpan(ColumnSpan);
	SetNudge(Nudge);

	SetLayer(Layer);
}
