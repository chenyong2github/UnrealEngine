// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassVisualizationLODProcessor.h"

namespace UE::MassRepresentation
{
	int32 bDebugRepresentationLOD = 0;
	FAutoConsoleVariableRef CVarDebugRepresentationLOD(TEXT("ai.debug.RepresentationLOD"), bDebugRepresentationLOD, TEXT("Debug representation LOD"), ECVF_Cheat);
} // UE::MassRepresentation


UMassVisualizationLODProcessor::UMassVisualizationLODProcessor()
{
	bAutoRegisterWithProcessingPhases = false;

	ExecutionOrder.ExecuteInGroup = UE::Mass::ProcessorGroupNames::LOD;

	BaseLODDistance[EMassLOD::High] = 0.f;
	BaseLODDistance[EMassLOD::Medium] = 1000.f;
	BaseLODDistance[EMassLOD::Low] = 2500.f;
	BaseLODDistance[EMassLOD::Off] = 10000.f;
	
	VisibleLODDistance[EMassLOD::High] = 0.f;
	VisibleLODDistance[EMassLOD::Medium] = 2000.f;
	VisibleLODDistance[EMassLOD::Low] = 4000.f;
	VisibleLODDistance[EMassLOD::Off] = 10000.f;
	
	LODMaxCount[EMassLOD::High] = 50;
	LODMaxCount[EMassLOD::Medium] = 100;
	LODMaxCount[EMassLOD::Low] = 500;
	LODMaxCount[EMassLOD::Off] = 0;
}

void UMassVisualizationLODProcessor::ConfigureQueries()
{
	CloseEntityQuery.AddRequirement<FMassLODInfoFragment>(ELWComponentAccess::ReadOnly);
	CloseEntityQuery.AddRequirement<FMassRepresentationLODFragment>(ELWComponentAccess::ReadWrite);
	CloseEntityQuery.AddRequirement<FDataFragment_Transform>(ELWComponentAccess::ReadOnly);

	FarEntityQuery = CloseEntityQuery;

	CloseEntityQuery.AddTagRequirement<FMassVisibilityCulledByDistanceTag>(ELWComponentPresence::None);
	
	FarEntityQuery.AddTagRequirement<FMassVisibilityCulledByDistanceTag>(ELWComponentPresence::All);
	FarEntityQuery.AddChunkRequirement<FMassVisualizationChunkFragment>(ELWComponentAccess::ReadOnly);
}

void UMassVisualizationLODProcessor::Initialize(UObject& Owner)
{
	Super::Initialize(Owner);

	LODCalculator.Initialize(BaseLODDistance, BufferHysteresisOnDistancePercentage / 100.f, LODMaxCount, nullptr, VisibleLODDistance);
}

void UMassVisualizationLODProcessor::PrepareExecution()
{
	check(World);

	check(LODManager);
	const TArray<FViewerInfo>& Viewers = LODManager->GetViewers();
	LODCalculator.PrepareExecution(Viewers);
}

void UMassVisualizationLODProcessor::Execute(UEntitySubsystem& EntitySubsystem, FLWComponentSystemExecutionContext& Context)
{
	ExecuteInternal(EntitySubsystem, Context);
}