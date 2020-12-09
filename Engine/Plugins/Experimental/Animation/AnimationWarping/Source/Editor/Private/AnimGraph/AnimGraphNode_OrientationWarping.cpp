// Copyright Epic Games, Inc. All Rights Reserved.


#include "AnimGraph/AnimGraphNode_OrientationWarping.h"

/////////////////////////////////////////////////////
// UAnimGraphNode_OrientationWarping

#define LOCTEXT_NAMESPACE "MomentumNodes"

UAnimGraphNode_OrientationWarping::UAnimGraphNode_OrientationWarping(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

FText UAnimGraphNode_OrientationWarping::GetControllerDescription() const
{
	return LOCTEXT("OrientationWarping", "Orientation Warping");
}

FText UAnimGraphNode_OrientationWarping::GetTooltipText() const
{
	return LOCTEXT("OrientationWarpingTooltip", "Orients RootBone to match locomotion direction, and counter rotates spine.");
}

FText UAnimGraphNode_OrientationWarping::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return GetControllerDescription();
}

#undef LOCTEXT_NAMESPACE
