// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/EngineTypes.h"
#include "MassEntityTypes.h"
#include "StateTreeTypes.h"

#include "MassComponentHitTypes.generated.h"

namespace UE::Mass::Signals
{
const FName HitReceived = FName(TEXT("HitReceived"));
}

USTRUCT()
struct FMassHitResult
{
	GENERATED_BODY()

	FMassHitResult() = default;

	FMassHitResult(const FMassEntityHandle& OtherEntity, const float Time)
		: OtherEntity(OtherEntity)
		, HitTime(Time)
		, LastFilteredHitTime(Time)
	{
	}

	FMassEntityHandle OtherEntity;

	/** Time when first hit was received. */
	float HitTime = 0.f;

	/** Time used for filtering frequent hits. */
	float LastFilteredHitTime = 0.f;
};

USTRUCT()
struct FComponentHitStateTreeResult : public FStateTreeResult
{
	GENERATED_BODY()

protected:
	virtual const UScriptStruct& GetStruct() const override { return *StaticStruct(); }

public:
	UPROPERTY()
	FMassHitResult MassHitResult;

	UPROPERTY()
	EStateTreeResultStatus Status = EStateTreeResultStatus::Unset;
};
