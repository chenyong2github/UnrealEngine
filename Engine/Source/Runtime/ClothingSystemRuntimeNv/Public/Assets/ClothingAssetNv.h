// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "ClothingAsset.h"
#include "ClothConfigNv.h"
#include "ClothLODDataNv.h"

#include "ClothingAssetNv.generated.h"

/**
 * NvCloth implementation of a clothing asset.
 *
 * Note: BaseEngine.ini remaps a class named "ClothingAsset" to this class.
 */
UCLASS(hidecategories = Object, BlueprintType)
class CLOTHINGSYSTEMRUNTIMENV_API UClothingAssetNv : public UClothingAssetCommon
{
	GENERATED_BODY()
public:
	UClothingAssetNv(const FObjectInitializer& ObjectInitializer);

	virtual void PostLoad() override;
	virtual void Serialize(FArchive& Ar) override;

#if WITH_EDITOR
	virtual void InvalidateCachedData() override;
	virtual int32 AddNewLod() override;
	virtual void PostEditChangeChainProperty(FPropertyChangedChainEvent& ChainEvent) override;
#endif // WITH_EDITOR

	/** 
	 * Deprecated property for transitioning the \c FClothConfig struct to the 
	 * \c UClothConfig class, in a new property called \c ClothSimConfig.
	 */
	UPROPERTY()
	FClothConfig ClothConfig_DEPRECATED;

	/** 
	 * Deprecated property for transitioning \c FClothLODData struct to the 
	 * \c UClothLODDataNv class, in a new property called \c ClothLodData.
	 */
	UPROPERTY()
	TArray<FClothLODData> LodData_DEPRECATED;
};

