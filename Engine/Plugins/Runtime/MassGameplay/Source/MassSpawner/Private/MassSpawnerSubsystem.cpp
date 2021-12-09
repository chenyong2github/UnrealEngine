// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassSpawnerSubsystem.h"
#include "MassSpawnerTypes.h"
#include "MassEntityTemplate.h"
#include "MassEntitySubsystem.h"
#include "MassEntityTemplateRegistry.h"
#include "Engine/World.h"
#include "MassExecutor.h"
#include "InstancedStruct.h"
#include "VisualLogger/VisualLogger.h"
#include "MassSpawner.h"
#include "MassTranslator.h"
#include "MassTranslatorRegistry.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "MassSimulationSubsystem.h"
#include "MassEntityUtils.h"


namespace UE::Mass::SpawnerSubsystem 
{

	bool RunFragmentDestructors(const FArchetypeChunkCollection& Chunks, UMassSpawnerSubsystem& SpawnerSubSystem, UMassEntitySubsystem& EntitySystem)
	{
		const FArchetypeHandle ArchetypeHandle = Chunks.GetArchetype();
		// @todo this is a temporary measure to skip entities we've destroyed without the
		// UMassSpawnerSubsystem::DestroyEntities' caller knowledge. Should be removed once that's addressed.
		if (ArchetypeHandle.IsValid() == false)
		{
			return false;
		}

		// @Todo: should queue up the entity destruction and apply in batch
		const UMassTranslatorRegistry& Registry = UMassTranslatorRegistry::Get();
		TArray<const UMassProcessor*> Destructors;

		EntitySystem.ForEachArchetypeFragmentType(ArchetypeHandle, [&Registry, &Destructors](const UScriptStruct* FragmentType)
		{
			check(FragmentType);
			const UMassFragmentDestructor* Destructor = Registry.GetFragmentDestructor(*FragmentType);
			if (Destructor)
			{
				Destructors.AddUnique(Destructor);
			}
		});

		if (Destructors.Num())
		{
			FMassRuntimePipeline DestructionPipeline;
			DestructionPipeline.InitializeFromArray(Destructors, SpawnerSubSystem);

			FMassProcessingContext ProcessingContext(EntitySystem, /*TimeDelta=*/0.0f);

			UE::Mass::Executor::RunSparse(DestructionPipeline, ProcessingContext, Chunks);
		}

		return Destructors.Num() > 0;
	}
	
	void CreateSparseChunks(const UMassEntitySubsystem& EntitySystem, const TConstArrayView<FMassEntityHandle> Entities, TArray<FArchetypeChunkCollection>& OutChunkCollections)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE_STR("SpawnerSubsystem_CreateSparseChunks");

		TMap<const FArchetypeHandle, TArray<FMassEntityHandle>> ArchetypeToEntities;

		for (const FMassEntityHandle& Entity : Entities)
		{
			FArchetypeHandle Archetype = EntitySystem.GetArchetypeForEntity(Entity);
			TArray<FMassEntityHandle>& PerArchetypeEntities = ArchetypeToEntities.FindOrAdd(Archetype);
			PerArchetypeEntities.Add(Entity);
		}

		for (auto& Pair : ArchetypeToEntities)
		{
			// @todo this is a temporary measure to skip entities we've destroyed without the
			// UMassSpawnerSubsystem::DestroyEntities' caller knowledge. Should be removed once that's addressed.
			if (Pair.Key.IsValid())
			{
				OutChunkCollections.Add(FArchetypeChunkCollection(Pair.Key, Pair.Value));
			}
		}
	}
	
} // namespace UE::Mass::SpawnerSubsystem

//----------------------------------------------------------------------//
//  UMassSpawnerSubsystem
//----------------------------------------------------------------------//
void UMassSpawnerSubsystem::Initialize(FSubsystemCollectionBase& Collection) 
{	
	Super::Initialize(Collection);

	// making sure UMassSimulationSubsystem gets created before the MassActorManager
	Collection.InitializeDependency<UMassSimulationSubsystem>();

	SimulationSystem = UWorld::GetSubsystem<UMassSimulationSubsystem>(GetWorld());
	EntitySystem = UWorld::GetSubsystem<UMassEntitySubsystem>(GetWorld());
	check(EntitySystem);
}

void UMassSpawnerSubsystem::PostInitialize()
{
	TemplateRegistryInstance = NewObject<UMassEntityTemplateRegistry>(this);
}

void UMassSpawnerSubsystem::SpawnEntities(const FMassEntityTemplate& EntityTemplate, const uint32 NumberToSpawn, TArray<FMassEntityHandle>& OutEntities)
{
	check(EntitySystem);
	check(EntityTemplate.IsValid());

	if (NumberToSpawn == 0)
	{
		UE_VLOG(this, LogMassSpawner, Warning, TEXT("Trying to spawn 0 entities. This would cause inefficiency. Bailing out with result FALSE."));
		return;
	}

	DoSpawning(EntityTemplate, NumberToSpawn, FStructView(), TSubclassOf<UMassProcessor>(), OutEntities);
}

