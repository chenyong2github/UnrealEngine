// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassSimulationLOD.h"

#include "MassCommonFragments.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "VisualLogger/VisualLogger.h"
#include "MassLODUtils.h"

//-----------------------------------------------------------------------------
// UMassProcessor_MassSimulationLODViewersInfo
//-----------------------------------------------------------------------------

UMassProcessor_MassSimulationLODViewersInfo::UMassProcessor_MassSimulationLODViewersInfo()
{
	bAutoRegisterWithProcessingPhases = false;
}

void UMassProcessor_MassSimulationLODViewersInfo::ConfigureQueries()
{
	EntityQuery.AddRequirement<FDataFragment_Transform>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FDataFragment_MassSimulationLODInfo>(EMassFragmentAccess::ReadWrite);
}

void UMassProcessor_MassSimulationLODViewersInfo::Execute(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context)
{
	check(LODManager);
	const TArray<FViewerInfo>& Viewers = LODManager->GetViewers();
	LODCollector.PrepareExecution(Viewers);

	EntityQuery.ForEachEntityChunk(EntitySubsystem, Context, [this](FMassExecutionContext& Context)
	{
		const TConstArrayView<FDataFragment_Transform> LocationList = Context.GetFragmentView<FDataFragment_Transform>();
		const TArrayView<FDataFragment_MassSimulationLODInfo> ViewersInfoList = Context.GetMutableFragmentView<FDataFragment_MassSimulationLODInfo>();
		LODCollector.CollectLODInfo(Context, LocationList, ViewersInfoList);
	});
}

//-----------------------------------------------------------------------------
// FMassSimulationLODConfig
//-----------------------------------------------------------------------------
FMassSimulationLODConfig::FMassSimulationLODConfig()
{
	LODDistance[EMassLOD::High] = 0.0f;
	LODDistance[EMassLOD::Medium] = 5000.0f;
	LODDistance[EMassLOD::Low] = 10000.0f;
	LODDistance[EMassLOD::Off] = 30000.0f;

	LODMaxCount[EMassLOD::High] = 100;
	LODMaxCount[EMassLOD::Medium] = 200;
	LODMaxCount[EMassLOD::Low] = 300;
	LODMaxCount[EMassLOD::Off] = INT_MAX;

	TickRates[EMassLOD::High] = 0.0f;
	TickRates[EMassLOD::Medium] = 0.5f;
	TickRates[EMassLOD::Low] = 1.0f;
	TickRates[EMassLOD::Off] = 1.5f;
}

//-----------------------------------------------------------------------------
// FMassSimulationLODSharedFragment
//-----------------------------------------------------------------------------
FMassSimulationLODSharedFragment::FMassSimulationLODSharedFragment(const FMassSimulationLODConfig& LODConfig)
{
	LODCalculator.Initialize(LODConfig.LODDistance, LODConfig.BufferHysteresisOnDistancePercentage / 100.0f, LODConfig.LODMaxCount);
	LODTickRateController.Initialize(LODConfig.TickRates, LODConfig.bSpreadFirstSimulationUpdate);
}

//-----------------------------------------------------------------------------
// UMassSimulationLODProcessor
//-----------------------------------------------------------------------------

namespace UE::MassLOD
{
	int32 bDebugSimulationLOD = 0;
	FAutoConsoleVariableRef CVarDebugSimulationLODTest(TEXT("ai.debug.SimulationLOD"), bDebugSimulationLOD, TEXT("Debug Simulation LOD"), ECVF_Cheat);
} // UE::MassLOD

UMassSimulationLODProcessor::UMassSimulationLODProcessor()
{
	ExecutionFlags = (int32)EProcessorExecutionFlags::All;
	ExecutionOrder.ExecuteInGroup = UE::Mass::ProcessorGroupNames::LOD;
	ExecutionOrder.ExecuteAfter.Add(UE::Mass::ProcessorGroupNames::LODCollector);
}

