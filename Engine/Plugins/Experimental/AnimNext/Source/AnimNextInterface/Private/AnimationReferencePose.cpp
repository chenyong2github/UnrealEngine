// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimationReferencePose.h"
#include "AnimNextInterfaceParam.h"

namespace UE::AnimNext::Interface
{

IMPLEMENT_ANIM_NEXT_INTERFACE_PARAM_TYPE(FAnimationReferencePose, AnimationReferencePose);
//IMPLEMENT_ANIM_NEXT_INTERFACE_PARAM_TYPE(FAnimationLODPose, AnimationLODPose);
//IMPLEMENT_ANIM_NEXT_INTERFACE_PARAM_TYPE(FAnimationLODPoseStack, AnimationLODPoseStack);

const FAnimTransform TransformAdditiveIdentity = FAnimTransform(FQuat::Identity, FVector::ZeroVector, FVector::ZeroVector);

} // namespace UE::AnimNext::Interface
