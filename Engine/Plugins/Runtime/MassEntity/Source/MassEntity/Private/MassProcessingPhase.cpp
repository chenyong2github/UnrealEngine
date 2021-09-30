// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassProcessingPhase.h"
#include "MassEntityTypes.h"
#include "MassEntityDebug.h"
#include "MassEntitySettings.h"
#include "MassProcessor.h"
#include "MassExecutor.h"
#include "MassEntitySubsystem.h"
#include "VisualLogger/VisualLogger.h"
#include "Engine/World.h"
#include "LWCCommandBuffer.h"

#define LOCTEXT_NAMESPACE "Pipe"

DECLARE_CYCLE_STAT(TEXT("Pipe Phase Done"), STAT_PipePhaseDone, STATGROUP_TaskGraphTasks);

namespace FPipeTweakables
{
	bool bFullyParallel = false;

	FAutoConsoleVariableRef CVars[] = {
		{TEXT("pipe.FullyParallel"), bFullyParallel, TEXT("Enables pipe processing distribution to all available thread (via the task graph)")},
	};
}

//----------------------------------------------------------------------//
//  FPipeProcessingPhase
//----------------------------------------------------------------------//
FPipeProcessingPhase::FPipeProcessingPhase()
{
	bCanEverTick = true;
	bStartWithTickEnabled = false;
}

