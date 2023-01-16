// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosFlesh/Asset/AssetTypeActions_ChaosDeformableSolverAsset.h"

#include "ThumbnailRendering/SceneThumbnailInfo.h"
#include "ChaosFlesh/ChaosDeformableSolverAsset.h"

#define LOCTEXT_NAMESPACE "AssetTypeActions"

UClass* FAssetTypeActions_ChaosDeformableSolver::GetSupportedClass() const
{
	return UChaosDeformableSolver::StaticClass();
}

UThumbnailInfo* FAssetTypeActions_ChaosDeformableSolver::GetThumbnailInfo(UObject* Asset) const
{
	UChaosDeformableSolver* ChaosDeformableSolver = CastChecked<UChaosDeformableSolver>(Asset);
	return NewObject<USceneThumbnailInfo>(ChaosDeformableSolver, NAME_None, RF_Transactional);
}

void FAssetTypeActions_ChaosDeformableSolver::GetActions(const TArray<UObject*>& InObjects, FToolMenuSection& Section)
{
	FAssetTypeActions_Base::GetActions(InObjects, Section);
}

void FAssetTypeActions_ChaosDeformableSolver::OpenAssetEditor(const TArray<UObject*>& InObjects, TSharedPtr<IToolkitHost> EditWithinLevelEditor)
{
}

#undef LOCTEXT_NAMESPACE
