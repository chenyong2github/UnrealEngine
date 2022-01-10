// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassRepresentationFragments.h"
#include "MassActorSpawnerSubsystem.h"
#include "MassObserverProcessor.h"

#include "MassRepresentationProcessor.generated.h"

class UMassRepresentationSubsystem;
struct FDataFragment_Actor;

enum EActorEnabledType
{
	Disabled,
	LowRes,
	HighRes,
};

UCLASS()
class MASSREPRESENTATION_API UMassRepresentationProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UMassRepresentationProcessor();

protected:

	/** Configure the owned FMassEntityQuery instances to express processor's requirements */
	virtual void ConfigureQueries() override;

	/**
	 * Initialize the processor 
	 * @param Owner of the Processor
	 */
	virtual void Initialize(UObject& Owner) override;

	/** 
	 * Execution method for this processor 
	 * @param EntitySubsystem is the system to execute the lambdas on each entity chunk
	 * @param Context is the execution context to be passed when executing the lambdas
	 */
	virtual void Execute(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context) override;

	/** 
	 * Returns an actor of the template type and setup fragments values from it
	 * @param MassAgent is the handle to the associated mass agent
	 * @param ActorInfo is the fragment where we are going to store the actor pointer
	 * @param Transform is the spatial information about where to spawn the actor 
	 * @param TemplateActorIndex is the index of the type fetched with UMassRepresentationSubsystem::FindOrAddTemplateActor()
	 * @param SpawnRequestHandle (in/out) In: previously requested spawn Out: newly requested spawn
	 * @param Priority of this spawn request in comparison with the others, lower value means higher priority
	 * @return the actor spawned
	 */
	virtual AActor* GetOrSpawnActor(const FMassEntityHandle MassAgent, FDataFragment_Actor& ActorInfo, const FTransform& Transform, const int16 TemplateActorIndex, FMassActorSpawnRequestHandle& SpawnRequestHandle, const float Priority);

	/**
	 * Release the actor to the subsystem, will only release it the actor or spawn request matches the template actor
	 * @param MassAgent is the handle to the associated mass agent
	 * @param ActorInfo is the fragment where we are going to store the actor pointer
	 * @param TemplateActorIndex is the index of the type to release
	 * @param SpawnRequestHandle (in/out) In: previously requested spawn to cancel if any
	 * @param Context of the execution from the entity sub system
	 * @param bCancelSpawningOnly tell to only cancel the existing spawning request and to not release the associated actor it any.
	 * @return if the actor was release or the spawning was canceled.
	 */
	virtual bool ReleaseActorOrCancelSpawning(const FMassEntityHandle MassAgent, FDataFragment_Actor& ActorInfo, const int16 TemplateActorIndex, FMassActorSpawnRequestHandle& SpawnRequestHandle, FMassExecutionContext& Context, const bool bCancelSpawningOnly = false);

	/** 
	 * Enable/disable a spawned actor
	 * @param EnabledType is the type of enabling to do on this actor
	 * @param Actor is the actual actor to perform enabling type on
	 * @param EntityIdx is the entity index currently processing
	 * @param Context is the current Mass execution context 
	 */
	virtual void SetActorEnabled(const EActorEnabledType EnabledType, AActor& Actor, const int32 EntityIdx, FMassExecutionContext& Context);

	/**
	 * Teleports the actor at the specified transform by preserving its velocity and without collision.
	 * The destination will be adjusted to fit an existing capsule.
	 * @param Transform is the new actor's transform 
	 * @param Actor is the actual actor to teleport
	 * @param Context is the current Mass execution context
	 */
	virtual void TeleportActor(const FTransform& Transform, AActor& Actor, FMassExecutionContext& Context);

	/**
	 * Method that will be bound to a delegate called before the spawning of an actor to let the requester prepare it
	 * @param SpawnRequestHandle the handle of the spawn request that is about to spawn
	 * @param SpawnRequest of the actor that is about to spawn
	 */
	void OnActorPreSpawn(const FMassActorSpawnRequestHandle& SpawnRequestHandle, const FStructView& SpawnRequest);

	/**
	 * Method that will be bound to a delegate used post-spawn to notify and let the requester configure the actor
	 * @param SpawnRequestHandle the handle of the spawn request that was just spawned
	 * @param SpawnRequest of the actor that just spawned
	 * @return The action to take on the spawn request, either keep it there or remove it.
	 */
	EMassActorSpawnRequestAction OnActorPostSpawn(const FMassActorSpawnRequestHandle& SpawnRequestHandle, const FStructView& SpawnRequest);

	/**
	 * Method called once the spawning of an actor just occurred, give the processors a chance to configure the actor during the spawn time slicing
	 * @param MassAgent mass entity to the spawn actor
	 * @param SpawnedActor that was just spawned
	 */
	virtual void PostActorSpawned(const FMassEntityHandle MassAgent, AActor& SpawnedActor) {}

	/**
	 * Updates chunk visibility info for later chunk logic optimization
	 * @param Context of the execution from the entity sub system
	 * @return The visualization chunk fragment
	 */
	FMassVisualizationChunkFragment& UpdateChunkVisibility(FMassExecutionContext& Context) const;

	/**
	 * Updates entity visibility tag for later chunk logic optimization
	 * @param Entity of the entity to update visibility on
	 * @param Representation fragment containing the current and previous visual state
	 * @param RepresentationLOD fragment containing the visibility information
	 * @param ChunkData is the visualization chunk fragment
     * @param Context of the execution from the entity sub system
	 */
	static void UpdateEntityVisibility(const FMassEntityHandle Entity, const FMassRepresentationFragment& Representation, const FMassRepresentationLODFragment& RepresentationLOD, FMassVisualizationChunkFragment& ChunkData, FMassExecutionContext& Context);

