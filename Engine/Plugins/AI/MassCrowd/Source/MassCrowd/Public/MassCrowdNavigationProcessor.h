// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassSignalProcessorBase.h"
#include "MassObserverProcessor.h"
#include "NavigationProcessor.h"
#include "MassCrowdNavigationProcessor.generated.h"

class UZoneGraphSubsystem;
class UMassCrowdSettings;
class UZoneGraphAnnotationSubsystem;
class UMassCrowdSubsystem;

/** Processor that monitors when entities change lane and notify the MassCrowd subsystem. */
UCLASS()
class MASSCROWD_API UMassCrowdLaneTrackingSignalProcessor : public UMassSignalProcessorBase
{
	GENERATED_BODY()
public:
	UMassCrowdLaneTrackingSignalProcessor();

protected:
	virtual void Initialize(UObject& Owner) override;
	virtual void ConfigureQueries() override;
	virtual void SignalEntities(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context, FMassSignalNameLookup& EntitySignals) override;

	UPROPERTY(Transient)
	UMassCrowdSubsystem* MassCrowdSubsystem;
};

/** Processors that cleans up the lane tracking on entity destruction. */
UCLASS()
class MASSCROWD_API UMassCrowdLaneTrackingDestructor : public UMassFragmentDeinitializer
{
	GENERATED_BODY()

public:
	UMassCrowdLaneTrackingDestructor();

protected:
	virtual void Initialize(UObject& Owner) override;
	virtual void ConfigureQueries() override;
	virtual void Execute(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context) override;

	UPROPERTY(Transient)
	UMassCrowdSubsystem* MassCrowdSubsystem;

	FMassEntityQuery EntityQuery;
};

UCLASS()
class MASSCROWD_API UMassCrowdDynamicObstacleProcessor : public UMassDynamicObstacleProcessor
{
	GENERATED_BODY()

public:
	UMassCrowdDynamicObstacleProcessor();

protected:
	virtual void Initialize(UObject& Owner) override;

	virtual void OnStop(FMassDynamicObstacleFragment& OutObstacle, const float Radius) override;
	virtual void OnMove(FMassDynamicObstacleFragment& OutObstacle) override;

	UPROPERTY(Transient)
	UZoneGraphSubsystem* ZoneGraphSubsystem;
	
	UPROPERTY(Transient)
	UZoneGraphAnnotationSubsystem* ZoneGraphAnnotationSubsystem;

	UPROPERTY(Transient)
	const UMassCrowdSettings* CrowdSettings;
};
