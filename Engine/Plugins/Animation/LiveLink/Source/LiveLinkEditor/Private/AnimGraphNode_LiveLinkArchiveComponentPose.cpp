// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "AnimGraphNode_LiveLinkArchiveComponentPose.h"

#define LOCTEXT_NAMESPACE "LiveLinkArchiveComponentAnimNode"

UAnimGraphNode_LiveLinkArchiveComponentPose::UAnimGraphNode_LiveLinkArchiveComponentPose(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

FText UAnimGraphNode_LiveLinkArchiveComponentPose::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return LOCTEXT("NodeTitle", "Live Link Archive Component Pose");
}

FText UAnimGraphNode_LiveLinkArchiveComponentPose::GetTooltipText() const
{
	return LOCTEXT("NodeTooltip", "Retrieves a pose from a Live Link Archive Component. Looks through owning Actor's components to find a LiveLinkArchive Component with the given name and uses it as a source for pose data.");
}

FText UAnimGraphNode_LiveLinkArchiveComponentPose::GetMenuCategory() const
{
	return LOCTEXT("NodeCategory", "Live Link");
}

#undef LOCTEXT_NAMESPACE