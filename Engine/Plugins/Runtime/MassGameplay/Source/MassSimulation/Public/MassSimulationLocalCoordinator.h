// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Stats/Stats.h"
#include "UObject/ObjectMacros.h"
#include "Templates/SubclassOf.h"
#include "UObject/SoftObjectPtr.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Engine/DataTable.h"
#include "MassProcessingTypes.h"
#include "MassCommonTypes.h"
#include "MassSimulationLocalCoordinator.generated.h"


class UMassSchematic;
class UMassEntitySubsystem;
class UHierarchicalInstancedStaticMeshComponent;
class UMassSpawningManager;
class UMassDebugVisualizationComponent;


#if WITH_EDITORONLY_DATA
class UBillboardComponent;
#endif // WITH_EDITORONLY_DATA

// @todo leaving editable properties for the reference during transition period
UCLASS(hidecategories = (Object, Actor, Input, Rendering, LOD, Cooking, Collision, HLOD, Partition), NotPlaceable, Transient)
class MASSSIMULATION_API AMassSimulationLocalCoordinator : public AActor
{
	GENERATED_BODY()
	//----------------------------------------------------------------------//
	// temporary agent config. Will be pulled from spawning components later 
	//----------------------------------------------------------------------//
public:
	UPROPERTY(EditAnywhere, Category = "Mass|Agent", meta=(ClampMin=0, UIMin=0))
	float MaxSpeed = 50;

public:
	AMassSimulationLocalCoordinator(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
	virtual void PostLoad() override;
	
protected:
	// all spawned entities will go through these pipelines
	UPROPERTY(EditAnywhere, Category = "Mass|Spawning")
	TArray<TSoftObjectPtr<UMassSchematic>> PostSpawnSchematics;

	// Every tick these pipelines will be spawned 
	UPROPERTY(EditAnywhere, Category = "Mass|Simulation")
	TArray<TSoftObjectPtr<UMassSchematic>> TickSchematics;

	UPROPERTY(EditAnywhere, Category = "Mass|Spawning", meta=(DisplayName="LEGACY_LocallyAppliedSpawnComponents"))
	TArray<const UScriptStruct*> LocallyAppliedSpawnComponents;
	
	UPROPERTY(EditAnywhere, meta=(ClampMin=100, UIMin=100), Category = "Mass|Simulation")
	float ControlRadius = 5000;

#if WITH_EDITORONLY_DATA
	UPROPERTY(EditAnywhere, Category = "Mass|Simulation")
	uint32 bTickInEditor : 1;

	UPROPERTY(EditAnywhere, Category = "Mass|Debug")
	uint32 bDrawDebug : 1;

private:
	UPROPERTY()
	UBillboardComponent* SpriteComponent;
#endif // WITH_EDITORONLY_DATA
};
