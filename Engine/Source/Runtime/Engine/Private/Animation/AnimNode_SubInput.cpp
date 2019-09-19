// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Animation/AnimNode_SubInput.h"
#include "Animation/AnimInstanceProxy.h"

const FName FAnimNode_SubInput::DefaultInputPoseName("InPose");

void FAnimNode_SubInput::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{
	if(InputProxy)
	{
		FAnimationInitializeContext InputContext(InputProxy);
		InputPose.Initialize(InputContext);
	}
}

void FAnimNode_SubInput::CacheBones_AnyThread(const FAnimationCacheBonesContext& Context)
{
	if(InputProxy)
	{
		FAnimationCacheBonesContext InputContext(InputProxy);
		InputPose.CacheBones(InputContext);
	}
}

void FAnimNode_SubInput::Update_AnyThread(const FAnimationUpdateContext& Context)
{
	if(InputProxy)
	{
		FAnimationUpdateContext InputContext = Context.WithOtherProxy(InputProxy);
		InputPose.Update(InputContext);
	}
}

void FAnimNode_SubInput::Evaluate_AnyThread(FPoseContext& Output)
{
	if(InputProxy)
	{
		Output.Pose.SetBoneContainer(&InputProxy->GetRequiredBones());

		FPoseContext InputContext(InputProxy, Output.ExpectsAdditivePose());
		InputPose.Evaluate(InputContext);

		Output.Pose.MoveBonesFrom(InputContext.Pose);
		Output.Curve.MoveFrom(InputContext.Curve);
	}
	else if(CachedInputPose.IsValid() && CachedInputCurve.IsValid())
	{
		Output.Pose.CopyBonesFrom(CachedInputPose);
		Output.Curve.CopyFrom(CachedInputCurve);
	}
	else
	{
		Output.ResetToRefPose();
	}
}

void FAnimNode_SubInput::GatherDebugData(FNodeDebugData& DebugData)
{
	FString DebugLine = DebugData.GetNodeName(this);
	DebugData.AddDebugItem(DebugLine);

	if(InputProxy)
	{
		InputPose.GatherDebugData(DebugData);
	}
}

void FAnimNode_SubInput::DynamicLink(FAnimInstanceProxy* InInputProxy, FPoseLinkBase* InPoseLink)
{
	check(InputProxy == nullptr);	// Must be unlinked before re-linking

	InputProxy = InInputProxy;
	InputPose.SetDynamicLinkNode(InPoseLink);
}

void FAnimNode_SubInput::DynamicUnlink()
{
	check(InputProxy != nullptr);	// Must be linked before unlinking

	InputProxy = nullptr;
	InputPose.SetDynamicLinkNode(nullptr);
}