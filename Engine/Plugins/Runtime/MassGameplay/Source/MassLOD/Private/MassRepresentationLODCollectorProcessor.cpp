// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassRepresentationLODCollectorProcessor.h"
#include "Engine/World.h"
#include "MassCommonFragments.h"
#include "GameFramework/PlayerController.h"

UMassRepresentationLODCollectorProcessor::UMassRepresentationLODCollectorProcessor()
{
	ExecutionFlags = (int32)EProcessorExecutionFlags::All;
}

void UMassRepresentationLODCollectorProcessor::ConfigureQueries()
{
	for (FMassRepresentationLODCollectorConfig& LODConfig : LODConfigs)
	{
		FMassEntityQuery BaseQuery;
		if (LODConfig.TagFilter.GetScriptStruct())
		{
			BaseQuery.AddTagRequirement(*LODConfig.TagFilter.GetScriptStruct(), EMassFragmentPresence::All);
		}
		BaseQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
		BaseQuery.AddRequirement<FMassViewerInfoFragment>(EMassFragmentAccess::ReadWrite);
		BaseQuery.AddChunkRequirement<FMassVisualizationChunkFragment>(EMassFragmentAccess::ReadOnly);

		LODConfig.CloseEntityQuery = BaseQuery;
		LODConfig.CloseEntityQuery.AddTagRequirement<FMassVisibilityCulledByDistanceTag>(EMassFragmentPresence::None);
		LODConfig.FarEntityQuery_Conditional = BaseQuery;
		LODConfig.FarEntityQuery_Conditional.AddTagRequirement<FMassVisibilityCulledByDistanceTag>(EMassFragmentPresence::All);
		LODConfig.FarEntityQuery_Conditional.SetChunkFilter(&FMassVisualizationChunkFragment::ShouldUpdateVisualizationForChunk);
	}
}

template <typename TCollector>
void CollectLODInfo(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context, const TArray<FViewerInfo>& Viewers, TCollector& Collector, FMassEntityQuery& HighFrequencyTickingEntityQuery, FMassEntityQuery& LowFrequencyTickingEntityQuery)
{
	Collector.PrepareExecution(Viewers);

	auto InternalCollectLODInfo = [&Collector](FMassExecutionContext& Context)
	{
		TConstArrayView<FTransformFragment> LocationList = Context.GetFragmentView<FTransformFragment>();
		TArrayView<FMassViewerInfoFragment> ViewerInfoList = Context.GetMutableFragmentView<FMassViewerInfoFragment>();

		Collector.CollectLODInfo(Context, LocationList, ViewerInfoList);
	};

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("HighFrequency"))
		HighFrequencyTickingEntityQuery.ForEachEntityChunk(EntitySubsystem, Context, InternalCollectLODInfo);
	}
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("LowFrequency"))
		LowFrequencyTickingEntityQuery.ForEachEntityChunk(EntitySubsystem, Context, InternalCollectLODInfo);
	}
}

void UMassRepresentationLODCollectorProcessor::Execute(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context)
{
	check(LODSubsystem);
	const TArray<FViewerInfo>& Viewers = LODSubsystem->GetViewers();

	check(World);

	for (FMassRepresentationLODCollectorConfig& LODConfig : LODConfigs)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("ViewerLODCollector"))
		CollectLODInfo(EntitySubsystem, Context, Viewers, LODConfig.RepresentationLODCollector, LODConfig.CloseEntityQuery, LODConfig.FarEntityQuery_Conditional);
	}
}
