// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/Object.h"

#if WITH_EDITOR
#include "Engine/MeshMerging.h"
#endif

#include "HLODLayer.generated.h"

class UWorldPartition;
class AActor;

UENUM()
enum class EHLODLayerType : uint8
{
	Instancing = 0		UMETA(DisplayName = "Instancing"),
	MeshMerge = 1		UMETA(DisplayName = "Merged Mesh"),
	MeshSimplify = 2	UMETA(DisplayName = "Simplified Mesh")
};

UCLASS(Blueprintable, Config=Engine, PerObjectConfig)
class ENGINE_API UHLODLayer : public UObject
{
	GENERATED_UCLASS_BODY()
	
#if WITH_EDITOR
public:
	static UHLODLayer* GetHLODLayer(const AActor* InActor);
	static UHLODLayer* GetHLODLayer(const FWorldPartitionActorDesc& InActorDesc, const UWorldPartition* InWorldPartition);

	uint32 GetCRC() const;
#endif

#if WITH_EDITORONLY_DATA
	EHLODLayerType GetLayerType() const { return LayerType; }
	const FMeshMergingSettings& GetMeshMergeSettings() const { return MeshMergeSettings; }
	const FMeshProxySettings& GetMeshSimplifySettings() const { return MeshSimplifySettings; }
	const TSoftObjectPtr<UMaterial>& GetHLODMaterial() const { return HLODMaterial; }
	FName GetRuntimeGrid(uint32 InHLODLevel) const;
	int32 GetCellSize() const { return bAlwaysLoaded ? 0 : CellSize; }
	float GetLoadingRange() const { return bAlwaysLoaded ? WORLD_MAX : LoadingRange; }
	const TSoftObjectPtr<UHLODLayer>& GetParentLayer() const;
	bool IsAlwaysLoaded() const { return bAlwaysLoaded; }

	static FName GetRuntimeGridName(uint32 InLODLevel, int32 InCellSize, float InLoadingRange);
#endif

#if WITH_EDITORONLY_DATA
private:
	/** Type of HLOD generation to use */
	UPROPERTY(EditAnywhere, Config, Category=HLOD)
	EHLODLayerType LayerType;

	/** Merged mesh generation settings - Used when this layer is of type EHLODLayerType::MeshMerge */
	UPROPERTY(EditAnywhere, Config, Category=HLOD, meta = (EditConditionHides, EditCondition = "LayerType == EHLODLayerType::MeshMerge"))
	FMeshMergingSettings MeshMergeSettings;

	/** Simplified mesh generation settings - Used when this layer is of type EHLODLayerType::MeshSimplify */
	UPROPERTY(EditAnywhere, Config, Category=HLOD, meta = (EditConditionHides, EditCondition = "LayerType == EHLODLayerType::MeshSimplify"))
	FMeshProxySettings MeshSimplifySettings;

	/** Material that will be used by the generated HLOD static mesh */
	UPROPERTY(EditAnywhere, Config, AdvancedDisplay, Category=HLOD, meta = (EditConditionHides, EditCondition = "LayerType == EHLODLayerType::MeshMerge || LayerType == EHLODLayerType::MeshSimplify"))
	TSoftObjectPtr<UMaterial> HLODMaterial;

	/** Whether HLOD actors generated for this layer will be always loaded */
	UPROPERTY(EditAnywhere, Config, Category=HLOD)
	uint32 bAlwaysLoaded : 1;

	/** Cell size of the runtime grid created to encompass HLOD actors generated for this HLOD Layer */
	UPROPERTY(EditAnywhere, Config, Category=HLOD, meta = (EditConditionHides, EditCondition = "!bAlwaysLoaded"))
	int32 CellSize;

	/** Loading range of the runtime grid created to encompass HLOD actors generated for this HLOD Layer */
	UPROPERTY(EditAnywhere, Config, Category=HLOD, meta = (EditConditionHides, EditCondition = "!bAlwaysLoaded"))
	float LoadingRange;

	/** HLOD Layer to assign to the generated HLOD actors */
	UPROPERTY(EditAnywhere, Config, Category=HLOD, meta = (EditConditionHides, EditCondition = "!bAlwaysLoaded"))
	TSoftObjectPtr<UHLODLayer> ParentLayer;
#endif
};
