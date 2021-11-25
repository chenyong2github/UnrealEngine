// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassEntityTypes.h"
#include "MassLODTypes.h"
#include "MassLODManager.h"
#include "MassLODCollector.h"

#include "MassLODCollectorProcessor.generated.h"

USTRUCT()
struct FMassCollectorLODConfig
{
	GENERATED_BODY()

	/** Component Tag that will be used to associate LOD config */
	UPROPERTY(EditAnywhere, Category = "Mass|LOD", config, meta = (BaseStruct = "MassTag"))
	FInstancedStruct TagFilter;

	/** How far away from frustrum does this entities are considered visible */
	UPROPERTY(EditAnywhere, Category = "Mass|LOD", meta = (ClampMin = "0.0", UIMin = "0.0"), config)
	float DistanceToFrustum = 0.0f;
	// Once visible how much further than DistanceToFrustum does the entities need to be before being cull again
	UPROPERTY(EditAnywhere, Category = "Mass|LOD", meta = (ClampMin = "0.0", UIMin = "0.0"), config)
	float DistanceToFrustumHysteresis = 0.0f;

	/** Runtime data for matching the LOD config */
	TMassLODCollector<FMassRepresentationLODLogic> RepresentationLODCollector;
	FMassEntityQuery CloseEntityQuery;
	FMassEntityQuery FarEntityQuery_Conditional;
	TMassLODCollector<FMassSimulationLODLogic> SimulationLODCollector;
	FMassEntityQuery OnLODEntityQuery;
	FMassEntityQuery OffLODEntityQuery_Conditional;
	TMassLODCollector<FMassCombinedLODLogic> CombinedLODCollector;
	// Keep these in this order as we are creating ArrayView from the first 3 queries.
	FMassEntityQuery CloseAndOnLODEntityQuery;
	FMassEntityQuery CloseAndOffLODEntityQuery;
	FMassEntityQuery FarAndOnLODEntityQuery;
	FMassEntityQuery FarAndOffLODEntityQuery_Conditional;
};

/*
 * LOD collector which combines collection of LOD information for both Viewer and Simulation LODing when possible.
 */
UCLASS(meta = (DisplayName = "LOD Collector"))
class MASSLOD_API UMassLODCollectorProcessor : public UMassProcessor_LODBase
{
	GENERATED_BODY()

public:
	UMassLODCollectorProcessor();

protected:
	virtual void Initialize(UObject& Owner) override;
	virtual void ConfigureQueries() override;
	virtual void Execute(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context) override;

	UPROPERTY(EditAnywhere, Category = "Mass|LOD", config)
	TArray<FMassCollectorLODConfig> LODConfigs;
};
