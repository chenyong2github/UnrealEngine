// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassSmartObjectRequest.h"
#include "MassSmartObjectTypes.h"

class UMassEntitySubsystem;
class UMassSignalSubsystem;
class USmartObjectSubsystem;
struct FMassExecutionContext;
struct FMassEntityHandle;
struct FMassSmartObjectUserFragment;
struct FTransformFragment;
struct FSmartObjectClaimHandle;
struct FSmartObjectHandle;
struct FZoneGraphCompactLaneLocation;
enum class ESmartObjectSlotState : uint8;

/**
 * Mediator struct that encapsulates communication between SmartObjectSubsystem and Mass.
 * This object is meant to be created and used in method scope to guarantee subsystems validity.
 */
struct MASSSMARTOBJECTS_API FMassSmartObjectHandler
{
	/**
	 * FMassSmartObjectHandler constructor
	 * @param InEntitySubsystem is the entity subsystem that the smart object should belong to
	 * @param InExecutionContext is the current execution context of the entity subsystem
	 * @param InSmartObjectSubsystem is the smart object subsystem
	 * @param InSignalSubsystem is the mass signal subsystem to use to send signal to affected entities
	 */
	FMassSmartObjectHandler(UMassEntitySubsystem& InEntitySubsystem, FMassExecutionContext& InExecutionContext, USmartObjectSubsystem& InSmartObjectSubsystem, UMassSignalSubsystem& InSignalSubsystem)
		: EntitySubsystem(InEntitySubsystem)
		, ExecutionContext(InExecutionContext)
		, SmartObjectSubsystem(InSmartObjectSubsystem)
		, SignalSubsystem(InSignalSubsystem)
	{
	}

	/**
	 * Creates an async request to build a list of compatible smart objects
	 * around the provided location. The caller must poll using the request id
	 * to know when the reservation can be done.
	 * @param RequestingEntity Entity requesting the candidates list
	 * @param Location The center of the query
	 * @return Request identifier that can be used to try claiming a result once available
	 */
	UE_NODISCARD FMassSmartObjectRequestID FindCandidatesAsync(const FMassEntityHandle RequestingEntity, const FVector& Location) const;

	/**
	 * Creates an async request to build a list of compatible smart objects
	 * around the provided lane location. The caller must poll using the request id
	 * to know when the reservation can be done.
	 * @param RequestingEntity Entity requesting the candidates list
	 * @param LaneLocation The lane location as reference for the query
	 * @return Request identifier that can be used to try claiming a result once available
	 */
	UE_NODISCARD FMassSmartObjectRequestID FindCandidatesAsync(const FMassEntityHandle RequestingEntity, const FZoneGraphCompactLaneLocation& LaneLocation) const;

	/**
	 * Provides the result of a previously created request from FindCandidatesAsync to indicate if it has been processed
	 * and the results can be used by ClaimCandidate.
	 * @param RequestID A valid request identifier (method will ensure otherwise)
	 * @return The current request's result
	 */
	UE_NODISCARD FMassSmartObjectRequestResult GetRequestResult(const FMassSmartObjectRequestID& RequestID) const;

	/**
	 * Deletes the request associated to the specified identifier
	 * @param RequestID A valid request identifier (method will ensure otherwise)
	 */
	void RemoveRequest(const FMassSmartObjectRequestID& RequestID) const;

	/**
	 * Claims the first available smart object from the results of the find request. This method
	 * can be called without calling IsRequestProcessed first but it will fail and return
	 * Failed_UnprocessedRequest. It can then be called again on next frame until it succeeds or
	 * returns a different error.
	 * @param Entity MassEntity associated to the user fragment
	 * @param User Fragment of the user claiming
	 * @param RequestID A valid request identifier (method will ensure otherwise)
	 * @return Whether the slot has been successfully claimed or not
	 */
	UE_NODISCARD EMassSmartObjectClaimResult ClaimCandidate(const FMassEntityHandle Entity, FMassSmartObjectUserFragment& User, const FMassSmartObjectRequestID& RequestID) const;

	/**
	 * Claims the first available smart object from the provided results.
	 * @param Entity MassEntity associated to the user fragment
	 * @param User Fragment of the user claiming
	 * @param SearchRequestResult Results of completed search request
	 * @return Whether the slot has been successfully claimed or not
	 */
	UE_NODISCARD EMassSmartObjectClaimResult ClaimCandidate(const FMassEntityHandle Entity, FMassSmartObjectUserFragment& User, const FMassSmartObjectRequestResult& SearchRequestResult) const;

	/**
	 * Claims the first available slot holding any type of USmartObjectMassBehaviorDefinition in the smart object
	 * associated to the provided identifier.
	 * @param Entity MassEntity associated to the user fragment
	 * @param User Fragment of the user claiming
	 * @param ObjectHandle A valid smart object identifier (method will ensure otherwise)
	 * @return Whether the slot has been successfully claimed or not
	 */
	bool ClaimSmartObject(const FMassEntityHandle Entity, FMassSmartObjectUserFragment& User, const FSmartObjectHandle& ObjectHandle) const;

	/**
	 * Activates the mass gameplay behavior associated to the previously claimed smart object.
	 * @param Entity MassEntity associated to the user fragment
	 * @param User Fragment of the user claiming
	 * @param Transform Fragment holding the transform of the user claiming
	 * @return Whether the slot has been successfully claimed or not
	 */
	bool UseSmartObject(const FMassEntityHandle Entity, FMassSmartObjectUserFragment& User, const FTransformFragment& Transform) const;

	/**
	 * Releases a claimed/in-use smart object and update user fragment.
	 * @param Entity MassEntity associated to the user fragment
	 * @param User Fragment of the user claiming
	 * @param Status The new status for in-progress interaction
	 */
	void ReleaseSmartObject(const FMassEntityHandle Entity, FMassSmartObjectUserFragment& User, const EMassSmartObjectInteractionStatus Status = EMassSmartObjectInteractionStatus::Unset) const;

private:
	UMassEntitySubsystem& EntitySubsystem;
	FMassExecutionContext& ExecutionContext;
	USmartObjectSubsystem& SmartObjectSubsystem;
	UMassSignalSubsystem& SignalSubsystem;
};
