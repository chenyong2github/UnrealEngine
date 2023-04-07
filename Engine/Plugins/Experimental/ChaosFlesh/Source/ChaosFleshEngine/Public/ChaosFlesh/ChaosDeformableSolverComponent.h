// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Deformable/ChaosDeformableSolver.h"
#include "Chaos/Deformable/ChaosDeformableSolverProxy.h"
#include "Chaos/Deformable/ChaosDeformableSolverTypes.h"
#include "ChaosFlesh/ChaosDeformableSolverThreading.h"
#include "ChaosFlesh/ChaosDeformableTypes.h"
#include "ChaosFlesh/FleshComponent.h"
#include "DeformableInterface.h"
#include "Components/SceneComponent.h"
#include "UObject/ObjectMacros.h"

#include "ChaosDeformableSolverComponent.generated.h"

class UDeformablePhysicsComponent;
class UDeformableCollisionsComponent;

/**
*	UDeformableSolverComponent
*/
UCLASS(meta = (BlueprintSpawnableComponent))
class CHAOSFLESHENGINE_API UDeformableSolverComponent : public USceneComponent, public IDeformableInterface
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

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Physics", meta = (EditCondition = "false"))
	TArray< TObjectPtr<UDeformablePhysicsComponent> > DeformableComponents;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Physics", meta = (EditCondition = "false"))
	TObjectPtr<UDeformableCollisionsComponent> CollisionComponent;

	/** ObjectType defines how to initialize the rigid objects state, Kinematic, Sleeping, Dynamic. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly , Category = "Physics")
	EDeformableExecutionModel ExecutionModel = EDeformableExecutionModel::Chaos_Deformable_DuringPhysics;

	UPROPERTY(EditAnywhere, Category = "Physics")
		int32 NumSubSteps = 2;

	UPROPERTY(EditAnywhere, Category = "Physics")
		int32 NumSolverIterations = 5;

	UPROPERTY(EditAnywhere, Category = "Physics")
		bool FixTimeStep = false;

	UPROPERTY(EditAnywhere, Category = "Physics")
		float TimeStepSize = 0.05;

	UPROPERTY(EditAnywhere, Category = "Physics")
		bool CacheToFile = false;

	UPROPERTY(EditAnywhere, Category = "Physics")
		bool bEnableKinematics = true;

	UPROPERTY(EditAnywhere, Category = "Physics")
		bool bUseFloor = true;

	UPROPERTY(EditAnywhere, Category = "Physics")
		bool bDoSelfCollision = false;

	UPROPERTY(EditAnywhere, Category = "Physics")
		bool bDoThreadedAdvance = true;

	UPROPERTY(EditAnywhere, Category = "Physics|Expermental" )
		bool bUseGridBasedConstraints = false;

	UPROPERTY(EditAnywhere, Category = "Physics")
		float GridDx = 25.;

	UPROPERTY(EditAnywhere, Category = "Physics")
		bool bDoQuasistatics = false;

	UPROPERTY(EditAnywhere, Category = "Physics")
		float YoungModulus = 100000;

	UPROPERTY(EditAnywhere, Category = "Physics")
		bool bDoBlended = false;

	UPROPERTY(EditAnywhere, Category = "Physics")
		float BlendedZeta = 0;

	UPROPERTY(EditAnywhere, Category = "Physics")
		float Damping = 0;

	UPROPERTY(EditAnywhere, Category = "Physics")
		bool bEnableGravity = true;

	UPROPERTY(EditAnywhere, Category = "Physics")
		bool bEnableCorotatedConstraint = true;

	UPROPERTY(EditAnywhere, Category = "Physics")
		bool bEnablePositionTargets = true;
	//UPROPERTY(EditAnywhere, Category = Chaos)
	//	bool bWaitForParallelDeformableTask = true;

	// Simulation Variables
	TUniquePtr<FDeformableSolver> Solver;

#if WITH_EDITOR
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
#endif

protected:

	/** Ref for the deformable solvers parallel task, so we can detect whether or not a sim is running */
	FGraphEventRef ParallelDeformableTask;
	FDeformableEndTickFunction DeformableEndTickFunction;
};