void UMassSpawnerSubsystem::SpawnEntities(FMassEntityTemplateID TemplateID, const uint32 NumberToSpawn, FConstStructView SpawnData, TSubclassOf<UMassProcessor> InitializerClass, TArray<FMassEntityHandle>& OutEntities)
{
	check(TemplateID.IsValid());

	if (!ensureMsgf(TemplateRegistryInstance, TEXT("UMassSpawnerSubsystem didn\'t get its OnPostWorldInit call yet!")))
	{
		return;
	}

	const FMassEntityTemplate* EntityTemplate = TemplateRegistryInstance->FindTemplateFromTemplateID(TemplateID);
	checkf(EntityTemplate && EntityTemplate->IsValid(), TEXT("SpawnEntities: TemplateID must have been registered!"));

	DoSpawning(*EntityTemplate, NumberToSpawn, SpawnData, InitializerClass, OutEntities);
}

void UMassSpawnerSubsystem::SpawnFromConfig(FStructView Config, const int32 NumToSpawn, FConstStructView SpawnData, TSubclassOf<UMassProcessor> InitializerClass)
{
	check(Config.IsValid());

	if (!ensureMsgf(TemplateRegistryInstance, TEXT("UMassSpawnerSubsystem didn\'t get its OnPostWorldInit call yet!")))
	{
		return;
	}
	
	const FMassEntityTemplate* EntityTemplate = TemplateRegistryInstance->FindOrBuildStructTemplate(Config);
	checkf(EntityTemplate && EntityTemplate->IsValid(), TEXT("SpawnFromConfig: TemplateID must have been registered!"));

	TArray<FMassEntityHandle> Entities;
	DoSpawning(*EntityTemplate, NumToSpawn, SpawnData, InitializerClass, Entities);
}

void UMassSpawnerSubsystem::RegisterCollection(TArrayView<FInstancedStruct> Collection)
{
	if (!ensureMsgf(TemplateRegistryInstance, TEXT("UMassSpawnerSubsystem didn't get its OnPostWorldInit called yet!")))
	{
		return;
	}

	for (FInstancedStruct& Entry : Collection)
	{
		if (Entry.IsValid())
		{
			TemplateRegistryInstance->FindOrBuildStructTemplate(Entry);
		}
	}
}

void UMassSpawnerSubsystem::DestroyEntities(const FMassEntityTemplateID TemplateID, TConstArrayView<FMassEntityHandle> Entities)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR("MassSpawnerSubsystem_DestroyEntities")

	if (!ensureMsgf(TemplateRegistryInstance, TEXT("UMassSpawnerSubsystem didn\'t get its OnPostWorldInit call yet!")))
	{
		return;
	}

	check(EntitySystem);
	check(SimulationSystem);
	checkf(!SimulationSystem->GetPhaseManager().IsDuringMassProcessing()
		, TEXT("%s called while MassEntity processing in progress. This is unsupported and dangerous!"), ANSI_TO_TCHAR(__FUNCTION__));

	UWorld* World = GetWorld();
	check(World);

	// Run deinitializers and destructor processors on all entities.
	const FMassEntityTemplate* EntityTemplate = TemplateRegistryInstance->FindTemplateFromTemplateID(TemplateID);
	if (EntityTemplate)
	{
		// There's no certainty all Entities still belong to the original archetype, the MutableEntityTemplate.GetArchetype().
		// We need to figure out the separate, per-archetype sparse chunks
		TArray<FArchetypeChunkCollection> ChunkCollections;
		UE::Mass::SpawnerSubsystem::CreateSparseChunks(*EntitySystem, Entities, ChunkCollections);

		FMassProcessingContext ProcessingContext(*EntitySystem, /*TimeDelta=*/0.0f);

		// Run destructors
		bool bDestructorsRun = false;
		for (const FArchetypeChunkCollection& Chunks : ChunkCollections)
		{
			bDestructorsRun = UE::Mass::SpawnerSubsystem::RunFragmentDestructors(Chunks, *this, *EntitySystem) || bDestructorsRun;
		}

		// if any destructors were called it's possible the entities changed archetypes. Need to 
		// recalculate the chunks
		if (bDestructorsRun)
		{
			ChunkCollections.Reset();
			UE::Mass::SpawnerSubsystem::CreateSparseChunks(*EntitySystem, Entities, ChunkCollections);
		}
		
		// Run template deinitializers
		FMassEntityTemplate& MutableEntityTemplate = const_cast<FMassEntityTemplate&>(*EntityTemplate);
		if (MutableEntityTemplate.GetDeinitializationPipeline().Processors.Num())
		{
			for (const FArchetypeChunkCollection& Chunks : ChunkCollections)
			{
				if (Chunks.GetArchetype().IsValid())
				{
					UE::Mass::Executor::RunSparse(MutableEntityTemplate.GetMutableDeinitializationPipeline(), ProcessingContext, Chunks);
				}
			}
		}

		// if any deinitializer were called it's possible the entities changed archetypes. Need to 
		// recalculate the chunks
		if (MutableEntityTemplate.GetDeinitializationPipeline().Processors.Num())
		{
			ChunkCollections.Reset();
			UE::Mass::SpawnerSubsystem::CreateSparseChunks(*EntitySystem, Entities, ChunkCollections);
		}

		for (const FArchetypeChunkCollection& Chunks : ChunkCollections)
		{
			EntitySystem->BatchDestroyEntityChunks(Chunks);
		}
	}
	else
	{
		EntitySystem->BatchDestroyEntities(Entities);
	}
}

