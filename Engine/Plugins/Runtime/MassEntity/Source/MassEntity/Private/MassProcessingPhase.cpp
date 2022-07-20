// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassProcessingPhase.h"
#include "MassProcessingTypes.h"
#include "MassDebugger.h"
#include "MassEntitySettings.h"
#include "MassProcessor.h"
#include "MassExecutor.h"
#include "MassEntitySubsystem.h"
#include "VisualLogger/VisualLogger.h"
#include "Engine/World.h"
#include "MassCommandBuffer.h"

#define LOCTEXT_NAMESPACE "Mass"

DECLARE_CYCLE_STAT(TEXT("Mass Phase Done"), STAT_MassPhaseDone, STATGROUP_TaskGraphTasks);

namespace UE::Mass::Tweakables
{
	bool bFullyParallel = MASS_DO_PARALLEL;

	FAutoConsoleVariableRef CVars[] = {
		{TEXT("mass.FullyParallel"), bFullyParallel, TEXT("Enables mass processing distribution to all available thread (via the task graph)")},
	};
}

namespace UE::Mass::Private
{
	ETickingGroup PhaseToTickingGroup[int(EMassProcessingPhase::MAX)]
	{
		ETickingGroup::TG_PrePhysics, // EMassProcessingPhase::PrePhysics
		ETickingGroup::TG_StartPhysics, // EMassProcessingPhase::StartPhysics
		ETickingGroup::TG_DuringPhysics, // EMassProcessingPhase::DuringPhysics
		ETickingGroup::TG_EndPhysics,	// EMassProcessingPhase::EndPhysics
		ETickingGroup::TG_PostPhysics,	// EMassProcessingPhase::PostPhysics
		ETickingGroup::TG_LastDemotable, // EMassProcessingPhase::FrameEnd
	};
} // UE::Mass::Private

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

	TRACE_CPUPROFILER_EVENT_SCOPE_STR(*FString::Printf(TEXT("FMassProcessingPhase::ExecuteTick %s"), *UEnum::GetValueAsString(Phase)));

	Manager->OnPhaseStart(*this);
	
	OnPhaseStart.Broadcast(DeltaTime);

	check(PhaseProcessor);
	
	UMassEntitySubsystem& EntitySubsystem = Manager->GetEntitySubsystemRef();
	FMassProcessingContext Context(EntitySubsystem, DeltaTime);

	bIsDuringMassProcessing = true;

	if (bRunInParallelMode)
	{
		bool bWorkRequested = false;
		if (PhaseProcessor->IsEmpty() == false)
		{
			const FGraphEventRef PipelineCompletionEvent = UE::Mass::Executor::TriggerParallelTasks(*PhaseProcessor, Context, [this, DeltaTime]()
				{
					OnParallelExecutionDone(DeltaTime);
				});

			if (PipelineCompletionEvent.IsValid())
			{
				MyCompletionGraphEvent->DontCompleteUntil(PipelineCompletionEvent);
				bWorkRequested = true;
			}
		}
		if (bWorkRequested == false)
		{
			OnParallelExecutionDone(DeltaTime);
		}
	}
	else
	{
		UE::Mass::Executor::Run(*PhaseProcessor, Context);

		OnPhaseEnd.Broadcast(DeltaTime);
		Manager->OnPhaseEnd(*this);
		bIsDuringMassProcessing = false;
	}
}

void FMassProcessingPhase::OnParallelExecutionDone(const float DeltaTime)
{
	bIsDuringMassProcessing = false;
	OnPhaseEnd.Broadcast(DeltaTime);
	check(Manager);
	Manager->OnPhaseEnd(*this);
}

FString FMassProcessingPhase::DiagnosticMessage()
{
	return (Manager ? Manager->GetFullName() : TEXT("NULL-MassProcessingPhaseManager")) + TEXT("[ProcessorTick]");
}

FName FMassProcessingPhase::DiagnosticContext(bool bDetailed)
{
	return Manager ? Manager->GetClass()->GetFName() : TEXT("NULL-MassProcessingPhaseManager");
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
			UMassEntitySettings* Settings = GetMutableDefault<UMassEntitySettings>();
			check(Settings);
			MassEntitySettingsChangeHandle = Settings->GetOnSettingsChange().AddUObject(this, &UMassProcessingPhaseManager::OnMassEntitySettingsChange);
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
	if (UMassEntitySettings* Settings = GetMutableDefault<UMassEntitySettings>())
	{
		Settings->GetOnSettingsChange().Remove(MassEntitySettingsChangeHandle);
	}
#endif // WITH_EDITOR
}

