// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetTypeActions_IKRigDefinition.h"
#include "IKRigDefinition.h"

//#include "ThumbnailRendering/SceneThumbnailInfo.h"
//#include "ToolMenus.h"

#define LOCTEXT_NAMESPACE "AssetTypeActions"

UClass* FAssetTypeActions_IKRigDefinition::GetSupportedClass() const
{
	return UIKRigDefinition::StaticClass();
}

// UThumbnailInfo* FAssetTypeActions_IKRigDefinition::GetThumbnailInfo(UObject* Asset) const
// {
// 	UChaosSolver* ChaosSolver = CastChecked<UChaosSolver>(Asset);
// 	return NewObject<USceneThumbnailInfo>(ChaosSolver, NAME_None, RF_Transactional);
// }

void FAssetTypeActions_IKRigDefinition::GetActions(const TArray<UObject*>& InObjects, FToolMenuSection& Section)
{
	FAssetTypeActions_Base::GetActions(InObjects, Section);
}

// void FAssetTypeActions_IKRigDefinition::OpenAssetEditor(const TArray<UObject*>& InObjects, TSharedPtr<IToolkitHost> EditWithinLevelEditor)
// {
// }

#undef LOCTEXT_NAMESPACE
