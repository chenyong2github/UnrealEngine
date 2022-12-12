// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/UIFSimpleButton.h"
#include "UIFPlayerComponent.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/UIFSimpleButtonUserWidget.h"

#include "Net/UnrealNetwork.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(UIFSimpleButton)


/**
 * 
 */
UUIFrameworkSimpleButton::UUIFrameworkSimpleButton()
{
	WidgetClass = FSoftObjectPath(TEXT("/UIFramework/Widgets/WBP_UIFSimpleButton.WBP_UIFSimpleButton_C"));
}


void UUIFrameworkSimpleButton::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	FDoRepLifetimeParams Params;
	Params.bIsPushBased = true;
	DOREPLIFETIME_WITH_PARAMS_FAST(ThisClass, Text, Params);
}


void UUIFrameworkSimpleButton::LocalOnUMGWidgetCreated()
{
	UUIFrameworkSimpleButtonUserWidget* UserWidget = CastChecked<UUIFrameworkSimpleButtonUserWidget>(LocalGetUMGWidget());
	UserWidget->TextBlock->SetText(Text);
	UserWidget->Button->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleClick);
}


void UUIFrameworkSimpleButton::SetText(FText InText)
{
	Text = InText;
	MARK_PROPERTY_DIRTY_FROM_NAME(ThisClass, Text, this);
	ForceNetUpdate();
}


void UUIFrameworkSimpleButton::OnRep_Text()
{
	if (LocalGetUMGWidget())
	{
		CastChecked<UUIFrameworkSimpleButtonUserWidget>(LocalGetUMGWidget())->TextBlock->SetText(Text);
	}
}


void UUIFrameworkSimpleButton::HandleClick()
{
	// todo the click event should send the userid
	ServerClick(Cast<APlayerController>(GetOuter()));
}


void UUIFrameworkSimpleButton::ServerClick_Implementation(APlayerController* PlayerController)
{
	FUIFrameworkClickEventArgument Argument;
	Argument.PlayerController = PlayerController;
	Argument.Sender = this;
	OnClick.Broadcast(Argument);
}
