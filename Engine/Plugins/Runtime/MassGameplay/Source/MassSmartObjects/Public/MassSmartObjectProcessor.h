// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassObserverProcessor.h"
#include "MassProcessor.h"
#include "MassSmartObjectProcessor.generated.h"

class USmartObjectSubsystem;
class UMassSignalSubsystem;
class UZoneGraphSubsystem;
class UZoneGraphAnnotationSubsystem;

/** Processor that builds a list of candidates objects for each users. */
UCLASS()
class MASSSMARTOBJECTS_API UMassSmartObjectCandidatesFinderProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UMassSmartObjectCandidatesFinderProcessor();

protected:
	virtual void Initialize(UObject& Owner) override;
	virtual void ConfigureQueries() override;
	virtual void Execute(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context) override;

	/** Extents used to perform the spatial query in the octree for world location queries. */
	UPROPERTY(EditDefaultsOnly, Category = SmartObject, config)
	float SearchExtents = 5000.f;

	UPROPERTY(Transient)
	USmartObjectSubsystem* SmartObjectSubsystem;

	UPROPERTY(Transient)
	UMassSignalSubsystem* SignalSubsystem;

	UPROPERTY(Transient)
	UZoneGraphSubsystem* ZoneGraphSubsystem;

	UPROPERTY(Transient)
	UZoneGraphAnnotationSubsystem* AnnotationSubsystem;

	/** Query to fetch and process requests to find smart objects using spacial query around a given world location. */
	FMassEntityQuery WorldRequestQuery;

	/** Query to fetch and process requests to find smart objects on zone graph lanes. */
	FMassEntityQuery LaneRequestQuery;
};

/** Processor for time based user's behavior that waits x seconds then releases its claim on the object */
UCLASS()
class MASSSMARTOBJECTS_API UMassSmartObjectTimedBehaviorProcessor : public UMassProcessor
{
	GENERATED_BODY()
public:
	UMassSmartObjectTimedBehaviorProcessor();

protected:
	virtual void Initialize(UObject& Owner) override;
	virtual void Execute(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context) override;
	virtual void ConfigureQueries() override;

	UPROPERTY(Transient)
	USmartObjectSubsystem* SmartObjectSubsystem;

	UPROPERTY(Transient)
	UMassSignalSubsystem* SignalSubsystem;

	FMassEntityQuery EntityQuery;
};

/** Deinitializer processor to unregister slot invalidation callback when SmartObjectUser fragment gets removed */
UCLASS()
class MASSSMARTOBJECTS_API UMassSmartObjectUserFragmentDeinitializer : public UMassObserverProcessor
{
	GENERATED_BODY()

	UMassSmartObjectUserFragmentDeinitializer();

protected:
	virtual void ConfigureQueries() override;
	virtual void Initialize(UObject& Owner) override;
	virtual void Execute(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context) override;

	UPROPERTY(Transient)
	USmartObjectSubsystem* SmartObjectSubsystem;

	FMassEntityQuery EntityQuery;
};