void UMassSimulationLODProcessor::ConfigureQueries()
{
	EntityQuery.AddRequirement<FDataFragment_Transform>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FMassViewerInfoFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FMassSimulationLODFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddChunkRequirement<FMassSimulationVariableTickChunkFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddConstSharedRequirement<FMassSimulationLODConfig>();
	EntityQuery.AddSharedRequirement<FMassSimulationLODSharedFragment>(EMassFragmentAccess::ReadWrite);

	EntityQueryCalculateLOD = EntityQuery;
	EntityQueryCalculateLOD.SetChunkFilter([](const FMassExecutionContext& Context)
	{
		const FMassSimulationLODSharedFragment& LODSharedFragment = Context.GetSharedFragment<FMassSimulationLODSharedFragment>();
		return LODSharedFragment.LODTickRateController.ShouldCalculateLODForChunk(Context);
	});

	EntityQueryAdjustDistances = EntityQuery;
	EntityQueryAdjustDistances.SetArchetypeFilter([](const FMassExecutionContext& Context)
	{
		const FMassSimulationLODSharedFragment& LODSharedFragment = Context.GetSharedFragment<FMassSimulationLODSharedFragment>();
		return LODSharedFragment.bHasAdjustedDistancesFromCount;
	});
	EntityQueryCalculateLOD.SetChunkFilter([](const FMassExecutionContext& Context)
	{
		const FMassSimulationLODSharedFragment& LODSharedFragment = Context.GetSharedFragment<FMassSimulationLODSharedFragment>();
		return LODSharedFragment.LODTickRateController.ShouldAdjustLODFromCountForChunk(Context);
	});
}

void UMassSimulationLODProcessor::Execute(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("SimulationLOD"))

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("PrepareExecution"));
		check(LODManager);
		const TArray<FViewerInfo>& Viewers = LODManager->GetViewers();

		EntitySubsystem.ForEachSharedFragment<FMassSimulationLODSharedFragment>([&Viewers](FMassSimulationLODSharedFragment& LODSharedFragment)
		{
			LODSharedFragment.LODCalculator.PrepareExecution(Viewers);
		});
	}

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("CalculateLOD"));
		EntityQueryCalculateLOD.ForEachEntityChunk(EntitySubsystem, Context, [](FMassExecutionContext& Context)
		{
			FMassSimulationLODSharedFragment& LODSharedFragment = Context.GetMutableSharedFragment<FMassSimulationLODSharedFragment>();
			TConstArrayView<FMassViewerInfoFragment> ViewersInfoList = Context.GetFragmentView<FMassViewerInfoFragment>();
			TArrayView<FMassSimulationLODFragment> SimulationLODFragments = Context.GetMutableFragmentView<FMassSimulationLODFragment>();
			LODSharedFragment.LODCalculator.CalculateLOD(Context, SimulationLODFragments, ViewersInfoList);
		});
	}

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("AdjustDistancesAndLODFromCount"));
		EntitySubsystem.ForEachSharedFragment<FMassSimulationLODSharedFragment>([](FMassSimulationLODSharedFragment& LODSharedFragment)
		{
			LODSharedFragment.bHasAdjustedDistancesFromCount = LODSharedFragment.LODCalculator.AdjustDistancesFromCount();
		});

		EntityQueryAdjustDistances.ForEachEntityChunk(EntitySubsystem, Context, [](FMassExecutionContext& Context)
		{
			FMassSimulationLODSharedFragment& LODSharedFragment = Context.GetMutableSharedFragment<FMassSimulationLODSharedFragment>();
			TConstArrayView<FMassViewerInfoFragment> ViewersInfoList = Context.GetFragmentView<FMassViewerInfoFragment>();
			TArrayView<FMassSimulationLODFragment> SimulationLODFragments = Context.GetMutableFragmentView<FMassSimulationLODFragment>();
			LODSharedFragment.LODCalculator.AdjustLODFromCount(Context, SimulationLODFragments, ViewersInfoList);
		});
	}

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("VariableTickRates"))
		check(World);
		const float Time = World->GetTimeSeconds();
		EntityQuery.ForEachEntityChunk(EntitySubsystem, Context, [Time](FMassExecutionContext& Context)
		{
			FMassSimulationLODSharedFragment& LODSharedFragment = Context.GetMutableSharedFragment<FMassSimulationLODSharedFragment>();
			TArrayView<FMassSimulationLODFragment> SimulationLODFragments = Context.GetMutableFragmentView<FMassSimulationLODFragment>();

			LODSharedFragment.LODTickRateController.UpdateTickRateFromLOD(Context, SimulationLODFragments, Time);
		});
	}

	// Optional debug display
	if (UE::MassLOD::bDebugSimulationLOD)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("DebugDisplayLOD"));
		EntityQuery.ForEachEntityChunk(EntitySubsystem, Context, [this](FMassExecutionContext& Context)
		{
			FMassSimulationLODSharedFragment& LODSharedFragment = Context.GetMutableSharedFragment<FMassSimulationLODSharedFragment>();
			TConstArrayView<FDataFragment_Transform> LocationList = Context.GetFragmentView<FDataFragment_Transform>();
			TConstArrayView<FMassSimulationLODFragment> SimulationLODList = Context.GetFragmentView<FMassSimulationLODFragment>();
			LODSharedFragment.LODCalculator.DebugDisplayLOD(Context, SimulationLODList, LocationList, World);
		});
	}
}