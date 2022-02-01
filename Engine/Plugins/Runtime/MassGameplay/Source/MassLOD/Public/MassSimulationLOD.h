// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassLODManager.h"
#include "MassLODCollector.h"
#include "MassLODCalculator.h"
#include "MassLODTickRateController.h"
#include "MassLODLogic.h"
#include "MassEntityTypes.h"
#include "MassLODFragments.h"

#include "MassSimulationLOD.generated.h"

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
};

USTRUCT()
struct MASSLOD_API FMassSimulationVariableTickFragment : public FMassFragment
{
	GENERATED_BODY()

	/** Accumulated delta time to use upon next tick */
	float LastTickedTime = 0.0f;
	float DeltaTime = 0.0f;
};

USTRUCT()
struct MASSLOD_API FMassSimulationVariableTickChunkFragment : public FMassVariableTickChunkFragment
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
struct MASSLOD_API FMassSimulationLODConfig : public FMassSharedFragment
{
	GENERATED_BODY()

	FMassSimulationLODConfig();

	/** Distance where each LOD becomes relevant */
	UPROPERTY(EditAnywhere, Category = "LOD", config)
	float LODDistance[EMassLOD::Max];

	/** Hysteresis percentage on delta between the LOD distances */
	UPROPERTY(EditAnywhere, Category = "LOD", meta = (ClampMin = "0.0", UIMin = "0.0"), config)
	float BufferHysteresisOnDistancePercentage = 10.0f;

	/** Maximum limit of entity per LOD */
	UPROPERTY(EditAnywhere, Category = "LOD", config)
	int32 LODMaxCount[EMassLOD::Max];

	/** If true, will set the associated LOD tag on the entity */
	UPROPERTY(EditAnywhere, Category = "LOD", config)
	bool bSetLODTags = false;
};

USTRUCT()
struct MASSLOD_API FMassSimulationVariableTickConfig : public FMassSharedFragment
{
	GENERATED_BODY()

	FMassSimulationVariableTickConfig();

	/** Rate in seconds at which entities should update when in this LOD */
	UPROPERTY(EditAnywhere, Category = "VariableTick", config)
	float TickRates[EMassLOD::Max];

	/** If true, will spread the first simulation update over TickRate period */
	UPROPERTY(EditAnywhere, Category = "VariableTick", config)
	bool bSpreadFirstSimulationUpdate = false;
};

USTRUCT()
struct MASSLOD_API FMassSimulationLODSharedFragment : public FMassSharedFragment
{
	GENERATED_BODY()

	FMassSimulationLODSharedFragment() = default;
	FMassSimulationLODSharedFragment(const FMassSimulationLODConfig& Config);

	/** Runtime data for matching the simulation LOD config */
	TMassLODCalculator<FMassSimulationLODLogic> LODCalculator;
	bool bHasAdjustedDistancesFromCount = false;
};

USTRUCT()
struct MASSLOD_API FMassSimulationVariableTickSharedFragment : public FMassSharedFragment
{
	GENERATED_BODY()

	FMassSimulationVariableTickSharedFragment() = default;
	FMassSimulationVariableTickSharedFragment(const FMassSimulationVariableTickConfig& Config);

	/** Runtime data for matching the simulation tick rate config */
	TMassLODTickRateController<FMassSimulationVariableTickChunkFragment, FMassSimulationLODLogic> LODTickRateController;

	static bool ShouldCalculateLODForChunk(const FMassExecutionContext& Context)
	{
		if (const FMassSimulationVariableTickSharedFragment* TickRateSharedFragment = Context.GetSharedFragmentPtr<FMassSimulationVariableTickSharedFragment>())
		{
			return TickRateSharedFragment->LODTickRateController.ShouldCalculateLODForChunk(Context);
		}
		return true;
	}

	static bool ShouldAdjustLODFromCountForChunk(const FMassExecutionContext& Context)
	{
		if (const FMassSimulationVariableTickSharedFragment* TickRateSharedFragment = Context.GetSharedFragmentPtr<FMassSimulationVariableTickSharedFragment>())
		{
			return TickRateSharedFragment->LODTickRateController.ShouldAdjustLODFromCountForChunk(Context);
		}
		return true;
	}
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
	FMassEntityQuery EntityQueryVariableTick;
	FMassEntityQuery EntityQuerySetLODTag;
};
