// Copyright Epic Games, Inc. All Rights Reserved.

#include "Animation/AnimNodeContext.h"
#include "Animation/AnimNodeBase.h"

FAnimNodeContext::FData::FData(const FAnimationInitializeContext& InContext, FAnimNode_Base& InAnimNode, UScriptStruct* InAnimNodeStruct)
{
	AnimNode = &InAnimNode;
    AnimNodeStruct = InAnimNodeStruct;
    Context = const_cast<FAnimationInitializeContext*>(&InContext);
	ContextType = EContextType::Initialize;
}

FAnimNodeContext::FData::FData(const FAnimationUpdateContext& InContext, FAnimNode_Base& InAnimNode, UScriptStruct* InAnimNodeStruct)
{
	AnimNode = &InAnimNode;
	AnimNodeStruct = InAnimNodeStruct;
	Context = const_cast<FAnimationUpdateContext*>(&InContext);
	ContextType = EContextType::Update;
}

FAnimNodeContext::FData::FData(FPoseContext& InContext, FAnimNode_Base& InAnimNode, UScriptStruct* InAnimNodeStruct)
{
	AnimNode = &InAnimNode;
	AnimNodeStruct = InAnimNodeStruct;
	Context = &InContext;
	ContextType = EContextType::Pose;
}

FAnimNodeContext::FData::FData(FComponentSpacePoseContext& InContext, FAnimNode_Base& InAnimNode, UScriptStruct* InAnimNodeStruct)
{
	AnimNode = &InAnimNode;
	AnimNodeStruct = InAnimNodeStruct;
	Context = &InContext;
	ContextType = EContextType::ComponentSpacePose;
}

FAnimationInitializeContext* FAnimNodeContext::GetInitializeContext() const
{
	if(TSharedPtr<FData> PinnedData = Data.Pin())
	{
		if(PinnedData->ContextType == FData::EContextType::Initialize)
		{
			return static_cast<FAnimationInitializeContext*>(PinnedData->Context);
		}
	}
	return nullptr;
}

FAnimationUpdateContext* FAnimNodeContext::GetUpdateContext() const
{
	if(TSharedPtr<FData> PinnedData = Data.Pin())
	{
		if(PinnedData->ContextType == FData::EContextType::Update)
		{
			return static_cast<FAnimationUpdateContext*>(PinnedData->Context);
		}
	}
	return nullptr;
}

FPoseContext* FAnimNodeContext::GetPoseContext() const
{
	if(TSharedPtr<FData> PinnedData = Data.Pin())
	{
		if(PinnedData->ContextType == FData::EContextType::Pose)
		{
			return static_cast<FPoseContext*>(PinnedData->Context);
		}
	}
	return nullptr;
}

FComponentSpacePoseContext* FAnimNodeContext::GetComponentSpacePoseContext() const
{
	if(TSharedPtr<FData> PinnedData = Data.Pin())
	{
		if(PinnedData->ContextType == FData::EContextType::ComponentSpacePose)
		{
			return static_cast<FComponentSpacePoseContext*>(PinnedData->Context);
		}
	}
	return nullptr;
}