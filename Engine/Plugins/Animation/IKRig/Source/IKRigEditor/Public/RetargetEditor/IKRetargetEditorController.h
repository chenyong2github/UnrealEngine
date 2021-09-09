// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once
#include "IPersonaToolkit.h"


class SIKRetargetChainMapList;
class UIKRetargetAnimInstance;
class FIKRetargetEditor;
class UIKRetargeter;
class UDebugSkelMeshComponent;

/** a home for cross-widget communication to synchronize state across all tabs and viewport */
class FIKRetargetEditorController
{
public:
	
	/** the data model */
	UIKRetargeter* Asset;

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

	/** Sequence Browser **/
	TWeakPtr<class IAnimationSequenceBrowser> SequenceBrowser;

	/** the persona toolkit */
	TWeakPtr<FIKRetargetEditor> Editor;

	/** get the skeletal meshes we are copying animation between */
	USkeletalMesh* GetSourceSkeletalMesh() const;
	USkeletalMesh* GetTargetSkeletalMesh() const;

	/** get current chain pose */
	FTransform GetTargetBoneTransform(const FName& BoneName) const;
	void GetTargetBoneStartAndEnd(const FName& BoneName, FVector& Start, FVector& End) const;

	/** get the retargeter that is running in the viewport (which is a duplicate of the source asset) */
	UIKRetargeter* GetCurrentlyRunningRetargeter() const;

	void RefreshAllViews();
};