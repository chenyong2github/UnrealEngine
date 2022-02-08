// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassLODSubsystem.h"
#include "MassLODCollector.h"
#include "MassLODCalculator.h"
#include "MassEntityTypes.h"

#include "MassRepresentationLODCollectorProcessor.generated.h"


USTRUCT()
struct FMassRepresentationLODCollectorConfig
{
	GENERATED_BODY()

	/** Component Tag that will be used to associate LOD config */
	UPROPERTY(EditAnywhere, Category = "Mass|LOD", config, meta = (BaseStruct = "MassTag"))
	FInstancedStruct TagFilter;

	/** Runtime data for matching the LOD config */
	TMassLODCollector<FMassRepresentationLODLogic> RepresentationLODCollector;
	FMassEntityQuery CloseEntityQuery;
	FMassEntityQuery FarEntityQuery_Conditional;
};

/*
* LOD collector which collects LOD information for Viewer LODing when possible.
*/
UCLASS(meta = (DisplayName = "LOD Collector"))
class MASSLOD_API UMassRepresentationLODCollectorProcessor : public UMassLODProcessorBase
{
	GENERATED_BODY()

public:
	UMassRepresentationLODCollectorProcessor();

protected:
	virtual void ConfigureQueries() override;
	virtual void Execute(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context) override;

	UPROPERTY(EditAnywhere, Category = "Mass|LOD", config)
	TArray<FMassRepresentationLODCollectorConfig> LODConfigs;
};
