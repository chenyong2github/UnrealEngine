// Copyright Epic Games, Inc. All Rights Reserved.

#include "PCGEditorGraph.h"

#include "PCGGraph.h"
#include "PCGEditorGraphNode.h"

#include "EdGraph/EdGraphPin.h"

void UPCGEditorGraph::InitFromNodeGraph(UPCGGraph* InPCGGraph)
{
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

		UEdGraphPin* OutPin = GraphNode->FindPin(TEXT("Out"));
		if (!OutPin)
		{
			continue;
		}

		for (UPCGNode* OutboundNode : PCGNode->GetOutboundNodes())
		{
			if (UPCGEditorGraphNode** ConnectedGraphNode = NodeLookup.Find(OutboundNode))
			{
				if (UEdGraphPin* InPin = (*ConnectedGraphNode)->FindPin(TEXT("In")))
				{
					OutPin->MakeLinkTo(InPin);
				}
			}
		}
	}
}
