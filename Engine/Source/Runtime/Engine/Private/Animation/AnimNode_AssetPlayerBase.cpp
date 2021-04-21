// Copyright Epic Games, Inc. All Rights Reserved.

#include "Animation/AnimNode_AssetPlayerBase.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimSyncScope.h"

void FAnimNode_AssetPlayerBase::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{
	FAnimNode_Base::Initialize_AnyThread(Context);

	MarkerTickRecord.Reset();
	bHasBeenFullWeight = false;
}

void FAnimNode_AssetPlayerBase::Update_AnyThread(const FAnimationUpdateContext& Context)
{
	// Cache the current weight and update the node
	BlendWeight = Context.GetFinalBlendWeight();
	bHasBeenFullWeight = bHasBeenFullWeight || (BlendWeight >= (1.0f - ZERO_ANIMWEIGHT_THRESH));

	UpdateAssetPlayer(Context);
}

void FAnimNode_AssetPlayerBase::CreateTickRecordForNode(const FAnimationUpdateContext& Context, UAnimSequenceBase* Sequence, bool bLooping, float PlayRate)
{
	// Create a tick record and push into the closest scope
	const float FinalBlendWeight = Context.GetFinalBlendWeight();

	UE::Anim::FAnimSyncGroupScope& SyncScope = Context.GetMessageChecked<UE::Anim::FAnimSyncGroupScope>();

	const EAnimGroupRole::Type SyncGroupRole = GetGroupRole();
	const FName SyncGroupName = GetGroupName();

	FName GroupNameToUse = ((SyncGroupRole < EAnimGroupRole::TransitionLeader) || bHasBeenFullWeight) ? SyncGroupName : NAME_None;
	EAnimSyncMethod MethodToUse = GetGroupMethod();
	if(GroupNameToUse == NAME_None && MethodToUse == EAnimSyncMethod::SyncGroup)
	{
		MethodToUse = EAnimSyncMethod::DoNotSync;
	}

	UE::Anim::FAnimSyncParams SyncParams(GroupNameToUse, SyncGroupRole, MethodToUse);
	FAnimTickRecord TickRecord(Sequence, bLooping, PlayRate, FinalBlendWeight, /*inout*/ InternalTimeAccumulator, MarkerTickRecord);
	TickRecord.RootMotionWeightModifier = Context.GetRootMotionWeightModifier();

	SyncScope.AddTickRecord(TickRecord, SyncParams, UE::Anim::FAnimSyncDebugInfo(Context));

	TRACE_ANIM_TICK_RECORD(Context, TickRecord);
}

float FAnimNode_AssetPlayerBase::GetCachedBlendWeight() const
{
	return BlendWeight;
}

float FAnimNode_AssetPlayerBase::GetAccumulatedTime() const
{
	return InternalTimeAccumulator;
}

void FAnimNode_AssetPlayerBase::SetAccumulatedTime(const float& NewTime)
{
	InternalTimeAccumulator = NewTime;
}

UAnimationAsset* FAnimNode_AssetPlayerBase::GetAnimAsset() const
{
	return nullptr;
}

void FAnimNode_AssetPlayerBase::ClearCachedBlendWeight()
{
	BlendWeight = 0.0f;
}

FName FAnimNode_AssetPlayerBase::GetGroupName() const
{
	return GET_ANIM_NODE_DATA(FName, GroupName);
}

EAnimGroupRole::Type FAnimNode_AssetPlayerBase::GetGroupRole() const
{
	return GET_ANIM_NODE_DATA(EAnimGroupRole::Type, GroupRole);
}

EAnimSyncMethod FAnimNode_AssetPlayerBase::GetGroupMethod() const
{
	return GET_ANIM_NODE_DATA(EAnimSyncMethod, Method);
}

bool FAnimNode_AssetPlayerBase::GetIgnoreForRelevancyTest() const
{
	return GET_ANIM_NODE_DATA(bool, bIgnoreForRelevancyTest);
}