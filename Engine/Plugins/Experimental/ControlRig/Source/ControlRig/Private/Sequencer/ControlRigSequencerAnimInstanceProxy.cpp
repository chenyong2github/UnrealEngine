// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "ControlRigSequencerAnimInstanceProxy.h"
#include "Sequencer/ControlRigSequencerAnimInstance.h"

void FControlRigSequencerAnimInstanceProxy::Initialize(UAnimInstance* InAnimInstance)
{
	FAnimSequencerInstanceProxy::Initialize(InAnimInstance);

	// insert our extension nodes just after the root
	FAnimNode_Base* OldBaseLinkedNode = SequencerRootNode.Base.GetLinkNode();
	FAnimNode_Base* OldAdditiveLinkedNode = SequencerRootNode.Additive.GetLinkNode();

	SequencerRootNode.Base.SetLinkNode(&LayeredBoneBlendNode);
	SequencerRootNode.Additive.SetLinkNode(&AdditiveLayeredBoneBlendNode);

	LayeredBoneBlendNode.BasePose.SetLinkNode(OldBaseLinkedNode);
	AdditiveLayeredBoneBlendNode.BasePose.SetLinkNode(OldAdditiveLinkedNode);

	BoolBlendNode.BlendTime.Reset();
	BoolBlendNode.BlendPose.Reset();
	BoolBlendNode.BlendTime.Add(0.1f);
	BoolBlendNode.BlendTime.Add(0.1f);
	new (BoolBlendNode.BlendPose) FPoseLink();
	new (BoolBlendNode.BlendPose) FPoseLink();

	BoolBlendNode.bActiveValue = true; // active disables preview
	BoolBlendNode.BlendPose[1].SetLinkNode(&PreviewPlayerNode);

	FAnimationInitializeContext Context(this);
	LayeredBoneBlendNode.Initialize_AnyThread(Context);
	AdditiveLayeredBoneBlendNode.Initialize_AnyThread(Context);
	BoolBlendNode.Initialize_AnyThread(Context);
	PreviewPlayerNode.Initialize_AnyThread(Context);
}

void FControlRigSequencerAnimInstanceProxy::Update(float DeltaSeconds)
{
	if (bLayeredBlendChanged)
	{
		LayeredBoneBlendNode.ReinitializeBoneBlendWeights(GetRequiredBones(), GetSkeleton());
		bLayeredBlendChanged = false;
	}
	if (bAdditiveLayeredBlendChanged)
	{
		AdditiveLayeredBoneBlendNode.ReinitializeBoneBlendWeights(GetRequiredBones(), GetSkeleton());
		bAdditiveLayeredBlendChanged = false;
	}

	FAnimSequencerInstanceProxy::Update(DeltaSeconds);
}

FAnimNode_Base* FControlRigSequencerAnimInstanceProxy::GetCustomRootNode()
{
	return &SequencerRootNode;
}

void FControlRigSequencerAnimInstanceProxy::ResetNodes()
{
	FAnimSequencerInstanceProxy::ResetNodes();

	FMemory::Memzero(LayeredBoneBlendNode.BlendWeights.GetData(), LayeredBoneBlendNode.BlendWeights.GetAllocatedSize());
	FMemory::Memzero(AdditiveLayeredBoneBlendNode.BlendWeights.GetData(), AdditiveLayeredBoneBlendNode.BlendWeights.GetAllocatedSize());
}

void FControlRigSequencerAnimInstanceProxy::InitControlRigTrack(UControlRig* InControlRig, bool bAdditive, bool bApplyBoneFilter, const FInputBlendPose& BoneFilter, uint32 SequenceId)
{
	if (InControlRig != nullptr)
	{
		FSequencerPlayerControlRig* PlayerState = FindValidPlayerState(InControlRig, bAdditive, bApplyBoneFilter, BoneFilter, SequenceId);
		if (PlayerState == nullptr)
		{
			if (bApplyBoneFilter)
			{
				// We are filtering by bone
				FAnimNode_LayeredBoneBlend& LayeredBlendNode = bAdditive ? AdditiveLayeredBoneBlendNode : LayeredBoneBlendNode;

				const int32 PoseIndex = LayeredBlendNode.BlendPoses.Num();
				LayeredBlendNode.AddPose();

				// add the new entry to map
				FSequencerPlayerControlRig* NewPlayerState = new FSequencerPlayerControlRig();
				NewPlayerState->PoseIndex = PoseIndex;
				NewPlayerState->ControlRigNode.Source.SetLinkNode(&BoolBlendNode);

				SequencerToPlayerMap.Add(SequenceId, NewPlayerState);


				// link ControlRig node
				LayeredBlendNode.BlendPoses[PoseIndex].SetLinkNode(&NewPlayerState->ControlRigNode);
				LayeredBlendNode.LayerSetup[PoseIndex] = BoneFilter;
				LayeredBlendNode.BlendWeights[PoseIndex] = 0.0f;

				// Reinit layered blend to rebuild per-bone blend weights next eval
				if (bAdditive)
				{
					bAdditiveLayeredBlendChanged = true;
				}
				else
				{
					bLayeredBlendChanged = true;
				}

				// set player state
				PlayerState = NewPlayerState;
			}
			else
			{
				// Full-body animation
				FAnimNode_MultiWayBlend& BlendNode = bAdditive ? AdditiveBlendNode : FullBodyBlendNode;

				const int32 PoseIndex = BlendNode.AddPose() - 1;

				// add the new entry to map
				FSequencerPlayerControlRig* NewPlayerState = new FSequencerPlayerControlRig();
				NewPlayerState->PoseIndex = PoseIndex;
				NewPlayerState->ControlRigNode.Source.SetLinkNode(&BoolBlendNode);

				SequencerToPlayerMap.Add(SequenceId, NewPlayerState);

				// link ControlRig node
				BlendNode.Poses[PoseIndex].SetLinkNode(&NewPlayerState->ControlRigNode);

				// set player state
				PlayerState = NewPlayerState;
			}
		}

		// now set animation data to player
		PlayerState->ControlRigNode.SetControlRig(InControlRig);
		PlayerState->bApplyBoneFilter = bApplyBoneFilter;
		PlayerState->bAdditive = bAdditive;

		// initialize player
		PlayerState->ControlRigNode.OnInitializeAnimInstance(this, CastChecked<UAnimInstance>(GetAnimInstanceObject()));
		PlayerState->ControlRigNode.Initialize_AnyThread(FAnimationInitializeContext(this));
	}
}

