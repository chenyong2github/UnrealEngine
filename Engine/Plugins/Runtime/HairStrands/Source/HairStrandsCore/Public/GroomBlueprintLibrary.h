// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "GroomBlueprintLibrary.generated.h"

// Forward Declare
class UGroomAsset;
class USkeletalMesh;
class UGroomBindingAsset;

UCLASS(meta = (ScriptName = "GroomLibrary"))
class HAIRSTRANDSCORE_API UGroomBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Create a new groom binding asset within the contents space of the project.
	 * @param InDesiredPackagePath The package path to use for the groom binding
	 * @param InGroomAsset Groom asset for binding
	 * @param InSkeletalMesh Skeletal mesh on which the groom should be bound to
	 * @param InNumInterpolationPoints Number of point used for RBF constraing (if used)
	 * @param InSourceSkeletalMeshForTransfer Skeletal mesh on which the groom was authored. This should be used only if the skeletal mesh on which the groom is attached to, does not match the rest pose of the groom
	 */
	UFUNCTION(BlueprintCallable, Category = "Groom")
	static UGroomBindingAsset* CreateNewGroomBindingAssetWithPath(
		const FString& InDesiredPackagePath,
		UGroomAsset* InGroomAsset,
		USkeletalMesh* InSkeletalMesh,
		int32 InNumInterpolationPoints = 100,
		USkeletalMesh* InSourceSkeletalMeshForTransfer = nullptr,
		int32 InMatchingSection = 0);

	/**
	 * Create a new groom binding asset within the contents space of the project. The asset name will be auto generated based on the groom asset name and the skeletal asset name
	 * @param InGroomAsset Groom asset for binding
	 * @param InSkeletalMesh Skeletal mesh on which the groom should be bound to
	 * @param InNumInterpolationPoints (Optional) Number of point used for RBF constraing
	 * @param InSourceSkeletalMeshForTransfer  (Optional) Skeletal mesh on which the groom was authored. This should be used only if the skeletal mesh on which the groom is attached to, does not match the rest pose of the groom
	 */
	UFUNCTION(BlueprintCallable, Category = "Groom")
	static UGroomBindingAsset* CreateNewGroomBindingAsset(
		UGroomAsset* InGroomAsset,
		USkeletalMesh* InSkeletalMesh,
		int32 InNumInterpolationPoints = 100,
		USkeletalMesh* InSourceSkeletalMeshForTransfer = nullptr,
		int32 InMatchingSection = 0);
};