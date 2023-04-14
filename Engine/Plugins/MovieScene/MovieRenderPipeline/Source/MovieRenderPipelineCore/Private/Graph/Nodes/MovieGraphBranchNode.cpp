// Copyright Epic Games, Inc. All Rights Reserved.

#include "Graph/Nodes/MovieGraphBranchNode.h"

#include "Graph/MovieGraphConfig.h"
#include "Graph/MovieGraphEdge.h"
#include "MovieRenderPipelineCoreModule.h"

#include "Styling/AppStyle.h"

#define LOCTEXT_NAMESPACE "MovieGraphNode"

namespace UE::MovieGraph::BranchNode
{
	static const FName TrueBranch("True");
	static const FName FalseBranch("False");
	static const FName Condition("Condition");
}

TArray<UMovieGraphPin*> UMovieGraphBranchNode::EvaluatePinsToFollow(const FMovieGraphTraversalContext& InContext) const
{
	TArray<UMovieGraphPin*> PinsToFollow;

	// The branch node has two branches that could be followed, True or False. To figure out which one we're actually going
	// to follow, we need to evaluate the Conditional pin. 
	UMovieGraphPin* ConditionalPin = GetInputPin(UE::MovieGraph::BranchNode::Condition);
	if (!ensure(ConditionalPin))
	{
		return PinsToFollow;
	}

	UMovieGraphPin* OtherPin = nullptr;
	for (UMovieGraphEdge* Edge : ConditionalPin->Edges)
	{
		OtherPin = Edge->GetOtherPin(ConditionalPin);

		// We only support a single connection to this pin type anyways.
		break;
	}

	// There may not be a node actually connected. We don't know what to do in this case (as our nodes don't have default values)
	// so for now we choose to follow neither branch.
	if (!OtherPin)
	{
		UE_LOG(LogMovieRenderPipeline, Warning, TEXT("Unconnected conditional pin has no default value. Node: %s"), *GetName());
		return PinsToFollow;
	}

	UMovieGraphNode* ConnectedNode = OtherPin->Node;
	if (!ensure(ConnectedNode))
	{
		return PinsToFollow;
	}

	// Connected Node is the node that the pin is actually connected to.
	const UMovieGraphValueContainer* ValueContainer = ConnectedNode->GetPropertyValueContainerForPin(OtherPin->GetName());

	if (!ValueContainer)
	{
		// Somehow we got connected to node that can't provide a value.
		UE_LOG(LogMovieRenderPipeline, Warning, TEXT("ConnectedNode: %s provided no ValueContainer to fetch value."), *ConnectedNode->GetName());
		return PinsToFollow;
	}

	bool bEvaluatedValue;
	bool bSuccessfullyFetched = ValueContainer->GetValueBool(bEvaluatedValue);
	if (!bSuccessfullyFetched)
	{
		// There was a node connected but it didn't know how to return a boolean value for us.
		UE_LOG(LogMovieRenderPipeline, Warning, TEXT("ValueContainer had no bool value."));
		return PinsToFollow;
	}

	if (bEvaluatedValue)
	{
		PinsToFollow.Add(GetInputPin(UE::MovieGraph::BranchNode::TrueBranch));
	}
	else
	{
		PinsToFollow.Add(GetInputPin(UE::MovieGraph::BranchNode::FalseBranch));
	}

	return PinsToFollow;
}

TArray<FMovieGraphPinProperties> UMovieGraphBranchNode::GetInputPinProperties() const
{
	TArray<FMovieGraphPinProperties> Properties;
	
	Properties.Add(FMovieGraphPinProperties(UE::MovieGraph::BranchNode::TrueBranch, EMovieGraphValueType::Branch, false));
	Properties.Add(FMovieGraphPinProperties(UE::MovieGraph::BranchNode::FalseBranch, EMovieGraphValueType::Branch, false));
	Properties.Add(FMovieGraphPinProperties(UE::MovieGraph::BranchNode::Condition, EMovieGraphValueType::Bool, false));
	return Properties;
}

TArray<FMovieGraphPinProperties> UMovieGraphBranchNode::GetOutputPinProperties() const
{
	TArray<FMovieGraphPinProperties> Properties;
	Properties.Add(FMovieGraphPinProperties(NAME_None, EMovieGraphValueType::Branch, false));
	return Properties;
}

#if WITH_EDITOR
FText UMovieGraphBranchNode::GetNodeTitle(const bool bGetDescriptive) const
{
	static const FText BranchNodeName = LOCTEXT("NodeName_Branch", "Branch");
	return BranchNodeName;
}

FText UMovieGraphBranchNode::GetMenuCategory() const
{
	return LOCTEXT("NodeCategory_Conditionals", "Conditionals");
}

FLinearColor UMovieGraphBranchNode::GetNodeTitleColor() const
{
	static const FLinearColor BranchNodeColor = FLinearColor(0.266f, 0.266f, 0.266f);
	return BranchNodeColor;
}

FSlateIcon UMovieGraphBranchNode::GetIconAndTint(FLinearColor& OutColor) const
{
	static const FSlateIcon BranchIcon = FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Merge");

	OutColor = FLinearColor::White;
	return BranchIcon;
}
#endif // WITH_EDITOR

#undef LOCTEXT_NAMESPACE