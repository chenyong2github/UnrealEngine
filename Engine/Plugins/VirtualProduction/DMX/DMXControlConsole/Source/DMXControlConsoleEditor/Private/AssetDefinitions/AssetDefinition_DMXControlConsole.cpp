// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetDefinition_DMXControlConsole.h"

#include "DMXControlConsolePreset.h"
#include "DMXControlConsoleEditorModule.h"
#include "Models/DMXControlConsoleEditorPresetModel.h"


#define LOCTEXT_NAMESPACE "AssetDefinition_DMXControlConsole"

FText UAssetDefinition_DMXControlConsole::GetAssetDisplayName() const
{ 
	return LOCTEXT("AssetTypeActions_DMXControlConsole", "DMXControlConsole"); 
}

FLinearColor UAssetDefinition_DMXControlConsole::GetAssetColor() const
{ 
	return FLinearColor(FColor(62, 140, 35)); 
}

TSoftClassPtr<UObject> UAssetDefinition_DMXControlConsole::GetAssetClass() const
{ 
	return UDMXControlConsolePreset::StaticClass(); 
}

TConstArrayView<FAssetCategoryPath> UAssetDefinition_DMXControlConsole::GetAssetCategories() const
{
	return TConstArrayView<FAssetCategoryPath>();
}

EAssetCommandResult UAssetDefinition_DMXControlConsole::OpenAssets(const FAssetOpenArgs& OpenArgs) const
{
	if (!OpenArgs.Assets.IsEmpty())
	{
		UDMXControlConsoleEditorPresetModel* PresetModel = GetMutableDefault<UDMXControlConsoleEditorPresetModel>();
		PresetModel->LoadPreset(OpenArgs.Assets[0]);
		FDMXControlConsoleEditorModule::OpenControlConsole();
	}

	return EAssetCommandResult::Handled;
}

#undef LOCTEXT_NAMESPACE
