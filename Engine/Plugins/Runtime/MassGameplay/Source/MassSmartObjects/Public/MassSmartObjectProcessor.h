// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassProcessor.h"
#include "SmartObjectRuntime.h"
#include "MassSmartObjectRequest.h"
#include "MassSimulationLOD.h"
#include "MassSmartObjectTypes.h"
#include "MassSmartObjectProcessor.generated.h"

class USmartObjectSubsystem;
class UMassSignalSubsystem;
class UZoneGraphSubsystem;
class UZoneGraphAnnotationSubsystem;

USTRUCT()
struct MASSSMARTOBJECTS_API FMassSmartObjectUserFragment : public FMassFragment
{
	GENERATED_BODY()

public:
	FVector GetTargetLocation() const { return TargetLocation; }

	const FSmartObjectClaimHandle& GetClaimHandle() const { return ClaimHandle; }

	float GetCooldown() const { return Cooldown; }
	void SetCooldown(const float InCooldown) { Cooldown = InCooldown; }

	float GetUseTime() const { return UseTime; }
	void SetUseTime(const float InUseTime) { UseTime = InUseTime; }

	UPROPERTY(Transient)
	FSmartObjectClaimHandle ClaimHandle;

	UPROPERTY(Transient)
	FVector TargetLocation = FVector::ZeroVector;

	UPROPERTY(Transient)
	EMassSmartObjectInteractionStatus InteractionStatus = EMassSmartObjectInteractionStatus::Unset;

private:
	UPROPERTY(Transient)
	float Cooldown = 0.f;

	UPROPERTY(Transient)
	float UseTime = 0.f; 
};

/**
 * Base class for smart object processors that takes care of caching the smart object manager.
 */
UCLASS(Abstract)
class MASSSMARTOBJECTS_API UMassProcessor_SmartObjectBase : public UMassProcessor
{
	GENERATED_BODY()

protected:
	virtual void Initialize(UObject& Owner) override;

	UPROPERTY(Transient)
	USmartObjectSubsystem* SmartObjectSubsystem;

	UPROPERTY(Transient)
	UMassSignalSubsystem* SignalSubsystem;
};

/** Processor that builds a list of candidates objects for each users. */
UCLASS()
class MASSSMARTOBJECTS_API UMassProcessor_SmartObjectCandidatesFinder : public UMassProcessor_SmartObjectBase
{
	GENERATED_BODY()

public:
	UMassProcessor_SmartObjectCandidatesFinder();

protected:
	virtual void Initialize(UObject& Owner) override;
	virtual void ConfigureQueries() override;
	virtual void Execute(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context) override;

	/** Extents used to perform the spatial query in the octree for world location queries. */
	UPROPERTY(EditDefaultsOnly, Category = SmartObject, config)
	float SearchExtents = 5000.f;

	UPROPERTY(Transient)
	UZoneGraphSubsystem* ZoneGraphSubsystem;

	UPROPERTY(Transient)
	UZoneGraphAnnotationSubsystem* AnnotationSubsystem;

	/** Query to fetch and process requests to find smart objects using spacial query around a given world location. */
	FMassEntityQuery WorldRequestQuery;

	/** Query to fetch and process requests to find smart objects on zone graph lanes. */
	FMassEntityQuery LaneRequestQuery;
};

USTRUCT()
struct MASSSMARTOBJECTS_API FMassSmartObjectTimedBehaviorTag : public FMassTag
{
	GENERATED_BODY()
};


/** Processor for time based user's behavior that waits x seconds then releases its claim on the object */
UCLASS()
class MASSSMARTOBJECTS_API UMassProcessor_SmartObjectTimedBehavior : public UMassProcessor_SmartObjectBase
{
	GENERATED_BODY()
public:
	UMassProcessor_SmartObjectTimedBehavior();

protected:
	virtual void Execute(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context) override;
	virtual void ConfigureQueries() override;

	FMassEntityQuery EntityQuery;
};