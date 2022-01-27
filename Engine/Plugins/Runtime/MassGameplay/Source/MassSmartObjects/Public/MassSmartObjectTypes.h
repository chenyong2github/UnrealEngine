// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SmartObjectTypes.h"
#include "MassSmartObjectTypes.generated.h"

namespace UE::Mass::Signals
{
	const FName SmartObjectRequestCandidates = FName(TEXT("SmartObjectRequestCandidates"));
	const FName SmartObjectCandidatesReady = FName(TEXT("SmartObjectCandidatesReady"));
	const FName SmartObjectInteractionDone = FName(TEXT("SmartObjectInteractionDone"));
}

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
    InProgress,			// Claimed and Behavior activated
    BehaviorCompleted,	// Behavior is completed but task still running (not updated yet)
	TaskCompleted,		// Task has been notified that behavior is completed and completes
    Aborted				// Task and Behavior were aborted
};
