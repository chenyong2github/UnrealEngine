// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassLODManager.h"
#include "MassLODCollector.h"
#include "MassLODCalculator.h"
#include "MassLODTickRateController.h"
#include "MassLODLogic.h"
#include "MassEntityTypes.h"

#include "MassSimulationLOD.generated.h"

/*
 * Data fragment to store the calculated distances to viewers
 * @todo To be removed and convert to new Simulation LOD stuff
 */
USTRUCT()
struct MASSLOD_API FDataFragment_MassSimulationLODInfo : public FMassFragment
{
	GENERATED_BODY()

	// Distance between each viewer and entity
	TStaticArray<float, UE::MassLOD::MaxNumOfViewers> DistanceToViewerSq;
};

struct FSimulationLODLogic : public FLODDefaultLogic
{
};

/*
 * Mass processor to calculate square distances from entity to viewers.
 * This is done once per entity and will be used for all subsequent LOD calculation.
 * @todo To be removed and convert to new Simulation LOD stuff
 */
UCLASS()
class MASSLOD_API UMassProcessor_MassSimulationLODViewersInfo : public UMassProcessor_LODBase
{
	GENERATED_BODY()
public:
	UMassProcessor_MassSimulationLODViewersInfo();

protected:
	virtual void Initialize(UObject& Owner) override;
	virtual void ConfigureQueries() override;
	virtual void Execute(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context) override;

	TMassLODCollector<FSimulationLODLogic> LODCollector;
	FMassEntityQuery EntityQuery;
};

USTRUCT()
struct MASSLOD_API FMassSimulationLODFragment : public FMassFragment
{
	GENERATED_BODY()

	/** Saved closest ViewerDistance */
	float ClosestViewerDistanceSq = FLT_MAX;

	/**LOD information */
	TEnumAsByte<EMassLOD::Type> LOD = EMassLOD::Max;

	/** Previous LOD information*/
	TEnumAsByte<EMassLOD::Type> PrevLOD = EMassLOD::Max;

	/** Accumulated delta time to use upon next tick */
	float LastTickedTime = 0.0f;
	float DeltaTime = 0.0f;
};

USTRUCT()
struct FMassSimulationVariableTickChunkFragment : public FMassVariableTickChunkFragment
{
	GENERATED_BODY();

	/**
	 * @return if Context contains an instance of FMassSimulationVariableTickChunkFragment then the function returns
	 *	the value of FMassVariableTickChunkFragment.ShouldTickThisFrame; otherwise it returns DefaultValue.
	 */
	static bool ShouldTickChunkThisFrame(const FMassExecutionContext& Context)
	{
		const FMassSimulationVariableTickChunkFragment* ChunkFragment = Context.GetChunkFragmentPtr<FMassSimulationVariableTickChunkFragment>();
		return ChunkFragment == nullptr || ChunkFragment->ShouldTickThisFrame();
	}

	static EMassLOD::Type GetChunkLOD(const FMassExecutionContext& Context)
	{
		const FMassSimulationVariableTickChunkFragment* ChunkFragment = Context.GetChunkFragmentPtr<FMassSimulationVariableTickChunkFragment>();
		return ChunkFragment ? ChunkFragment->GetLOD() : EMassLOD::High;
	}

};

USTRUCT()
struct FMassSimulationLODConfig : public FMassSharedFragment
{
	GENERATED_BODY()

	FMassSimulationLODConfig();

	/** Distance where each LOD becomes relevant */
	UPROPERTY(EditAnywhere, Category = "Mass|LOD", config)
	float LODDistance[EMassLOD::Max];

	/** Hysteresis percentage on delta between the LOD distances */
	UPROPERTY(EditAnywhere, Category = "Mass|LOD", meta = (ClampMin = "0.0", UIMin = "0.0"), config)
	float BufferHysteresisOnDistancePercentage = 10.0f;

	/** Maximum limit of entity per LOD */
	UPROPERTY(EditAnywhere, Category = "Mass|LOD", config)
	int32 LODMaxCount[EMassLOD::Max];

	/** Rate in seconds at which entities should update when in this LOD */
	UPROPERTY(EditAnywhere, Category = "Mass|LOD", config)
	float TickRates[EMassLOD::Max];

	/** If true, will spread the first simulation update over TickRate period */
	UPROPERTY(EditAnywhere, Category = "Mass|Representation", config)
	bool bSpreadFirstSimulationUpdate = false;
};


USTRUCT()
struct FMassSimulationLODSharedFragment : public FMassSharedFragment
{
	GENERATED_BODY()

	FMassSimulationLODSharedFragment() = default;
	FMassSimulationLODSharedFragment(const FMassSimulationLODConfig& Config);

	/** Runtime data for matching the LOD config */
	bool bHasAdjustedDistancesFromCount = false;
	TMassLODCalculator<FMassSimulationLODLogic> LODCalculator;
	TMassLODTickRateController<FMassSimulationVariableTickChunkFragment, FMassSimulationLODLogic> LODTickRateController;
};

UCLASS(meta = (DisplayName = "Simulation LOD"))
class MASSLOD_API UMassSimulationLODProcessor : public UMassProcessor_LODBase
{
	GENERATED_BODY()

public:
	UMassSimulationLODProcessor();

protected:

	virtual void ConfigureQueries() override;
	virtual void Execute(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context) override;

	void CalculateLODForConfig(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context, FMassSimulationLODConfig& LODConfig);

	FMassEntityQuery EntityQuery;
	FMassEntityQuery EntityQueryCalculateLOD;
	FMassEntityQuery EntityQueryAdjustDistances;
};
