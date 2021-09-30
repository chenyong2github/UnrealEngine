// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassEntityTypes.h"
#include "MassSmartObjectTypes.generated.h"

namespace UE::Mass::Signals
{
	const FName SmartObjectRequestCandidates = FName(TEXT("SmartObjectRequestCandidates"));
	const FName SmartObjectCandidatesReady = FName(TEXT("SmartObjectCandidatesReady"));
	const FName SmartObjectInteractionDone = FName(TEXT("SmartObjectInteractionDone"));
}

/**
 * Identifier associated to a request for smart object candidates. We use a 1:1 match
 * with an LWEntity since all requests are batched together using the EntitySubsystem.
 */
USTRUCT()
struct MASSSMARTOBJECTS_API FMassSmartObjectRequestID
{
	GENERATED_BODY()

	FMassSmartObjectRequestID() = default;
	FMassSmartObjectRequestID(const FMassEntityHandle& InEntity) : Entity(InEntity) {}
	
	bool IsSet() const { return Entity.IsSet(); }
	void Reset() { Entity.Reset(); }
	
	explicit operator FMassEntityHandle() const { return Entity; }

private:
	UPROPERTY(Transient)
	FMassEntityHandle Entity;
};

UENUM()
enum class EMassSmartObjectClaimResult: uint8
{
	Unset,
	Failed_InvalidRequest,
	Failed_UnprocessedRequest,
	Failed_NoAvailableCandidate,
	Succeeded
};

UENUM()
enum class EMassSmartObjectInteractionStatus: uint8
{
	Unset,
    InProgress,
    Completed,
    Aborted
};