// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassLODCollectorProcessor.h"
#include "MassLODUtils.h"
#include "MassCommonFragments.h"
#include "Engine/World.h"
#include "MassSimulationLOD.h"

UMassLODCollectorProcessor::UMassLODCollectorProcessor()
{
	ExecutionFlags = (int32)EProcessorExecutionFlags::All;

	ExecutionOrder.ExecuteInGroup = UE::Mass::ProcessorGroupNames::LODCollector;
	ExecutionOrder.ExecuteAfter.Add(UE::Mass::ProcessorGroupNames::SyncWorldToMass);
}

void UMassLODCollectorProcessor::ConfigureQueries()
{
	for (FMassCollectorLODConfig& LODConfig : LODConfigs)
	{
		FMassEntityQuery BaseQuery;
		if (LODConfig.TagFilter.GetScriptStruct())
		{
			BaseQuery.AddTagRequirement(*LODConfig.TagFilter.GetScriptStruct(), EMassFragmentPresence::All);
		}
		BaseQuery.AddRequirement<FDataFragment_Transform>(EMassFragmentAccess::ReadOnly);
		BaseQuery.AddRequirement<FMassViewerInfoFragment>(EMassFragmentAccess::ReadWrite);

		LODConfig.OnLODEntityQuery = BaseQuery;
		LODConfig.OnLODEntityQuery.AddTagRequirement<FMassOffLODTag>(EMassFragmentPresence::None);
		LODConfig.OffLODEntityQuery_Conditional = BaseQuery;
		LODConfig.OffLODEntityQuery_Conditional.AddTagRequirement<FMassOffLODTag>(EMassFragmentPresence::All);
		LODConfig.OffLODEntityQuery_Conditional.AddChunkRequirement<FMassSimulationVariableTickChunkFragment>(EMassFragmentAccess::ReadOnly);
		LODConfig.OffLODEntityQuery_Conditional.SetChunkFilter(&FMassSimulationVariableTickChunkFragment::ShouldTickChunkThisFrame);

		LODConfig.CloseEntityQuery = BaseQuery;
		LODConfig.CloseEntityQuery.AddTagRequirement<FMassVisibilityCulledByDistanceTag>(EMassFragmentPresence::None);
		LODConfig.FarEntityQuery_Conditional = BaseQuery;
		LODConfig.FarEntityQuery_Conditional.AddTagRequirement<FMassVisibilityCulledByDistanceTag>(EMassFragmentPresence::All);
		LODConfig.FarEntityQuery_Conditional.AddChunkRequirement<FMassVisualizationChunkFragment>(EMassFragmentAccess::ReadOnly);
		LODConfig.FarEntityQuery_Conditional.SetChunkFilter(&FMassVisualizationChunkFragment::ShouldUpdateVisualizationForChunk);

		// @todo: Cleanup once we can do more complex logic with entity queries. There is no other way to do "Notall", we need to create by hand 3 queries to achieve it.
		LODConfig.CloseAndOnLODEntityQuery = BaseQuery;
		LODConfig.CloseAndOnLODEntityQuery.AddTagRequirement<FMassVisibilityCulledByDistanceTag>(EMassFragmentPresence::None);
		LODConfig.CloseAndOnLODEntityQuery.AddTagRequirement<FMassOffLODTag>(EMassFragmentPresence::None);
		LODConfig.CloseAndOffLODEntityQuery = BaseQuery;
		LODConfig.CloseAndOffLODEntityQuery.AddTagRequirement<FMassVisibilityCulledByDistanceTag>(EMassFragmentPresence::None);
		LODConfig.CloseAndOffLODEntityQuery.AddTagRequirement<FMassOffLODTag>(EMassFragmentPresence::All);
		LODConfig.FarAndOnLODEntityQuery = BaseQuery;
		LODConfig.FarAndOnLODEntityQuery.AddTagRequirement<FMassVisibilityCulledByDistanceTag>(EMassFragmentPresence::All);
		LODConfig.FarAndOnLODEntityQuery.AddTagRequirement<FMassOffLODTag>(EMassFragmentPresence::None);
		LODConfig.FarAndOffLODEntityQuery_Conditional = BaseQuery;
		LODConfig.FarAndOffLODEntityQuery_Conditional.AddTagRequirement<FMassVisibilityCulledByDistanceTag>(EMassFragmentPresence::All);
		LODConfig.FarAndOffLODEntityQuery_Conditional.AddTagRequirement<FMassOffLODTag>(EMassFragmentPresence::All);
		LODConfig.FarAndOffLODEntityQuery_Conditional.AddChunkRequirement<FMassSimulationVariableTickChunkFragment>(EMassFragmentAccess::ReadOnly);
		LODConfig.FarAndOffLODEntityQuery_Conditional.AddChunkRequirement<FMassVisualizationChunkFragment>(EMassFragmentAccess::ReadOnly);
		LODConfig.FarAndOffLODEntityQuery_Conditional.SetChunkFilter([](const FMassExecutionContext& Context)
			{
				return Context.GetChunkFragment<FMassVisualizationChunkFragment>().ShouldUpdateVisualization()
					|| Context.GetChunkFragment<FMassSimulationVariableTickChunkFragment>().ShouldTickThisFrame();
			});
	}
}

