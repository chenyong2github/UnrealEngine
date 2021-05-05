// Copyright Epic Games, Inc. All Rights Reserved.


#include "AnimGraph/AnimGraphNode_SlopeWarping.h"
#include "Animation/AnimRootMotionProvider.h"

#define LOCTEXT_NAMESPACE "MomentumNodes"

UAnimGraphNode_SlopeWarping::UAnimGraphNode_SlopeWarping(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
}

FText UAnimGraphNode_SlopeWarping::GetControllerDescription() const
{
	return LOCTEXT("SlopeWarping", "Slope Warping");
}

FText UAnimGraphNode_SlopeWarping::GetTooltipText() const
{
	return LOCTEXT("SlopeWarpingTooltip", "Adjust Feet IK to Match Floor Normal");
}

FText UAnimGraphNode_SlopeWarping::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return GetControllerDescription();
}

void UAnimGraphNode_SlopeWarping::GetInputLinkAttributes(FNodeAttributeArray& OutAttributes) const
{
	if (Node.Mode == EWarpingEvaluationMode::Graph)
	{
		OutAttributes.Add(UE::Anim::IAnimRootMotionProvider::RootMotionDeltaAttributeName);
	}
}

void UAnimGraphNode_SlopeWarping::GetOutputLinkAttributes(FNodeAttributeArray& OutAttributes) const
{
	if (Node.Mode == EWarpingEvaluationMode::Graph)
	{
		OutAttributes.Add(UE::Anim::IAnimRootMotionProvider::RootMotionDeltaAttributeName);
	}
}

#undef LOCTEXT_NAMESPACE
