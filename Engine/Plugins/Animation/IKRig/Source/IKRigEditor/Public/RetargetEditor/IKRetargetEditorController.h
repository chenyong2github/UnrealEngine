// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "IKRetargeterController.h"
#include "IPersonaToolkit.h"
#include "SIKRetargetAssetBrowser.h"
#include "UObject/ObjectMacros.h"
#include "UObject/ObjectPtr.h"
#include "Input/Reply.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/SWindow.h"

class SIKRigOutputLog;
class UIKRetargetProcessor;
class SIKRetargetChainMapList;
class UIKRetargetAnimInstance;
class FIKRetargetEditor;
class UDebugSkelMeshComponent;
class UIKRigDefinition;

/** a home for cross-widget communication to synchronize state across all tabs and viewport */
class FIKRetargetEditorController : public TSharedFromThis<FIKRetargetEditorController>
{
public:

	/** Initialize the editor */
	void Initialize(TSharedPtr<FIKRetargetEditor> InEditor, UIKRetargeter* InAsset);
	/** Bind callbacks to this IK Rig */
	void BindToIKRigAsset(UIKRigDefinition* InIKRig) const;
	/** callback when IK Rig asset requires reinitialization */
	void OnIKRigNeedsInitialized(UIKRigDefinition* ModifiedIKRig) const;
	/** callback when IK Rig asset's retarget chain has been renamed */
	void OnRetargetChainRenamed(UIKRigDefinition* ModifiedIKRig, FName OldName, FName NewName) const;
	/** callback when IK Rig asset's retarget chain has been removed */
	void OnRetargetChainRemoved(UIKRigDefinition* ModifiedIKRig, const FName& InChainRemoved) const;
	/** callback when IK Retargeter asset requires reinitialization */
	void OnRetargeterNeedsInitialized(const UIKRetargeter* Retargeter) const;
	
	/** all modifications to the data model should go through this controller */
	UIKRetargeterController* AssetController;

	/** Preview scene to be supplied by IHasPersonaToolkit::GetPersonaToolkit */
	TSharedPtr<IPersonaToolkit> PersonaToolkit;

	/** the persona toolkit */
	TWeakPtr<FIKRetargetEditor> Editor;

	/** viewport skeletal mesh */
	UDebugSkelMeshComponent* SourceSkelMeshComponent;
	UDebugSkelMeshComponent* TargetSkelMeshComponent;

	/** viewport anim instance */
	UPROPERTY(transient, NonTransactional)
	TWeakObjectPtr<class UAnimPreviewInstance> SourceAnimInstance;
	UPROPERTY(transient, NonTransactional)
	TWeakObjectPtr<class UIKRetargetAnimInstance> TargetAnimInstance;

	void AddOffsetAndUpdatePreviewMeshPosition(const FVector& Offset, USceneComponent* Component) const;

	/** asset properties tab */
	TSharedPtr<IDetailsView> DetailsView;
	/** chain list view */
	TSharedPtr<SIKRetargetChainMapList> ChainsView;
	/** asset browser view */
	TSharedPtr<SIKRetargetAssetBrowser> AssetBrowserView;
	/** output log view */
	TSharedPtr<SIKRigOutputLog> OutputLogView;
	/** clear the output log */
	void ClearOutputLog() const;
	/** force refresh all views in the editor */
	void RefreshAllViews() const;

	/** get the source skeletal mesh we are copying FROM */
	USkeletalMesh* GetSourceSkeletalMesh() const;
	/** get the target skeletal mesh we are copying TO */
	USkeletalMesh* GetTargetSkeletalMesh() const;
	/** get the source skeleton asset */
	const USkeleton* GetSourceSkeleton() const;
	/** get the target skeleton asset */
	const USkeleton* GetTargetSkeleton() const;

	/** get list of currently selected bones */
	const TArray<FName>& GetSelectedBones() const;
	
	/** get current chain pose */
	FTransform GetTargetBoneGlobalTransform(const UIKRetargetProcessor* RetargetProcessor, const int32& TargetBoneIndex) const;
	FTransform GetTargetBoneLocalTransform(const UIKRetargetProcessor* RetargetProcessor, const int32& TargetBoneIndex) const;
	/** get the line segments to draw from this bone to each child */
	bool GetTargetBoneLineSegments(
		const UIKRetargetProcessor* RetargetProcessor,
		const int32& TargetBoneIndex,
		FVector& OutStart,
		TArray<FVector>& OutChildren) const;

	/** get the retargeter that is running in the viewport (which is a duplicate of the source asset) */
	UIKRetargetProcessor* GetRetargetProcessor() const;
	/** Reset the planting state of the IK (when scrubbing or animation loops over) */
	void ResetIKPlantingState() const;

	/** Sequence Browser**/
	void PlayAnimationAsset(UAnimationAsset* AssetToPlay);
	/** END Sequence Browser */

	/** Set viewport / editor tool mode */
	void SetRetargeterMode(ERetargeterOutputMode Mode);
	UAnimationAsset* AnimThatWasPlaying = nullptr;
	bool bWasPlayingAnim = false;
	/** END viewport / editor tool mode */

	/* START RETARGET POSES */
	
	/** go to retarget pose */
	void HandleGoToRetargetPose();
	
	/** toggle current retarget pose */
	TArray<TSharedPtr<FName>> PoseNames;
	FText GetCurrentPoseName() const;
	void OnPoseSelected(TSharedPtr<FName> InPoseName, ESelectInfo::Type SelectInfo) const;
	
	/** edit retarget poses */
	void HandleEditPose();
	bool CanEditPose() const;
	bool IsEditingPose() const;

	/** reset retarget pose */
	void HandleResetAllBones() const;
	void HandleResetSelectedBones() const;
	void HandleResetSelectedAndChildrenBones() const;
	bool CanResetPose() const;
	bool CanResetSelected() const;

	/** create new retarget pose */
	void HandleNewPose();
	bool CanCreatePose() const;
	FReply CreateNewPose() const;
	TSharedPtr<SWindow> NewPoseWindow;
	TSharedPtr<SEditableTextBox> NewPoseEditableText;

	/** duplicate current retarget pose */
	void HandleDuplicatePose();
	FReply CreateDuplicatePose() const;

	/** import retarget pose from asset*/
	void HandleImportPose();
	FReply ImportRetargetPose() const;
	void OnRetargetPoseSelected(const FAssetData& SelectedAsset);
	FSoftObjectPath RetargetPoseToImport;
	TSharedPtr<SWindow> ImportPoseWindow;

	/** import retarget pose from animation sequence*/
	void HandleImportPoseFromSequence();
	bool OnShouldFilterSequenceToImport(const struct FAssetData& AssetData) const;
	FReply OnImportPoseFromSequence();
	void OnSequenceSelectedForPose(const FAssetData& SelectedAsset);
	TSharedPtr<SWindow> ImportPoseFromSequenceWindow;
	FSoftObjectPath SequenceToImportAsPose;
	int32 FrameOfSequenceToImport;
	FText ImportedPoseName;

	/** export retarget pose to asset*/
	void HandleExportPose();

	/** delete retarget pose */
	void HandleDeletePose() const;
	bool CanDeletePose() const;

	/** rename retarget pose */
	void HandleRenamePose();
	FReply RenamePose() const;
	bool CanRenamePose() const;
	TSharedPtr<SWindow> RenamePoseWindow;
	TSharedPtr<SEditableTextBox> NewNameEditableText;
	
	/* END RETARGET POSES */
};
