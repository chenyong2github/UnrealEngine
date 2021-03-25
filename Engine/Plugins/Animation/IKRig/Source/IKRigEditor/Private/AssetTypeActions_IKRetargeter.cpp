// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetTypeActions_IKRetargeter.h"
#include "Retargeter/IKRetargeter.h"

//#include "ThumbnailRendering/SceneThumbnailInfo.h"
//#include "ToolMenus.h"

#define LOCTEXT_NAMESPACE "AssetTypeActions"

UClass* FAssetTypeActions_IKRetargeter::GetSupportedClass() const
{
	return UIKRetargeter::StaticClass();
}

// UThumbnailInfo* FAssetTypeActions_IKRigDefinition::GetThumbnailInfo(UObject* Asset) const
// {
// 	UChaosSolver* ChaosSolver = CastChecked<UChaosSolver>(Asset);
// 	return NewObject<USceneThumbnailInfo>(ChaosSolver, NAME_None, RF_Transactional);
// }

void FAssetTypeActions_IKRetargeter::GetActions(const TArray<UObject*>& InObjects, FToolMenuSection& Section)
{
	FAssetTypeActions_Base::GetActions(InObjects, Section);
}

// void FAssetTypeActions_IKRetargeter::OpenAssetEditor(const TArray<UObject*>& InObjects, TSharedPtr<IToolkitHost> EditWithinLevelEditor)
// {
// }

#undef LOCTEXT_NAMESPACE
