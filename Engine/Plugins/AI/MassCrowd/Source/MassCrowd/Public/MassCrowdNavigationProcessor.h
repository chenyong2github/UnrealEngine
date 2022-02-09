// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassSignalProcessorBase.h"
#include "MassObserverProcessor.h"
#include "MassCrowdFragments.h"
#include "MassCrowdNavigationProcessor.generated.h"

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
class MASSCROWD_API UMassCrowdLaneTrackingDestructor : public UMassObserverProcessor
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
class MASSCROWD_API UMassCrowdDynamicObstacleProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UMassCrowdDynamicObstacleProcessor();

protected:
	virtual void Initialize(UObject& Owner) override;
	virtual void ConfigureQueries() override;
	virtual void Execute(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context);

	void OnStop(FMassCrowdObstacleFragment& OutObstacle, const float Radius);
	void OnMove(FMassCrowdObstacleFragment& OutObstacle);

	FMassEntityQuery EntityQuery_Conditional;

	UPROPERTY(Transient)
	UZoneGraphAnnotationSubsystem* ZoneGraphAnnotationSubsystem;
};


UCLASS()
class MASSCROWD_API UMassCrowdDynamicObstacleInitializer : public UMassObserverProcessor
{
	GENERATED_BODY()

public:
	UMassCrowdDynamicObstacleInitializer();

protected:
	virtual void ConfigureQueries() override;
	virtual void Execute(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context) override;

	FMassEntityQuery EntityQuery;
};


UCLASS()
class MASSCROWD_API UMassCrowdDynamicObstacleDeinitializer : public UMassObserverProcessor
{
	GENERATED_BODY()

public:
	UMassCrowdDynamicObstacleDeinitializer();

protected:
	virtual void Initialize(UObject& Owner) override;
	virtual void ConfigureQueries() override;
	virtual void Execute(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context) override;

	UPROPERTY(Transient)
	UZoneGraphAnnotationSubsystem* ZoneGraphAnnotationSubsystem;

	FMassEntityQuery EntityQuery;
};
