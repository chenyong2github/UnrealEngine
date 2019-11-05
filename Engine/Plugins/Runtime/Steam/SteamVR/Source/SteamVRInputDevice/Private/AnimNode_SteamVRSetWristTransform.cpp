// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "AnimNode_SteamVRSetWristTransform.h"
#include "ISteamVRInputDeviceModule.h"
#include "AnimationRuntime.h"
#include "Runtime/Engine/Public/Animation/AnimInstanceProxy.h"
#include "SteamVRInputDevice.h"
#include "UE4HandSkeletonDefinition.h"

FAnimNode_SteamVRSetWristTransform::FAnimNode_SteamVRSetWristTransform()
{
}

void FAnimNode_SteamVRSetWristTransform::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{
	// Initialize our poses
	ReferencePose.Initialize(Context);
	TargetPose.Initialize(Context);

}

void FAnimNode_SteamVRSetWristTransform::CacheBones_AnyThread(const FAnimationCacheBonesContext& Context)
{

}

void FAnimNode_SteamVRSetWristTransform::Update_AnyThread(const FAnimationUpdateContext& Context)
{
	// Update our poses this frame
	ReferencePose.Update(Context);
	TargetPose.Update(Context);
}

void FAnimNode_SteamVRSetWristTransform::Evaluate_AnyThread(FPoseContext& Output)
{
	Output.ResetToRefPose();

	// Apply all the bones from the target to the output pose
	TargetPose.Evaluate(Output);

	// Setup buffer for the reference pose
	FPoseContext ReferenceContext = FPoseContext(Output.AnimInstanceProxy);
	ReferencePose.Evaluate(ReferenceContext);

	// Apply the appropriate root and/or wrist transforms to our output pose based on the skeleton being used
	if (HandSkeleton == EHandSkeleton::VR_SteamVRHandSkeleton)
	{
		// Since we are dealing with the SteamVR Hand Skeleton, we need to set both the root and wrist Bones
		Output.Pose[RootBoneIndex] = ReferenceContext.Pose[RootBoneIndex];
		Output.Pose[SteamVRWristBoneIndex] = ReferenceContext.Pose[SteamVRWristBoneIndex];
	}
	else
	{
		// For the UE4 stock hand skeleton, we only need the root bone (which is the wrist bone in this skeleton)
		Output.Pose[RootBoneIndex] = ReferenceContext.Pose[RootBoneIndex];
	}
}
