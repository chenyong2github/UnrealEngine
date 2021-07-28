// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "WorldPartition/HLOD/HLODBuilder.h"
#include "Engine/MeshMerging.h"

#include "HLODBuilderMeshMerge.generated.h"

class UMaterial;


UCLASS(Blueprintable, Config = Engine, PerObjectConfig)
class WORLDPARTITIONHLODUTILITIES_API UHLODBuilderMeshMergeSettings : public UHLODBuilderSettings
{
	GENERATED_UCLASS_BODY()

	virtual uint32 GetCRC() const override;

	/** Merged mesh generation settings */
	UPROPERTY(EditAnywhere, Config, Category = HLOD)
	FMeshMergingSettings MeshMergeSettings;

	/** Material that will be used by the generated HLOD static mesh */
	UPROPERTY(EditAnywhere, Config, AdvancedDisplay, Category = HLOD)
	TSoftObjectPtr<UMaterial> HLODMaterial;
};


/**
 * Build a merged mesh using geometry from the provided actors
 */
UCLASS()
class WORLDPARTITIONHLODUTILITIES_API UHLODBuilderMeshMerge : public UHLODBuilder
{
	GENERATED_UCLASS_BODY()

public:
	virtual UHLODBuilderSettings* CreateSettings(UHLODLayer* InHLODLayer) const override;
	virtual TArray<UPrimitiveComponent*> CreateComponents(AWorldPartitionHLOD* InHLODActor, const UHLODLayer* InHLODLayer, const TArray<UPrimitiveComponent*>& InSubComponents) const override;
};
