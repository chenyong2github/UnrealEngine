// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "AnimNode_ControlRig_ExternalSource.h"
#include "ControlRig.h"

FAnimNode_ControlRig_ExternalSource::FAnimNode_ControlRig_ExternalSource()
{
}

void FAnimNode_ControlRig_ExternalSource::SetControlRig(UControlRig* InControlRig)
{
	ControlRig = InControlRig;
	// requires initializing animation system
}

UControlRig* FAnimNode_ControlRig_ExternalSource::GetControlRig() const
{
	return (ControlRig.IsValid()? ControlRig.Get() : nullptr);
}

void FAnimNode_ControlRig_ExternalSource::GatherDebugData(FNodeDebugData& DebugData)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	Source.GatherDebugData(DebugData.BranchFlow(1.f));
}

void FAnimNode_ControlRig_ExternalSource::Update_AnyThread(const FAnimationUpdateContext& Context)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	FAnimNode_ControlRigBase::Update_AnyThread(Context);
	Source.Update(Context);
}

void FAnimNode_ControlRig_ExternalSource::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	FAnimNode_ControlRigBase::Initialize_AnyThread(Context);
	Source.Initialize(Context);
}

void FAnimNode_ControlRig_ExternalSource::CacheBones_AnyThread(const FAnimationCacheBonesContext& Context)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	FAnimNode_ControlRigBase::CacheBones_AnyThread(Context);
	Source.CacheBones(Context);
}

void FAnimNode_ControlRig_ExternalSource::Evaluate_AnyThread(FPoseContext& Output)
{
	Output.ResetToRefPose();

	if (Source.GetLinkNode())
	{
		Source.Evaluate(Output);
	}

	FAnimNode_ControlRigBase::Evaluate_AnyThread(Output);
}