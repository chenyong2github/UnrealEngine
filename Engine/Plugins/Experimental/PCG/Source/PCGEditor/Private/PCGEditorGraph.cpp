// Copyright Epic Games, Inc. All Rights Reserved.

#include "PCGEditorGraph.h"

#include "PCGGraph.h"
#include "PCGEdge.h"
#include "PCGEditorGraphNode.h"
#include "PCGEditorGraphNodeInput.h"
#include "PCGEditorGraphNodeOutput.h"

#include "EdGraph/EdGraphPin.h"

void UPCGEditorGraph::InitFromNodeGraph(UPCGGraph* InPCGGraph)
{
	check(InPCGGraph && !PCGGraph);
	PCGGraph = InPCGGraph;

	TMap<UPCGNode*, UPCGEditorGraphNodeBase*> NodeLookup;
	const bool bSelectNewNode = false;

	UPCGNode* InputNode = PCGGraph->GetInputNode();
	FGraphNodeCreator<UPCGEditorGraphNodeInput> InputNodeCreator(*this);
	UPCGEditorGraphNodeInput* InputGraphNode = InputNodeCreator.CreateNode(bSelectNewNode);
	InputGraphNode->Construct(InputNode, EPCGEditorGraphNodeType::Input);
	InputNodeCreator.Finalize();
	NodeLookup.Add(InputNode, InputGraphNode);

	UPCGNode* OutputNode = PCGGraph->GetOutputNode();
	FGraphNodeCreator<UPCGEditorGraphNodeOutput> OutputNodeCreator(*this);
	UPCGEditorGraphNodeOutput* OutputGraphNode = OutputNodeCreator.CreateNode(bSelectNewNode);
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
		UPCGEditorGraphNodeBase* GraphNode = NodeLookupIt.Value;
		CreateLinks(GraphNode, /*bCreateInbound=*/false, /*bCreateOutbound=*/true, NodeLookup);
	}

	for (const UObject* ExtraNode : PCGGraph->GetExtraEditorNodes())
	{
		if (const UEdGraphNode* ExtraGraphNode = Cast<UEdGraphNode>(ExtraNode))
		{
			UEdGraphNode* NewNode = DuplicateObject(ExtraGraphNode, /*Outer=*/this);
			const bool bIsUserAction = false;
			AddNode(NewNode, bIsUserAction, bSelectNewNode);
		}
	}
}

void UPCGEditorGraph::CreateLinks(UPCGEditorGraphNodeBase* GraphNode, bool bCreateInbound, bool bCreateOutbound)
{
	check(GraphNode);
	// Build graph node to pcg node map
	TMap<UPCGNode*, UPCGEditorGraphNodeBase*> GraphNodeToPCGNodeMap;

	for (const TObjectPtr<UEdGraphNode>& EdGraphNode : Nodes)
	{
		if (UPCGEditorGraphNodeBase* SomeGraphNode = Cast<UPCGEditorGraphNodeBase>(EdGraphNode))
		{
			GraphNodeToPCGNodeMap.Add(SomeGraphNode->GetPCGNode(), SomeGraphNode);
		}
	}

	// Forward the call
	CreateLinks(GraphNode, bCreateInbound, bCreateOutbound, GraphNodeToPCGNodeMap);
}

void UPCGEditorGraph::CreateLinks(UPCGEditorGraphNodeBase* GraphNode, bool bCreateInbound, bool bCreateOutbound, const TMap<UPCGNode*, UPCGEditorGraphNodeBase*>& GraphNodeToPCGNodeMap)
{
	check(GraphNode);
	const UPCGNode* PCGNode = GraphNode->GetPCGNode();
	check(PCGNode);

	if (bCreateInbound)
	{
		for (const UPCGPin* InputPin : PCGNode->GetInputPins())
		{
			UEdGraphPin* InPin = GraphNode->FindPin(InputPin->Properties.Label, EEdGraphPinDirection::EGPD_Input);

			if (!InPin)
			{
				continue;
			}

			for (const UPCGEdge* InboundEdge : InputPin->Edges)
			{
				if (!InboundEdge->IsValid())
				{
					continue;
				}

				const UPCGNode* InboundNode = InboundEdge->InputPin->Node;
				if (UPCGEditorGraphNodeBase* const* ConnectedGraphNode = GraphNodeToPCGNodeMap.Find(InboundNode))
				{
					if (UEdGraphPin* OutPin = (*ConnectedGraphNode)->FindPin(InboundEdge->InputPin->Properties.Label, EEdGraphPinDirection::EGPD_Output))
					{
						OutPin->MakeLinkTo(InPin);
					}
					else
					{
						continue;
					}
				}
			}
		}
	}

	if (bCreateOutbound)
	{
		for (const UPCGPin* OutputPin : PCGNode->GetOutputPins())
		{
			UEdGraphPin* OutPin = GraphNode->FindPin(OutputPin->Properties.Label, EEdGraphPinDirection::EGPD_Output);

			if (!OutPin)
			{
				continue;
			}

			for (const UPCGEdge* OutboundEdge : OutputPin->Edges)
			{
				if (!OutboundEdge->IsValid())
				{
					continue;
				}

				const UPCGNode* OutboundNode = OutboundEdge->OutputPin->Node;
				if (UPCGEditorGraphNodeBase* const* ConnectedGraphNode = GraphNodeToPCGNodeMap.Find(OutboundNode))
				{
					if (UEdGraphPin* InPin = (*ConnectedGraphNode)->FindPin(OutboundEdge->OutputPin->Properties.Label, EEdGraphPinDirection::EGPD_Input))
					{
						OutPin->MakeLinkTo(InPin);
					}
					else
					{
						continue;
					}
				}
			}
		}
	}
}
