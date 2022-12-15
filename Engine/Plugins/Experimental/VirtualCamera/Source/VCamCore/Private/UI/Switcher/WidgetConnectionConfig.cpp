// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/Switcher/WidgetConnectionConfig.h"

#include "Blueprint/WidgetTree.h"
#include "LogVCamCore.h"
#include "UI/Switcher/VCamStateSwitcherWidget.h"

#if WITH_EDITOR
#include "BaseWidgetBlueprint.h"
#endif

namespace UE::VCamCore::Private
{
#if WITH_EDITOR
	static UWidgetTree* GetWidgetTreeThroughBlueprintAsset(UUserWidget* ClassDefaultWidget)
	{
		UObject* Blueprint = ClassDefaultWidget->GetClass()->ClassGeneratedBy;
		UBaseWidgetBlueprint* WidgetBlueprint = Cast<UBaseWidgetBlueprint>(Blueprint);
		return WidgetBlueprint
			? WidgetBlueprint->WidgetTree
			: nullptr;
	}
#endif
}

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
		WidgetTree = UE::VCamCore::Private::GetWidgetTreeThroughBlueprintAsset(OwnerWidget);
	}
#endif

	const bool bCanFindWidget = !HasNoWidgetSet() && WidgetTree;
	UE_CLOG(!bCanFindWidget, LogVCamCore, Warning, TEXT("Failed to get tree for widget %s"), *OwnerWidget->GetPathName());
	return bCanFindWidget
		? WidgetTree->FindWidget<UVCamWidget>(Widget)
		: nullptr;
}
