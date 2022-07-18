// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/UIFTextBlock.h"

#include "Blueprint/UserWidget.h"
#include "Components/TextBlock.h"
#include "Net/UnrealNetwork.h"


/**
 *
 */
UUIFTextBlock::UUIFTextBlock()
{
	WidgetClass = UTextBlock::StaticClass();
}


void UUIFTextBlock::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	FDoRepLifetimeParams Params;
	Params.bIsPushBased = true;
	DOREPLIFETIME_WITH_PARAMS_FAST(ThisClass, Text, Params);
}


void UUIFTextBlock::OnLocalUserWidgetCreated()
{
	CastChecked<UTextBlock>(GetWidget())->SetText(Text);
}


void UUIFTextBlock::SetText(FText InText)
{
	Text = InText;
	MARK_PROPERTY_DIRTY_FROM_NAME(ThisClass, Text, this);

	if (GetWidget())
	{
		CastChecked<UTextBlock>(GetWidget())->SetText(InText);
	}
}


void UUIFTextBlock::OnRep_Text()
{
	SetText(Text);
}