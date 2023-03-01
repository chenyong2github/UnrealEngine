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
		FEdGraphPinType EdPinType;
		EdPinType.ResetToDefaults();
		
		EdPinType.PinCategory = FName("Outputs");
		EdPinType.PinSubCategory = FName();
		
		CreatePin(EGPD_Output, EdPinType, FName(VariableNode->GetVariable()->Name));
	}
}

#undef LOCTEXT_NAMESPACE