void FPipeProcessingPhase::ExecuteTick(float DeltaTime, ELevelTick TickType, ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
{
	if (TickType == LEVELTICK_ViewportsOnly || TickType == LEVELTICK_PauseTick)
	{
		return;
	}

	checkf(Manager, TEXT("Manager is null which is not a supported case. Either this FPipeProcessingPhase has not been initialized properly or it's been left dangling after the FPipeProcessingPhase owner got destroyed."));

	Manager->OnPhaseStart(*this);
	
	OnPhaseStart.Broadcast(DeltaTime);

	check(PhaseProcessor);
	
	FPipeContext Context(Manager->GetEntitySubsystemRef(), DeltaTime);

	if (bRunInParallelMode)
	{
		bIsDuringPipeProcessing = true;
		const FGraphEventRef PipelineCompletionEvent = UE::Pipe::Executor::TriggerParallelTasks(*PhaseProcessor, Context, [this, DeltaTime]()
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
		UE::Pipe::Executor::Run(*PhaseProcessor, Context);

		OnPhaseEnd.Broadcast(DeltaTime);
		Manager->OnPhaseEnd(*this);
	}
}

void FPipeProcessingPhase::OnParallelExecutionDone(const float DeltaTime)
{
	bIsDuringPipeProcessing = false;
	OnPhaseEnd.Broadcast(DeltaTime);
	check(Manager);
	Manager->OnPhaseEnd(*this);
}

FString FPipeProcessingPhase::DiagnosticMessage()
{
	return (Manager ? Manager->GetFullName() : TEXT("NULL-PipeProcessingPhaseManager")) + TEXT("[ProcessorTick]");
}

FName FPipeProcessingPhase::DiagnosticContext(bool bDetailed)
{
	return Manager ? Manager->GetClass()->GetFName() : TEXT("NULL-PipeProcessingPhaseManager");
}

//----------------------------------------------------------------------//
// UPipeProcessingPhaseManager  
//----------------------------------------------------------------------//
void UPipeProcessingPhaseManager::PostInitProperties()
{
	Super::PostInitProperties();
	if (HasAnyFlags(RF_ClassDefaultObject) == false)
	{
#if WITH_EDITOR
		UWorld* World = GetWorld();
		if (World && World->IsGameWorld() == false)
		{
			UPipeSettings* PipeSettings = GetMutableDefault<UPipeSettings>();
			check(PipeSettings);
			PipeSettingsChangeHandle = PipeSettings->GetOnSettingsChange().AddUObject(this, &UPipeProcessingPhaseManager::OnPipeSettingsChange);
		}
#endif // WITH_EDITOR
	}
	CreatePhases();
}

void UPipeProcessingPhaseManager::BeginDestroy()
{
	Stop();
	Super::BeginDestroy();

#if WITH_EDITOR
	if (UPipeSettings* PipeSettings = GetMutableDefault<UPipeSettings>())
	{
		PipeSettings->GetOnSettingsChange().Remove(PipeSettingsChangeHandle);
	}
#endif // WITH_EDITOR
}

void UPipeProcessingPhaseManager::InitializePhases(UObject& InProcessorOwner)
{
	const FPipeProcessingPhaseConfig* ProcessingPhasesConfig = GET_PIPE_CONFIG_VALUE(GetProcessingPhasesConfig());

	FString DependencyGraphFileName;

#if WITH_EDITOR
	const UWorld* World = InProcessorOwner.GetWorld();
	const UPipeSettings* PipeSettings = GetMutableDefault<UPipeSettings>();
	if (World != nullptr && PipeSettings != nullptr && !PipeSettings->DumpDependencyGraphFileName.IsEmpty())
	{
		DependencyGraphFileName = FString::Printf(TEXT("%s_%s"), *PipeSettings->DumpDependencyGraphFileName,*ToString(World->GetNetMode()));
	}
#endif // WITH_EDITOR

	for (int i = 0; i < int(EPipeProcessingPhase::MAX); ++i)
	{
		const FPipeProcessingPhaseConfig& PhaseConfig = ProcessingPhasesConfig[i];
		UPipeCompositeProcessor* PhaseProcessor = ProcessingPhases[i].PhaseProcessor;
		FString FileName = !DependencyGraphFileName.IsEmpty() ? FString::Printf(TEXT("%s_%s"), *DependencyGraphFileName, *PhaseConfig.PhaseName.ToString()) : FString();
		PhaseProcessor->CopyAndSort(PhaseConfig, FileName);
		PhaseProcessor->Initialize(InProcessorOwner);
	}

#if WITH_MASSENTITY_DEBUG
	// print it all out to vislog
	UE_VLOG_UELOG(this, LogPipe, Verbose, TEXT("Phases initialization done. Current composition:"));

	FStringOutputDevice OutDescription;
	for (int i = 0; i < int(EPipeProcessingPhase::MAX); ++i)
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

void UPipeProcessingPhaseManager::Start(UWorld& World)
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

void UPipeProcessingPhaseManager::Start(UMassEntitySubsystem& InEntitySubsystem)
{
	UWorld* World = InEntitySubsystem.GetWorld();
	check(World);
	EntitySubsystem = &InEntitySubsystem;
	EnableTickFunctions(*World);
}

void UPipeProcessingPhaseManager::EnableTickFunctions(const UWorld& World)
{
	check(EntitySubsystem);
	for (FPipeProcessingPhase& Phase : ProcessingPhases)
	{
		Phase.Manager = this;
		Phase.RegisterTickFunction(World.PersistentLevel);
		Phase.SetTickFunctionEnable(true);
	}
	UE_VLOG_UELOG(this, LogPipe, Log, TEXT("UPipeTickManager %s.%s has been started")
		, *GetNameSafe(GetOuter()), *GetName());
}

void UPipeProcessingPhaseManager::Stop()
{
	EntitySubsystem = nullptr;
	
	for (FPipeProcessingPhase& Phase : ProcessingPhases)
	{
		Phase.SetTickFunctionEnable(false);
	}

	UE_VLOG_UELOG(this, LogPipe, Log, TEXT("UPipeTickManager %s.%s has been stopped")
		, *GetNameSafe(GetOuter()), *GetName());
}

void UPipeProcessingPhaseManager::CreatePhases() 
{
	static ETickingGroup PhaseToTickingGroup[int(EPipeProcessingPhase::MAX)]
	{
		ETickingGroup::TG_PrePhysics, // EPipeProcessingPhase::PrePhysics
		ETickingGroup::TG_StartPhysics, // EPipeProcessingPhase::StartPhysics
		ETickingGroup::TG_DuringPhysics, // EPipeProcessingPhase::DuringPhysics
		ETickingGroup::TG_EndPhysics,	// EPipeProcessingPhase::EndPhysics
		ETickingGroup::TG_PostPhysics,	// EPipeProcessingPhase::PostPhysics
		ETickingGroup::TG_LastDemotable, // EPipeProcessingPhase::FrameEnd
	};

	// @todo copy from settings instead of blindly creating from scratch
	for (int i = 0; i < int(EPipeProcessingPhase::MAX); ++i)
	{
		ProcessingPhases[i].Phase = EPipeProcessingPhase(i);
		ProcessingPhases[i].TickGroup = PhaseToTickingGroup[i];
		ProcessingPhases[i].PhaseProcessor = NewObject<UPipeCompositeProcessor>(this, UPipeCompositeProcessor::StaticClass()
			, *FString::Printf(TEXT("ProcessingPhase_%s"), *EnumToString(EPipeProcessingPhase(i))));
		REDIRECT_OBJECT_TO_VLOG(ProcessingPhases[i].PhaseProcessor, this);
		ProcessingPhases[i].PhaseProcessor->SetGroupName(*EnumToString(EPipeProcessingPhase(i)));
		ProcessingPhases[i].PhaseProcessor->SetProcessingPhase(EPipeProcessingPhase(i));
	}
}

void UPipeProcessingPhaseManager::OnPhaseStart(const FPipeProcessingPhase& Phase)
{
	ensure(CurrentPhase == EPipeProcessingPhase::MAX);
	CurrentPhase = Phase.Phase;
}

void UPipeProcessingPhaseManager::OnPhaseEnd(FPipeProcessingPhase& Phase)
{
	ensure(CurrentPhase == Phase.Phase);
	CurrentPhase = EPipeProcessingPhase::MAX;

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
void UPipeProcessingPhaseManager::OnPipeSettingsChange(const FPropertyChangedEvent& PropertyChangedEvent)
{
	check(GetOuter());
	InitializePhases(*GetOuter());
}
#endif // WITH_EDITOR

#undef LOCTEXT_NAMESPACE