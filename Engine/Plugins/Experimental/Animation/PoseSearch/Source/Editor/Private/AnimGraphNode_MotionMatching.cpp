// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimGraphNode_MotionMatching.h"

#define LOCTEXT_NAMESPACE "AnimGraphNode_MotionMatching"


FLinearColor UAnimGraphNode_MotionMatching::GetNodeTitleColor() const
{
	return FColor(86, 182, 194);
}

FText UAnimGraphNode_MotionMatching::GetTooltipText() const
{
	return LOCTEXT("NodeToolTip", "Motion Matching");
}

FText UAnimGraphNode_MotionMatching::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return LOCTEXT("NodeTitle", "Motion Matching");
}

FText UAnimGraphNode_MotionMatching::GetMenuCategory() const
{
	return LOCTEXT("NodeCategory", "Pose Search");
}


#undef LOCTEXT_NAMESPACE
