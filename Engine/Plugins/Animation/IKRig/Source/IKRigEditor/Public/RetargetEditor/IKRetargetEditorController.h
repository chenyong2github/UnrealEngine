// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "IKRetargeterController.h"
#include "IPersonaToolkit.h"
#include "UObject/ObjectMacros.h"
#include "UObject/ObjectPtr.h"
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
	void BindToIKRigAsset(UIKRigDefinition* InIKRig);
	/** callback when IK Rig asset requires reinitialization */
	void OnIKRigNeedsInitialized(UIKRigDefinition* ModifiedIKRig);
	/** callback when IK Rig asset's retarget chain has been renamed */
	void OnRetargetChainRenamed(UIKRigDefinition* ModifiedIKRig, FName OldName, FName NewName);
	/** callback when IK Retargeter asset requires reinitialization */
	void OnRetargeterNeedsInitialized(const UIKRetargeter* Retargeter);
	
	/** all modifications to the data model should go through this controller */
	UIKRetargeterController* AssetController;

	/** viewport skeletal mesh */
	UDebugSkelMeshComponent* SourceSkelMeshComponent;
	UDebugSkelMeshComponent* TargetSkelMeshComponent;

	/** viewport anim instance */
	UPROPERTY(transient, NonTransactional)
	TObjectPtr<class UAnimPreviewInstance> SourceAnimInstance;
	UPROPERTY(transient, NonTransactional)
	TObjectPtr<class UIKRetargetAnimInstance> TargetAnimInstance;

	/** asset properties tab */
	TSharedPtr<class IDetailsView> DetailsView;

	/** chain list view */
	TSharedPtr<SIKRetargetChainMapList> ChainsView;

	/** Preview scene to be supplied by IHasPersonaToolkit::GetPersonaToolkit */
	TSharedPtr<IPersonaToolkit> PersonaToolkit;

	/** the persona toolkit */
	TWeakPtr<FIKRetargetEditor> Editor;

	/** get the skeletal meshes we are copying animation between */
	USkeletalMesh* GetSourceSkeletalMesh() const;
	USkeletalMesh* GetTargetSkeletalMesh() const;

	/** get current chain pose */
	FTransform GetTargetBoneTransform(const int32& TargetBoneIndex) const;
	/** get the line segments to draw from this bone to each child */
	bool GetTargetBoneLineSegments(const int32& TargetBoneIndex, FVector& OutStart, TArray<FVector>& OutChildren) const;
	/** get if the target bone is being retargeted or not */
	bool IsTargetBoneRetargeted(const int32& TargetBoneIndex);

	/** get the retargeter that is running in the viewport (which is a duplicate of the source asset) */
	const UIKRetargetProcessor* GetRetargetProcessor() const;

	/** Sequence Browser and Edit Pose mode **/
	void PlayAnimationAsset(UAnimationAsset* AssetToPlay);
	void PlayPreviousAnimationAsset();
	UAnimationAsset* PreviousAsset = nullptr;

	void RefreshAllViews();
};
