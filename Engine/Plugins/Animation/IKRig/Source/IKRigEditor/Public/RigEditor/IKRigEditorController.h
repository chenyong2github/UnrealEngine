// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IKRigSolver.h"
#include "SIKRigRetargetChainList.h"
#include "Templates/SubclassOf.h"
#include "Animation/AnimationAsset.h"
#include "SAdvancedTransformInputBox.h"

#include "IKRigEditorController.generated.h"

class SIKRigAssetBrowser;
class SIKRigOutputLog;
class UIKRigAnimInstance;
class FIKRigEditorToolkit;
class SIKRigSolverStack;
class SIKRigSkeleton;
class UIKRigController;
class FSolverStackElement;
class FIKRigTreeElement;
class UDebugSkelMeshComponent;

enum EIKRigSelectionType : int8
{
	Hierarchy,
	SolverStack,
	RetargetChains,
};

UCLASS(config = Engine, hidecategories = UObject)
class IKRIGEDITOR_API UIKRigBoneDetails : public UObject
{
	GENERATED_BODY()

public:

	// todo update bone info automatically using something else
	void SetBone(const FName& BoneName)
	{
		SelectedBone = BoneName;
	};

	UPROPERTY(VisibleAnywhere, Category = "Selection")
	FName SelectedBone;
	
	UPROPERTY(VisibleAnywhere, Category = "Bone Transforms")
	FTransform CurrentTransform;

	UPROPERTY(VisibleAnywhere, Category = "Bone Transforms")
	FTransform ReferenceTransform;

	UPROPERTY()
	TWeakObjectPtr<UAnimInstance> AnimInstancePtr;

	UPROPERTY()
	TWeakObjectPtr<UIKRigDefinition> AssetPtr;

#if WITH_EDITOR

	TOptional<FTransform> GetTransform(EIKRigTransformType::Type TransformType) const;
	bool IsComponentRelative(ESlateTransformComponent::Type Component, EIKRigTransformType::Type TransformType) const;
	void OnComponentRelativeChanged(ESlateTransformComponent::Type Component, bool bIsRelative, EIKRigTransformType::Type TransformType);
	void OnCopyToClipboard(ESlateTransformComponent::Type Component, EIKRigTransformType::Type TransformType) const;
	void OnPasteFromClipboard(ESlateTransformComponent::Type Component, EIKRigTransformType::Type TransformType);

#endif

private:
	
	static bool CurrentTransformRelative[3];
	static bool ReferenceTransformRelative[3];
};

/** a home for cross-widget communication to synchronize state across all tabs and viewport */
class FIKRigEditorController : public TSharedFromThis<FIKRigEditorController>, FGCObject
{
public:

	/** initialize the editor controller to an instance of the IK Rig editor */
	void Initialize(TSharedPtr<FIKRigEditorToolkit> Toolkit, UIKRigDefinition* Asset);

	/** get the currently active processor running the IK Rig in the editor */
	UIKRigProcessor* GetIKRigProcessor() const;

	/** callback when IK Rig requires re-initialization */
	void OnIKRigNeedsInitialized(UIKRigDefinition* ModifiedIKRig);

	/** create goals */
	void AddNewGoals(const TArray<FName>& GoalNames, const TArray<FName>& BoneNames);
	/** clear all selected objects */
	void ClearSelection();
	/** callback when goal is selected in the viewport */
	void HandleGoalSelectedInViewport(const FName& GoalName, bool bReplace) const;
	/** callback when bone is selected in the viewport */
	void HandleBoneSelectedInViewport(const FName& BoneName, bool bReplace) const;
	/** reset all goals to initial transforms */
	void Reset() const;
	/** refresh all views */
	void RefreshAllViews() const;
	/** refresh just the skeleton tree view */
	void RefreshTreeView() const;
	/** clear the output log */
	void ClearOutputLog() const;

	/** return list of those solvers in the stack that are selected by user */
	void GetSelectedSolvers(TArray<TSharedPtr<FSolverStackElement> >& OutSelectedSolvers);
	/** get index of the first selected solver, return INDEX_None if nothing selected */
	int32 GetSelectedSolverIndex();
	/** get names of all goals that are selected */
	void GetSelectedGoalNames(TArray<FName>& OutGoalNames) const;
	/** return the number of selected goals */
	int32 GetNumSelectedGoals() const;
	/** get names of all bones that are selected */
	void GetSelectedBoneNames(TArray<FName>& OutBoneNames) const;
	/** get all bones that are selected */
	void GetSelectedBones(TArray<TSharedPtr<FIKRigTreeElement>>& OutBoneItems) const;
	/** returns true if Goal is currently selected */
	bool IsGoalSelected(const FName& GoalName) const;
	/** get name of the selected retargeting chain */
	FName GetSelectedChain() const;
	/** is anything selected in the skeleton view? */
	bool DoesSkeletonHaveSelectedItems() const;
	
