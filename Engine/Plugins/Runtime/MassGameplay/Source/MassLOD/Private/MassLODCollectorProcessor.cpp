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

void UMassLODCollectorProcessor::Initialize(UObject& InOwner)
{
	Super::Initialize(InOwner);

	for (FMassCollectorLODConfig& LODConfig : LODConfigs)
	{
		LODConfig.RepresentationLODCollector.Initialize(LODConfig.FOVAnglesToDriveVisibility, LODConfig.BufferHysteresisOnFOVPercentage / 100.f);
		LODConfig.SimulationLODCollector.Initialize();
		LODConfig.CombinedLODCollector.Initialize(LODConfig.FOVAnglesToDriveVisibility, LODConfig.BufferHysteresisOnFOVPercentage / 100.f);
	}
}

void UMassLODCollectorProcessor::ConfigureQueries()
{
	for (FMassCollectorLODConfig& LODConfig : LODConfigs)
	{
		FLWComponentQuery BaseQuery;
		if (LODConfig.TagFilter.GetScriptStruct())
		{
			BaseQuery.AddTagRequirement(*LODConfig.TagFilter.GetScriptStruct(), ELWComponentPresence::All);
		}
		BaseQuery.AddRequirement<FDataFragment_Transform>(ELWComponentAccess::ReadOnly);
		BaseQuery.AddRequirement<FMassLODInfoFragment>(ELWComponentAccess::ReadWrite);

		LODConfig.OnLODEntityQuery = BaseQuery;
		LODConfig.OnLODEntityQuery.AddTagRequirement<FMassOffLODTag>(ELWComponentPresence::None);
		LODConfig.OffLODEntityQuery_Conditional = BaseQuery;
		LODConfig.OffLODEntityQuery_Conditional.AddTagRequirement<FMassOffLODTag>(ELWComponentPresence::All);
		LODConfig.OffLODEntityQuery_Conditional.AddChunkRequirement<FMassSimulationVariableTickChunkFragment>(ELWComponentAccess::ReadOnly);
		LODConfig.OffLODEntityQuery_Conditional.SetChunkFilter(&FMassSimulationVariableTickChunkFragment::ShouldTickChunkThisFrame);

		LODConfig.CloseEntityQuery = BaseQuery;
		LODConfig.CloseEntityQuery.AddTagRequirement<FMassVisibilityCulledByDistanceTag>(ELWComponentPresence::None);
		LODConfig.FarEntityQuery_Conditional = BaseQuery;
		LODConfig.FarEntityQuery_Conditional.AddTagRequirement<FMassVisibilityCulledByDistanceTag>(ELWComponentPresence::All);
		LODConfig.FarEntityQuery_Conditional.AddChunkRequirement<FMassVisualizationChunkFragment>(ELWComponentAccess::ReadOnly);
		LODConfig.FarEntityQuery_Conditional.SetChunkFilter(&FMassVisualizationChunkFragment::ShouldUpdateVisualizationForChunk);

		// @todo: Cleanup once we can do more complex logic with entity queries. There is no other way to do "Notall", we need to create by hand 3 queries to achieve it.
		LODConfig.CloseAndOnLODEntityQuery = BaseQuery;
		LODConfig.CloseAndOnLODEntityQuery.AddTagRequirement<FMassVisibilityCulledByDistanceTag>(ELWComponentPresence::None);
		LODConfig.CloseAndOnLODEntityQuery.AddTagRequirement<FMassOffLODTag>(ELWComponentPresence::None);
		LODConfig.CloseAndOffLODEntityQuery = BaseQuery;
		LODConfig.CloseAndOffLODEntityQuery.AddTagRequirement<FMassVisibilityCulledByDistanceTag>(ELWComponentPresence::None);
		LODConfig.CloseAndOffLODEntityQuery.AddTagRequirement<FMassOffLODTag>(ELWComponentPresence::All);
		LODConfig.FarAndOnLODEntityQuery = BaseQuery;
		LODConfig.FarAndOnLODEntityQuery.AddTagRequirement<FMassVisibilityCulledByDistanceTag>(ELWComponentPresence::All);
		LODConfig.FarAndOnLODEntityQuery.AddTagRequirement<FMassOffLODTag>(ELWComponentPresence::None);
		LODConfig.FarAndOffLODEntityQuery_Conditional = BaseQuery;
		LODConfig.FarAndOffLODEntityQuery_Conditional.AddTagRequirement<FMassVisibilityCulledByDistanceTag>(ELWComponentPresence::All);
		LODConfig.FarAndOffLODEntityQuery_Conditional.AddTagRequirement<FMassOffLODTag>(ELWComponentPresence::All);
		LODConfig.FarAndOffLODEntityQuery_Conditional.AddChunkRequirement<FMassSimulationVariableTickChunkFragment>(ELWComponentAccess::ReadOnly);
		LODConfig.FarAndOffLODEntityQuery_Conditional.AddChunkRequirement<FMassVisualizationChunkFragment>(ELWComponentAccess::ReadOnly);
		LODConfig.FarAndOffLODEntityQuery_Conditional.SetChunkFilter([](const FLWComponentSystemExecutionContext& Context)
			{
				return Context.GetChunkComponent<FMassVisualizationChunkFragment>().ShouldUpdateVisualization()
					|| Context.GetChunkComponent<FMassSimulationVariableTickChunkFragment>().ShouldTickThisFrame();
			});
	}
}

template <typename TCollector>
void CollectLOD(UEntitySubsystem& EntitySubsystem, FLWComponentSystemExecutionContext& Context, const TArray<FViewerInfo>& Viewers, TCollector& Collector, TArrayView<FLWComponentQuery> HighFrequencyTickingEntityQueries, FLWComponentQuery& LowFrequencyTickingEntityQuery)
{
	Collector.PrepareExecution(Viewers);

	auto CollectLODInfo = [&Collector](FLWComponentSystemExecutionContext& Context)
	{
		TConstArrayView<FDataFragment_Transform> LocationList = Context.GetComponentView<FDataFragment_Transform>();
		TArrayView<FMassLODInfoFragment> ViewerInfoList = Context.GetMutableComponentView<FMassLODInfoFragment>();

		Collector.CollectLODInfo(Context, LocationList, ViewerInfoList);
	};

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("HighFrequency"))
		for(FLWComponentQuery& HighFrequencyTickingEntityQuery : HighFrequencyTickingEntityQueries)
		{
			HighFrequencyTickingEntityQuery.ForEachEntityChunk(EntitySubsystem, Context, CollectLODInfo);
		}
	}
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("LowFrequency"))
		LowFrequencyTickingEntityQuery.ForEachEntityChunk(EntitySubsystem, Context, CollectLODInfo);
	}
}

void UMassLODCollectorProcessor::Execute(UEntitySubsystem& EntitySubsystem, FLWComponentSystemExecutionContext& Context)
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
