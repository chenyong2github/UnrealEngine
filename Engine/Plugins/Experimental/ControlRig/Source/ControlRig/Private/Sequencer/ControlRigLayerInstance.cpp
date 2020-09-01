// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	UControlRigLayerInstance.cpp: Layer AnimInstance that support single Source Anim Instance and multiple control rigs
	The source AnimInstance can be any AnimBlueprint 
=============================================================================*/ 

#include "Sequencer/ControlRigLayerInstance.h"
#include "ControlRigLayerInstanceProxy.h"

/////////////////////////////////////////////////////
// UControlRigLayerInstance
/////////////////////////////////////////////////////

const FName UControlRigLayerInstance::SequencerPoseName(TEXT("Sequencer_Pose_Name"));

UControlRigLayerInstance::UControlRigLayerInstance(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bUseMultiThreadedAnimationUpdate = false;
}

FAnimInstanceProxy* UControlRigLayerInstance::CreateAnimInstanceProxy()
{
	return new FControlRigLayerInstanceProxy(this);
}

void UControlRigLayerInstance::UpdateAnimTrack(UAnimSequenceBase* InAnimSequence, int32 SequenceId, float InPosition, float Weight, bool bFireNotifies)
{
	GetProxyOnGameThread<FControlRigLayerInstanceProxy>().UpdateAnimTrack(InAnimSequence, SequenceId, InPosition, Weight, bFireNotifies);
}

void UControlRigLayerInstance::UpdateAnimTrack(UAnimSequenceBase* InAnimSequence, int32 SequenceId, float InFromPosition, float InToPosition, float Weight, bool bFireNotifies)
{
	GetProxyOnGameThread<FControlRigLayerInstanceProxy>().UpdateAnimTrack(InAnimSequence, SequenceId, InFromPosition, InToPosition, Weight, bFireNotifies);
}

void UControlRigLayerInstance::ConstructNodes()
{
	GetProxyOnGameThread<FControlRigLayerInstanceProxy>().ConstructNodes();
}

void UControlRigLayerInstance::ResetNodes()
{
	GetProxyOnGameThread<FControlRigLayerInstanceProxy>().ResetNodes();
}

void UControlRigLayerInstance::ResetPose()
{
	GetProxyOnGameThread<FControlRigLayerInstanceProxy>().ResetPose();
}

UControlRig* UControlRigLayerInstance::GetFirstAvailableControlRig() const
{
	return GetProxyOnGameThread<FControlRigLayerInstanceProxy>().GetFirstAvailableControlRig();
}

/** Anim Instance Source info - created externally and used here */
void UControlRigLayerInstance::SetSourceAnimInstance(UAnimInstance* SourceAnimInstance)
{
	USkeletalMeshComponent* MeshComponent = GetOwningComponent();
	ensure (MeshComponent->GetAnimInstance() != SourceAnimInstance);

	if (SourceAnimInstance)
	{
		// we only have one instance fir this layer
		FLinkedInstancesAdapter::ResetLinkedInstance(MeshComponent);
		FLinkedInstancesAdapter::AddLinkedInstance(MeshComponent, SourceAnimInstance);
	}
	else
	{
		FLinkedInstancesAdapter::ResetLinkedInstance(MeshComponent);
	}

	if (SourceAnimInstance)
	{
		GetProxyOnGameThread<FControlRigLayerInstanceProxy>().SetSourceAnimInstance(SourceAnimInstance, UAnimInstance::GetProxyOnGameThreadStatic<FAnimInstanceProxy>(SourceAnimInstance));
	}
	else
	{
		GetProxyOnGameThread<FControlRigLayerInstanceProxy>().SetSourceAnimInstance(nullptr, nullptr);
	}
}

/** ControlRig related support */
void UControlRigLayerInstance::AddControlRigTrack(int32 ControlRigID, UControlRig* InControlRig)
{
	GetProxyOnGameThread<FControlRigLayerInstanceProxy>().AddControlRigTrack(ControlRigID, InControlRig);
}

bool UControlRigLayerInstance::HasControlRigTrack(int32 ControlRigID)
{
	return GetProxyOnGameThread<FControlRigLayerInstanceProxy>().HasControlRigTrack(ControlRigID);
}

void UControlRigLayerInstance::UpdateControlRigTrack(int32 ControlRigID, float Weight, const FControlRigIOSettings& InputSettings, bool bExecute)
{
	GetProxyOnGameThread<FControlRigLayerInstanceProxy>().UpdateControlRigTrack(ControlRigID, Weight, InputSettings, bExecute);
}

void UControlRigLayerInstance::RemoveControlRigTrack(int32 ControlRigID)
{
	GetProxyOnGameThread<FControlRigLayerInstanceProxy>().RemoveControlRigTrack(ControlRigID);
}

void UControlRigLayerInstance::ResetControlRigTracks()
{
	GetProxyOnGameThread<FControlRigLayerInstanceProxy>().ResetControlRigTracks();
}

/** Sequencer AnimInstance Interface */
void UControlRigLayerInstance::AddAnimation(int32 SequenceId, UAnimSequenceBase* InAnimSequence)
{
	GetProxyOnGameThread<FControlRigLayerInstanceProxy>().AddAnimation(SequenceId, InAnimSequence);
}

void UControlRigLayerInstance::RemoveAnimation(int32 SequenceId)
{
	GetProxyOnGameThread<FControlRigLayerInstanceProxy>().RemoveAnimation(SequenceId);
}

void UControlRigLayerInstance::SavePose()
{
	if (USkeletalMeshComponent* SkeletalMeshComponent = GetSkelMeshComponent())
	{
		if (SkeletalMeshComponent->SkeletalMesh && SkeletalMeshComponent->GetComponentSpaceTransforms().Num() > 0)
		{
			SavePoseSnapshot(UControlRigLayerInstance::SequencerPoseName);
		}
	}
}

UAnimInstance* UControlRigLayerInstance::GetSourceAnimInstance()  
{	
	return GetProxyOnGameThread<FControlRigLayerInstanceProxy>().GetSourceAnimInstance();
}

