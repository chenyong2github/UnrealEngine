// Copyright Epic Games, Inc. All Rights Reserved.

#include "Graph/MovieEdGraphNode.h"
#include "Graph/MovieGraphPin.h"
#include "Graph/MovieGraphNode.h"
#include "EdGraph/EdGraphPin.h"

void UMoviePipelineEdGraphNodeBase::Construct(UMovieGraphNode* InRuntimeNode)
{
	check(InRuntimeNode);
	RuntimeNode = InRuntimeNode;

	// NodePosX = InRuntimeNode->PositionX;
	// NodePosY = InRuntimeNode->PositionY;
	// NodeComment = InRuntimeNode->NodeComment;
	// bCommentBubblePinned = InRuntimeNode->bCommentBubblePinned;
	// bCommentBubbleVisible = InRuntimeNode->bCommentBubbleVisible;
}

FEdGraphPinType UMoviePipelineEdGraphNodeBase::GetPinType(const UMovieGraphPin* InPin)
{
	FEdGraphPinType EdPinType;
	EdPinType.ResetToDefaults();
	
	EdPinType.PinCategory = FName("TestCategory");
	EdPinType.PinSubCategory = FName("TestSubCategory");
	return EdPinType;
}

void UMoviePipelineEdGraphNode::AllocateDefaultPins()
{
	if(RuntimeNode)
	{
		for(const UMovieGraphPin* InputPin : RuntimeNode->GetInputPins())
		{
			CreatePin(EEdGraphPinDirection::EGPD_Input, GetPinType(InputPin), InputPin->Properties.Label);
		}
		
		for(const UMovieGraphPin* OutputPin : RuntimeNode->GetOutputPins())
		{
			CreatePin(EEdGraphPinDirection::EGPD_Output, GetPinType(OutputPin), OutputPin->Properties.Label);
		}
	}
}

FText UMoviePipelineEdGraphNode::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	if (RuntimeNode)
	{
		return RuntimeNode->GetMenuDescription();
	}

	return NSLOCTEXT("MoviePipelineGraph", "GraphTestTitle", "TestTitle");
}

FText UMoviePipelineEdGraphNode::GetTooltipText() const
{
	return NSLOCTEXT("MoviePipelineGraph", "GraphTestTooltip", "Test Tooltip");
}

bool UMoviePipelineEdGraphNodeBase::ShouldCreatePin(const UMovieGraphPin* InPin) const
{
	return true;
}

void UMoviePipelineEdGraphNodeBase::CreatePins(const TArray<UMovieGraphPin*>& InInputPins, const TArray<UMovieGraphPin*>& InOutputPins)
{
	bool bHasAdvancedPin = false;

	for (const UMovieGraphPin* InputPin : InInputPins)
	{
		if (!ShouldCreatePin(InputPin))
		{
			continue;
		}

		UEdGraphPin* Pin = CreatePin(EEdGraphPinDirection::EGPD_Input, GetPinType(InputPin), InputPin->Properties.Label);
		// Pin->bAdvancedView = InputPin->Properties.bAdvancedPin;
		bHasAdvancedPin |= Pin->bAdvancedView;
	}

	for (const UMovieGraphPin* OutputPin : InOutputPins)
	{
		if (!ShouldCreatePin(OutputPin))
		{
			continue;
		}

		UEdGraphPin* Pin = CreatePin(EEdGraphPinDirection::EGPD_Output, GetPinType(OutputPin), OutputPin->Properties.Label);
		// Pin->bAdvancedView = OutputPin->Properties.bAdvancedPin;
		bHasAdvancedPin |= Pin->bAdvancedView;
	}

	if (bHasAdvancedPin && AdvancedPinDisplay == ENodeAdvancedPins::NoPins)
	{
		AdvancedPinDisplay = ENodeAdvancedPins::Hidden;
	}
	else if (!bHasAdvancedPin)
	{
		AdvancedPinDisplay = ENodeAdvancedPins::NoPins;
	}
}