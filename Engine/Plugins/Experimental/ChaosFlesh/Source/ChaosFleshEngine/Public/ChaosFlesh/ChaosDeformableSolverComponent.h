// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Deformable/ChaosDeformableSolver.h"
#include "Chaos/Deformable/ChaosDeformableSolverProxy.h"
#include "Chaos/Deformable/ChaosDeformableSolverTypes.h"
#include "ChaosFlesh/ChaosDeformableSolverThreading.h"
#include "ChaosFlesh/ChaosDeformableTypes.h"
#include "ChaosFlesh/FleshComponent.h"
#include "Components/SceneComponent.h"
#include "UObject/ObjectMacros.h"

#include "ChaosDeformableSolverComponent.generated.h"

class UDeformablePhysicsComponent;

/**
*	UDeformableSolverComponent
*/
UCLASS()
class CHAOSFLESHENGINE_API UDeformableSolverComponent : public USceneComponent
{
	GENERATED_UCLASS_BODY()

public:
	typedef Chaos::Softs::FThreadingProxy FThreadingProxy;
	typedef Chaos::Softs::FFleshThreadingProxy FFleshThreadingProxy;
	typedef Chaos::Softs::FDeformablePackage FDeformablePackage;
	typedef Chaos::Softs::FDataMapValue FDataMapValue;
	typedef Chaos::Softs::FDeformableSolver FDeformableSolver;

	~UDeformableSolverComponent();
	void UpdateTickGroup();

#if WITH_EDITOR
	TArray< TObjectPtr<AActor> > PreEditChangeDeformableActors;
	virtual void PreEditChange(FProperty* PropertyThatWillChange) override;
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	/* Solver API */
	FDeformableSolver::FGameThreadAccess GameThreadAccess();

	bool IsSimulating(UDeformablePhysicsComponent*) const;
	bool IsSimulatable() const;
	void Reset();
	void AddDeformableProxy(UDeformablePhysicsComponent* InComponent);
	void Simulate(float DeltaTime);
	void UpdateFromGameThread(float DeltaTime);
	void UpdateFromSimulation(float DeltaTime);

	/* Component Thread Management */
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	//bool ShouldWaitForDeformableInTickFunction() const;
	void UpdateDeformableEndTickState(bool bRegister);

	/* Properties */

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Chaos)
		TArray< TObjectPtr<AActor> > DeformableActors;

	/** ObjectType defines how to initialize the rigid objects state, Kinematic, Sleeping, Dynamic. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly , Category = Chaos)
		EDeformableExecutionModel ExecutionModel = EDeformableExecutionModel::Chaos_Deformable_DuringPhysics;

	UPROPERTY(EditAnywhere, Category = Chaos)
		int32 NumSubSteps = 2;

	UPROPERTY(EditAnywhere, Category = Chaos)
		int32 NumSolverIterations = 5;

	UPROPERTY(EditAnywhere, Category = Chaos)
		bool FixTimeStep = false;

	UPROPERTY(EditAnywhere, Category = Chaos)
		float TimeStepSize = 0.05;

	UPROPERTY(EditAnywhere, Category = Chaos)
		bool CacheToFile = false;

	UPROPERTY(EditAnywhere, Category = Chaos)
		bool bEnableKinematics = true;

	UPROPERTY(EditAnywhere, Category = Chaos)
		bool bUseFloor = true;

	UPROPERTY(EditAnywhere, Category = Chaos)
		bool bDoSelfCollision = false;

	UPROPERTY(EditAnywhere, Category = Chaos)
		bool bDoThreadedAdvance = true;

	UPROPERTY(EditAnywhere, Category = "Chaos|Expermental" )
		bool bUseGridBasedConstraints = false;

	UPROPERTY(EditAnywhere, Category = Chaos)
		float GridDx = 25.;

	UPROPERTY(EditAnywhere, Category = Chaos)
		bool bDoQuasistatics = false;

	UPROPERTY(EditAnywhere, Category = Chaos)
		float EMesh = 100000;

	UPROPERTY(EditAnywhere, Category = Chaos)
		bool bDoBlended = false;

	UPROPERTY(EditAnywhere, Category = Chaos)
		float BlendedZeta = 0;

	//UPROPERTY(EditAnywhere, Category = Chaos)
	//	bool bWaitForParallelDeformableTask = true;

	// Simulation Variables
	TUniquePtr<FDeformableSolver> Solver;

protected:

	/** Ref for the deformable solvers parallel task, so we can detect whether or not a sim is running */
	FGraphEventRef ParallelDeformableTask;
	FDeformableEndTickFunction DeformableEndTickFunction;
};

