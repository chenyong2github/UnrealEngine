// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IKRigDefinition.h"

#include "IKRetargeter.generated.h"

struct FIKRetargetPose;

USTRUCT(Blueprintable)
struct IKRIG_API FRetargetChainMap
{
	GENERATED_BODY()

	FRetargetChainMap(){}
	
	FRetargetChainMap(const FName& TargetChain) : TargetChain(TargetChain){}
	
	/** The name of the chain on the Source IK Rig asset to copy animation from. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Offsets)
	FName SourceChain = NAME_None;

	/** The name of the chain on the Target IK Rig asset to copy animation from. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Offsets)
	FName TargetChain = NAME_None;

	/** Range -1 to 2. Default 0. Brings IK effector closer (-) or further (+) from origin of chain.
	*  At -1 the end is on top of the origin. At +2 the end is fully extended twice the length of the chain. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Offsets, meta = (ClampMin = "-1.0", ClampMax = "2.0", UIMin = "-1.0", UIMax = "2.0"))
	float Extension = 0.0f;

	/** Range 0 to 1. Default 0. Allow the chain to stretch by translating to reach the IK goal locations.
	*  At 0 the chain will not stretch at all. At 1 the chain will be allowed to stretch double it's full length to reach IK. */
	//UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Stretch, meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	//float StretchTolerance = 0.0f;

	/** Range 0 to 1. Default is 1.0. Blend IK effector at the end of this chain towards the original position
	 *  on the source skeleton (0.0) or the position on the retargeted target skeleton (1.0). */
	//UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = IkMode, meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	//float IkToSourceOrTarget = 1.0f;
	
	/** When true, the source IK position is calculated relative to a source bone and applied relative to a target bone. */
	//UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = IkMode)
	//bool bIkRelativeToSourceBone = false;
	
	/** A bone in the SOURCE skeleton that the IK location is relative to. */
	//UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = IkMode, meta = (EditCondition="bIkRelativeToSourceBone"))
	//FName IkRelativeSourceBone;
	
	/** A bone in the TARGET skeleton that the IK location is relative to.
	 * This is usually the same bone as the source skeleton, but may have a different name in the target skeleton. */
	//UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = IkMode, meta = (EditCondition="bIkRelativeToSourceBone"))
	//FName IkRelativeTargetBone;	
};

USTRUCT()
struct IKRIG_API FIKRetargetPose
{
	GENERATED_BODY()
	
	FIKRetargetPose(){}
	
	UPROPERTY(EditAnywhere, Category = RetargetPose)
	FVector RootTranslationOffset = FVector::ZeroVector;
	
	UPROPERTY(EditAnywhere, Category = RetargetPose)
	TMap<FName, FQuat> BoneRotationOffsets;

	void AddRotationDeltaToBone(FName BoneName, FQuat RotationDelta);

	void AddTranslationDeltaToRoot(FVector TranslateDelta);
};


UCLASS(Blueprintable)
class IKRIG_API UIKRetargeter : public UObject
{
	GENERATED_BODY()
public:

	/** Get read-only access to the source IK Rig asset */
	const UIKRigDefinition* GetSourceIKRig() const { return SourceIKRigAsset.Get(); };
	/** Get read-only access to the target IK Rig asset */
	const UIKRigDefinition* GetTargetIKRig() const { return TargetIKRigAsset.Get(); };
	/** Get read-write access to the source IK Rig asset.
	 * WARNING: do not use for editing the data model. Use Controller class instead. */
	 UIKRigDefinition* GetSourceIKRigWriteable() const { return SourceIKRigAsset.Get(); };
	/** Get read-write access to the target IK Rig asset.
	 * WARNING: do not use for editing the data model. Use Controller class instead. */
	UIKRigDefinition* GetTargetIKRigWriteable() const { return TargetIKRigAsset.Get(); };
	/** Get read-only access to the chain mapping */
	const TArray<FRetargetChainMap>& GetChainMapping() const { return ChainMapping; };
	/** Get read-only access to a retarget pose */
	const FIKRetargetPose* GetCurrentRetargetPose() const { return &RetargetPoses[CurrentRetargetPose]; };

