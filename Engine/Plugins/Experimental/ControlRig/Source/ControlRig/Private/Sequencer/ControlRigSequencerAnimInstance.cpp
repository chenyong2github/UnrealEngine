// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Sequencer/ControlRigSequencerAnimInstance.h"
#include "ControlRigSequencerAnimInstanceProxy.h"
#include "ControlRig.h"

UControlRigSequencerAnimInstance::UControlRigSequencerAnimInstance(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bUseMultiThreadedAnimationUpdate = false;
}

FAnimInstanceProxy* UControlRigSequencerAnimInstance::CreateAnimInstanceProxy()
{
	return new FControlRigSequencerAnimInstanceProxy(this);
}

bool UControlRigSequencerAnimInstance::UpdateControlRig(UControlRig* InControlRig, uint32 SequenceId, bool bAdditive, bool bApplyBoneFilter, const FInputBlendPose& BoneFilter, float Weight)
{
	CachedControlRig = InControlRig;
	return GetProxyOnGameThread<FControlRigSequencerAnimInstanceProxy>().UpdateControlRig(InControlRig, SequenceId, bAdditive, bApplyBoneFilter, BoneFilter, Weight);
}

void UControlRigSequencerAnimInstance::NativeInitializeAnimation()
{
	//Do nothing since UAnimSequencerInstance save's a pose snapshot which can cause ensure and crash issues 
	//since it may not have a skeletal mesh or component transform set up yet.
}
