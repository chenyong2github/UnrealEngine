// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Retargeter/IKRetargeter.h"
#include "UObject/Object.h"

#include "IKRetargeterController.generated.h"

class FIKRetargetEditorController;
class URetargetChainSettings;
class UIKRigDefinition;
class UIKRetargeter;

/** A singleton (per-asset) class used to make modifications to a UIKRetargeter asset.
* Call the static UIKRetargeterController() function to get the controller for the asset you want to modify. */ 
UCLASS(config = Engine, hidecategories = UObject)
class IKRIGEDITOR_API UIKRetargeterController : public UObject
{
	GENERATED_BODY()

public:

	/** Use this to get the controller for the given retargeter asset */
	static UIKRetargeterController* GetController(UIKRetargeter* InRetargeterAsset);
	/** Get access to the retargeter asset.
	 *@warning Do not make modifications to the asset directly. Use this API instead. */
	UIKRetargeter* GetAsset() const;
	/** Get access to the editor controller.*/
	FIKRetargetEditorController* GetEditorController() const { return EditorController; };
	/** Set the currently used editor controller.*/
	void SetEditorController(FIKRetargetEditorController* InEditorController) { EditorController = InEditorController; };

	/** SOURCE / TARGET
	* 
	*/
	/** Set the IK Rig to use as the source (to copy animation FROM) */
	void SetSourceIKRig(UIKRigDefinition* SourceIKRig);
	/** Get source skeletal mesh */
	USkeletalMesh* GetSourcePreviewMesh() const;
	/** Get target skeletal mesh */
	USkeletalMesh* GetTargetPreviewMesh() const;
	/** Get source IK Rig asset */
	const UIKRigDefinition* GetSourceIKRig() const;
	/** Get source IK Rig asset */
	const UIKRigDefinition* GetTargetIKRig() const;
	/** Set the target preview mesh based on the mesh in the target IK Rig asset */
	void OnTargetIKRigChanged() const;
	/** Set the source preview mesh based on the mesh in the source IK Rig asset */
	void OnSourceIKRigChanged() const;

	/** Get name of the Root bone used for retargeting the Source skeleton. */
	FName GetSourceRootBone() const;
	/** Get name of the Root bone used for retargeting the Target skeleton. */
	FName GetTargetRootBone() const;

	/** RETARGET CHAIN MAPPING
	* 
	*/
	/** Get names of all the target bone chains. */
	void GetTargetChainNames(TArray<FName>& OutNames) const;
	/** Get names of all the source bone chains. */
	void GetSourceChainNames(TArray<FName>& OutNames) const;
	/** Remove invalid chain mappings (no longer existing in currently referenced source/target IK Rig assets) */
	void CleanChainMapping(const bool bForceReinitialization=true) const;
	/** Use fuzzy string search to find "best" Source chain to map to each Target chain */
	void AutoMapChains() const;
	/** Callback when IK Rig chain is renamed. Retains existing mappings using the new name */
	void OnRetargetChainRenamed(UIKRigDefinition* IKRig, FName OldChainName, FName NewChainName) const;
	/** Callback when IK Rig chain is removed. */
	void OnRetargetChainRemoved(UIKRigDefinition* IKRig, const FName& InChainRemoved) const;
	/** Set the source chain to map to a given target chain */
	void SetSourceChainForTargetChain(URetargetChainSettings* ChainMap, FName SourceChainToMapTo) const;
	/** Get read-only access to the list of chain mappings */
	const TArray<TObjectPtr<URetargetChainSettings>>& GetChainMappings() const;
	/** END RETARGET CHAIN MAPPING */

	/** RETARGET POSE EDITING
	 * 
	 */
	/** Remove bones from retarget poses that are no longer in skeleton */
	void CleanPoseList(const bool bForceReinitialization=true);
	/** Add new retarget pose. */
	void AddRetargetPose(FName NewPoseName, const FIKRetargetPose* ToDuplicate = nullptr) const;
	/** Rename current retarget pose. */
	void RenameCurrentRetargetPose(FName NewPoseName) const;
	/** Remove a retarget pose. */
	void RemoveRetargetPose(FName PoseToRemove) const;
	/** Reset a retarget pose for the specified bones.
	 *If BonesToReset is Empty, will removes all stored deltas, returning pose to reference pose */
	void ResetRetargetPose(FName PoseToReset, const TArray<FName>& BonesToReset) const;
	/** Get the current retarget pose */
    FName GetCurrentRetargetPoseName() const;
	/** Change which retarget pose is used by the retargeter at runtime */
	void SetCurrentRetargetPose(FName CurrentPose) const;
	/** Get read-only access to list of retarget poses */
	const TMap<FName, FIKRetargetPose>& GetRetargetPoses();
	/** Get the current retarget pose */
	const FIKRetargetPose& GetCurrentRetargetPose() const;
	/** Set a delta rotation for a given bone for the current retarget pose (used in Edit Mode in the retarget editor) */
	void SetRotationOffsetForRetargetPoseBone(FName BoneName, FQuat RotationOffset) const;
	/** Get a delta rotation for a given bone for the current retarget pose (used in Edit Mode in the retarget editor) */
	FQuat GetRotationOffsetForRetargetPoseBone(FName BoneName) const;
	/** Add a delta translation to the root bone (used in Edit Mode in the retarget editor) */
	void AddTranslationOffsetToRetargetRootBone(FVector TranslationOffset) const;
	/** Add a numbered suffix to the given pose name to make it unique. */
	FName MakePoseNameUnique(FString PoseName) const;
	/** END RETARGET POSE EDITING */

private:
	
	/** Called whenever the retargeter is modified in such a way that would require re-initialization by dependent systems.*/
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnRetargeterNeedsInitialized, UIKRetargeter*);
	FOnRetargeterNeedsInitialized RetargeterNeedsInitialized;
	
public:
	
	FOnRetargeterNeedsInitialized& OnRetargeterNeedsInitialized(){ return RetargeterNeedsInitialized; };

	void BroadcastNeedsReinitialized() const
	{
		RetargeterNeedsInitialized.Broadcast(GetAsset());
	}
	
private:

	URetargetChainSettings* GetChainMap(const FName& TargetChainName) const;

	/** Sort the Asset ChainMapping based on the StartBone of the target chains. */
	void SortChainMapping() const;

	/** The actual asset that this Controller modifies. */
	TObjectPtr<UIKRetargeter> Asset = nullptr;

	/** The editor controller for this asset. */
	TObjectPtr<FIKRetargetEditorController> EditorController = nullptr;
};
