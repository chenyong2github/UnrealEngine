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

USTRUCT()
struct FGoalBone
{
	GENERATED_BODY()
	
	FName BoneName;
	int32 BoneIndex;
};

UCLASS(BlueprintType)
class IKRIG_API UIKRigProcessor : public UObject
{
	GENERATED_BODY()

public:

	/** the runtime for an IKRig to convert an input pose into
	*   a solved output pose given a set of IK Rig Goals:
	*   
	* 1. Create a new IKRigProcessor once using MakeNewIKRigProcessor()
	* 2. Initialize() with an IKRigDefinition asset
	* 3. each tick, call SetIKGoal() and SetInputPoseGlobal()
	* 4. Call Solve()
	* 5. Copy output transforms with CopyOutputGlobalPoseToArray()
	* 
	*/

	static UIKRigProcessor* MakeNewIKRigProcessor(UObject* Outer);
	 
	/** setup a new processor to run the given IKRig asset  */
	void Initialize(UIKRigDefinition* InRigDefinition, const FReferenceSkeleton& RefSkeleton);

	//
	// BEGIN UPDATE SEQUENCE FUNCTIONS
	//
	// This is the general sequence of function calls to run a typical IK solve:
	// 
	
	/** Set all bone transforms in global space. This is the pose the IK solve will start from */
	void SetInputPoseGlobal(const TArray<FTransform>& InGlobalBoneTransforms);

	/** Optionally can be called before Solve() to use the reference pose as start pose */
	void SetInputPoseToRefPose();

	/** Set a named IK goal to go to a specific location and rotation (assumed in component space) blended by separate position/rotation alpha (0-1)*/
	void SetIKGoal(const FIKRigGoal& Goal);

	/** Run entire stack of solvers */
	void Solve();

	/** Get the results after calling Solve() */
	void CopyOutputGlobalPoseToArray(TArray<FTransform>& OutputPoseGlobal) const;

	//
	// END UPDATE SEQUENCE FUNCTIONS
	//

	bool IsInitialized() const { return bInitialized; };

	/** Get interface for debug drawing. */
	const FControlRigDrawInterface& GetDrawInterface() const { return DrawInterface; }

	/** Get access to the internal goal data (read only) */
	const FIKRigGoalContainer& GetGoalContainer() const;
	
	/** Get access to the internal skeleton data */
	FIKRigSkeleton& GetSkeleton();

	/** Get bone for goal */
	bool GetBoneForGoal(FName GoalName, FGoalBone& OutBone) const;
	
private:

	/** Update the final pos/rot of all the goals based on their alpha values. */
	void BlendGoalsByAlpha();

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
	FIKRigGoalContainer GoalContainer;

	/** map of goal names to bone names/indices */
	TMap<FName, FGoalBone> GoalBones;

	/** storage for hierarchy and bone transforms */
	UPROPERTY(transient)
	FIKRigSkeleton Skeleton;
};