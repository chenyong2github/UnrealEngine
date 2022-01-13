// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/VerticalBoxSlot.h"
#include "Components/Widget.h"
#include "Components/VerticalBox.h"

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

#if WITH_EDITOR

bool UVerticalBoxSlot::NudgeByDesigner(const FVector2D& NudgeDirection, const TOptional<int32>& GridSnapSize)
{
	if (NudgeDirection.Y == 0)
	{
		return false;
	}
	
	const FVector2D ClampedDirection = NudgeDirection.ClampAxes(-1, 1);
	UVerticalBox* ParentVerticalBox = CastChecked<UVerticalBox>(Parent);

	const int32 CurrentIndex = ParentVerticalBox->GetChildIndex(Content);

	if ((CurrentIndex == 0 && ClampedDirection.Y < 0.0f) ||
		(CurrentIndex + 1 >= ParentVerticalBox->GetChildrenCount() && ClampedDirection.Y > 0.0f))
	{
		return false;
	}

	ParentVerticalBox->Modify();
	ParentVerticalBox->ShiftChild(CurrentIndex + ClampedDirection.Y, Content);

	return true;
}

void UVerticalBoxSlot::SynchronizeFromTemplate(const UPanelSlot* const TemplateSlot)
{
	const ThisClass* const TemplateVerticalBoxSlot = CastChecked<ThisClass>(TemplateSlot);
	const int32 CurrentIndex = TemplateVerticalBoxSlot->Parent->GetChildIndex(TemplateVerticalBoxSlot->Content);

	UVerticalBox* ParentVerticalBox = CastChecked<UVerticalBox>(Parent);
	ParentVerticalBox->ShiftChild(CurrentIndex, Content);
}

#endif
