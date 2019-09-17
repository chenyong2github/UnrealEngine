// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AnimSequencerInstanceProxy.h"
#include "AnimNode_ControlRig_ExternalSource.h"
#include "AnimNodes/AnimNode_LayeredBoneBlend.h"
#include "AnimNodes/AnimNode_BlendListByBool.h"
#include "Animation/AnimNode_SequencePlayer.h"
#include "ControlRigSequencerAnimInstanceProxy.generated.h"

struct FInputBlendPose;

struct FSequencerPlayerControlRig : public FSequencerPlayerBase
{
	SEQUENCER_INSTANCE_PLAYER_TYPE(FSequencerPlayerControlRig, FSequencerPlayerBase)

	FAnimNode_ControlRig_ExternalSource ControlRigNode;

	bool bApplyBoneFilter;
};

/** Proxy that manages adding animation ControlRig nodes as well as acting as a regular sequencer proxy */
USTRUCT()
struct CONTROLRIG_API FControlRigSequencerAnimInstanceProxy : public FAnimSequencerInstanceProxy
{
	GENERATED_BODY()

public:
	FControlRigSequencerAnimInstanceProxy()
		: bLayeredBlendChanged(false)
		, bAdditiveLayeredBlendChanged(false)
	{
	}

	FControlRigSequencerAnimInstanceProxy(UAnimInstance* InAnimInstance)
		: FAnimSequencerInstanceProxy(InAnimInstance)
		, bLayeredBlendChanged(false)
		, bAdditiveLayeredBlendChanged(false)
	{
	}

	// FAnimInstanceProxy interface
	virtual void Initialize(UAnimInstance* InAnimInstance) override;
	virtual void Update(float DeltaSeconds) override;
	virtual FAnimNode_Base* GetCustomRootNode() override;

	// FAnimSequencerInstanceProxy interface
	virtual void ResetNodes() override;

	bool UpdateControlRig(UControlRig* InControlRig, uint32 SequenceId, bool bAdditive, bool bApplyBoneFilter, const FInputBlendPose& BoneFilter, float Weight, bool bUpdateInput, bool bExecute);
	bool SetAnimationAsset(class UAnimationAsset* NewAsset);

private:
	void InitControlRigTrack(UControlRig* InControlRig, bool bAdditive, bool bApplyBoneFilter, const FInputBlendPose& BoneFilter, uint32 SequenceId);
	bool EnsureControlRigTrack(UControlRig* InControlRig, bool bAdditive, bool bApplyBoneFilter, const FInputBlendPose& BoneFilter, uint32 SequenceId);
	FSequencerPlayerControlRig* FindValidPlayerState(UControlRig* InControlRig, bool bAdditive, bool bApplyBoneFilter, const FInputBlendPose& BoneFilter, uint32 SequenceId);

	FAnimNode_LayeredBoneBlend LayeredBoneBlendNode;
	FAnimNode_LayeredBoneBlend AdditiveLayeredBoneBlendNode;
	FAnimNode_BlendListByBool BoolBlendNode;
	FAnimNode_SequencePlayer PreviewPlayerNode;

	bool bLayeredBlendChanged;
	bool bAdditiveLayeredBlendChanged;
};