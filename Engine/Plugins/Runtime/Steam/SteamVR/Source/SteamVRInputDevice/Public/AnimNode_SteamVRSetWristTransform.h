// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNodeBase.h"
#include "SteamVRInputDeviceFunctionLibrary.h"
#include "SteamVRSkeletonDefinition.h"
#include "AnimNode_SteamVRSetWristTransform.generated.h"

/**
* Custom animation node that sets the wrist transform of a target pose from a reference pose
*/
USTRUCT(BlueprintInternalUseOnly)
struct STEAMVRINPUTDEVICE_API FAnimNode_SteamVRSetWristTransform : public FAnimNode_Base
{
	GENERATED_USTRUCT_BODY()

	/** The pose from where we will get the root and/or wrist transform from */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Links)
	FPoseLink ReferencePose;

	/** What kind of skeleton is used in the reference pose */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings, meta = (AlwaysAsPin))
	EHandSkeleton HandSkeleton = EHandSkeleton::VR_SteamVRHandSkeleton;

	/** The pose to apply the wrist transform to */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Links)
	FPoseLink TargetPose;

public:

	// FAnimNode_Base interface
	virtual void Initialize_AnyThread(const FAnimationInitializeContext& Context) override;
	virtual void CacheBones_AnyThread(const FAnimationCacheBonesContext& Context) override;
	virtual void Update_AnyThread(const FAnimationUpdateContext& Context) override;
	virtual void Evaluate_AnyThread(FPoseContext& Output) override;
	// End of FAnimNode_Base interface

	FAnimNode_SteamVRSetWristTransform();

private:
	/** The root bone index of the SteamVR & UE4 Skeletons */
	FCompactPoseBoneIndex RootBoneIndex = FCompactPoseBoneIndex(0);

	/** The wrist bone index of the SteamVR Skeleton */
	FCompactPoseBoneIndex SteamVRWristBoneIndex = FCompactPoseBoneIndex(1);

};
