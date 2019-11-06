// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "AnimGraphNode_SteamVRSetWristTransform.h"
#include "SteamVREditor.h"

#define LOCTEXT_NAMESPACE "SteamVRSetWristTransformAnimNode"

UAnimGraphNode_SteamVRSetWristTransform::UAnimGraphNode_SteamVRSetWristTransform(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

// Node Color
FLinearColor UAnimGraphNode_SteamVRSetWristTransform::GetNodeTitleColor() const 
{ 
	return FLinearColor(0.f, 0.f, 255.f, 1.f);
}

// Node Category
FText UAnimGraphNode_SteamVRSetWristTransform::GetMenuCategory() const
{
	return LOCTEXT("NodeCategory", "SteamVR Input");
}

// Node Title
FText UAnimGraphNode_SteamVRSetWristTransform::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return LOCTEXT("NodeTitle", "SteamVR Set Wrist Transform");
}

// Node Tooltip
FText UAnimGraphNode_SteamVRSetWristTransform::GetTooltipText() const
{
	return LOCTEXT("NodeTooltip", "Sets the wrist transform of target pose from the reference pose");
}

#undef LOCTEXT_NAMESPACE