	/** determine if the element is connected to the selected solver */
	bool IsElementConnectedToSolver(TSharedRef<FIKRigTreeElement> TreeElement, int32 SolverIndex);
	/** determine if the element is connected to ANY solver */
	bool IsElementConnectedToAnySolver(TSharedRef<FIKRigTreeElement> TreeElement);
	/** determine if the element is an excluded bone*/
	bool IsElementExcludedBone(TSharedRef<FIKRigTreeElement> TreeElement);
	
	/** show single transform of bone in details view */
	void ShowDetailsForBone(const FName BoneName) const;
	/** show single BONE settings in details view */
	void ShowDetailsForBoneSettings(const FName& BoneName, int32 SolverIndex) const;
	/** show single GOAL settings in details view */
	void ShowDetailsForGoal(const FName& GoalName) const;
	/** show single EFFECTOR settings in details view */
	void ShowDetailsForGoalSettings(const FName GoalName, const int32 SolverIndex) const;
	/** show single SOLVER settings in details view */
	void ShowDetailsForSolver(const int32 SolverIndex) const;
	/** show nothing in details view */
	void ShowEmptyDetails() const;
	/** show selected items in details view */
	void ShowDetailsForElements(const TArray<TSharedPtr<FIKRigTreeElement>>& InItems) const;
	/** callback when detail is edited */
	void OnFinishedChangingDetails(const FPropertyChangedEvent& PropertyChangedEvent);
	
	/** set details tab view */
	void SetDetailsView(const TSharedPtr<class IDetailsView>& InDetailsView);
	/** set skeleton tab view */
	void SetSkeletonsView(const TSharedPtr<SIKRigSkeleton>& InSkeletonView){ SkeletonView = InSkeletonView; };
	/** set solver stack tab view */
	void SetSolverStackView(const TSharedPtr<SIKRigSolverStack>& InSolverStackView){ SolverStackView = InSolverStackView; };
	/** set retargeting tab view */
	void SetRetargetingView(const TSharedPtr<SIKRigRetargetChainList>& InRetargetingView){ RetargetingView = InRetargetingView; };

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
	TWeakObjectPtr<UIKRigAnimInstance> AnimInstance;

	/** the persona toolkit */
	TWeakPtr<FIKRigEditorToolkit> EditorToolkit;

	/** UI and viewport selection state */
	bool bManipulatingGoals = false;

	/** record which part of the UI was last selected */
	EIKRigSelectionType GetLastSelectedType() const;
	void SetLastSelectedType(EIKRigSelectionType SelectionType);
	/** END selection type */

	/** FGCObject interface */
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override
	{
		Collector.AddReferencedObject(BoneDetails);
	};
	virtual FString GetReferencerName() const override { return "IKRigEditorController"; };
	/** END FGCObject interface */

	/** UIKRigBoneDetails factory **/
	TObjectPtr<UIKRigBoneDetails> CreateBoneDetails(const TSharedPtr<FIKRigTreeElement const>& InItem) const;
	
private:
	
	/** right after importing a skeleton, we ask user what solver they want to use */
	bool PromptToAddSolver() const;

	/** Initializes editor's solvers instances */
	void InitializeSolvers() const;

	/** asset properties tab */
	TSharedPtr<IDetailsView> DetailsView;

	/** the skeleton tree view */
	TSharedPtr<SIKRigSkeleton> SkeletonView;
	
	/** the solver stack view */
	TSharedPtr<SIKRigSolverStack> SolverStackView;

	/** the solver stack view */
	TSharedPtr<SIKRigRetargetChainList> RetargetingView;

	/** asset browser view */
	TSharedPtr<SIKRigAssetBrowser> AssetBrowserView;

	/** output log view */
	TSharedPtr<SIKRigOutputLog> OutputLogView;

	EIKRigSelectionType LastSelectedType;

	UPROPERTY()
	TObjectPtr<UIKRigBoneDetails> BoneDetails;

	friend struct FIKRigOutputLogTabSummoner;
	friend class SIKRigAssetBrowser;
};

struct FIKRigSolverTypeAndName
{
	FText NiceName;
	TSubclassOf<UIKRigSolver> SolverType;
};
