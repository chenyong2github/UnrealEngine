// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "IKRigDataTypes.h"
#include "Drawing/ControlRigDrawInterface.h"
#include "IKRigProcessor.generated.h"

class UIKRigDefinition;
class UIKRigSolver;
struct FControlRigDrawInterface;

UCLASS(BlueprintType)
class IKRIG_API UIKRigProcessor : public UObject
{
	GENERATED_BODY()

public:

	// the runtime for an IKRig, general steps are:
	// 1. Initialize() with an IKRigDefinition asset
	// 2. each tick, call SetGoalTransform() and update GlobalBoneTransforms
	// 3. Call Solve()
	// 4. Copy back output transforms...
	// 5. Profit.
	 
	// setup
	void Initialize(UIKRigDefinition* InRigDefinition);

	// run entire stack of solvers
	void Solve();

	// optionally can be called before Solve() to do deterministic solve
	void ResetToRefPose();

	// get all global bone transforms
	FIKRigTransforms& GetCurrentGlobalTransforms();

	// set goal transform by name
	void SetGoalTransform(const FName& GoalName, const FVector& Position, const FQuat& Rotation);

	// get all goal names in the solver stack
	void GetGoalNames(TArray<FName>& OutGoalNames) const;

	// total number of goals in solver stack
	int GetNumGoals() const;

	// get access to the hierarchy the solver stack expects
	const FIKRigHierarchy* GetHierarchy() const;

	// interface for debug drawing
	const FControlRigDrawInterface& GetDrawInterface() { return DrawInterface; }
	
private:

	bool bInitialized = false;

	UPROPERTY(EditAnywhere, Category = "RigDefinition")
	UIKRigDefinition* RigDefinition = nullptr;

	UPROPERTY(transient)
	TArray<FTransform> RefPoseGlobalTransforms;

	UPROPERTY(transient)
	FIKRigTransforms GlobalBoneTransforms;

	UPROPERTY(transient)
	TArray<UIKRigSolver*> Solvers;

	UPROPERTY(transient)
	FControlRigDrawInterface DrawInterface;

	UPROPERTY(transient)
	FIKRigGoalContainer Goals;
};

