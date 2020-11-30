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
class AWorldPartitionHLOD;
class AActor;
struct FHLODGenerationContext;

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
	static TArray<AWorldPartitionHLOD*> GenerateHLODForCell(UWorldPartition* InWorldPartition, FHLODGenerationContext* Context, FName InCellName, const FBox& InCellBounds, uint32 InLODLevel, const TArray<AActor*>& InCellActors);

	static UHLODLayer* GetHLODLayer(const AActor* InActor);
	static bool ShouldIncludeInHLOD(const UPrimitiveComponent* InComponent, int32 InLevelIndex);
#endif

#if WITH_EDITORONLY_DATA
	EHLODLayerType GetLayerType() const { return LayerType; }
	const FMeshMergingSettings& GetMeshMergeSettings() const { return MeshMergeSettings; }
	const FMeshProxySettings& GetMeshSimplifySettings() const { return MeshSimplifySettings; }
	const TSoftObjectPtr<UMaterial>& GetHLODMaterial() const { return HLODMaterial; }
	FName GetRuntimeGrid(uint32 InHLODLevel) const;
	int32 GetCellSize() const { return CellSize; }
	float GetLoadingRange() const { return LoadingRange; }
	const TSoftObjectPtr<UHLODLayer>& GetParentLayer() const { return ParentLayer; }

	static FName GetRuntimeGridName(uint32 InLODLevel, int32 InCellSize, float InLoadingRange);
#endif

	static bool ShouldIncludeInHLOD(const AActor* InActor);

#if WITH_EDITORONLY_DATA
private:
	/** Type of HLOD generation to use */
	UPROPERTY(EditAnywhere, Config, Category=HLOD)
	EHLODLayerType LayerType;

	/** Merged mesh generation settings - Used when this layer is of type EHLODLayerType::MeshMerge */
	UPROPERTY(EditAnywhere, Config, Category=HLOD, meta = (editcondition = "LayerType == EHLODLayerType::MeshMerge"))
	FMeshMergingSettings MeshMergeSettings;

	/** Simplified mesh generation settings - Used when this layer is of type EHLODLayerType::MeshSimplify */
	UPROPERTY(EditAnywhere, Config, Category=HLOD, meta = (editcondition = "LayerType == EHLODLayerType::MeshSimplify"))
	FMeshProxySettings MeshSimplifySettings;

	/** Material that will be used by the generated HLOD static mesh */
	UPROPERTY(EditAnywhere, Config, Category=HLOD, meta = (editcondition = "LayerType == EHLODLayerType::MeshMerge || LayerType == EHLODLayerType::MeshSimplify"))
	TSoftObjectPtr<UMaterial> HLODMaterial;

	UPROPERTY(EditAnywhere, Config, Category=HLOD)
	int32 CellSize;

	/** Loading range of the RuntimeGrid */
	UPROPERTY(EditAnywhere, Config, Category=HLOD)
	float LoadingRange;

	/** HLODLayer to assign to the generated HLOD actors */
	UPROPERTY(EditAnywhere, Config, Category=HLOD)
	TSoftObjectPtr<UHLODLayer> ParentLayer;
#endif
};
