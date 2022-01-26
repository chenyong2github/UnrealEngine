// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassEntityTypes.h"
#include "MassSmartObjectTypes.h"
#include "SmartObjectRuntime.h"
#include "MassSmartObjectFragments.generated.h"

/** Fragment used by an entity to be able to interact with smart objects */
USTRUCT()
struct MASSSMARTOBJECTS_API FMassSmartObjectUserFragment : public FMassFragment
{
	GENERATED_BODY()

	UPROPERTY(Transient)
	FSmartObjectClaimHandle ClaimHandle;

	UPROPERTY(Transient)
	FVector TargetLocation = FVector::ZeroVector;

	UPROPERTY(Transient)
	FVector TargetDirection = FVector::ForwardVector;

	UPROPERTY(Transient)
	EMassSmartObjectInteractionStatus InteractionStatus = EMassSmartObjectInteractionStatus::Unset;

	/**
	 * World time in seconds before which the user is considered in cooldown and
	 * won't look for new interactions (value of 0 indicates no cooldown).
	 */
	UPROPERTY(Transient)
	float CooldownEndTime = 0.f;
};

/** Fragment used to process time based smartobject interactions */
USTRUCT()
struct MASSSMARTOBJECTS_API FMassSmartObjectTimedBehaviorFragment : public FMassFragment
{
	GENERATED_BODY()

	UPROPERTY(Transient)
	float UseTime = 0.f;
};