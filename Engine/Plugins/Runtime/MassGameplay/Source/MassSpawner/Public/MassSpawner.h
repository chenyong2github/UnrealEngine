// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once 

#include "GameFramework/Actor.h"
#include "EnvironmentQuery/EnvQueryTypes.h"
#include "InstancedStruct.h"
#include "Engine/World.h"
#include "MassSpawnerTypes.h"
#include "MassSpawner.generated.h"

class UMassEntitySpawnPointsGeneratorConfigAsset;
class UMassSchematic;
struct FStreamableHandle;

/** A spawner you can put on a map and configure it to spawn different things */
UCLASS(hidecategories = (Object, Actor, Input, Rendering, LOD, Cooking, Collision, HLOD, Partition))
class MASSSPAWNER_API AMassSpawner : public AActor
{
	GENERATED_BODY()
public:
	AMassSpawner();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void PostInitProperties() override;
	virtual void PostLoad() override;
	virtual void PostRegisterAllComponents() override;
	virtual void BeginDestroy() override;

public:
#if WITH_EDITOR
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Debug")
	void DEBUG_Spawn();

	/** Remove all the entities */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Debug")
	void DEBUG_Clear();
#endif // WITH_EDITOR

protected:
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif // WITH_EDITOR

	void RegisterEntityTemplates();

public:
	/**
	 * Starts the spawning of all the agent types of this spawner
	 */
	UFUNCTION(BlueprintCallable, Category = "Spawning")
	void DoSpawning();

	/**
	 * Despawn all mass agent that was spawned by this spawner
	 */
	UFUNCTION(BlueprintCallable, Category = "Spawning")
	void DoDespawning();

	UFUNCTION(BlueprintCallable, Category = "Spawning")
	void ClearTemplates();

	UFUNCTION(BlueprintCallable, Category = "Spawning")
	void UnloadConfig();

	/**
	 * If given entity has been spawned by this MassSpawner instance then it will get destroyed and all the book keeping 
	 * updated. Otherwise the call has no effect.
	 * @return true if the entity got removed. False otherwise.
	 */
	bool DespawnEntity(const FMassEntityHandle Entity);

	/**
	 * Scales the spawning counts
	 * @param Scale is the number to multiply the all counts of each agent types 
	 */
	UFUNCTION(BlueprintCallable, Category = "Spawning")
	void ScaleSpawningCount(float Scale) { SpawningCountScale = Scale; }

protected:
	void OnAdjustTickSchematics(UWorld* World, TArray<TSoftObjectPtr<UMassSchematic>>& InOutTickSchematics);
	void OnPostWorldInit(UWorld* World, const UWorld::InitializationValues);
	void SpawnAtLocations(TConstArrayView<FVector> Locations);
	void OnSpawnPointGenerationFinished(const TArray<FVector>& Locations, FMassSpawnPointGenerator* FinishedGenerator);

	int32 GetSpawnCount() const;

protected:

	struct FSpawnedEntities
	{
		FMassEntityTemplateID TemplateID;
		TArray<FMassEntityHandle> Entities;
	};

	UPROPERTY(EditAnywhere, Category = "Mass|Spawn")
	int32 Count;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Mass|Spawn")
	TArray<FMassSpawnedEntityType> EntityTypes;

	/** Asset that describes the way we want to generate SpawnPoints of the entity */
	UPROPERTY(EditAnywhere, Category = "Mass|Spawn")
	TArray<FMassSpawnPointGenerator> SpawnPointsGenerators;

	UPROPERTY(Category = "Mass|Spawn", EditAnywhere)
	uint32 bAutoSpawnOnBeginPlay : 1;

	/** By default TickSchematics will be appended to the simulation's schematics. If this property is set to true the
	 *  TickSchematics will override the original simulation schematics */
	UPROPERTY(Category = "Mass|Simulation", EditAnywhere)
	uint32 bOverrideSchematics : 1;

	UPROPERTY(Category = "Mass|Simulation", EditAnywhere)
	TArray<TSoftObjectPtr<UMassSchematic>> TickSchematics;

	/** Scale of the spawning count */
	float SpawningCountScale = 1.0f;

	FDelegateHandle TickSchematicsAdjustHandle;

	FDelegateHandle SimulationStartedHandle;

	FDelegateHandle OnPostWorldInitDelegateHandle;

	TArray<FSpawnedEntities> AllSpawnedEntities;

	TArray<FVector> AllGeneratedLocations;

	TSharedPtr<FStreamableHandle> StreamingHandle;

#if WITH_EDITORONLY_DATA
private:
	UPROPERTY()
	UBillboardComponent* SpriteComponent;
#endif // WITH_EDITORONLY_DATA
 };

namespace UE::MassSpawner
{
	MASSSPAWNER_API extern float ScalabilitySpawnDensityMultiplier;
}

