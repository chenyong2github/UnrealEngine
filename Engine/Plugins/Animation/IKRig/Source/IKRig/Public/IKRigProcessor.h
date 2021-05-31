// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
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

USTRUCT()
struct IKRIG_API FIKRigProcessor
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
	 
	/** setup a new processor to run the given IKRig asset
	 *  NOTE!! this function creates new UObjects and consequently MUST be called from the main thread!!	 */
	void Initialize(UIKRigDefinition* InRigAsset, const FReferenceSkeleton& RefSkeleton, UObject* Outer);

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

	/** Set a named IK goal to go to a specific location and rotation (assumed in component space) blended by separate position/rotation alpha (0-1)*/
	void SetIKGoal(const UIKRigEffectorGoal* Goal);

	/** Run entire stack of solvers */
	void Solve();

	/** Get the results after calling Solve() */
	void CopyOutputGlobalPoseToArray(TArray<FTransform>& OutputPoseGlobal) const;

	//
	// END UPDATE SEQUENCE FUNCTIONS
	//

	/** Used to propagate setting values from the source asset at runtime (settings that do not require re-initialization) */
	void CopyAllInputsFromSourceAssetAtRuntime(UIKRigDefinition* IKRigAsset);

	/** checks if the source IKRig asset has been modified in a way that would require reinitialization. */
	bool NeedsInitialized(UIKRigDefinition* IKRigAsset) const;

	/** Get access to the internal goal data (read only) */
	const FIKRigGoalContainer& GetGoalContainer() const;
	
	/** Get access to the internal skeleton data */
	FIKRigSkeleton& GetSkeleton();
	
private:

	/** Update the final pos/rot of all the goals based on their alpha values. */
	void BlendGoalsByAlpha();

	/** the stack of solvers to run in order */
	UPROPERTY(Transient)
	TArray<UIKRigSolver*> Solvers;

	/** the named transforms that solvers use as end effectors */
	FIKRigGoalContainer GoalContainer;

	/** map of goal names to bone names/indices */
	TMap<FName, FGoalBone> GoalBones;

	/** storage for hierarchy and bone transforms */
	FIKRigSkeleton Skeleton;

	/** solving disabled until this flag is true */
	bool bInitialized = false;
	/** which version of the IKRig asset was this instance last initialized with?
	 * this allows the IKRig asset to undergo modifications at runtime via the editor*/
	int32 InitializedWithIKRigAssetVersion = -1;
	int32 LastVersionTried = -1;
};