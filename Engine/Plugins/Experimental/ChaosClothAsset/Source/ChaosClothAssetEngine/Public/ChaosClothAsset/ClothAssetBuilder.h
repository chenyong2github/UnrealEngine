// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ChaosClothAsset/ClothAsset.h"
#include "Features/IModularFeature.h"

#include "ClothAssetBuilder.generated.h"

class UClothAssetBuilder;

/**
 * A modular interface to provide ways to build a SkeletalMesh LODModel from a cloth asset.
 * This cannot be done in a non Editor build due to the required dependencies,
 * and instead is exposed as a modular feature in order to be called from the ClothAsset engine class.
 */
class CHAOSCLOTHASSETENGINE_API IClothAssetBuilderClassProvider : public IModularFeature
{
public:
	inline static const FName FeatureName = TEXT("IClothAssetBuilderClassProvider");

	virtual TSubclassOf<UClothAssetBuilder> GetClothAssetBuilderClass() const = 0;
};

/** Modular builder base class. */
UCLASS(Abstract)
class CHAOSCLOTHASSETENGINE_API UClothAssetBuilder : public UObject
{
	GENERATED_BODY()

public:
	/** Build a FSkeletalMeshLODModel out of the cloth asset for the specified LOD index. */
	virtual void BuildLod(FSkeletalMeshLODModel& LODModel, const UChaosClothAsset& ClothAsset, int32 LodIndex) const
	PURE_VIRTUAL(UClothAssetBuilder::BuildLod, );
};