	/* Get name of Source IK Rig property */
	static const FName GetSourceIKRigPropertyName();
	/* Get name of Target IK Rig property */
	static const FName GetTargetIKRigPropertyName();
#if WITH_EDITOR
	/* Get name of Target Preview Mesh property */
	static const FName GetTargetPreviewMeshPropertyName();
#endif
	/* Get name of default pose */
	static const FName GetDefaultPoseName();

#if WITH_EDITOR
	bool IsInEditRetargetPoseMode() const { return bEditRetargetPoseMode; };

	// This delegate is called when an edit operation is undone on the rig asset.
	DECLARE_MULTICAST_DELEGATE(OnIKRigEditUndo);
	OnIKRigEditUndo IKRigEditUndo;
	
	virtual void PostEditUndo() override;
#endif

private:

	/** The rig to copy animation FROM.*/
	UPROPERTY(VisibleAnywhere, Category = Rigs)
	TWeakObjectPtr<UIKRigDefinition> SourceIKRigAsset = nullptr;
	
	/** The rig to copy animation TO.*/
	UPROPERTY(EditAnywhere, Category = Rigs)
	TWeakObjectPtr<UIKRigDefinition> TargetIKRigAsset = nullptr;

public:

#if WITH_EDITORONLY_DATA
	/** The Skeletal Mesh to preview the retarget on.*/
	UPROPERTY(EditAnywhere, Category = Rigs)
	TWeakObjectPtr<USkeletalMesh> TargetPreviewMesh = nullptr;
#endif
	
	/** When false, translational motion of skeleton root is not copied. Useful for debugging.*/
	UPROPERTY(EditAnywhere, Category = RetargetPhases)
	bool bRetargetRoot = true;
	
	/** When false, limbs are not copied via FK. Useful for debugging limb issues suspected to be caused by FK pose.*/
	UPROPERTY(EditAnywhere, Category = RetargetPhases)
	bool bRetargetFK = true;
	
	/** When false, IK is not applied as part of retargeter. Useful for debugging limb issues suspected to be caused by IK.*/
	UPROPERTY(EditAnywhere, Category = RetargetPhases)
	bool bRetargetIK = true;

#if WITH_EDITORONLY_DATA
	/** Move the target actor in the viewport for easier visualization next to the source actor.*/
	UPROPERTY(EditAnywhere, Category = TargetActorPreview, meta = (UIMin = "-2000.0", UIMax = "2000.0"))
	float TargetActorOffset = 150.0f;

	/** Scale the target actor in the viewport for easier visualization next to the source actor.*/
	UPROPERTY(EditAnywhere, Category = TargetActorPreview, meta = (UIMin = "0.01", UIMax = "10.0"))
	float TargetActorScale = 1.0f;

	/** The visual size of the bones in the viewport when editing the retarget pose.*/
	UPROPERTY(EditAnywhere, Category = PoseEditSettings, meta = (ClampMin = "0.0", UIMin = "0.01", UIMax = "10.0"))
	float BoneDrawSize = 8.0f;

	/** The visual thickness of the bones in the viewport when editing the retarget pose.*/
	UPROPERTY(EditAnywhere, Category = PoseEditSettings, meta = (ClampMin = "0.0", UIMin = "0.01", UIMax = "10.0"))
	float BoneDrawThickness = 1.0f;

private:
	/** A special editor-only mode which forces the retargeter to output the current retarget reference pose,
	* rather than actually running the retarget and outputting the retargeted pose. Used in Edit-Pose mode.*/
	UPROPERTY()
	bool bEditRetargetPoseMode = false;

	/** The controller responsible for managing this asset's data (all editor mutation goes through this) */
	UPROPERTY(Transient)
	TObjectPtr<UObject> Controller;
#endif

private:
	/** The set of retarget poses available as options for retargeting.*/
	UPROPERTY()
	TMap<FName, FIKRetargetPose> RetargetPoses;

	/** Mapping of chains to copy animation between source and target rigs.*/
	UPROPERTY()
	TArray<FRetargetChainMap> ChainMapping;
	
	/** The set of retarget poses available as options for retargeting.*/
	UPROPERTY()
	FName CurrentRetargetPose = DefaultPoseName;
	static const FName DefaultPoseName;

	friend class UIKRetargeterController;
};


