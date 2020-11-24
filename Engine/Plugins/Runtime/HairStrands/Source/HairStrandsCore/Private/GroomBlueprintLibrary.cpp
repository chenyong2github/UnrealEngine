// Copyright Epic Games, Inc. All Rights Reserved.

#include "GroomBlueprintLibrary.h"
#include "HairStrandsCore.h"
#include "GroomBindingAsset.h"

UGroomBindingAsset* UGroomBlueprintLibrary::CreateNewGroomBindingAssetWithPath(
	const FString& InDesiredPackagePath,
	UGroomAsset* InGroomAsset,
	USkeletalMesh* InSkeletalMesh,
	int32 InNumInterpolationPoints,
	USkeletalMesh* InSourceSkeletalMeshForTransfer,
	int32 InMatchingSection)
{
#if WITH_EDITOR
	if (!InGroomAsset || !InSkeletalMesh)
	{
		return nullptr;
	}

	UGroomBindingAsset* BindingAsset = FHairStrandsCore::CreateGroomBindingAsset(InDesiredPackagePath, nullptr, InGroomAsset, InSourceSkeletalMeshForTransfer, InSkeletalMesh, InNumInterpolationPoints, InMatchingSection);
	if (BindingAsset)
	{
		BindingAsset->Build();
	}
	FHairStrandsCore::SaveAsset(BindingAsset);
	return BindingAsset;
#else
	return nullptr;
#endif
}

UGroomBindingAsset* UGroomBlueprintLibrary::CreateNewGroomBindingAsset(
	UGroomAsset* InGroomAsset,
	USkeletalMesh* InSkeletalMesh,
	int32 InNumInterpolationPoints,
	USkeletalMesh* InSourceSkeletalMeshForTransfer,
	int32 InMatchingSection)
{
#if WITH_EDITOR
	if (!InGroomAsset || !InSkeletalMesh)
	{
		return nullptr;
	}

	UGroomBindingAsset* BindingAsset = FHairStrandsCore::CreateGroomBindingAsset(InGroomAsset, InSourceSkeletalMeshForTransfer, InSkeletalMesh, InNumInterpolationPoints, InMatchingSection);
	if (BindingAsset)
	{
		BindingAsset->Build();
	}
	FHairStrandsCore::SaveAsset(BindingAsset);
	return BindingAsset;
#else
	return nullptr;
#endif
}
