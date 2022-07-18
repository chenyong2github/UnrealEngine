// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/UIFButton.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"

#include "Net/UnrealNetwork.h"


/**
 * 
 */
UUIFButton::UUIFButton()
{
	WidgetClass = FSoftObjectPath(TEXT("/Game/UEFN/MyButton.MyButton_C"));
}


void UUIFButton::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	FDoRepLifetimeParams Params;
	Params.bIsPushBased = true;
	DOREPLIFETIME(ThisClass, Text);
}


void UUIFButton::OnLocalUserWidgetCreated()
{
	UUIFButtonUserWidget* UserWidget = CastChecked<UUIFButtonUserWidget>(GetWidget());
	UserWidget->TextBlock->SetText(Text);
	UserWidget->Button->OnClicked.AddUniqueDynamic(this, &UUIFButton::ServerClick);
}


void UUIFButton::SetText(FText InText)
{
	Text = InText;
	MARK_PROPERTY_DIRTY_FROM_NAME(ThisClass, Text, this);

	if (GetWidget())
	{
		CastChecked<UUIFButtonUserWidget>(GetWidget())->TextBlock->SetText(InText);
	}
}


void UUIFButton::OnRep_Text()
{
	SetText(Text);
}


void UUIFButton::ServerClick_Implementation()
{
	OnClick.Broadcast(GetOuterAPlayerController());
}
