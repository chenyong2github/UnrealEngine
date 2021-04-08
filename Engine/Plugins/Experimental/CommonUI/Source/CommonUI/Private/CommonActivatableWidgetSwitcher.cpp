// Copyright Epic Games, Inc. All Rights Reserved.

#include "CommonActivatableWidgetSwitcher.h"
#include "CommonWidgetPaletteCategories.h"
#include "Components/WidgetSwitcherSlot.h"
#include "Components/PanelSlot.h"

void UCommonActivatableWidgetSwitcher::HandleOutgoingWidget()
{
	if (UCommonActivatableWidget* OutgoingActivatable = Cast<UCommonActivatableWidget>(GetWidgetAtIndex(ActiveWidgetIndex)))
	{
		OutgoingActivatable->DeactivateWidget();
	}
}

void UCommonActivatableWidgetSwitcher::HandleSlateActiveIndexChanged(int32 ActiveIndex)
{
	Super::HandleSlateActiveIndexChanged(ActiveIndex);

	if (UCommonActivatableWidget* IncomingActivatable = Cast<UCommonActivatableWidget>(GetWidgetAtIndex(ActiveWidgetIndex)))
	{
		IncomingActivatable->ActivateWidget();
	}
}

