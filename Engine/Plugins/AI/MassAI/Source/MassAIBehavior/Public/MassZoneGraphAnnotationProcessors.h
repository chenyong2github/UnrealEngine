// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassSignalProcessorBase.h"
#include "MassTranslator.h"
#include "MassZoneGraphAnnotationProcessors.generated.h"

class UMassSignalSubsystem;
class UZoneGraphAnnotationSubsystem;
struct FMassZoneGraphAnnotationTagsFragment;
struct FMassZoneGraphLaneLocationFragment;
struct FLWEntity;

/** 
 * Processor for initializing ZoneGraph annotation tags.
 */
UCLASS()
class MASSAIBEHAVIOR_API UMassZoneGraphAnnotationTagsInitializer : public UMassFragmentInitializer
{
	GENERATED_BODY()

public:
	UMassZoneGraphAnnotationTagsInitializer();

protected:
	virtual void Initialize(UObject& Owner) override;
	virtual void ConfigureQueries() override;
	virtual void Execute(UMassEntitySubsystem& EntitySubsystem, FLWComponentSystemExecutionContext& Context) override;

	FLWComponentQuery EntityQuery;

	UPROPERTY(Transient)
	UZoneGraphAnnotationSubsystem* ZoneGraphAnnotationSubsystem = nullptr;
};

/** 
 * Processor for update ZoneGraph annotation tags periodically and on lane changed signal.
 */
UCLASS()
class MASSAIBEHAVIOR_API UMassZoneGraphAnnotationTagUpdateProcessor : public UMassSignalProcessorBase
{
	GENERATED_BODY()

public:
	UMassZoneGraphAnnotationTagUpdateProcessor();
	
protected:
	virtual void Initialize(UObject& Owner) override;
	virtual void ConfigureQueries() override;
	virtual void Execute(UMassEntitySubsystem& EntitySubsystem, FLWComponentSystemExecutionContext& Context) override;

	void UpdateAnnotationTags(FMassZoneGraphAnnotationTagsFragment& AnnotationTags, const FMassZoneGraphLaneLocationFragment& LaneLocation, FLWEntity Entity);

	virtual void SignalEntities(UMassEntitySubsystem& EntitySubsystem, FLWComponentSystemExecutionContext& Context, FMassSignalNameLookup& EntitySignals) override;

	UPROPERTY(Transient)
	UZoneGraphAnnotationSubsystem* ZoneGraphAnnotationSubsystem = nullptr;

	// Frame buffer, it gets reset every frame.
	TArray<FLWEntity> TransientEntitiesToSignal;
};
