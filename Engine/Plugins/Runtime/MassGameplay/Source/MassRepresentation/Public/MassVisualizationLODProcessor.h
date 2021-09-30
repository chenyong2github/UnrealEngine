// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassCommonFragments.h"
#include "MassRepresentationFragments.h"
#include "MassLODCalculator.h"

#include "MassVisualizationLODProcessor.generated.h"

UCLASS()
class MASSREPRESENTATION_API UMassVisualizationLODProcessor : public UMassProcessor_LODBase
{
	GENERATED_BODY()

public:
	UMassVisualizationLODProcessor();

protected:

	/** Configure the owned FMassEntityQuery instances to express processor's requirements */
	virtual void ConfigureQueries() override;

	/**
	 * Initialize the processor 
	 * @param Owner of the Processor
	 */
	virtual void Initialize(UObject& Owner) override;

	/** 
	 * Execution method for this processor 
	 * @param EntitySubsystem is the system to execute the lambdas on each entity chunk
	 * @param Context is the execution context to be passed when executing the lambdas
	 */
	virtual void Execute(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context) override;

	void PrepareExecution();

	template <typename TMassViewerLODInfoFragment = FMassLODInfoFragment>
	void ExecuteInternal(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context);

	
	/**
	 * Forces Off LOD on all calculation
	 * @param bForce to whether force or not Off LOD
	 */
	void ForceOffLOD(bool bForce) { bForceOFFLOD = bForce; }

protected:
	/** Distances where each LOD becomes relevant */
	UPROPERTY(EditAnywhere, Category = "Mass|LOD", config)
	float BaseLODDistance[EMassLOD::Max];
	UPROPERTY(EditAnywhere, Category = "Mass|LOD", config)
	float VisibleLODDistance[EMassLOD::Max];
	UPROPERTY(EditAnywhere, Category = "Mass|LOD", meta = (ClampMin = "0.0", UIMin = "0.0"), config)
	float BufferHysteresisOnDistancePercentage = 10.0f;

	/** Maximum limit for each entity per LOD */
	UPROPERTY(EditAnywhere, Category = "Mass|LOD", config)
	int32 LODMaxCount[EMassLOD::Max];

	TMassLODCalculator<FMassRepresentationLODLogic> LODCalculator;

	FMassEntityQuery CloseEntityQuery;
	FMassEntityQuery FarEntityQuery;

	bool bForceOFFLOD = false;
};

namespace UE::MassRepresentation
{
	extern MASSREPRESENTATION_API int32 bDebugRepresentationLOD;
} // UE::MassRepresentation

template <typename TMassViewerLODInfoFragment>
void UMassVisualizationLODProcessor::ExecuteInternal(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context)
{
	if (bForceOFFLOD)
	{
		CloseEntityQuery.ForEachEntityChunk(EntitySubsystem, Context, [this](FMassExecutionContext& Context)
		{
			TArrayView<FMassRepresentationLODFragment> RepresentationLODList = Context.GetMutableComponentView<FMassRepresentationLODFragment>();
			LODCalculator.ForceOffLOD(Context, RepresentationLODList);
		});
		return;
	}

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PrepareExecution)

		PrepareExecution();
	}

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(CalculateLOD)

		// @todo this is the only block in this processor actually using FMassRepresentationFragment. Consider refactoring it into a separate Processor
		// the "debug" section below is non-production, should be separated as well
		auto CalculateLOD = [this](FMassExecutionContext& Context)
		{
			TArrayView<FMassRepresentationLODFragment> RepresentationLODList = Context.GetMutableComponentView<FMassRepresentationLODFragment>();
			TConstArrayView<TMassViewerLODInfoFragment> ViewerInfoList = Context.GetComponentView<TMassViewerLODInfoFragment>();
			LODCalculator.CalculateLOD(Context, RepresentationLODList, ViewerInfoList);
		};
		CloseEntityQuery.ForEachEntityChunk(EntitySubsystem, Context, CalculateLOD);

		// @todo this is the only block in this processor actually using FMassRepresentationFragment. Consider refactoring it into a separate Processor
		// the "debug" section below is non-production, should be separated as well
		FarEntityQuery.SetChunkFilter(&FMassVisualizationChunkFragment::ShouldUpdateVisualizationForChunk);
		FarEntityQuery.ForEachEntityChunk(EntitySubsystem, Context, [this, &CalculateLOD](FMassExecutionContext& Context)
			{
				CalculateLOD(Context);
			});
		FarEntityQuery.ClearChunkFilter();
	}

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(AdjustDistanceAndLODFromCount)
		if (LODCalculator.AdjustDistancesFromCount())
		{
			CloseEntityQuery.ForEachEntityChunk(EntitySubsystem, Context, [this](FMassExecutionContext& Context)
			{
				TConstArrayView<TMassViewerLODInfoFragment> ViewerInfoList = Context.GetComponentView<TMassViewerLODInfoFragment>();
				TArrayView<FMassRepresentationLODFragment> RepresentationLODList = Context.GetMutableComponentView<FMassRepresentationLODFragment>();
				LODCalculator.AdjustLODFromCount(Context, RepresentationLODList, ViewerInfoList);
			});
			// Far entities do not need to maximice count
		}
	}

	// Optional debug display
	if(UE::MassRepresentation::bDebugRepresentationLOD)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(DebugDisplayLOD)

		auto DebugDisplayLOD = [this](FMassExecutionContext& Context)
		{
			TConstArrayView<FMassRepresentationLODFragment> RepresentationLODList = Context.GetComponentView<FMassRepresentationLODFragment>();
			TConstArrayView<FDataFragment_Transform> TransformList = Context.GetComponentView<FDataFragment_Transform>();
			LODCalculator.DebugDisplayLOD(Context, RepresentationLODList, TransformList, World);
		};

		CloseEntityQuery.ForEachEntityChunk(EntitySubsystem, Context, DebugDisplayLOD);
		FarEntityQuery.ForEachEntityChunk(EntitySubsystem, Context, DebugDisplayLOD);
	}
}