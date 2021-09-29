// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EntitySubsystem.h"
#include "Subsystems/WorldSubsystem.h"
#include "MassSpawnerSubsystem.generated.h"

class UPipeEntitySubsystem;
struct FMassSpawnConfigBase;
struct FMassEntityTemplate;
class UMassEntityTemplateRegistry;
struct FInstancedStruct;
struct FStructView;
struct FMassEntityTemplateID;
class UMassSimulationSubsystem;

UCLASS()
class MASSSPAWNER_API UMassSpawnerSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()
public:

	void SpawnEntities(FMassEntityTemplateID TemplateID, const FMassSpawnConfigBase& SpawnConfig, const FStructView& AuxData, TArray<FLWEntity>& OutEntities);

	/** Finds the appropriate entity template for ActorInstance and calls the other SpawnEntities implementation 
	 *  @return true if spawning was successful, false otherwise. The failure might come from there being no EntityTemplate 
	 *  @param ActorInstance instance of an actor for which we want to spawn entities for
	 *  @param NumberToSpawn number of entities to spawn
	 *  @param OutEntities where the IDs of created entities get added. Note that the contents of OutEntities get overridden by the function.
	 *  for ActorInstance. See logs for more details. */
	bool SpawnEntities(const AActor& ActorInstance, const uint32 NumberToSpawn, TArray<FLWEntity>& OutEntities) const;

	/** Spawns entities of the kind described by the given EntityTemplate. The spawned entities are fully initialized
	 *  meaning the EntityTemplate.InitializationPipeline gets run for all spawned entities.
	 *  @param EntityTemplate template to use for spawning entities
	 *  @param NumberToSpawn number of entities to spawn
	 *  @param OutEntities where the IDs of created entities get added. Note that the contents of OutEntities get overridden by the function.
	 *  @return true if spawning was successful, false otherwise. In case of failure see logs for more details. */
	bool SpawnEntities(const FMassEntityTemplate& EntityTemplate, const uint32 NumberToSpawn, TArray<FLWEntity>& OutEntities) const;

	void SpawnCollection(TArrayView<FInstancedStruct> Collection, const int32 Count, const FStructView& AuxData = FStructView());

	void DestroyEntities(const FMassEntityTemplateID TemplateID, TConstArrayView<FLWEntity> Entities);

	UMassEntityTemplateRegistry& GetTemplateRegistryInstance() const { check(TemplateRegistryInstance); return *TemplateRegistryInstance; }

	void RegisterCollection(TArrayView<FInstancedStruct> Collection);

	const FMassEntityTemplate* GetMassEntityTemplate(FMassEntityTemplateID TemplateID) const;

protected:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void PostInitialize() override;

	void DoSpawning(const FMassEntityTemplate& EntityTemplate, const FMassSpawnConfigBase& Data, const FStructView& AuxData, TArray<FLWEntity>& OutEntities) const;

	UPROPERTY()
	UPipeEntitySubsystem* EntitySystem;

	UPROPERTY()
	UMassEntityTemplateRegistry* TemplateRegistryInstance;

	UPROPERTY()
	UMassSimulationSubsystem* SimulationSystem;
};

