// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/Object.h"
#include "Engine/EngineBaseTypes.h"
#include "MassEntityTypes.h"
#include "MassProcessingPhase.generated.h"


class UPipeProcessingPhaseManager;
class UPipeProcessor;
class UPipeCompositeProcessor;
class UPipeEntitySubsystem;
struct FLWCCommandBuffer;

USTRUCT()
struct FPipeProcessingPhase : public FTickFunction
{
	GENERATED_BODY()

	DECLARE_MULTICAST_DELEGATE_OneParam(FOnPhaseEvent, const float /*DeltaSeconds*/);

	FPipeProcessingPhase();

protected:
	// FTickFunction interface
	virtual void ExecuteTick(float DeltaTime, ELevelTick TickType, ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent) override;
	virtual FString DiagnosticMessage() override;
	virtual FName DiagnosticContext(bool bDetailed) override;
	// End of FTickFunction interface

	void OnParallelExecutionDone(const float DeltaTime);

	bool IsConfiguredForParallelMode() const { return bRunInParallelMode; }
	void ConfigureForParallelMode() { bRunInParallelMode = true; }
	void ConfigureForSingleThreadMode() { bRunInParallelMode = false; }

public:
	bool IsDuringPipeProcessing() const { return bIsDuringPipeProcessing; }

protected:
	friend UPipeProcessingPhaseManager;

	UPROPERTY(EditAnywhere, Category=Pipe)
	UPipeCompositeProcessor* PhaseProcessor = nullptr;

	UPROPERTY()
	UPipeProcessingPhaseManager* Manager = nullptr;

	EPipeProcessingPhase Phase = EPipeProcessingPhase::MAX;
	FOnPhaseEvent OnPhaseStart;
	FOnPhaseEvent OnPhaseEnd;

private:
	bool bRunInParallelMode = false;
	bool bIsDuringPipeProcessing = false;
};

// It is unsafe to copy FTickFunctions and any subclasses of FTickFunction should specify the type trait WithCopy = false
template<>
struct TStructOpsTypeTraits<FPipeProcessingPhase> : public TStructOpsTypeTraitsBase2<FPipeProcessingPhase>
{
	enum
	{
		WithCopy = false
	};
};

/** PipeProcessingPhaseManager owns separate FPipeProcessingPhase instances for every ETickingGroup. When activated
 *  via Start function it registers and enables the FPipeProcessingPhase instances which themselves are tick functions 
 *  that host FRuntimePipeline which they trigger as part of their Tick function. 
 *  PipeProcessingPhaseManager serves as an interface to said FPipeProcessingPhase instances and allows initialization
 *  with PipeSchematics (via InitializePhases function) as well as registering arbitrary functions to be called 
 *  when a particular phase starts of ends (via GetOnPhaseStart and GetOnPhaseEnd functions). */
UCLASS(Transient, HideCategories = (Tick))
class MASSENTITY_API UPipeProcessingPhaseManager : public UObject
{
	GENERATED_BODY()
public:
	UPipeEntitySubsystem& GetEntitySubsystemRef() { check(EntitySubsystem); return *EntitySubsystem; }

	/** Retrieves OnPhaseStart multicast delegate's reference for a given Phase */
	FPipeProcessingPhase::FOnPhaseEvent& GetOnPhaseStart(const EPipeProcessingPhase Phase) { return ProcessingPhases[uint8(Phase)].OnPhaseStart; }
	/** Retrieves OnPhaseEnd multicast delegate's reference for a given Phase */
	FPipeProcessingPhase::FOnPhaseEvent& GetOnPhaseEnd(const EPipeProcessingPhase Phase) { return ProcessingPhases[uint8(Phase)].OnPhaseEnd; }

	/** 
	 *  Populates hosted FPipeProcessingPhase instances with Processors read from PipeSettings configuration.
	 *  Calling this function overrides previous configuration of Phases.
	 */
	void InitializePhases(UObject& InProcessorOwner);

	/** 
	 *  Both flavors of Start function boil down to setting EntitySubsystem and Executor. If the callee has these 
	 *  at hand it's suggested to use that Start version, otherwise call the World-using one. 
	 */
	void Start(UWorld& World);
	void Start(UPipeEntitySubsystem& InEntitySubsystem);
	void Stop();
	bool IsRunning() const { return EntitySubsystem != nullptr; }

	/** 
	 *  returns true when called while any of the ProcessingPhases is actively executing its processors. Used to 
	 *  determine whether it's safe to do entity-related operations like adding fragments.
	 *  Note that the function will return false while the OnPhaseStart or OnPhaseEnd are being broadcast,
	 *  the value returned will be `true` only when the entity subsystem is actively engaged 
	 */
	bool IsDuringPipeProcessing() const { return CurrentPhase != EPipeProcessingPhase::MAX && ProcessingPhases[int(CurrentPhase)].IsDuringPipeProcessing(); }

protected:
	virtual void PostInitProperties() override;
	virtual void BeginDestroy() override;
	void EnableTickFunctions(const UWorld& World);

	/** Creates phase processors instances for each declared phase name, based on PipeSettings */
	virtual void CreatePhases();

#if WITH_EDITOR
	virtual void OnPipeSettingsChange(const FPropertyChangedEvent& PropertyChangedEvent);
#endif // WITH_EDITOR

	friend FPipeProcessingPhase;

	/** 
	 *  Called by the given Phase at the very start of its execution function (the FPipeProcessingPhase::ExecuteTick),
	 *  even before the FPipeProcessingPhase.OnPhaseStart broadcast delegate
	 */
	void OnPhaseStart(const FPipeProcessingPhase& Phase);

	/**
	 *  Called by the given Phase at the very end of its execution function (the FPipeProcessingPhase::ExecuteTick),
	 *  after the FPipeProcessingPhase.OnPhaseEnd broadcast delegate
	 */
	void OnPhaseEnd(FPipeProcessingPhase& Phase);

protected:	
	UPROPERTY(VisibleAnywhere, Category=Pipe)
	FPipeProcessingPhase ProcessingPhases[(uint8)EPipeProcessingPhase::MAX];

	UPROPERTY()
	UPipeEntitySubsystem* EntitySubsystem;

	EPipeProcessingPhase CurrentPhase = EPipeProcessingPhase::MAX;

#if WITH_EDITOR
	FDelegateHandle PipeSettingsChangeHandle;
#endif // WITH_EDITOR
};
