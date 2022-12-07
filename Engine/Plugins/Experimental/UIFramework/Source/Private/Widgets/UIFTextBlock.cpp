// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/UIFTextBlock.h"

#include "Blueprint/UserWidget.h"
#include "Components/TextBlock.h"
#include "Net/Core/PushModel/PushModel.h"
#include "Net/UnrealNetwork.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(UIFTextBlock)


/**
 *
 */
UUIFrameworkTextBlock::UUIFrameworkTextBlock()
{
	WidgetClass = UTextBlock::StaticClass();
}


void UUIFrameworkTextBlock::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	FDoRepLifetimeParams Params;
	Params.bIsPushBased = true;
	DOREPLIFETIME_WITH_PARAMS_FAST(ThisClass, Text, Params);
	DOREPLIFETIME_WITH_PARAMS_FAST(ThisClass, Justification, Params);
}

void UUIFrameworkTextBlock::LocalOnUMGWidgetCreated()
{
	UTextBlock* TextBlock = CastChecked<UTextBlock>(LocalGetUMGWidget());
	TextBlock->SetText(Text);
	TextBlock->SetJustification(Justification);
}

void UUIFrameworkTextBlock::SetText(FText InText)
{
	Text = InText;
	MARK_PROPERTY_DIRTY_FROM_NAME(ThisClass, Text, this);
}

void UUIFrameworkTextBlock::SetJustification(ETextJustify::Type InJustification)
{
	if (Justification != InJustification)
	{
		Justification = InJustification;
		MARK_PROPERTY_DIRTY_FROM_NAME(ThisClass, Justification, this);
	}
}

void UUIFrameworkTextBlock::OnRep_Text()
{
	if (LocalGetUMGWidget())
	{
		CastChecked<UTextBlock>(LocalGetUMGWidget())->SetText(Text);
	}
}

void UUIFrameworkTextBlock::OnRep_Justification()
{
	if (LocalGetUMGWidget())
	{
		CastChecked<UTextBlock>(LocalGetUMGWidget())->SetJustification(Justification);
	}
}
