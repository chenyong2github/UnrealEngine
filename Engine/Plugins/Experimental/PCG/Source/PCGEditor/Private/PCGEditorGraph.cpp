// Copyright Epic Games, Inc. All Rights Reserved.

#include "PCGEditorGraph.h"

#include "PCGGraph.h"
#include "PCGEdge.h"
#include "PCGEditorGraphNode.h"

#include "EdGraph/EdGraphPin.h"

void UPCGEditorGraph::InitFromNodeGraph(UPCGGraph* InPCGGraph)
{
	check(InPCGGraph && !PCGGraph);
	PCGGraph = InPCGGraph;

	TMap<UPCGNode*, UPCGEditorGraphNode*> NodeLookup;
	const bool bSelectNewNode = false;

	UPCGNode* InputNode = PCGGraph->GetInputNode();
	FGraphNodeCreator<UPCGEditorGraphNode> InputNodeCreator(*this);
	UPCGEditorGraphNode* InputGraphNode = InputNodeCreator.CreateNode(bSelectNewNode);
	InputGraphNode->Construct(InputNode, EPCGEditorGraphNodeType::Input);
	InputNodeCreator.Finalize();
	NodeLookup.Add(InputNode, InputGraphNode);

	UPCGNode* OutputNode = PCGGraph->GetOutputNode();
	FGraphNodeCreator<UPCGEditorGraphNode> OutputNodeCreator(*this);
	UPCGEditorGraphNode* OutputGraphNode = OutputNodeCreator.CreateNode(bSelectNewNode);
	OutputGraphNode->Construct(OutputNode, EPCGEditorGraphNodeType::Output);
	OutputNodeCreator.Finalize();
	NodeLookup.Add(OutputNode, OutputGraphNode);

	for (UPCGNode* PCGNode : PCGGraph->GetNodes())
	{
		FGraphNodeCreator<UPCGEditorGraphNode> NodeCreator(*this);
		UPCGEditorGraphNode* GraphNode = NodeCreator.CreateNode(bSelectNewNode);
		GraphNode->Construct(PCGNode, EPCGEditorGraphNodeType::Settings);
		NodeCreator.Finalize();
		NodeLookup.Add(PCGNode, GraphNode);
	}

	for (const auto& NodeLookupIt : NodeLookup)
	{
		UPCGNode* PCGNode = NodeLookupIt.Key;
		UPCGEditorGraphNode* GraphNode = NodeLookupIt.Value;

		for (UPCGEdge* OutboundEdge : PCGNode->GetOutboundEdges())
		{
			const FName OutPinName = OutboundEdge->InboundLabel == NAME_None ? TEXT("Out") : OutboundEdge->InboundLabel;
			UEdGraphPin* OutPin = GraphNode->FindPin(OutPinName);

			if (!OutPin)
			{
				continue;
			}
			
			UPCGNode* OutboundNode = OutboundEdge->OutboundNode;
			if (UPCGEditorGraphNode** ConnectedGraphNode = NodeLookup.Find(OutboundNode))
			{
				const FName InPinName = OutboundEdge->OutboundLabel == NAME_None ? TEXT("In") : OutboundEdge->OutboundLabel;

				if(UEdGraphPin* InPin = (*ConnectedGraphNode)->FindPin(InPinName))
				{
					OutPin->MakeLinkTo(InPin);
				}
			}
		}
	}
}