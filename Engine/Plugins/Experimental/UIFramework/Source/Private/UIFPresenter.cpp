// Copyright Epic Games, Inc. All Rights Reserved.

#include "UIFPresenter.h"
#include "UIFPlayerComponent.h"

#include "Blueprint/GameViewportSubsystem.h"
#include "Components/Widget.h"
#include "GameFramework/PlayerController.h"

void UUIFrameworkGameViewportPresenter::AddToViewport(UWidget* UMGWidget, const FUIFrameworkGameLayerSlot& Slot)
{
	if (UGameViewportSubsystem* Subsystem = UGameViewportSubsystem::Get(GetOuterUUIFrameworkPlayerComponent()->GetWorld()))
	{
		FGameViewportWidgetSlot GameViewportWidgetSlot;
		GameViewportWidgetSlot.ZOrder = Slot.ZOrder;
		if (Slot.Type == EUIFrameworkGameLayerType::Viewport)
		{
			Subsystem->AddWidget(UMGWidget, GameViewportWidgetSlot);
		}
		else
		{
			APlayerController* LocalOwner = GetOuterUUIFrameworkPlayerComponent()->GetPlayerController();
			check(LocalOwner);
			Subsystem->AddWidgetForPlayer(UMGWidget, LocalOwner->GetLocalPlayer(), GameViewportWidgetSlot);
		}
	}
}
