// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimGraphNode_RemapCurvesFromMesh.h"

#define LOCTEXT_NAMESPACE "AnimGraphNode_RemapCurvesFromMesh"


FText UAnimGraphNode_RemapCurvesFromMesh::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return LOCTEXT("NodeTitle", "Remap Curves From Mesh");
}

FText UAnimGraphNode_RemapCurvesFromMesh::GetTooltipText() const
{
	return LOCTEXT("Tooltip", "The Remap Curves From Mesh node copies curves from another component to this. Can be used to map any curve to any and perform mathematical operations on them.");
}

void UAnimGraphNode_RemapCurvesFromMesh::GetOutputLinkAttributes(FNodeAttributeArray& OutAttributes) const
{
	OutAttributes.Add(UE::Anim::FAttributes::Curves);
}

#undef LOCTEXT_NAMESPACE
