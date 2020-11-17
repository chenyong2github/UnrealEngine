// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetTypeActions_ControlRigPose.h"
#include "EditorFramework/AssetImportData.h"
#include "ThumbnailRendering/SceneThumbnailInfo.h"
#include "Tools/ControlRigPose.h"
#include "ToolMenus.h"

FText FAssetTypeActions_ControlRigPose::GetName() const
{
	return NSLOCTEXT("AssetTypeActions", "AssetTypeActions_ControlRigPose", "Control Pose");
}

FColor FAssetTypeActions_ControlRigPose::GetTypeColor() const
{
	return FColor(222, 128, 64);
}

UClass* FAssetTypeActions_ControlRigPose::GetSupportedClass() const
{
	return UControlRigPoseAsset::StaticClass();
}

bool FAssetTypeActions_ControlRigPose::HasActions(const TArray<UObject*>& InObjects) const
{
	return true;
}

void FAssetTypeActions_ControlRigPose::OpenAssetEditor(const TArray<UObject*>& InObjects, TSharedPtr<class IToolkitHost> EditWithinLevelEditor /*= TSharedPtr<IToolkitHost>()*/)
{
	FAssetTypeActions_Base::OpenAssetEditor(InObjects, EditWithinLevelEditor);
}

uint32 FAssetTypeActions_ControlRigPose::GetCategories()
{
	return EAssetTypeCategories::Animation;
}

class UThumbnailInfo* FAssetTypeActions_ControlRigPose::GetThumbnailInfo(UObject* Asset) const
{
	return nullptr; //mz todo it looks like this is not needed if we use the default. Will remove based on feedback.
	/*
	UControlRigPoseAsset* PoseAsset = CastChecked<UControlRigPoseAsset>(Asset);
	UThumbnailInfo* ThumbnailInfo = PoseAsset->ThumbnailInfo;
	if (ThumbnailInfo == NULL)
	{
		ThumbnailInfo = NewObject<USceneThumbnailInfo>(PoseAsset, NAME_None, RF_Transactional);
		PoseAsset->ThumbnailInfo = ThumbnailInfo;
	}
	
	return ThumbnailInfo;
	*/
}

bool FAssetTypeActions_ControlRigPose::IsImportedAsset() const
{
	return false;
}

void FAssetTypeActions_ControlRigPose::GetActions(const TArray<UObject*>& InObjects, FToolMenuSection& Section)
{
	//TODO Add custom Actions based on feedback

//	FAssetTypeActions_Base::GetActions(InObjects, Section);
//	TArray<TWeakObjectPtr<UControlRigPoseAsset>> ControlRigPoseAsset = GetTypedWeakObjectPtrs<UControlRigPoseAsset>(InObjects);
}