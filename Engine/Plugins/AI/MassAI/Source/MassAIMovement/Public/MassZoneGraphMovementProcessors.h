// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassProcessor.h"
#include "MassObserverProcessor.h"
#include "MassZoneGraphMovementProcessors.generated.h"

class UMassSignalSubsystem;
class UZoneGraphSubsystem;

/**
 * Processor for initializing nearest location on ZoneGraph.
 */
UCLASS()
class MASSAIMOVEMENT_API UMassZoneGraphLocationInitializer : public UMassFragmentInitializer
{
	GENERATED_BODY()
	
public:
	UMassZoneGraphLocationInitializer();

protected:
	virtual void Initialize(UObject& Owner) override;
	virtual void ConfigureQueries() override;
	virtual void Execute(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context) override;

	FMassEntityQuery EntityQuery;

	UPROPERTY(Transient)
	UZoneGraphSubsystem* ZoneGraphSubsystem = nullptr;

	UPROPERTY(Transient)
	UMassSignalSubsystem* SignalSubsystem = nullptr;
};

/** 
 * Processor for updating move target on ZoneGraph path.
 */
UCLASS()
class MASSAIMOVEMENT_API UMassZoneGraphPathFollowProcessor : public UMassProcessor
{
	GENERATED_BODY()

protected:
	UMassZoneGraphPathFollowProcessor();
	
	virtual void Initialize(UObject& Owner) override;
	virtual void ConfigureQueries() override;
	virtual void Execute(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context) override;

	FMassEntityQuery EntityQuery_Conditional;

	UPROPERTY(Transient)
	UZoneGraphSubsystem* ZoneGraphSubsystem = nullptr;

	UPROPERTY(Transient)
	UMassSignalSubsystem* SignalSubsystem = nullptr;
};

/** 
 * Processor for updating steering towards MoveTarget.
 */
UCLASS()
class MASSAIMOVEMENT_API UMassZoneGraphSteeringProcessor : public UMassProcessor
{
	GENERATED_BODY()

protected:
	UMassZoneGraphSteeringProcessor();
	
	virtual void Initialize(UObject& Owner) override;
	virtual void ConfigureQueries() override;
	virtual void Execute(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context) override;

	FMassEntityQuery EntityQuery;

	UPROPERTY(Transient)
	UMassSignalSubsystem* SignalSubsystem = nullptr;
};
