// Copyright Epic Games, Inc. All Rights Reserved.

#include "RetargetEditor/AssetTypeActions_RetargetPose.h"
#include "Retargeter/IKRetargeter.h"

#include "Animation/AnimationAsset.h"
#include "ContentBrowserMenuContexts.h"
#include "ToolMenus.h"
#include "ThumbnailRendering/SceneThumbnailInfo.h"

#define LOCTEXT_NAMESPACE "AssetTypeActions"

UClass* FAssetTypeActions_RetargetPose::GetSupportedClass() const
{
	return URetargetPose::StaticClass();
}

const TArray<FText>& FAssetTypeActions_RetargetPose::GetSubMenus() const
{
	static const TArray<FText> SubMenus
	{
		LOCTEXT("AnimIKRigSubMenu", "IK Rig")
	};
	return SubMenus;
}

UThumbnailInfo* FAssetTypeActions_RetargetPose::GetThumbnailInfo(UObject* Asset) const
{
	URetargetPose* RetargetPose = CastChecked<URetargetPose>(Asset);
	return NewObject<USceneThumbnailInfo>(RetargetPose, NAME_None, RF_Transactional);
}

void FAssetTypeActions_RetargetPose::GetActions(const TArray<UObject*>& InObjects, FToolMenuSection& Section)
{
	FAssetTypeActions_Base::GetActions(InObjects, Section);
}

#undef LOCTEXT_NAMESPACE
