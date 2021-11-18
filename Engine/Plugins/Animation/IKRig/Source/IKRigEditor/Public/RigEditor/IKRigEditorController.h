// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IKRigSolver.h"
#include "SIKRigRetargetChainList.h"
#include "Templates/SubclassOf.h"
#include "Animation/AnimationAsset.h"

#include "IKRigEditorController.generated.h"

class UIKRigAnimInstance;
class FIKRigEditorToolkit;
class SIKRigSolverStack;
class SIKRigSkeleton;
class UIKRigController;
class FSolverStackElement;
class FIKRigTreeElement;
class UDebugSkelMeshComponent;

/** a home for cross-widget communication to synchronize state across all tabs and viewport */
class FIKRigEditorController : public TSharedFromThis<FIKRigEditorController>
{
public:

	/** initialize the editor controller to an instance of the IK Rig editor */
	void Initialize(TSharedPtr<FIKRigEditorToolkit> Toolkit, UIKRigDefinition* Asset);

	/** callback when IK Rig requires re-initialization */
	void OnIKRigNeedsInitialized(UIKRigDefinition* ModifiedIKRig);

	/** create goals */
	void AddNewGoals(const TArray<FName>& GoalNames, const TArray<FName>& BoneNames);
	/** delete goals */
	void DeleteGoal(const FName& GoalToDelete);
	/** get array of the names of currently selected goals */
	const TArray<FName>& GetSelectedGoals() const {return SelectedGoals;};
	/** return true if a goal with the given name is selected */
	bool IsGoalSelected(const FName& GoalName) const;
	/** when goal is renamed, update selection list */
	void ReplaceGoalInSelection(const FName& OldName, const FName& NewName);
	/** return number of goals that are currently selected */
	int32 GetNumSelectedGoals();
	/** callback when goal is selected in the viewport */
	void HandleGoalSelectedInViewport(const FName& GoalName, bool bReplace);
	/** callback when goal is selected in the skeleton view */
	void HandleGoalsSelectedInTreeView(const TArray<FName>& GoalNames);
	/** reset all goals to initial transforms */
	void Reset() const;
	/** refresh all views */
	void RefreshAllViews() const;

	/** return list of those solvers in the stack that are selected by user */
	void GetSelectedSolvers(TArray<TSharedPtr<FSolverStackElement> >& OutSelectedSolvers);
	/** get index of the first selected solver, return INDEX_None if nothing selected */
	int32 GetSelectedSolverIndex();
	/** right after importing a skeleton, we ask user what solver they want to use */
	bool PromptToAddSolver() const;
	/** determine if the element is connected to the selected solver */
	bool IsElementConnectedToSolver(TSharedRef<FIKRigTreeElement> TreeElement, int32 SolverIndex);
	/** determine if the element is connected to ANY solver */
	bool IsElementConnectedToAnySolver(TSharedRef<FIKRigTreeElement> TreeElement);
	/** determine if the element is an excluded bone*/
	bool IsElementExcludedBone(TSharedRef<FIKRigTreeElement> TreeElement);
	
	/** todo show BONE transform in details view */
	void ShowDetailsForBone(const FName BoneName);
	/** show BONE settings in details view */
	void ShowDetailsForBoneSettings(const FName BoneName, int32 SolverIndex);
	/** show GOAL settings in details view */
	void ShowDetailsForGoal(const FName GoalName);
	/** show EFFECTOR settings in details view */
	void ShowDetailsForGoalSettings(const FName GoalName, const int32 SolverIndex);
	/** show SOLVER settings in details view */
	void ShowDetailsForSolver(const int32 SolverIndex);
	/** show nothing in details view */
	void ShowEmptyDetails();

	/** create a new retarget chain */
	void AddNewRetargetChain(const FName ChainName, const FName StartBone, const FName EndBone);

	/** play preview animation on running anim instance in editor (before IK) */
	void PlayAnimationAsset(UAnimationAsset* AssetToPlay);
	
	/** all modifications to the data model should go through this controller */
	UIKRigController* AssetController;

	/** viewport skeletal mesh */
	UDebugSkelMeshComponent* SkelMeshComponent;

	/** viewport anim instance */
	UPROPERTY(transient, NonTransactional)
	TWeakObjectPtr<class UIKRigAnimInstance> AnimInstance;

	/** asset properties tab */
	TSharedPtr<class IDetailsView> DetailsView;

	/** the skeleton tree view */
	TSharedPtr<SIKRigSkeleton> SkeletonView;
	
	/** the solver stack view */
	TSharedPtr<SIKRigSolverStack> SolverStackView;

	/** the solver stack view */
	TSharedPtr<SIKRigRetargetChainList> RetargetingView;

	/** the persona toolkit */
	TWeakPtr<FIKRigEditorToolkit> EditorToolkit;

	/** UI and viewport selection state */
	bool bManipulatingGoals = false;

private:

	/** Initializes editor's solvers instances */
	void InitializeSolvers() const;
	
	/** UI and viewport selection state */
	TArray<FName> SelectedGoals;	
};

/** only used for pop-up window to selected a first solver to add*/
USTRUCT()
struct FIKRigAddFirstSolverSettings
{
	GENERATED_BODY()

	FIKRigAddFirstSolverSettings()
        : SolverType(nullptr)
	{}

	UPROPERTY(EditAnywhere, Category = "Add Solver")
	TSubclassOf<UIKRigSolver> SolverType;
};
