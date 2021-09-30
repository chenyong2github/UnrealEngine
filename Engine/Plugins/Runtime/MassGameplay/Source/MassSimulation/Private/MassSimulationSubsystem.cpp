// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassSimulationSubsystem.h"
#include "MassSimulationLocalCoordinator.h"
#include "MassEntitySubsystem.h"
#include "MassExecutor.h"
#include "MassSimulationSettings.h"
#include "VisualLogger/VisualLogger.h"
#if WITH_EDITOR
#include "Editor.h"
#endif // WITH_EDITOR

DEFINE_LOG_CATEGORY(LogMassSim);

namespace UE::MassSimulation
{
	int32 bDoEntityCompaction = 1;
	FAutoConsoleVariableRef CVarEntityCompaction(TEXT("ai.mass.EntityCompaction"), bDoEntityCompaction, TEXT("Maximize the nubmer of entities per chunk"), ECVF_Cheat);
}

//----------------------------------------------------------------------//
// UMassSimulationSubsystem
//----------------------------------------------------------------------//
UMassSimulationSubsystem::UMassSimulationSubsystem(const FObjectInitializer& ObjectInitializer)
	: Super()
{
	PhaseManager = CreateDefaultSubobject<UMassProcessingPhaseManager>(TEXT("PhaseManager"));
}

void UMassSimulationSubsystem::PostInitProperties()
{
	REDIRECT_OBJECT_TO_VLOG(PhaseManager, this);

	Super::PostInitProperties();

	if (HasAnyFlags(RF_ClassDefaultObject) == false)
	{
#if WITH_EDITOR
		GET_MASS_CONFIG_VALUE(GetOnTickSchematicChanged()).AddUObject(this, &UMassSimulationSubsystem::RebuildTickPipeline);
#endif // WITH_EDITOR
	}
}

void UMassSimulationSubsystem::BeginDestroy()
{
#if WITH_EDITOR
	if (PieBeginEventHandle.IsValid())
	{
		FEditorDelegates::BeginPIE.Remove(PieBeginEventHandle);
	}
	if (PieEndedEventHandle.IsValid())
	{
		FEditorDelegates::PrePIEEnded.Remove(PieEndedEventHandle);
	}
#endif //  WITH_EDITOR
	Super::BeginDestroy();
}

FMassProcessingPhase::FOnPhaseEvent& UMassSimulationSubsystem::GetOnProcessingPhaseStarted(const EMassProcessingPhase Phase) const
{
	check(PhaseManager);
	return PhaseManager->GetOnPhaseStart(Phase);
}

FMassProcessingPhase::FOnPhaseEvent& UMassSimulationSubsystem::GetOnProcessingPhaseFinished(const EMassProcessingPhase Phase) const
{
	check(PhaseManager);
	return PhaseManager->GetOnPhaseEnd(Phase);
}

void UMassSimulationSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	EntitySubsystem = Collection.InitializeDependency<UMassEntitySubsystem>();
	check(EntitySubsystem);
	
	GetOnProcessingPhaseStarted(EMassProcessingPhase::PrePhysics).AddUObject(this, &UMassSimulationSubsystem::OnProcessingPhaseStarted, EMassProcessingPhase::PrePhysics);
}

void UMassSimulationSubsystem::Deinitialize()
{
#if WITH_EDITOR
	GET_MASS_CONFIG_VALUE(GetOnTickSchematicChanged()).RemoveAll(this);
#endif // WITH_EDITOR
	GetOnProcessingPhaseStarted(EMassProcessingPhase::PrePhysics).RemoveAll(this);
	StopSimulation();

	Super::Deinitialize();
}

void UMassSimulationSubsystem::PostInitialize()
{
	Super::PostInitialize();

#if WITH_EDITOR
	UWorld* World = GetWorld();
	if (GEditor && World!= nullptr && !World->IsGameWorld())
	{
		// in editor worlds we need to rebuild the pipeline at this point since OnWorldBeginPlay won't be called
		RebuildTickPipeline();

		PieBeginEventHandle = FEditorDelegates::BeginPIE.AddUObject(this, &UMassSimulationSubsystem::OnPieBegin);
		PieEndedEventHandle = FEditorDelegates::PrePIEEnded.AddUObject(this, &UMassSimulationSubsystem::OnPieEnded);

		// note that this starts ticking for the editor world
		StartSimulation(*World);
	}
#endif // WITH_EDITOR
}

void UMassSimulationSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	// To evaluate the effective processors execution mode, we need to wait on OnWorldBeginPlay before calling RebuildTickPipeline as we are sure by this time the network is setup correctly.
	RebuildTickPipeline();
	// note that we're running for a game world right now. This means the PhaseManager->Start in OnPostWorldInit won't get called
	StartSimulation(InWorld);
}

void UMassSimulationSubsystem::RebuildTickPipeline()
{
	check(PhaseManager);
	PhaseManager->InitializePhases(*this);
}

void UMassSimulationSubsystem::StartSimulation(UWorld& InWorld)
{
	check(PhaseManager);
	PhaseManager->Start(InWorld);

	bSimulationStarted = true;

	OnSimulationStarted.Broadcast(&InWorld);
}

void UMassSimulationSubsystem::StopSimulation()
{
	check(PhaseManager);
	PhaseManager->Stop();

	bSimulationStarted = false;
}

void UMassSimulationSubsystem::OnProcessingPhaseStarted(const float DeltaSeconds, const EMassProcessingPhase Phase) const
{
	switch (Phase)
	{
		case EMassProcessingPhase::PrePhysics:
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(DoEntityCompation);
				check(EntitySubsystem);
				if (UE::MassSimulation::bDoEntityCompaction)
				{
					EntitySubsystem->DoEntityCompaction(GET_MASS_CONFIG_VALUE(DesiredEntityCompactionTimeSlicePerTick));
				}
			}
			break;
		default:
			// unhandled phases, by design, not every phase needs to be handled by the Actor subsystem
			break;
	}
}


#if WITH_EDITOR
void UMassSimulationSubsystem::OnPieBegin(const bool bIsSimulation)
{
	check(PhaseManager);
	// called so that we're not processing phases for the editor world while PIE/SIE is running
	StopSimulation();
}

void UMassSimulationSubsystem::OnPieEnded(const bool bIsSimulation)
{
	if (UWorld * World = GetWorld())
	{
		// Resume processing phases in the editor world.
		StartSimulation(*World);
	}
}
#endif // WITH_EDITOR