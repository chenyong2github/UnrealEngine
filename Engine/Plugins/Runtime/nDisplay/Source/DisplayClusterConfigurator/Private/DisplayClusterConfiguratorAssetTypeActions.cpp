// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterConfiguratorAssetTypeActions.h"

#include "DisplayClusterConfiguratorEditor.h"
#include "DisplayClusterConfiguratorEditorData.h"

#include "Subsystems/AssetEditorSubsystem.h"

UClass* FDisplayClusterConfiguratorAssetTypeActions::GetSupportedClass() const
{
	return UDisplayClusterConfiguratorEditorData::StaticClass();
}

void FDisplayClusterConfiguratorAssetTypeActions::OpenAssetEditor(const TArray<UObject*>& InObjects, TSharedPtr<class IToolkitHost> EditWithinLevelEditor)
{
	UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
	if (AssetEditorSubsystem)
	{
		UDisplayClusterConfiguratorEditor* AssetEditor = NewObject<UDisplayClusterConfiguratorEditor>(AssetEditorSubsystem, NAME_None, RF_Transient);
		if (AssetEditor)
		{
			AssetEditor->SetObjectsToEdit(InObjects);
			AssetEditor->Initialize();
		}
	}
}

void FDisplayClusterConfiguratorAssetTypeActions::GetResolvedSourceFilePaths(const TArray<UObject*>& TypeAssets, TArray<FString>& OutSourceFilePaths) const
{
	for (const UObject* Asset : TypeAssets)
	{
		if (const UDisplayClusterConfiguratorEditorData* EditingObject = Cast<UDisplayClusterConfiguratorEditorData>(Asset))
		{
			OutSourceFilePaths.Add(EditingObject->PathToConfig);
		}
	}
}