bool FControlRigSequencerAnimInstanceProxy::UpdateControlRig(UControlRig* InControlRig, uint32 SequenceId, bool bAdditive, bool bApplyBoneFilter, const FInputBlendPose& BoneFilter, float Weight, bool bUpdateInput, bool bExecute)
{
	bool bCreated = EnsureControlRigTrack(InControlRig, bAdditive, bApplyBoneFilter, BoneFilter, SequenceId);

	FSequencerPlayerControlRig* PlayerState = FindPlayer<FSequencerPlayerControlRig>(SequenceId);
	if (bApplyBoneFilter)
	{
		FAnimNode_LayeredBoneBlend& LayeredBlendNode = bAdditive ? AdditiveLayeredBoneBlendNode : LayeredBoneBlendNode;
		LayeredBlendNode.BlendWeights[PlayerState->PoseIndex] = Weight;
	}
	else
	{
		FAnimNode_MultiWayBlend& BlendNode = bAdditive ? AdditiveBlendNode : FullBodyBlendNode;
		BlendNode.DesiredAlphas[PlayerState->PoseIndex] = Weight;
	}

	PlayerState->ControlRigNode.bUpdateInput = bUpdateInput;
	PlayerState->ControlRigNode.bExecute = bExecute;

	return bCreated;
}

bool FControlRigSequencerAnimInstanceProxy::EnsureControlRigTrack(UControlRig* InControlRig, bool bAdditive, bool bApplyBoneFilter, const FInputBlendPose& BoneFilter, uint32 SequenceId)
{
	if (!FindValidPlayerState(InControlRig, bAdditive, bApplyBoneFilter, BoneFilter, SequenceId))
	{
		InitControlRigTrack(InControlRig, bAdditive, bApplyBoneFilter, BoneFilter, SequenceId);
		return true;
	}

	return false;
}

FSequencerPlayerControlRig* FControlRigSequencerAnimInstanceProxy::FindValidPlayerState(UControlRig* InControlRig, bool bAdditive, bool bApplyBoneFilter, const FInputBlendPose& BoneFilter, uint32 SequenceId)
{
	FSequencerPlayerControlRig* PlayerState = FindPlayer<FSequencerPlayerControlRig>(SequenceId);
	if (PlayerState == nullptr)
	{
		return nullptr;
	}
	else if (InControlRig != PlayerState->ControlRigNode.GetControlRig() || bAdditive != PlayerState->bAdditive || bApplyBoneFilter != PlayerState->bApplyBoneFilter)
	{
		// If our criteria are different, force our weight to zero as we will (probably) occupy a new slot this time
		if (PlayerState->bApplyBoneFilter)
		{
			FAnimNode_LayeredBoneBlend& LayeredBlendNode = PlayerState->bAdditive ? AdditiveLayeredBoneBlendNode : LayeredBoneBlendNode;
			LayeredBlendNode.BlendWeights[PlayerState->PoseIndex] = 0.0f;
		}
		else
		{
			FAnimNode_MultiWayBlend& BlendNode = PlayerState->bAdditive ? AdditiveBlendNode : FullBodyBlendNode;
			BlendNode.DesiredAlphas[PlayerState->PoseIndex] = 0.0f;
		}

		return nullptr;
	}
	return PlayerState;
}

bool FControlRigSequencerAnimInstanceProxy::SetAnimationAsset(class UAnimationAsset* NewAsset)
{
	UAnimSequenceBase* Sequence = Cast<UAnimSequenceBase>(NewAsset);
	BoolBlendNode.bActiveValue = Sequence == nullptr;
	PreviewPlayerNode.Sequence = Sequence;
	PreviewPlayerNode.PlayRate = 1.0f;
	return true;
}