// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassProcessingPhase.h"
#include "MassProcessingTypes.h"
#include "MassEntityDebug.h"
#include "MassEntitySettings.h"
#include "MassProcessor.h"
#include "MassExecutor.h"
#include "MassEntitySubsystem.h"
#include "VisualLogger/VisualLogger.h"
#include "Engine/World.h"
#include "MassCommandBuffer.h"

#define LOCTEXT_NAMESPACE "Mass"

DECLARE_CYCLE_STAT(TEXT("Mass Phase Done"), STAT_MassPhaseDone, STATGROUP_TaskGraphTasks);

namespace FPipeTweakables
{
	bool bFullyParallel = false;

	FAutoConsoleVariableRef CVars[] = {
		{TEXT("pipe.FullyParallel"), bFullyParallel, TEXT("Enables pipe processing distribution to all available thread (via the task graph)")},
	};
}

//----------------------------------------------------------------------//
//  FMassProcessingPhase
//----------------------------------------------------------------------//
FMassProcessingPhase::FMassProcessingPhase()
{
	bCanEverTick = true;
	bStartWithTickEnabled = false;
}

void FMassProcessingPhase::ExecuteTick(float DeltaTime, ELevelTick TickType, ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
{
	if (TickType == LEVELTICK_ViewportsOnly || TickType == LEVELTICK_PauseTick)
	{
		return;
	}

	checkf(Manager, TEXT("Manager is null which is not a supported case. Either this FMassProcessingPhase has not been initialized properly or it's been left dangling after the FMassProcessingPhase owner got destroyed."));

	Manager->OnPhaseStart(*this);
	
	OnPhaseStart.Broadcast(DeltaTime);

	check(PhaseProcessor);
	
	FMassProcessingContext Context(Manager->GetEntitySubsystemRef(), DeltaTime);

	if (bRunInParallelMode)
	{
		bIsDuringPipeProcessing = true;
		const FGraphEventRef PipelineCompletionEvent = UE::Mass::Executor::TriggerParallelTasks(*PhaseProcessor, Context, [this, DeltaTime]()
			{
				OnParallelExecutionDone(DeltaTime);
			});

		if (PipelineCompletionEvent.IsValid())
		{
			MyCompletionGraphEvent->DontCompleteUntil(PipelineCompletionEvent);
		}
		else
		{
			OnParallelExecutionDone(DeltaTime);
		}
	}
	else
	{
		TGuardValue<bool> PipeRunningGuard(bIsDuringPipeProcessing, true);
		UE::Mass::Executor::Run(*PhaseProcessor, Context);

		OnPhaseEnd.Broadcast(DeltaTime);
		Manager->OnPhaseEnd(*this);
	}
}

void FMassProcessingPhase::OnParallelExecutionDone(const float DeltaTime)
{
	bIsDuringPipeProcessing = false;
	OnPhaseEnd.Broadcast(DeltaTime);
	check(Manager);
	Manager->OnPhaseEnd(*this);
}

FString FMassProcessingPhase::DiagnosticMessage()
{
	return (Manager ? Manager->GetFullName() : TEXT("NULL-PipeProcessingPhaseManager")) + TEXT("[ProcessorTick]");
}

FName FMassProcessingPhase::DiagnosticContext(bool bDetailed)
{
	return Manager ? Manager->GetClass()->GetFName() : TEXT("NULL-PipeProcessingPhaseManager");
}

//----------------------------------------------------------------------//
// UMassProcessingPhaseManager  
//----------------------------------------------------------------------//
void UMassProcessingPhaseManager::PostInitProperties()
{
	Super::PostInitProperties();
	if (HasAnyFlags(RF_ClassDefaultObject) == false)
	{
#if WITH_EDITOR
		UWorld* World = GetWorld();
		if (World && World->IsGameWorld() == false)
		{
			UMassSettings* PipeSettings = GetMutableDefault<UMassSettings>();
			check(PipeSettings);
			PipeSettingsChangeHandle = PipeSettings->GetOnSettingsChange().AddUObject(this, &UMassProcessingPhaseManager::OnPipeSettingsChange);
		}
#endif // WITH_EDITOR
	}
	CreatePhases();
}

void UMassProcessingPhaseManager::BeginDestroy()
{
	Stop();
	Super::BeginDestroy();

#if WITH_EDITOR
	if (UMassSettings* PipeSettings = GetMutableDefault<UMassSettings>())
	{
		PipeSettings->GetOnSettingsChange().Remove(PipeSettingsChangeHandle);
	}
#endif // WITH_EDITOR
}

void UMassProcessingPhaseManager::InitializePhases(UObject& InProcessorOwner)
{
	const FMassProcessingPhaseConfig* ProcessingPhasesConfig = GET_PIPE_CONFIG_VALUE(GetProcessingPhasesConfig());

	FString DependencyGraphFileName;

#if WITH_EDITOR
	const UWorld* World = InProcessorOwner.GetWorld();
	const UMassSettings* PipeSettings = GetMutableDefault<UMassSettings>();
	if (World != nullptr && PipeSettings != nullptr && !PipeSettings->DumpDependencyGraphFileName.IsEmpty())
	{
		DependencyGraphFileName = FString::Printf(TEXT("%s_%s"), *PipeSettings->DumpDependencyGraphFileName,*ToString(World->GetNetMode()));
	}
#endif // WITH_EDITOR

	for (int i = 0; i < int(EMassProcessingPhase::MAX); ++i)
	{
		const FMassProcessingPhaseConfig& PhaseConfig = ProcessingPhasesConfig[i];
		UMassCompositeProcessor* PhaseProcessor = ProcessingPhases[i].PhaseProcessor;
		FString FileName = !DependencyGraphFileName.IsEmpty() ? FString::Printf(TEXT("%s_%s"), *DependencyGraphFileName, *PhaseConfig.PhaseName.ToString()) : FString();
		PhaseProcessor->CopyAndSort(PhaseConfig, FileName);
		PhaseProcessor->Initialize(InProcessorOwner);
	}

#if WITH_MASSENTITY_DEBUG
	// print it all out to vislog
	UE_VLOG_UELOG(this, LogPipe, Verbose, TEXT("Phases initialization done. Current composition:"));

	FStringOutputDevice OutDescription;
	for (int i = 0; i < int(EMassProcessingPhase::MAX); ++i)
	{
		if (ProcessingPhases[i].PhaseProcessor)
		{
			ProcessingPhases[i].PhaseProcessor->DebugOutputDescription(OutDescription);
			UE_VLOG_UELOG(this, LogPipe, Verbose, TEXT("--- %s"), *OutDescription);
			OutDescription.Reset();
		}
	}	
#endif // WITH_MASSENTITY_DEBUG
}

void UMassProcessingPhaseManager::Start(UWorld& World)
{
	EntitySubsystem = UWorld::GetSubsystem<UMassEntitySubsystem>(&World);

	if (ensure(EntitySubsystem))
	{
		EnableTickFunctions(World);
	}
	else
	{
		UE_VLOG_UELOG(this, LogPipe, Error, TEXT("Called %s while missing the EntitySubsystem"), ANSI_TO_TCHAR(__FUNCTION__));
	}
}

void UMassProcessingPhaseManager::Start(UMassEntitySubsystem& InEntitySubsystem)
{
	UWorld* World = InEntitySubsystem.GetWorld();
	check(World);
	EntitySubsystem = &InEntitySubsystem;
	EnableTickFunctions(*World);
}

void UMassProcessingPhaseManager::EnableTickFunctions(const UWorld& World)
{
	check(EntitySubsystem);
	for (FMassProcessingPhase& Phase : ProcessingPhases)
	{
		Phase.Manager = this;
		Phase.RegisterTickFunction(World.PersistentLevel);
		Phase.SetTickFunctionEnable(true);
	}
	UE_VLOG_UELOG(this, LogPipe, Log, TEXT("UPipeTickManager %s.%s has been started")
		, *GetNameSafe(GetOuter()), *GetName());
}

void UMassProcessingPhaseManager::Stop()
{
	EntitySubsystem = nullptr;
	
	for (FMassProcessingPhase& Phase : ProcessingPhases)
	{
		Phase.SetTickFunctionEnable(false);
	}

	UE_VLOG_UELOG(this, LogPipe, Log, TEXT("UPipeTickManager %s.%s has been stopped")
		, *GetNameSafe(GetOuter()), *GetName());
}

void UMassProcessingPhaseManager::CreatePhases() 
{
	static ETickingGroup PhaseToTickingGroup[int(EMassProcessingPhase::MAX)]
	{
		ETickingGroup::TG_PrePhysics, // EMassProcessingPhase::PrePhysics
		ETickingGroup::TG_StartPhysics, // EMassProcessingPhase::StartPhysics
		ETickingGroup::TG_DuringPhysics, // EMassProcessingPhase::DuringPhysics
		ETickingGroup::TG_EndPhysics,	// EMassProcessingPhase::EndPhysics
		ETickingGroup::TG_PostPhysics,	// EMassProcessingPhase::PostPhysics
		ETickingGroup::TG_LastDemotable, // EMassProcessingPhase::FrameEnd
	};

	// @todo copy from settings instead of blindly creating from scratch
	for (int i = 0; i < int(EMassProcessingPhase::MAX); ++i)
	{
		ProcessingPhases[i].Phase = EMassProcessingPhase(i);
		ProcessingPhases[i].TickGroup = PhaseToTickingGroup[i];
		ProcessingPhases[i].PhaseProcessor = NewObject<UMassCompositeProcessor>(this, UMassCompositeProcessor::StaticClass()
			, *FString::Printf(TEXT("ProcessingPhase_%s"), *EnumToString(EMassProcessingPhase(i))));
		REDIRECT_OBJECT_TO_VLOG(ProcessingPhases[i].PhaseProcessor, this);
		ProcessingPhases[i].PhaseProcessor->SetGroupName(*EnumToString(EMassProcessingPhase(i)));
		ProcessingPhases[i].PhaseProcessor->SetProcessingPhase(EMassProcessingPhase(i));
	}
}

void UMassProcessingPhaseManager::OnPhaseStart(const FMassProcessingPhase& Phase)
{
	ensure(CurrentPhase == EMassProcessingPhase::MAX);
	CurrentPhase = Phase.Phase;
}

void UMassProcessingPhaseManager::OnPhaseEnd(FMassProcessingPhase& Phase)
{
	ensure(CurrentPhase == Phase.Phase);
	CurrentPhase = EMassProcessingPhase::MAX;

	// switch between parallel and single-thread versions only after a given batch of processing has been wrapped up	
	if (Phase.IsConfiguredForParallelMode() != FPipeTweakables::bFullyParallel)
	{
		if (FPipeTweakables::bFullyParallel)
		{
			Phase.ConfigureForParallelMode();
		}
		else
		{
			Phase.ConfigureForSingleThreadMode();
		}
	}
}

#if WITH_EDITOR
void UMassProcessingPhaseManager::OnPipeSettingsChange(const FPropertyChangedEvent& PropertyChangedEvent)
{
	check(GetOuter());
	InitializePhases(*GetOuter());
}
#endif // WITH_EDITOR

#undef LOCTEXT_NAMESPACE