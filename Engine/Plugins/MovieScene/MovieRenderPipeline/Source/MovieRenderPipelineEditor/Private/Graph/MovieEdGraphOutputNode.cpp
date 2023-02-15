// Copyright Epic Games, Inc. All Rights Reserved.

#include "Graph/MovieEdGraphOutputNode.h"

#define LOCTEXT_NAMESPACE "MoviePipelineGraph"

FText UMoviePipelineEdGraphNodeOutput::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return FText::FromName(TEXT("Output"));
}

void UMoviePipelineEdGraphNodeOutput::AllocateDefaultPins()
{
	if (RuntimeNode)
	{
		CreatePins(RuntimeNode->GetInputPins(), /*InOutputPins=*/{});
	}
}
#undef LOCTEXT_NAMESPACE