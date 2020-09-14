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

UENUM()
enum class EHLODLevelType : uint8
{
	Instancing = 0		UMETA(DisplayName = "Use instancing"),
	MeshMerge = 1		UMETA(DisplayName = "Generate merged mesh"),
	MeshSimplify = 2	UMETA(DisplayName = "Generate simplified mesh")
};

USTRUCT()
struct FHLODLevelSettings
{
	GENERATED_USTRUCT_BODY()

#if WITH_EDITORONLY_DATA
	UPROPERTY(EditAnywhere, Category=HLOD)
	EHLODLevelType LevelType;

	UPROPERTY(EditAnywhere, Category=HLOD, meta = (editcondition = "LevelType == EHLODLevelType::MeshMerge", ShowOnlyInnerProperties))
	FMeshMergingSettings MergeSetting;

	UPROPERTY(EditAnywhere, Category=HLOD, meta = (editcondition = "LevelType == EHLODLevelType::MeshSimplify", ShowOnlyInnerProperties))
	FMeshProxySettings ProxySetting;

	UPROPERTY(EditAnywhere, Category=HLOD, meta = (editcondition = "LevelType == EHLODLevelType::MeshMerge || LevelType == EHLODLevelType::MeshSimplify", ShowOnlyInnerProperties))
	TSoftObjectPtr<UMaterial> FlattenMaterial;

	UPROPERTY(EditAnywhere, Category=HLOD)
	FName TargetGrid;

	UPROPERTY(EditAnywhere, Category=HLOD)
	float LoadingRange;
#endif // WITH_EDITORONLY_DATA
};

UCLASS(Blueprintable, Config=Engine, PerObjectConfig)
class ENGINE_API UHLODLayer : public UObject
{
	GENERATED_UCLASS_BODY()
	
#if WITH_EDITOR
public:
	const TArray<FHLODLevelSettings>& GetLevels() const { return Levels; }

	static TArray<AWorldPartitionHLOD*> GenerateHLODForCell(UWorldPartition* InWorldPartition, FName InCellName, FBox InCellBounds, float InCellLoadingRange, const TSet<FGuid>& InCellActors);

	static UHLODLayer* GetHLODLayer(const AActor* InActor);
	static bool ShouldIncludeInHLOD(UPrimitiveComponent* InComponent, int32 InLevelIndex);
#endif

	static bool ShouldIncludeInHLOD(AActor* InActor);


#if WITH_EDITORONLY_DATA
private:
	UPROPERTY(EditAnywhere, Config, Category = HLOD)
	TArray<FHLODLevelSettings> Levels;
#endif
};
