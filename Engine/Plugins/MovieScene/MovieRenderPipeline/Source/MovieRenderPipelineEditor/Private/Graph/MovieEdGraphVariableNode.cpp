// Copyright Epic Games, Inc. All Rights Reserved.

#include "Graph/MovieEdGraphVariableNode.h"

#include "EdGraph/EdGraphPin.h"
#include "Graph/MovieGraphConfig.h"

#define LOCTEXT_NAMESPACE "MoviePipelineGraph"

FText UMoviePipelineEdGraphVariableNode::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return LOCTEXT("GetVariableNodeTitle", "Get Variable");
}

void UMoviePipelineEdGraphVariableNode::AllocateDefaultPins()
{
	if (const UMovieGraphVariableNode* VariableNode = Cast<UMovieGraphVariableNode>(RuntimeNode))
	{
		const TArray<TObjectPtr<UMovieGraphPin>>& OutputPins = RuntimeNode->GetOutputPins();
		if (!OutputPins.IsEmpty())
		{
			CreatePin(EGPD_Output, GetPinType(OutputPins[0].Get()), FName(VariableNode->GetVariable()->Name));
		}
	}
}

#undef LOCTEXT_NAMESPACE