// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

/** This class represents an ChaosSolver Actor. */

#include "Chaos/ChaosSolver.h"
#include "Chaos/ChaosSolverComponentTypes.h"
#include "Chaos/ClusterCreationParameters.h"
#include "Components/BillboardComponent.h"
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Physics/Experimental/PhysScene_Chaos.h"
#include "UObject/ObjectMacros.h"
#include "SolverEventFilters.h"

#include "Engine/EngineTypes.h"

#include "ChaosSolverActor.generated.h"

class UChaosGameplayEventDispatcher;

UENUM()
enum class EClusterConnectionTypeEnum : uint8
{
	Chaos_PointImplicit = Chaos::FClusterCreationParameters<float>::PointImplicit UMETA(DisplayName = "PointImplicit"),
	Chaos_DelaunayTriangulation = Chaos::FClusterCreationParameters<float>::DelaunayTriangulation UMETA(DisplayName = "DelaunayTriangulation"),
	Chaos_MinimalSpanningSubsetDelaunayTriangulation = Chaos::FClusterCreationParameters<float>::MinimalSpanningSubsetDelaunayTriangulation UMETA(DisplayName = "MinimalSpanningSubsetDelaunayTriangulation"),
	Chaos_PointImplicitAugmentedWithMinimalDelaunay = Chaos::FClusterCreationParameters<float>::PointImplicitAugmentedWithMinimalDelaunay UMETA(DisplayName = "PointImplicitAugmentedWithMinimalDelaunay"),
	Chaos_None = Chaos::FClusterCreationParameters<float>::None UMETA(DisplayName = "None"),
	//
	Chaos_EClsuterCreationParameters_Max UMETA(Hidden)
};

USTRUCT()
struct FChaosDebugSubstepControl
{
	GENERATED_USTRUCT_BODY()

	FChaosDebugSubstepControl() : bPause(false), bSubstep(false), bStep(false) {}
	
	/*
	* Pause the solver at the next synchronization point.
	*/
	UPROPERTY(EditAnywhere, Category = "ChaosPhysics|Debug")
	bool bPause;

	/*
	* Substep the solver to the next synchronization point.
	*/
	UPROPERTY(EditAnywhere, Category = "ChaosPhysics|Debug", meta = (EditCondition = "bPause"))
	bool bSubstep;

	/*
	* Step the solver to the next synchronization point.
	*/
	UPROPERTY(EditAnywhere, Category = "ChaosPhysics|Debug", meta = (EditCondition = "bPause"))
	bool bStep;

#if WITH_EDITOR
	FSimpleDelegate OnPauseChanged;  // Delegate used to refresh the Editor's details customization when the pause value changed.
#endif
};

UCLASS()
class CHAOSSOLVERENGINE_API AChaosSolverActor : public AActor
{
	GENERATED_UCLASS_BODY()

public:
	/**
	* NumberOfSubSteps
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChaosPhysics")
	float TimeStepMultiplier;

	/**
	* Collision Iteration
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChaosPhysics|Iterations")
	int32 CollisionIterations;

	/**
	* PushOut Iteration
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChaosPhysics|Iterations")
	int32 PushOutIterations;

	/**
	* PushOut Iteration
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChaosPhysics|Iterations")
	int32 PushOutPairIterations;

	/**
	* ClusterConnectionFactor
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChaosPhysics|Clustering")
	float ClusterConnectionFactor;

	/*
	*  ObjectType defines how to initialize the rigid objects state, Kinematic, Sleeping, Dynamic.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChaosPhysics|Clustering")
	EClusterConnectionTypeEnum ClusterUnionConnectionType;

	/*
	* Turns on/off collision data generation
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChaosPhysics|CollisionData Generation", meta = (DisplayName = "Generate Collision Data"))
	bool DoGenerateCollisionData;

	/*
	*
	*/
	UPROPERTY(EditAnywhere, Category = "ChaosPhysics|CollisionData Generation")
	FSolverCollisionFilterSettings CollisionFilterSettings;


	/*
	* Turns on/off breaking data generation
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChaosPhysics|BreakingData Generation", meta = (DisplayName = "Generate Breaking Data"))
	bool DoGenerateBreakingData;

	/*
	*
	*/
	UPROPERTY(EditAnywhere, Category = "ChaosPhysics|BreakingData Generation")
	FSolverBreakingFilterSettings BreakingFilterSettings;

	/*
	* Turns on/off trailing data generation
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChaosPhysics|TrailingData Generation", meta = (DisplayName = "Generate Trailing Data"))
	bool DoGenerateTrailingData;

	/*
	*
	*/
	UPROPERTY(EditAnywhere, Category = "ChaosPhysics|TrailingData Generation")
	FSolverTrailingFilterSettings TrailingFilterSettings;

	/*
	* 
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChaosPhysics|Floor", meta = (DisplayName = "Use Floor"))
	bool bHasFloor;

	/*
	* 
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChaosPhysics|Floor", meta = (DisplayName = "Floor Height"))
	float FloorHeight;

	/*
	* 
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChaosPhysics", meta = (DisplayName = "Mass Scale"))
	float MassScale;

	/*
	* Control to pause/step/substep the solver to the next synchronization point.
	*/
	UPROPERTY(EditAnywhere, Category = "ChaosPhysics|Debug")
	FChaosDebugSubstepControl ChaosDebugSubstepControl;

	/** Makes this solver the current world solver. Dynamically spawned objects will have their physics state created in this solver. */
	UFUNCTION(BlueprintCallable, Category = "ChaosPhysics")
	void SetAsCurrentWorldSolver();

	/** Controles whether the solver is able to simulate particles it controls */
	UFUNCTION(BlueprintCallable, Category = "ChaosPhysics")
	virtual void SetSolverActive(bool bActive);

	/*
	* Display icon in the editor
	*/
	UPROPERTY()
	UBillboardComponent* SpriteComponent;

	UChaosGameplayEventDispatcher* GetGameplayEventDispatcher() const { return GameplayEventDispatcherComponent; };

	TSharedPtr<FPhysScene_Chaos> GetPhysicsScene() const { return PhysScene; }
	Chaos::FPhysicsSolver* GetSolver() const { return Solver; }

#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	virtual void PostRegisterAllComponents() override;
	
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type ReasonEnd) override;

private:
	TSharedPtr<FPhysScene_Chaos> PhysScene;
	Chaos::FPhysicsSolver* Solver;

	/** Component responsible for harvesting and triggering physics-related gameplay events (hits, breaks, etc) */
	UPROPERTY()
	UChaosGameplayEventDispatcher* GameplayEventDispatcherComponent;
};