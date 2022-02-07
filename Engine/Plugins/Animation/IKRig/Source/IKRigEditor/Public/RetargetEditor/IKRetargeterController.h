// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Retargeter/IKRetargeter.h"
#include "UObject/Object.h"

#include "IKRetargeterController.generated.h"

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

	/** SOURCE / TARGET
	* 
	*/
	/** Set the IK Rig to use as the source (to copy animation FROM) */
	void SetSourceIKRig(UIKRigDefinition* SourceIKRig);
	/** Set the IK Rig to use as the target (to copy animation TO) */
	void SetTargetIKRig(UIKRigDefinition* TargetIKRig);
	/** Get target skeletal mesh */
	USkeletalMesh* GetTargetPreviewMesh();
	
	/** Get the USkeleton used on the Source asset */
	USkeleton* GetSourceSkeletonAsset() const;

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
	void CleanChainMapping();
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
	void CleanPoseList();
	/** Add new retarget pose. */
	void AddRetargetPose(FName NewPoseName) const;
	/** Remove a retarget pose. */
	void RemoveRetargetPose(FName PoseToRemove) const;
	/** Reset a retarget pose (removes all stored deltas, returning pose to reference pose */
	void ResetRetargetPose(FName PoseToReset) const;
	/** Get the current retarget pose */
    FName GetCurrentRetargetPoseName() const;
	/** Change which retarget pose is used by the retargeter at runtime */
	void SetCurrentRetargetPose(FName CurrentPose) const;
	/** Get read-only access to list of retarget poses */
	const TMap<FName, FIKRetargetPose>& GetRetargetPoses();
	/** Set a delta rotation for a given bone for the current retarget pose (used in Edit Mode in the retarget editor) */
	void SetRotationOffsetForRetargetPoseBone(FName BoneName, FQuat RotationOffset) const;
	/** Get a delta rotation for a given bone for the current retarget pose (used in Edit Mode in the retarget editor) */
	FQuat GetRotationOffsetForRetargetPoseBone(FName BoneName) const;
	/** Add a delta translation to the root bone (used in Edit Mode in the retarget editor) */
	void AddTranslationOffsetToRetargetRootBone(FVector TranslationOffset) const;
	/** Set whether to output retarget pose. Will output current retarget pose if true, or run retarget otherwise. */
	void SetEditRetargetPoseMode(bool bOutputRetargetPose, bool bReinitializeAfter=true) const;
	/** Get whether in mode to output retarget pose (true) or run retarget (false). */
	bool GetEditRetargetPoseMode() const;
	/** Add a numbered suffix to the given pose name to make it unique. */
	FName MakePoseNameUnique(FName PoseName) const;
	/** END RETARGET POSE EDITING */

private:
	
	/** Called whenever the retargeter is modified in such a way that would require re-initialization by dependent systems.*/
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnRetargeterNeedsInitialized, const UIKRetargeter*);
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
	UIKRetargeter* Asset = nullptr;
};