UMassProcessor* UMassSpawnerSubsystem::GetSpawnLocationInitializer(TSubclassOf<UMassProcessor> InitializerClass)
{	
	if (!InitializerClass)
	{
		return nullptr;
	}

	UMassProcessor** const Initializer = SpawnLocationInitializers.FindByPredicate([InitializerClass](const UMassProcessor* Processor)
		{
			return Processor && Processor->GetClass() == InitializerClass;
		}
	);

	if (Initializer == nullptr)
	{
		UMassProcessor* NewInitializer = NewObject<UMassProcessor>(this, InitializerClass);
		NewInitializer->Initialize(*this);
		SpawnLocationInitializers.Add(NewInitializer);
		return NewInitializer;
	}

	return *Initializer;
}

void UMassSpawnerSubsystem::DoSpawning(const FMassEntityTemplate& EntityTemplate, const int32 NumToSpawn, FConstStructView SpawnData, TSubclassOf<UMassProcessor> InitializerClass, TArray<FMassEntityHandle>& OutEntities)
{
	check(EntitySystem);
	check(EntityTemplate.GetArchetype().IsValid());
	UE_VLOG(this, LogMassSpawner, Log, TEXT("Spawning with EntityTemplate:\n%s"), *EntityTemplate.DebugGetDescription(EntitySystem));

	//TRACE_CPUPROFILER_EVENT_SCOPE_STR("MassSpawnerSubsystem DoSpawning");

	// 1. Create required number of entities with EntityTemplate.Archetype
	// 2. Copy data from FMassEntityTemplate.Fragments.
	//		a. @todo, could be done as part of creation?
	// 3. Run all EntityTemplate.Translators to configure remaining values (i.e. not set via FMassEntityTemplate.Fragments).

	TArray<FMassEntityHandle> SpawnedEntities;
	EntitySystem->BatchCreateEntities(EntityTemplate.GetArchetype(), NumToSpawn, SpawnedEntities);

	TConstArrayView<FInstancedStruct> FragmentInstances = EntityTemplate.GetInitialFragmentValues();
	const FArchetypeChunkCollection ChunkCollection(EntityTemplate.GetArchetype(), SpawnedEntities);
	EntitySystem->BatchSetEntityFragmentsValues(ChunkCollection, FragmentInstances);
	
	UMassProcessor* SpawnLocationInitializer = SpawnData.IsValid() ? GetSpawnLocationInitializer(InitializerClass) : nullptr;

	if (SpawnLocationInitializer)
	{
		FMassProcessingContext ProcessingContext(*EntitySystem, /*TimeDelta=*/0.0f);
		ProcessingContext.AuxData = SpawnData;
		UE::Mass::Executor::RunProcessorsView(MakeArrayView(&SpawnLocationInitializer, 1), ProcessingContext, &ChunkCollection);
	}

	if (EntityTemplate.GetInitializationPipeline().Processors.Num())
	{
		FMassEntityTemplate& MutableEntityTemplate = const_cast<FMassEntityTemplate&>(EntityTemplate);

		FMassProcessingContext ProcessingContext(*EntitySystem, /*TimeDelta=*/0.0f);
		UE::Mass::Executor::RunSparse(MutableEntityTemplate.GetMutableInitializationPipeline(), ProcessingContext, ChunkCollection);
	}

	OutEntities.Append(MoveTemp(SpawnedEntities));
}

const FMassEntityTemplate* UMassSpawnerSubsystem::GetMassEntityTemplate(FMassEntityTemplateID TemplateID) const
{
	check(TemplateID.IsValid());

	if (!ensureMsgf(TemplateRegistryInstance, TEXT("UMassSpawnerSubsystem didn\'t get its OnPostWorldInit call yet!")))
	{
		return nullptr;
	}

	return TemplateRegistryInstance->FindTemplateFromTemplateID(TemplateID);
}
