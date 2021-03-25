// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "Drawing/ControlRigDrawInterface.h"
#include "IKRigDataTypes.h"
#include "IKRigSkeleton.h"

#include "IKRigProcessor.generated.h"

class UIKRigDefinition;
class UIKRigSolver;

UCLASS(BlueprintType)
class IKRIG_API UIKRigProcessor : public UObject
{
	GENERATED_BODY()

public:

	/** the runtime for an IKRig to convert an input pose into
	*   a solved output pose given the goal transforms:
	* 1. Create a new IKRigProcessor once using MakeNewIKRigProcessor()
	* 2. Initialize() with an IKRigDefinition asset
	* 3. each tick, call SetGoalTransform() and update GlobalBoneTransforms
	* 4. Call Solve()
	* 5. Copy back output transforms...
	*/

	static UIKRigProcessor* MakeNewIKRigProcessor(UObject* Outer);
	 
	/** setup a new processor to run the given IKRig asset  */
	void Initialize(UIKRigDefinition* InRigDefinition);

	//
	// BEGIN UPDATE SEQUENCE FUNCTIONS
	//
	// This is the general sequence of function calls to run a typical IK solve:
	// 
	
	/** set all transforms in global space */
	void SetInputPoseGlobal(const TArray<FTransform>& InGlobalBoneTransforms);

	/** optionally can be called before Solve() to use the reference pose as start pose */
	void SetInputPoseToRefPose();

	/** set goal transform by name */
	void SetGoalTransform(const FName& GoalName, const FVector& Position, const FQuat& Rotation);

	/** run entire stack of solvers */
	void Solve();

	/** get the results after calling Solve() */
	void CopyOutputGlobalPoseToArray(TArray<FTransform>& OutputPoseGlobal) const;

	//
	// END UPDATE SEQUENCE FUNCTIONS
	//

	/** get all goal names in the solver stack */
	void GetGoalNames(TArray<FName>& OutGoalNames) const;

	/** total number of goals in solver stack */
	int GetNumGoals() const;

	bool IsInitialized() const { return bInitialized; };

	/** interface for debug drawing */
	const FControlRigDrawInterface& GetDrawInterface() const { return DrawInterface; }

	/** get access to the internal skeleton data */
	FIKRigSkeleton& GetSkeleton();
	
private:

	/** solving disabled until this flag is true */
	bool bInitialized = false;

	/** the stack of solvers to run in order */
	UPROPERTY(transient)
	TArray<UIKRigSolver*> Solvers;

	/** storage for solver debug drawing data (lines, boxes etc) */
	UPROPERTY(transient)
	FControlRigDrawInterface DrawInterface;

	/** the named transforms that solvers use as end effectors */
	UPROPERTY(transient)
	FIKRigGoalContainer Goals;

	/** storage for hierarchy and bone transforms */
	UPROPERTY(transient)
	FIKRigSkeleton Skeleton;
};