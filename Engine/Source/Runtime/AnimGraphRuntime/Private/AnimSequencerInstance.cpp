// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	UAnimSequencerInstance.cpp: Single Node Tree Instance 
	Only plays one animation at a time. 
=============================================================================*/ 

#include "AnimSequencerInstance.h"
#include "AnimSequencerInstanceProxy.h"

/////////////////////////////////////////////////////
// UAnimSequencerInstance
/////////////////////////////////////////////////////

const FName UAnimSequencerInstance::SequencerPoseName(TEXT("Sequencer_Pose_Name"));

UAnimSequencerInstance::UAnimSequencerInstance(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bUseMultiThreadedAnimationUpdate = false;
}

FAnimInstanceProxy* UAnimSequencerInstance::CreateAnimInstanceProxy()
{
	return new FAnimSequencerInstanceProxy(this);
}

void UAnimSequencerInstance::UpdateAnimTrack(UAnimSequenceBase* InAnimSequence, int32 SequenceId, float InPosition, float Weight, bool bFireNotifies)
{
	GetProxyOnGameThread<FAnimSequencerInstanceProxy>().UpdateAnimTrack(InAnimSequence, SequenceId, InPosition, Weight, bFireNotifies);
}

void UAnimSequencerInstance::ResetNodes()
{
	GetProxyOnGameThread<FAnimSequencerInstanceProxy>().ResetNodes();
}

void UAnimSequencerInstance::ResetPose()
{
	GetProxyOnGameThread<FAnimSequencerInstanceProxy>().ResetPose();
}

void UAnimSequencerInstance::NativeInitializeAnimation()
{
	SavePose();
}

void UAnimSequencerInstance::SavePose()
{
	SavePoseSnapshot(UAnimSequencerInstance::SequencerPoseName);
}