template <typename TCollector>
void CollectLOD(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context, const TArray<FViewerInfo>& ViewersInfo, TCollector& Collector, TArrayView<FMassEntityQuery> HighFrequencyTickingEntityQueries, FMassEntityQuery& LowFrequencyTickingEntityQuery)
{
	Collector.PrepareExecution(ViewersInfo);

	auto CollectLODInfo = [&Collector](FMassExecutionContext& Context)
	{
		TConstArrayView<FDataFragment_Transform> LocationList = Context.GetFragmentView<FDataFragment_Transform>();
		TArrayView<FMassViewerInfoFragment> ViewerInfoList = Context.GetMutableFragmentView<FMassViewerInfoFragment>();

		Collector.CollectLODInfo(Context, LocationList, ViewerInfoList);
	};

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("HighFrequency"))
		for(FMassEntityQuery& HighFrequencyTickingEntityQuery : HighFrequencyTickingEntityQueries)
		{
			HighFrequencyTickingEntityQuery.ForEachEntityChunk(EntitySubsystem, Context, CollectLODInfo);
		}
	}
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("LowFrequency"))
		LowFrequencyTickingEntityQuery.ForEachEntityChunk(EntitySubsystem, Context, CollectLODInfo);
	}
}

void UMassLODCollectorProcessor::Execute(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context)
{
	check(LODManager);
	const TArray<FViewerInfo>& Viewers = LODManager->GetViewers();

	check(World);

	for (FMassCollectorLODConfig& LODConfig : LODConfigs)
	{
		if (World->IsNetMode(NM_Client))
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("RepresentationLODCollector"))
			CollectLOD(EntitySubsystem, Context, Viewers, LODConfig.RepresentationLODCollector, MakeArrayView(&LODConfig.CloseEntityQuery,1), LODConfig.FarEntityQuery_Conditional);
		}
		else if (World->IsNetMode(NM_DedicatedServer))
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("SimulationLODCollector"))
			CollectLOD(EntitySubsystem, Context, Viewers, LODConfig.SimulationLODCollector, MakeArrayView(&LODConfig.OnLODEntityQuery,1), LODConfig.OffLODEntityQuery_Conditional);
		}
		else
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("CombinedLODCollector"))
			CollectLOD(EntitySubsystem, Context, Viewers, LODConfig.CombinedLODCollector, MakeArrayView(&LODConfig.CloseAndOnLODEntityQuery,3), LODConfig.FarAndOffLODEntityQuery_Conditional);
		}
	}
}
