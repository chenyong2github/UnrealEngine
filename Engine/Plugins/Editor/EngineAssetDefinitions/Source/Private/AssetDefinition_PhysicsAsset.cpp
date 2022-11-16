// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetDefinition_PhysicsAsset.h"
#include "Modules/ModuleManager.h"
#include "PhysicsAssetEditorModule.h"
#include "ThumbnailRendering/SceneThumbnailInfo.h"

#define LOCTEXT_NAMESPACE "AssetTypeActions"

UThumbnailInfo* UAssetDefinition_PhysicsAsset::LoadThumbnailInfo(const FAssetData& InAsset) const
{
	UPhysicsAsset* PhysicsAsset = CastChecked<UPhysicsAsset>(InAsset.GetAsset());
	UThumbnailInfo* ThumbnailInfo = PhysicsAsset->ThumbnailInfo;
	if (ThumbnailInfo == NULL)
	{
		ThumbnailInfo = NewObject<USceneThumbnailInfo>(PhysicsAsset, NAME_None, RF_Transactional);
		PhysicsAsset->ThumbnailInfo = ThumbnailInfo;
	}

	return ThumbnailInfo;
}

EAssetCommandResult UAssetDefinition_PhysicsAsset::OpenAssets(const FAssetOpenArgs& OpenArgs) const
{
	for (UPhysicsAsset* PhysicsAsset : OpenArgs.LoadObjects<UPhysicsAsset>())
	{
		IPhysicsAssetEditorModule* PhysicsAssetEditorModule = &FModuleManager::LoadModuleChecked<IPhysicsAssetEditorModule>( "PhysicsAssetEditor" );
		PhysicsAssetEditorModule->CreatePhysicsAssetEditor(OpenArgs.GetToolkitMode(), OpenArgs.ToolkitHost, PhysicsAsset);
	}

	return EAssetCommandResult::Handled;
}

#undef LOCTEXT_NAMESPACE
