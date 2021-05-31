// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Object.h"
#include "IKRigDataTypes.h"

#include "IKRigSolver.generated.h"

class FPrimitiveDrawInterface;
struct FIKRigGoalContainer;
struct FIKRigSkeleton;

// this is the base class for creating your own solver type that integrates into the IK Rig framework/editor.
UCLASS(abstract, hidecategories = UObject)
class IKRIG_API UIKRigSolver : public UObject
{
	GENERATED_BODY()
	
public:

	UPROPERTY(EditAnywhere, Category = "Solver Settings")
	bool bEnabled = true;

	//** RUNTIME */
	/** override to setup internal data based on ref pose */
	virtual void Initialize(const FIKRigSkeleton& IKRigSkeleton) PURE_VIRTUAL("Init");
	/** override Solve() to evaluate new output pose (InOutGlobalTransform) */
	virtual void Solve(FIKRigSkeleton& IKRigSkeleton, const FIKRigGoalContainer& Goals) PURE_VIRTUAL("Solve");
	//** END RUNTIME */

	//** SOLVER SETTINGS */
	/** override to support RECEIVING modified solver settings from outside systems for editing/UI.
	 * Note: you can safely cast this to your own solver type and copy any relevant settings at runtime
	 * This is necessary because at runtime, the IKRigProcessor creates a copy of your solver class
	 * and the copy must be notified of changes made to the class settings in the source asset.*/
	virtual void UpdateSolverSettings(UIKRigSolver* InSettings){};
	//** END SOLVER SETTINGS */

	//** GOALS */
	/** override to support ADDING a new goal to custom solver */
	virtual void AddGoal(const UIKRigEffectorGoal& NewGoal) PURE_VIRTUAL("AddGoal");
	/** override to support REMOVING a goal from custom solver */
	virtual void RemoveGoal(const FName& GoalName) PURE_VIRTUAL("RemoveGoal");
	/** override to support RENAMING an existing goal */
	virtual void RenameGoal(const FName& OldName, const FName& NewName) PURE_VIRTUAL("RenameGoal");
	/** override to support CHANGING BONE for an existing goal */
	virtual void SetGoalBone(const FName& GoalName, const FName& NewBoneName) PURE_VIRTUAL("SetGoalBone");
	/** override to support QUERY for a connected goal */
	virtual bool IsGoalConnected(const FName& GoalName) const {return false;};
	/** override to support supplying effector settings to outside systems for editing/UI */
	virtual UObject* GetEffectorWithGoal(const FName& GoalName) {return nullptr;};
	//** END GOALS */

	//** ROOT BONE (optional, implement if your solver requires a root bone) */
	/** override to support SETTING ROOT BONE for the solver */
	virtual void SetRootBone(const FName& RootBoneName){};
	virtual bool CanSetRootBone() const { return false; };
	//** END ROOT BONE */

	//** BONE SETTINGS (optional, implement if your solver supports per-bone settings) */
	/** override to support ADDING PER-BONE settings for this solver */
	virtual void AddBoneSetting(const FName& BoneName){};
	/** override to support ADDING PER-BONE settings for this solver */
	virtual void RemoveBoneSetting(const FName& BoneName){};
	/** override to support supplying per-bone settings to outside systems for editing/UI */
	virtual UObject* GetBoneSetting(const FName& BoneName) const { return nullptr;};
	/** override to tell systems if this solver supports per-bone settings */
	virtual bool UsesBoneSettings() const { return false;};
	/** override to draw custom per-bone settings in the editor viewport */
	virtual void DrawBoneSettings(const FName& BoneName, const FIKRigSkeleton& IKRigSkeleton, FPrimitiveDrawInterface* PDI) const {};
	/** return true if the supplied Bone is affected by this solver - this provides UI feedback for user */
	virtual bool IsBoneAffectedBySolver(const FName& BoneName, const FIKRigSkeleton& IKRigSkeleton) const {return false;};
	//** END ROOT BONE */
};
