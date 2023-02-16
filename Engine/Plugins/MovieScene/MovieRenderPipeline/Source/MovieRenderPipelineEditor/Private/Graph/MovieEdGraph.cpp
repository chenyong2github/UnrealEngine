// Copyright Epic Games, Inc. All Rights Reserved.

#include "Graph/MovieEdGraph.h"
#include "Graph/MovieGraphConfig.h"
#include "Graph/MovieGraphEdge.h"
#include "Graph/MovieGraphPin.h"
#include "Graph/MovieGraphNode.h"
#include "MovieEdGraphOutputNode.h"
#include "MovieEdGraphInputNode.h"
#include "MovieRenderPipelineCoreModule.h"
#include "EdGraph/EdGraphPin.h"

UMovieGraphConfig* UMoviePipelineEdGraph::GetPipelineGraph() const
{
	return CastChecked<UMovieGraphConfig>(GetOuter());
}


void UMoviePipelineEdGraph::InitFromRuntimeGraph(UMovieGraphConfig* InGraph)
{
	// Don't allow reinitialization of an existing graph
	check(InGraph && !bInitialized);

	const bool bSelectNewNode = false;
	TMap<UMovieGraphNode*, UMoviePipelineEdGraphNodeBase*> NodeLookup;

	// Input
	{
		UMovieGraphNode* InputNode = InGraph->GetInputNode();
		FGraphNodeCreator<UMoviePipelineEdGraphNodeInput> InputNodeCreator(*this);
		UMoviePipelineEdGraphNodeInput* InputGraphNode = InputNodeCreator.CreateNode(bSelectNewNode);
		InputGraphNode->Construct(InputNode);
		InputNodeCreator.Finalize();
	}

	// Output
	{
		UMovieGraphNode* OutputNode = InGraph->GetOutputNode();
		FGraphNodeCreator<UMoviePipelineEdGraphNodeOutput> OutputNodeCreator(*this);
		UMoviePipelineEdGraphNodeOutput* OutputGraphNode = OutputNodeCreator.CreateNode(bSelectNewNode);
		OutputGraphNode->Construct(OutputNode);
		OutputNodeCreator.Finalize();
	}

	// Create the rest of the nodes in the graph
	for (const TObjectPtr<UMovieGraphNode> RuntimeNode : InGraph->GetNodes())
	{
		FGraphNodeCreator<UMoviePipelineEdGraphNode> NodeCreator(*this);
		UMoviePipelineEdGraphNode* GraphNode = NodeCreator.CreateNode(bSelectNewNode);
		GraphNode->Construct(RuntimeNode);
		NodeCreator.Finalize();
		NodeLookup.Add(RuntimeNode, GraphNode);
	}

	// Now that we've added an Editor Graph representation for every node in the graph, link
	// the editor nodes together to match the Runtime Layout.
	for (const TPair< UMovieGraphNode*, UMoviePipelineEdGraphNodeBase*> Pair : NodeLookup)
	{
		const bool bCreateInboundLinks = false;
		const bool bCreateOutboundLinks = true;
		CreateLinks(Pair.Value, bCreateInboundLinks, bCreateOutboundLinks, NodeLookup);
	}

	bInitialized = true;
}

void UMoviePipelineEdGraph::CreateLinks(UMoviePipelineEdGraphNodeBase* InGraphNode, bool bCreateInboundLinks, bool bCreateOutboundLinks,
	const TMap<UMovieGraphNode*, UMoviePipelineEdGraphNodeBase*>& RuntimeNodeToEdNodeMap)
{
	check(InGraphNode);
	const UMovieGraphNode* RuntimeNode = InGraphNode->GetRuntimeNode();
	check(RuntimeNode);

	auto CreateLinks = [&](const TArray<TObjectPtr<UMovieGraphPin>>& Pins, EEdGraphPinDirection PrimaryDirection)
	{
		for (const UMovieGraphPin* Pin : Pins)
		{
			UEdGraphPin* EdPin = InGraphNode->FindPin(Pin->Properties.Label, PrimaryDirection);
			if (!EdPin)
			{
				UE_LOG(LogMovieRenderPipeline, Error, TEXT("Invalid Pin for %s"), *Pin->Properties.Label.ToString());
				continue;
			}

			for (const UMovieGraphEdge* Edge : Pin->Edges)
			{
				if (!Edge->IsValid())
				{
					UE_LOG(LogMovieRenderPipeline, Error, TEXT("Invalid edge for Pin: %s"), *Pin->Properties.Label.ToString());
					continue;
				}

				UMovieGraphPin* OtherPin = PrimaryDirection == EEdGraphPinDirection::EGPD_Input ?
					Edge->InputPin : Edge->OutputPin;
				if (UMoviePipelineEdGraphNodeBase* const* ConnectedGraphNode = RuntimeNodeToEdNodeMap.Find(OtherPin->Node))
				{
					EEdGraphPinDirection SecondaryDirection = PrimaryDirection == EEdGraphPinDirection::EGPD_Input ?
						EEdGraphPinDirection::EGPD_Output : EEdGraphPinDirection::EGPD_Input;
					if (UEdGraphPin* OutEdPin = (*ConnectedGraphNode)->FindPin(OtherPin->Properties.Label, SecondaryDirection))
					{
						OutEdPin->MakeLinkTo(EdPin);
					}
					else
					{
						UE_LOG(LogMovieRenderPipeline, Error, TEXT("Could not create link to Pin %s from Node %s"), *Pin->Properties.Label.ToString(), *OtherPin->Node->GetFName().ToString());
						continue;
					}
				}
			}
		}
	};


	if (bCreateInboundLinks)
	{
		CreateLinks(RuntimeNode->GetInputPins(), EEdGraphPinDirection::EGPD_Input);
	}
	if (bCreateOutboundLinks)
	{
		CreateLinks(RuntimeNode->GetInputPins(), EEdGraphPinDirection::EGPD_Output);
	}
}