public:
	/**
	 * Release an actor or cancel its spawning (calls ReleaseAnyActorOrCancelAnySpawning)
	 * WARNING: This method will destroy the associated actor in any and by the same fact might also move the entity into a new archetype.
	 *          So any reference to fragment might become invalid.
	 * @param RepresentationSubsystem to use to release any actors or cancel spawning requests
	 * @param MassAgent is the handle to the associated mass agent
	 * @return True if actor was release or spawning request was canceled
	 */
	static void ReleaseAnyActorOrCancelAnySpawning(UMassRepresentationSubsystem& RepresentationSubsystem, const FMassEntityHandle MassAgent);

	/**
	 * Release an actor or cancel its spawning
	 * WARNING: This method will destroy the associated actor in any and by the same fact might also move the entity into a new archetype.
	 *          So any reference to fragment might become invalid if you are not within the pipe execution
	 * @param RepresentationSubsystem to use to release any actors or cancel spawning requests
	 * @param MassAgent is the handle to the associated mass agent
	 * @param ActorInfo is the fragment where we are going to store the actor pointer
	 * @param Representation fragment containing the current and previous visual state
	 */
	static void ReleaseAnyActorOrCancelAnySpawning(UMassRepresentationSubsystem& RepresentationSubsystem, const FMassEntityHandle MassAgent, FDataFragment_Actor& ActorInfo, FMassRepresentationFragment& Representation);

protected:

	virtual float GetSpawnPriority(const FMassRepresentationLODFragment& Representation)
	{
		// Bump up the spawning priority on the visible entities
		return Representation.LODSignificance - (Representation.Visibility == EMassVisibility::CanBeSeen ? 1.0f : 0.0f);
	}

	/*
	 * Update representation type for each entity, must be called within a ForEachEntityChunk
	 * @param Context of the execution from the entity sub system
	 */
	void UpdateRepresentation(FMassExecutionContext& Context);

	/** 
	 * Update representation and visibility for each entity, must be called within a ForEachEntityChunk
	 * @param Context of the execution from the entity sub system
	 */
	void UpdateVisualization(FMassExecutionContext& Context);

	/** What should be the representation of this entity for each specificLOD */
	UPROPERTY(EditAnywhere, Category = "Mass|Representation", config)
	ERepresentationType LODRepresentation[EMassLOD::Max];

	/** If true, LowRes actors will be kept around, disabled, whilst StaticMeshInstance representation is active */
	UPROPERTY(EditAnywhere, Category = "Mass|Representation", config)
	bool bKeepLowResActors = true;

	/** When switching to ISM keep the actor an extra frame, helps cover rendering glitches (i.e. occlusion query being one frame late) */
	UPROPERTY(EditAnywhere, Category = "Mass|Representation", config)
	bool bKeepActorExtraFrame = false;

	/** If true, will spread the first visualization update over the period specified in NotVisibleUpdateRate member */
	UPROPERTY(EditAnywhere, Category = "Mass|Representation", config)
	bool bSpreadFirstVisualizationUpdate = false;

	/** World Partition grid name to test collision against, default None will be the main grid */
	UPROPERTY(EditAnywhere, Category = "Mass|Representation", config)
	FName WorldPartitionGridNameContainingCollision;

	/** Caching ptr to our associated world */
	UPROPERTY(Transient)
	UWorld* World;

	UPROPERTY(Transient)
	UMassEntitySubsystem* CachedEntitySubsystem;

	/** A cache pointer to the representation subsystem */
	UPROPERTY(Transient)
	UMassRepresentationSubsystem* RepresentationSubsystem;

	/** Default representation when unable to spawn an actor */
	ERepresentationType DefaultRepresentationType = ERepresentationType::None;

	FMassEntityQuery EntityQuery;

	/** At what rate should the not visible entity be updated in seconds */
	UPROPERTY(EditAnywhere, Category = "Mass|Visualization", config)
	float NotVisibleUpdateRate = 0.5f;
};


USTRUCT()
struct FMassRepresentationDefaultDestructorTag : public FMassTag
{
	GENERATED_BODY()
};

UCLASS()
class MASSREPRESENTATION_API UMassRepresentationFragmentDestructor : public UMassFragmentDeinitializer
{
	GENERATED_BODY()

public:
	UMassRepresentationFragmentDestructor();

protected:
	virtual void Initialize(UObject& Owner) override;
	virtual void ConfigureQueries() override;
	virtual void Execute(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context) override;

	/** A cache pointer to the representation subsystem */
	UPROPERTY(Transient)
	UMassRepresentationSubsystem* RepresentationSubsystem;

	FMassEntityQuery EntityQuery;
};