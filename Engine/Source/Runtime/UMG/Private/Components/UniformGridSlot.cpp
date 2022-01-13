// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/UniformGridSlot.h"
#include "Components/Widget.h"

/////////////////////////////////////////////////////
// UUniformGridSlot

UUniformGridSlot::UUniformGridSlot(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, Slot(nullptr)
{
	HorizontalAlignment = HAlign_Left;
	VerticalAlignment = VAlign_Top;
}

void UUniformGridSlot::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);

	Slot = nullptr;
}

void UUniformGridSlot::BuildSlot(TSharedRef<SUniformGridPanel> GridPanel)
{
	GridPanel->AddSlot(Column, Row)
		.Expose(Slot)
		.HAlign(HorizontalAlignment)
		.VAlign(VerticalAlignment)
		[
			Content == nullptr ? SNullWidget::NullWidget : Content->TakeWidget()
		];
}

void UUniformGridSlot::SetRow(int32 InRow)
{
	Row = InRow;
	if ( Slot )
	{
		Slot->SetRow(InRow);
	}
}

void UUniformGridSlot::SetColumn(int32 InColumn)
{
	Column = InColumn;
	if ( Slot )
	{
		Slot->SetColumn(InColumn);
	}
}

void UUniformGridSlot::SetHorizontalAlignment(EHorizontalAlignment InHorizontalAlignment)
{
	HorizontalAlignment = InHorizontalAlignment;
	if ( Slot )
	{
		Slot->SetHorizontalAlignment(InHorizontalAlignment);
	}
}

void UUniformGridSlot::SetVerticalAlignment(EVerticalAlignment InVerticalAlignment)
{
	VerticalAlignment = InVerticalAlignment;
	if ( Slot )
	{
		Slot->SetVerticalAlignment(InVerticalAlignment);
	}
}

void UUniformGridSlot::SynchronizeProperties()
{
	SetRow(Row);
	SetColumn(Column);
	SetHorizontalAlignment(HorizontalAlignment);
	SetVerticalAlignment(VerticalAlignment);
}

#if WITH_EDITOR

bool UUniformGridSlot::NudgeByDesigner(const FVector2D& NudgeDirection, const TOptional<int32>& GridSnapSize)
{
	const FVector2D ClampedDirection = NudgeDirection.ClampAxes(-1.0f, 1.0f);
	const int32 NewColumn = Column + ClampedDirection.X;
	const int32 NewRow = Row + ClampedDirection.Y;

	if (NewColumn < 0 || NewRow < 0 || (NewColumn == Column && NewRow == Row))
	{
		return false;
	}

	Modify();

	SetRow(NewRow);
	SetColumn(NewColumn);

	return true;
}

void UUniformGridSlot::SynchronizeFromTemplate(const UPanelSlot* const TemplateSlot)
{
	const ThisClass* const TemplateUniformGridSlot = CastChecked<ThisClass>(TemplateSlot);
	SetRow(TemplateUniformGridSlot->Row);
	SetColumn(TemplateUniformGridSlot->Column);
}

#endif
