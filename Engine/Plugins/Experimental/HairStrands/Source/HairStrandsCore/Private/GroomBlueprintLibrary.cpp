// Copyright Epic Games, Inc. All Rights Reserved.

#include "GroomBlueprintLibrary.h"
#include "HairStrandsCore.h"
#include "GroomBindingAsset.h"

UGroomBindingAsset* UGroomBlueprintLibrary::CreateNewGroomBindingAssetWithPath(
	const FString& InDesiredPackagePath,
	UGroomAsset* InGroomAsset,
	USkeletalMesh* InSkeletalMesh,
	int32 InNumInterpolationPoints,
	USkeletalMesh* InSourceSkeletalMeshForTransfer)
{
#if WITH_EDITOR
	if (!InGroomAsset || !InSkeletalMesh)
	{
		return nullptr;
	}

	UGroomBindingAsset* BindingAsset = FHairStrandsCore::CreateGroomBindingAsset(InDesiredPackagePath, nullptr, InGroomAsset, InSourceSkeletalMeshForTransfer, InSkeletalMesh, InNumInterpolationPoints);
	if (BindingAsset)
	{
		BindingAsset->Build();
	}
	return BindingAsset;
#else
	return nullptr;
#endif
}

UGroomBindingAsset* UGroomBlueprintLibrary::CreateNewGroomBindingAsset(
	UGroomAsset* InGroomAsset,
	USkeletalMesh* InSkeletalMesh,
	int32 InNumInterpolationPoints,
	USkeletalMesh* InSourceSkeletalMeshForTransfer)
{
#if WITH_EDITOR
	if (!InGroomAsset || !InSkeletalMesh)
	{
		return nullptr;
	}

	UGroomBindingAsset* BindingAsset = FHairStrandsCore::CreateGroomBindingAsset(InGroomAsset, InSourceSkeletalMeshForTransfer, InSkeletalMesh, InNumInterpolationPoints);
	if (BindingAsset)
	{
		BindingAsset->Build();
	}
	return BindingAsset;
#else
	return nullptr;
#endif
}