void UMassProcessingPhaseManager::InitializePhases(UObject& InProcessorOwner)
{
	const FMassProcessingPhaseConfig* ProcessingPhasesConfig = GET_MASS_CONFIG_VALUE(GetProcessingPhasesConfig());

	FString DependencyGraphFileName;

#if WITH_EDITOR
	const UWorld* World = InProcessorOwner.GetWorld();
	const UMassEntitySettings* Settings = GetMutableDefault<UMassEntitySettings>();
	if (World != nullptr && Settings != nullptr && !Settings->DumpDependencyGraphFileName.IsEmpty())
	{
		DependencyGraphFileName = FString::Printf(TEXT("%s_%s"), *Settings->DumpDependencyGraphFileName,*ToString(World->GetNetMode()));
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
	UE_VLOG_UELOG(this, LogMass, Verbose, TEXT("Phases initialization done. Current composition:"));

	FStringOutputDevice OutDescription;
	for (int i = 0; i < int(EMassProcessingPhase::MAX); ++i)
	{
		if (ProcessingPhases[i].PhaseProcessor)
		{
			ProcessingPhases[i].PhaseProcessor->DebugOutputDescription(OutDescription);
			UE_VLOG_UELOG(this, LogMass, Verbose, TEXT("--- %s"), *OutDescription);
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
		UE_VLOG_UELOG(this, LogMass, Error, TEXT("Called %s while missing the EntitySubsystem"), ANSI_TO_TCHAR(__FUNCTION__));
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

	const bool bIsGameWorld = World.IsGameWorld();

	for (FMassProcessingPhase& Phase : ProcessingPhases)
	{
		Phase.Manager = this;
		Phase.RegisterTickFunction(World.PersistentLevel);
		Phase.SetTickFunctionEnable(true);
#if WITH_MASSENTITY_DEBUG
		if (Phase.PhaseProcessor && bIsGameWorld)
		{
			// not logging this in the editor mode since it messes up the game-recorded vislog display (with its progressively larger timestamp)
			FStringOutputDevice Ar;
			Phase.PhaseProcessor->DebugOutputDescription(Ar);
			UE_VLOG_UELOG(this, LogMass, Log, TEXT("Enabling phase %s tick:\n%s")
				, *EnumToString(Phase.Phase), *Ar);
		}
#endif // WITH_MASSENTITY_DEBUG		
	}

	if (bIsGameWorld)
	{
		// not logging this in the editor mode since it messes up the game-recorded vislog display (with its progressively larger timestamp)
		UE_VLOG_UELOG(this, LogMass, Log, TEXT("MassProcessingPhaseManager %s.%s has been started")
			, *GetNameSafe(GetOuter()), *GetName());
	}
}

void UMassProcessingPhaseManager::Stop()
{
	EntitySubsystem = nullptr;
	
	for (FMassProcessingPhase& Phase : ProcessingPhases)
	{
		Phase.SetTickFunctionEnable(false);
	}

	UWorld* World = GetWorld();
	if (World && World->IsGameWorld())
	{
		// not logging this in editor mode since it messes up the game-recorded vislog display (with its progressively larger timestamp) 
		UE_VLOG_UELOG(this, LogMass, Log, TEXT("MassProcessingPhaseManager %s.%s has been stopped")
			, *GetNameSafe(GetOuter()), *GetName());
	}
}

void UMassProcessingPhaseManager::SetPhaseProcessor(const EMassProcessingPhase Phase, UMassCompositeProcessor* PhaseProcessor)
{
	if (Phase == EMassProcessingPhase::MAX)
	{
		UE_VLOG_UELOG(this, LogMass, Error, TEXT("MassProcessingPhaseManager::OverridePhaseProcessor called with Phase == MAX"));
		return;
	}
	
	// note that it's ok to use PhaseProcessor == nullptr
	ProcessingPhases[int32(Phase)].PhaseProcessor = PhaseProcessor;
	
	if (PhaseProcessor)
	{
		REDIRECT_OBJECT_TO_VLOG(PhaseProcessor, this);
		PhaseProcessor->SetProcessingPhase(Phase);
		PhaseProcessor->SetGroupName(FName(FString::Printf(TEXT("%s-%s"), *PhaseProcessor->GetName(), *UEnum::GetValueAsString(Phase))));
		
		check(GetOuter());
		PhaseProcessor->Initialize(*GetOuter());

#if WITH_MASSENTITY_DEBUG
		FStringOutputDevice Ar;
		PhaseProcessor->DebugOutputDescription(Ar);
		UE_VLOG(this, LogMass, Log, TEXT("Setting new group processor for phase %s:\n%s")
			, *EnumToString(Phase), *Ar);
#endif // WITH_MASSENTITY_DEBUG
	}
	else
	{
		UE_VLOG_UELOG(this, LogMass, Log, TEXT("Nulling-out phase processor for phase %s"), *UEnum::GetValueAsString(Phase));
	}
}

void UMassProcessingPhaseManager::CreatePhases() 
{
	// @todo copy from settings instead of blindly creating from scratch
	for (int i = 0; i < int(EMassProcessingPhase::MAX); ++i)
	{
		ProcessingPhases[i].Phase = EMassProcessingPhase(i);
		ProcessingPhases[i].TickGroup = UE::Mass::Private::PhaseToTickingGroup[i];
		UMassCompositeProcessor* PhaseProcessor = NewObject<UMassCompositeProcessor>(this, UMassCompositeProcessor::StaticClass()
			, *FString::Printf(TEXT("ProcessingPhase_%s"), *UEnum::GetValueAsString(EMassProcessingPhase(i))));
		SetPhaseProcessor(EMassProcessingPhase(i), PhaseProcessor);
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
	if (Phase.IsConfiguredForParallelMode() != UE::Mass::Tweakables::bFullyParallel)
	{
		if (UE::Mass::Tweakables::bFullyParallel)
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
void UMassProcessingPhaseManager::OnMassEntitySettingsChange(const FPropertyChangedEvent& PropertyChangedEvent)
{
	check(GetOuter());
	InitializePhases(*GetOuter());
}
#endif // WITH_EDITOR

#undef LOCTEXT_NAMESPACE