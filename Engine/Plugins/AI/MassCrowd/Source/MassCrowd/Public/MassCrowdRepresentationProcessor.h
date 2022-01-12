// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassRepresentationProcessor.h"

#include "MassCrowdRepresentationProcessor.generated.h"

/**
 * Overridden representation processor to make it tied to the crowd via the requirements.
 * It is also the base class for all the different type of crowd representation (Visualization & ServerSideRepresentation)
 */
UCLASS(abstract)
class MASSCROWD_API UMassCrowdRepresentationProcessor : public UMassRepresentationProcessor
{
	GENERATED_BODY()

public:
	UMassCrowdRepresentationProcessor();

protected:

	/**
	 * Initialization of this processor
	 * @param Owner of this processor
	 */
	virtual void Initialize(UObject& Owner) override;

	/** Configure the owned FMassEntityQuery instances to express processor's requirements */
	virtual void ConfigureQueries() override;

	/**
	 * Enable/disable a spawned actor
	 * @param EnabledType is the type of enabling to do on this actor
	 * @param Actor is the actual actor to perform enabling type on
	 * @param EntityIdx is the entity index currently processing
	 * @param Context is the current Mass execution context
	 */
	virtual void SetActorEnabled(const EActorEnabledType EnabledType, AActor& Actor, const int32 EntityIdx, FMassCommandBuffer& CommandBuffer) override;

	/** 
	 * Returns an actor of the template type and setup fragments values from it
	 * @param MassAgent is the handle to the associated mass agent
	 * @param ActorInfo is the fragment where we are going to store the actor pointer
	 * @param Transform is the spatial information about where to spawn the actor 
	 * @param TemplateActorIndex is the index of the type fetched with UMassRepresentationSubsystem::FindOrAddTemplateActor()
	 * @param SpawnRequestHandle (in/out) In: previously requested spawn Out: newly requested spawn
	 * @param Priority of this spawn request in comparison with the others, lower value means higher priority
	 * @param Context of the execution from the entity sub system
	 * @return the actor spawned
	 */
	virtual AActor* GetOrSpawnActor(const FMassEntityHandle MassAgent, FDataFragment_Actor& ActorInfo, const FTransform& Transform, const int16 TemplateActorIndex, FMassActorSpawnRequestHandle& SpawnRequestHandle, const float Priority) override;

	/**
	 * Teleports the actor at the specified transform by preserving its velocity and without collision.
	 * The destination will be adjusted to fit an existing capsule.
	 * @param Transform is the new actor's transform 
	 * @param Actor is the actual actor to teleport
	 * @param Context is the current Mass execution context
	 */
	virtual void TeleportActor(const FTransform& Transform, AActor& Actor, FMassCommandBuffer& CommandBuffer) override;

	/**
	 * Initializes velocity for each entity when just switch to use movement component, must be called within a ForEachEntityChunk
	 * @param EntitySubsystem The subsystem in which all entities must be initialized
	 * @param Context of the execution from the entity sub system
	 */
	void InitializeVelocity(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context);

	/** 
	 * A dedicated query for processing entities owning a FDataFragment_CharacterMovementComponentWrapper
	 */
	FMassEntityQuery CharacterMovementEntitiesQuery_Conditional;
};