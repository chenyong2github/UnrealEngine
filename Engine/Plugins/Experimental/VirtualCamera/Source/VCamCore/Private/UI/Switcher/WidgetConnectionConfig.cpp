// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/Switcher/WidgetConnectionConfig.h"

#include "LogVCamCore.h"
#include "UI/Switcher/VCamStateSwitcherWidget.h"
#include "Util/WidgetTreeUtils.h"

#include "Blueprint/WidgetTree.h"

UVCamWidget* FWidgetConnectionConfig::ResolveWidget(UVCamStateSwitcherWidget* OwnerWidget) const
{
	if (!OwnerWidget)
	{
		return nullptr;
	}
	
	UWidgetTree* WidgetTree = OwnerWidget->WidgetTree;
#if WITH_EDITOR
	if (!WidgetTree && OwnerWidget->HasAnyFlags(RF_ClassDefaultObject))
	{
		WidgetTree = UE::VCamCore::GetWidgetTreeThroughBlueprintAsset(*OwnerWidget);
	}
#endif

	const bool bCanFindWidget = !HasNoWidgetSet() && WidgetTree;
	UE_CLOG(!bCanFindWidget, LogVCamCore, Warning, TEXT("Failed to get tree for widget %s"), *OwnerWidget->GetPathName());
	return bCanFindWidget
		? WidgetTree->FindWidget<UVCamWidget>(Widget)
		: nullptr;
}
