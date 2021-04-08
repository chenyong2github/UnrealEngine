// Copyright Epic Games, Inc. All Rights Reserved.

#include "CommonVisibilitySwitcherSlot.h"

#include "Components/Widget.h"
#include "Widgets/Layout/SBox.h"

UCommonVisibilitySwitcherSlot::UCommonVisibilitySwitcherSlot(const FObjectInitializer& Initializer)
	: Super(Initializer)
{
	HorizontalAlignment = EHorizontalAlignment::HAlign_Fill;
	VerticalAlignment = EVerticalAlignment::VAlign_Fill;
}

void UCommonVisibilitySwitcherSlot::BuildSlot(TSharedRef<SOverlay> Overlay)
{
	Slot = &Overlay->AddSlot()
		.Padding(Padding)
		.HAlign(HorizontalAlignment)
		.VAlign(VerticalAlignment)
		[
			SAssignNew(VisibilityBox, SBox)
			[
				Content ? Content->TakeWidget() : SNullWidget::NullWidget
			]
		];
}

void UCommonVisibilitySwitcherSlot::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);

	VisibilityBox.Reset();
}

void UCommonVisibilitySwitcherSlot::SetSlotVisibility(ESlateVisibility Visibility)
{
	if (SBox* Box = VisibilityBox.Get())
	{
		Box->SetVisibility(UWidget::ConvertSerializedVisibilityToRuntime(Visibility));
	}
}
