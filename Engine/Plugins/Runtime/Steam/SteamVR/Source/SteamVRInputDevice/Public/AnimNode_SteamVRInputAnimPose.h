// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNodeBase.h"
#include "SteamVRInputDeviceFunctionLibrary.h"
#include "SteamVRSkeletonDefinition.h"
#include "AnimNode_SteamVRInputAnimPose.generated.h"

/**
* Custom animation node to retrieve poses from the Skeletal Input System
*/
USTRUCT(BlueprintType)
struct STEAMVRINPUTDEVICE_API FAnimNode_SteamVRInputAnimPose : public FAnimNode_Base
{
	GENERATED_USTRUCT_BODY()

	/** Range of motion for the skeletal input values */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings, meta = (AlwaysAsPin))
	EMotionRange MotionRange = EMotionRange::VR_WithoutController;

	/** Which hand should the animation node retrieve skeletal input values for */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings, meta = (AlwaysAsPin))
	EHand Hand = EHand::VR_LeftHand;

	/** What kind of skeleton are we dealing with */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings, meta = (AlwaysAsPin))
	EHandSkeleton HandSkeleton = EHandSkeleton::VR_SteamVRHandSkeleton;

	/** Should the pose be mirrored so it can be applied to the opposite hand */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings, meta = ( AlwaysAsPin ))
	bool Mirror = false;

	/** The UE4 equivalent of the SteamVR Transform values per bone */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = SteamVRInput)
	FSteamVRSkeletonTransform SteamVRSkeletalTransform;

	/** SteamVR Skeleton to UE4 retargetting cache */
	UPROPERTY()
	FUE4RetargettingRefs UE4RetargettingRefs;

public:

	// FAnimNode_Base interface
	virtual void Initialize_AnyThread(const FAnimationInitializeContext& Context) override;
	virtual void CacheBones_AnyThread(const FAnimationCacheBonesContext& Context) override;
	virtual void Update_AnyThread(const FAnimationUpdateContext & Context) override;
	virtual void Evaluate_AnyThread(FPoseContext& Output) override;
	// End of FAnimNode_Base interface

	FAnimNode_SteamVRInputAnimPose();

	/** 
	 * Retarget the given array of bone transforms for the SteamVR skeleton to the UE4 hand skeleton and apply it to the given FPoseContext. 
	 * The bone transforms are in the bone's local space.  Assumes that PoseContest.Pose has already been set to its reference pose
	*/
	void PoseUE4HandSkeleton(FCompactPose& Pose, const FTransform* BoneTransformsLS, int32 BoneTransformCount);

	/** Retrieve the first active SteamVRInput device present in this game */
	FSteamVRInputDevice* GetSteamVRInputDevice();

	/** Recursively calculate the model-space transform of the given bone from the local-space transforms on the given pose */
	FTransform CalcModelSpaceTransform(const FCompactPose& Pose, FCompactPoseBoneIndex BoneIndex);

};
