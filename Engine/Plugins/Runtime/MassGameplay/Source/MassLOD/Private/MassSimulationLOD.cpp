// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassSimulationLOD.h"

#include "MassCommonFragments.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"

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
	EntityQuery.AddRequirement<FDataFragment_Transform>(ELWComponentAccess::ReadOnly);
	EntityQuery.AddRequirement<FDataFragment_MassSimulationLODInfo>(ELWComponentAccess::ReadWrite);
}

void UMassProcessor_MassSimulationLODViewersInfo::Execute(UEntitySubsystem& EntitySubsystem, FLWComponentSystemExecutionContext& Context)
{
	check(LODManager);
	const TArray<FViewerInfo>& Viewers = LODManager->GetViewers();
	LODCollector.PrepareExecution(Viewers);

	EntityQuery.ForEachEntityChunk(EntitySubsystem, Context, [this](FLWComponentSystemExecutionContext& Context)
	{
		const TConstArrayView<FDataFragment_Transform> LocationList = Context.GetComponentView<FDataFragment_Transform>();
		const TArrayView<FDataFragment_MassSimulationLODInfo> ViewersInfoList = Context.GetMutableComponentView<FDataFragment_MassSimulationLODInfo>();
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
}

void UMassSimulationLODProcessor::ConfigureQueries()
{
	for (FMassSimulationLODConfig& LODConfig : LODConfigs)
	{
		if(LODConfig.TagFilter.GetScriptStruct())
		{
			LODConfig.EntityQuery.AddTagRequirement(*LODConfig.TagFilter.GetScriptStruct(), ELWComponentPresence::All);
		}
		LODConfig.EntityQuery.AddRequirement<FDataFragment_Transform>(ELWComponentAccess::ReadOnly);
		LODConfig.EntityQuery.AddRequirement<FMassLODInfoFragment>(ELWComponentAccess::ReadOnly);
		LODConfig.EntityQuery.AddRequirement<FMassSimulationLODFragment>(ELWComponentAccess::ReadWrite);
		LODConfig.EntityQuery.AddChunkRequirement<FMassSimulationVariableTickChunkFragment>(ELWComponentAccess::ReadOnly);
	}
}

void UMassSimulationLODProcessor::Initialize(UObject& InOwner)
{
	for (FMassSimulationLODConfig& LODConfig : LODConfigs)
	{
		LODConfig.LODCalculator.Initialize(LODConfig.LODDistance, LODConfig.BufferHysteresisOnDistancePercentage / 100.0f, LODConfig.LODMaxCount);
		LODConfig.LODTickRateController.Initialize(LODConfig.TickRates);
	}

	Super::Initialize(InOwner);
}

void UMassSimulationLODProcessor::Execute(UEntitySubsystem& EntitySubsystem, FLWComponentSystemExecutionContext& Context)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("SimulationLOD"))

	for (FMassSimulationLODConfig& LODConfig : LODConfigs)
	{
		CalculateLODForConfig(EntitySubsystem, Context, LODConfig);
	}
}

void UMassSimulationLODProcessor::CalculateLODForConfig(UEntitySubsystem& EntitySubsystem, FLWComponentSystemExecutionContext& Context, FMassSimulationLODConfig& LODConfig)
{
	check(LODManager);
	const TArray<FViewerInfo>& Viewers = LODManager->GetViewers();
	LODConfig.LODCalculator.PrepareExecution(Viewers);

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("CalculateLOD"));

		LODConfig.EntityQuery.ForEachEntityChunk(EntitySubsystem, Context, [&LODConfig](FLWComponentSystemExecutionContext& Context)
		{
			if (!LODConfig.LODTickRateController.ShouldCalculateLODForChunk(Context))
			{
				return;
			}

			const TConstArrayView<FMassLODInfoFragment> ViewersInfoList = Context.GetComponentView<FMassLODInfoFragment>();
			const TArrayView<FMassSimulationLODFragment> SimulationLODFragments = Context.GetMutableComponentView<FMassSimulationLODFragment>();
			LODConfig.LODCalculator.CalculateLOD(Context, SimulationLODFragments, ViewersInfoList);
		});
	}

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("AdjustDistancesAndLODFromCount"));

		if (LODConfig.LODCalculator.AdjustDistancesFromCount())
		{
			LODConfig.EntityQuery.ForEachEntityChunk(EntitySubsystem, Context, [&LODConfig](FLWComponentSystemExecutionContext& Context)
			{
				if (!LODConfig.LODTickRateController.ShouldAdjustLODFromCountForChunk(Context))
				{
					return;
				}

				const TConstArrayView<FMassLODInfoFragment> ViewersInfoList = Context.GetComponentView<FMassLODInfoFragment>();
				const TArrayView<FMassSimulationLODFragment> SimulationLODFragments = Context.GetMutableComponentView<FMassSimulationLODFragment>();
				LODConfig.LODCalculator.AdjustLODFromCount(Context, SimulationLODFragments, ViewersInfoList);
			});
		}
	}

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("VariableTickRates"))
		check(World);
		const float Time = World->GetTimeSeconds();
		LODConfig.EntityQuery.ForEachEntityChunk(EntitySubsystem, Context, [&LODConfig, Time](FLWComponentSystemExecutionContext& Context)
		{
			TArrayView<FMassSimulationLODFragment> SimulationLODFragments = Context.GetMutableComponentView<FMassSimulationLODFragment>();
			LODConfig.LODTickRateController.UpdateTickRateFromLOD(Context, SimulationLODFragments, Time);
		});
	}

	// Optional debug display
	if (UE::MassLOD::bDebugSimulationLOD)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("DebugDisplayLOD"));

		LODConfig.EntityQuery.ForEachEntityChunk(EntitySubsystem, Context, [this, &LODConfig](FLWComponentSystemExecutionContext& Context)
		{
			const TConstArrayView<FDataFragment_Transform> LocationList = Context.GetComponentView<FDataFragment_Transform>();
			const TConstArrayView<FMassSimulationLODFragment> SimulationLODList = Context.GetComponentView<FMassSimulationLODFragment>();
			LODConfig.LODCalculator.DebugDisplayLOD(Context, SimulationLODList, LocationList, World);
		});
	}
}