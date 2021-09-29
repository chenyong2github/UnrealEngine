// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Delegates/IDelegateInstance.h"
#include "MassEntityTypes.h"
#include "MassProcessingPhase.h"
#include "Subsystems/WorldSubsystem.h"
#include "MassSimulationSubsystem.generated.h"


class AMassSimulationLocalCoordinator;
class UMassEntitySubsystem;
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

	//UMassEntitySubsystem* GetEntitySubsystem() const { return CachedEntitySubsystem; }
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
	UMassEntitySubsystem* EntitySubsystem;

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
