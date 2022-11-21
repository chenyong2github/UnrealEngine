// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/Switcher/WidgetConnectionConfig.h"

#include "Blueprint/WidgetTree.h"
#include "UI/Switcher/VCamStateSwitcherWidget.h"

UVCamWidget* FWidgetConnectionConfig::ResolveWidget(UVCamStateSwitcherWidget* OwnerWidget) const
{
	return !HasNoWidgetSet() && ensure(OwnerWidget->WidgetTree)
		? OwnerWidget->WidgetTree->FindWidget<UVCamWidget>(Widget)
		: nullptr;
}
