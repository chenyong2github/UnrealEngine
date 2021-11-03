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

void UMassProcessor_MassSimulationLODViewersInfo::Initialize(UObject& Owner)
{
	Super::Initialize(Owner);

	LODCollector.Initialize();
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
	LODConfigs.AddDefaulted();
}

void UMassSimulationLODProcessor::ConfigureQueries()
{
	if (LODConfigs.Num() == 0)
	{
		UE_VLOG_UELOG(this, LogMassLOD, Warning, TEXT("Using MassSimulationLODProcessor while no LOD configs set (see MassSimulationLODProcessor.LODConfigs)."));
	}

	for (FMassSimulationLODConfig& LODConfig : LODConfigs)
	{
		if(LODConfig.TagFilter.GetScriptStruct())
		{
			LODConfig.EntityQuery.AddTagRequirement(*LODConfig.TagFilter.GetScriptStruct(), EMassFragmentPresence::All);
		}
		LODConfig.EntityQuery.AddRequirement<FDataFragment_Transform>(EMassFragmentAccess::ReadOnly);
		LODConfig.EntityQuery.AddRequirement<FMassLODInfoFragment>(EMassFragmentAccess::ReadOnly);
		LODConfig.EntityQuery.AddRequirement<FMassSimulationLODFragment>(EMassFragmentAccess::ReadWrite);
		LODConfig.EntityQuery.AddChunkRequirement<FMassSimulationVariableTickChunkFragment>(EMassFragmentAccess::ReadOnly);
	}
}

void UMassSimulationLODProcessor::Initialize(UObject& InOwner)
{
	for (FMassSimulationLODConfig& LODConfig : LODConfigs)
	{
		LODConfig.LODCalculator.Initialize(LODConfig.LODDistance, LODConfig.BufferHysteresisOnDistancePercentage / 100.0f, LODConfig.LODMaxCount);
		LODConfig.LODTickRateController.Initialize(LODConfig.TickRates, LODConfig.bSpreadFirstSimulationUpdate);
	}

	Super::Initialize(InOwner);
}

void UMassSimulationLODProcessor::Execute(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("SimulationLOD"))

	for (FMassSimulationLODConfig& LODConfig : LODConfigs)
	{
		CalculateLODForConfig(EntitySubsystem, Context, LODConfig);
	}
}

void UMassSimulationLODProcessor::CalculateLODForConfig(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context, FMassSimulationLODConfig& LODConfig)
{
	check(LODManager);
	const TArray<FViewerInfo>& Viewers = LODManager->GetViewers();
	LODConfig.LODCalculator.PrepareExecution(Viewers);

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("CalculateLOD"));

		LODConfig.EntityQuery.ForEachEntityChunk(EntitySubsystem, Context, [&LODConfig](FMassExecutionContext& Context)
		{
			if (!LODConfig.LODTickRateController.ShouldCalculateLODForChunk(Context))
			{
				return;
			}

			const TConstArrayView<FMassLODInfoFragment> ViewersInfoList = Context.GetFragmentView<FMassLODInfoFragment>();
			const TArrayView<FMassSimulationLODFragment> SimulationLODFragments = Context.GetMutableFragmentView<FMassSimulationLODFragment>();
			LODConfig.LODCalculator.CalculateLOD(Context, SimulationLODFragments, ViewersInfoList);
		});
	}

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("AdjustDistancesAndLODFromCount"));

		if (LODConfig.LODCalculator.AdjustDistancesFromCount())
		{
			LODConfig.EntityQuery.ForEachEntityChunk(EntitySubsystem, Context, [&LODConfig](FMassExecutionContext& Context)
			{
				if (!LODConfig.LODTickRateController.ShouldAdjustLODFromCountForChunk(Context))
				{
					return;
				}

				const TConstArrayView<FMassLODInfoFragment> ViewersInfoList = Context.GetFragmentView<FMassLODInfoFragment>();
				const TArrayView<FMassSimulationLODFragment> SimulationLODFragments = Context.GetMutableFragmentView<FMassSimulationLODFragment>();
				LODConfig.LODCalculator.AdjustLODFromCount(Context, SimulationLODFragments, ViewersInfoList);
			});
		}
	}

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("VariableTickRates"))
		check(World);
		const float Time = World->GetTimeSeconds();
		LODConfig.EntityQuery.ForEachEntityChunk(EntitySubsystem, Context, [&LODConfig, Time](FMassExecutionContext& Context)
		{
			TArrayView<FMassSimulationLODFragment> SimulationLODFragments = Context.GetMutableFragmentView<FMassSimulationLODFragment>();
			LODConfig.LODTickRateController.UpdateTickRateFromLOD(Context, SimulationLODFragments, Time);
		});
	}

	// Optional debug display
	if (UE::MassLOD::bDebugSimulationLOD)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("DebugDisplayLOD"));

		LODConfig.EntityQuery.ForEachEntityChunk(EntitySubsystem, Context, [this, &LODConfig](FMassExecutionContext& Context)
		{
			const TConstArrayView<FDataFragment_Transform> LocationList = Context.GetFragmentView<FDataFragment_Transform>();
			const TConstArrayView<FMassSimulationLODFragment> SimulationLODList = Context.GetFragmentView<FMassSimulationLODFragment>();
			LODConfig.LODCalculator.DebugDisplayLOD(Context, SimulationLODList, LocationList, World);
		});
	}
}