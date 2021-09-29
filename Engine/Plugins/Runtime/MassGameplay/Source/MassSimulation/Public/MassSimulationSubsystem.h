// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Delegates/IDelegateInstance.h"
#include "MassEntityTypes.h"
#include "MassProcessingPhase.h"
#include "Subsystems/WorldSubsystem.h"
#include "MassSimulationSubsystem.generated.h"


class AMassSimulationLocalCoordinator;
class UPipeEntitySubsystem;
class UPipeProcessingPhaseManager;

DECLARE_LOG_CATEGORY_EXTERN(LogMassSim, Log, All);

UCLASS(config = Game, defaultconfig)
class MASSSIMULATION_API UMassSimulationSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()
public:
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnAdjustTickSchematics, UWorld* /*World*/, TArray<TSoftObjectPtr<UPipeSchematic>>& /*InOutTickSchematics*/);
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnSimulationStarted, UWorld* /*World*/);
	
	UMassSimulationSubsystem(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	UPipeEntitySubsystem* GetEntitySubsystem() const { return EntitySubsystem; }
	const UPipeProcessingPhaseManager& GetPhaseManager() const { check(PhaseManager); return *PhaseManager; }

	FPipeProcessingPhase::FOnPhaseEvent& GetOnProcessingPhaseStarted(const EPipeProcessingPhase Phase) const;
	FPipeProcessingPhase::FOnPhaseEvent& GetOnProcessingPhaseFinished(const EPipeProcessingPhase Phase) const;
	static FOnAdjustTickSchematics& GetOnAdjustTickSchematics() { return OnAdjustTickSchematics; }
	static FOnSimulationStarted& GetOnSimulationStarted() { return OnSimulationStarted; }

	bool IsSimulationStarted() const { return bSimulationStarted; }

protected:
	virtual void PostInitProperties() override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void PostInitialize() override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;
	virtual void BeginDestroy() override;

	/** Creates an instance of UPipeEntitySubsystem and assigns it to EntitySubsystem property. This is the entity 
	 *  subsystem that Mass is going to be using. If you want to instantiate a different entity system class or point 
	 *  at a preexisting instance just override this function. */
	virtual void CreateEntitySubsystem();

	void RebuildTickPipeline();

	void StartSimulation(UWorld& InWorld);
	void StopSimulation();

	void OnProcessingPhaseStarted(const float DeltaSeconds, const EPipeProcessingPhase Phase) const;

#if WITH_EDITOR
	void OnPieBegin(const bool bIsSimulation);
	void OnPieEnded(const bool bIsSimulation);
#endif // WITH_EDITOR

protected:

	UPROPERTY()
	UPipeEntitySubsystem* EntitySubsystem;

	UPROPERTY()
	UPipeProcessingPhaseManager* PhaseManager;

	inline static FOnAdjustTickSchematics OnAdjustTickSchematics={};
	inline static FOnSimulationStarted OnSimulationStarted={};

	UPROPERTY()
	FRuntimePipeline RuntimePipeline;

	float CurrentDeltaSeconds = 0.f;
	bool bTickInProgress = false;
	bool bSimulationStarted = false;

#if WITH_EDITOR
	FDelegateHandle PieBeginEventHandle;
	FDelegateHandle PieEndedEventHandle;
#endif // WITH_EDITOR
};
