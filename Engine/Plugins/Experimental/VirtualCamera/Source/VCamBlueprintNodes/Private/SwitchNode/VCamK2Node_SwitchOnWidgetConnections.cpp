// Copyright Epic Games, Inc. All Rights Reserved.

#include "VCamK2Node_SwitchOnWidgetConnections.h"

#include "UI/VCamWidget.h"

#include "Engine/Blueprint.h"

FText UVCamK2Node_SwitchOnWidgetConnections::GetTooltipText() const
{
	return NSLOCTEXT("VCamEditor", "VCamK2Node.Switch_WidgetConnection.Tooltip", "Selects an output that matches the connection");
}

FText UVCamK2Node_SwitchOnWidgetConnections::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return NSLOCTEXT("VCamEditor", "VCamK2Node.Switch_WidgetConnection.NodeTitle", "Switch on Connection");
}

bool UVCamK2Node_SwitchOnWidgetConnections::SupportsBlueprintClass(UClass* Class) const
{
	return Class->IsChildOf(UVCamWidget::StaticClass());
}

TArray<FName> UVCamK2Node_SwitchOnWidgetConnections::GetPinsToCreate() const
{
	TArray<FName> Result;
	AccessBlueprintCDO([&Result](UVCamWidget* Widget)
	{
		Widget->Connections.GenerateKeyArray(Result);
	});
	return Result;
}

void UVCamK2Node_SwitchOnWidgetConnections::AccessBlueprintCDO(TFunctionRef<void(UVCamWidget*)> Func) const
{
	if (!GetBlueprint())
	{
		return;
	}

	const UBlueprint* Blueprint = GetBlueprint();
	if (!Blueprint || Blueprint->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject))
	{
		return;
	}
	
	UObject* DefaultObject = Blueprint->GeneratedClass->GetDefaultObject();
	if (UVCamWidget* Widget = Cast<UVCamWidget>(DefaultObject))
	{
		Func(Widget);
	}
}
