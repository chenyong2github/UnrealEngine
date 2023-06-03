// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosClothAsset/ClothEditorContextObject.h"
#include "Dataflow/DataflowEdNode.h"
#include "Dataflow/DataflowGraphEditor.h"
#include "Dataflow/DataflowSNode.h"
#include "Dataflow/DataflowObject.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(ClothEditorContextObject)

void UClothEditorContextObject::Init(TWeakPtr<SDataflowGraphEditor> InDataflowGraphEditor, TObjectPtr<UDataflow> InDataflowGraph, UE::Chaos::ClothAsset::EClothPatternVertexType InConstructionViewMode, TWeakPtr<FManagedArrayCollection> InSelectedClothCollection)
{
	DataflowGraphEditor = InDataflowGraphEditor;
	DataflowGraph = InDataflowGraph;
	ConstructionViewMode = InConstructionViewMode;
	SelectedClothCollection = InSelectedClothCollection;
}

TWeakPtr<SDataflowGraphEditor> UClothEditorContextObject::GetDataflowGraphEditor()
{
	return DataflowGraphEditor;
}

const TWeakPtr<const SDataflowGraphEditor> UClothEditorContextObject::GetDataflowGraphEditor() const
{
	return DataflowGraphEditor;
}

TObjectPtr<UDataflow> UClothEditorContextObject::GetDataflowGraph()
{
	return DataflowGraph;
}

const TObjectPtr<const UDataflow> UClothEditorContextObject::GetDataflowGraph() const
{
	return DataflowGraph;
}

UEdGraphNode* UClothEditorContextObject::GetSingleSelectedNode() const
{
	const TSharedPtr<const SDataflowGraphEditor> PinnedDataflowGraphEditor = DataflowGraphEditor.Pin();
	if (!PinnedDataflowGraphEditor)
	{
		return nullptr;
	}

	return PinnedDataflowGraphEditor->GetSingleSelectedNode();
}


UEdGraphNode* UClothEditorContextObject::GetSingleSelectedNodeWithOutputType(const FName& SelectedNodeOutputTypeName) const
{
	const TSharedPtr<const SDataflowGraphEditor> PinnedDataflowGraphEditor = DataflowGraphEditor.Pin();
	if (!PinnedDataflowGraphEditor)
	{
		return nullptr;
	}

	UEdGraphNode* const SelectedNode = PinnedDataflowGraphEditor->GetSingleSelectedNode();
	if (!SelectedNode)
	{
		return nullptr;
	}

	const UDataflowEdNode* const SelectedDataflowEdNode = CastChecked<UDataflowEdNode>(SelectedNode);
	const TSharedPtr<const FDataflowNode> SelectedDataflowNode = SelectedDataflowEdNode->GetDataflowNode();
	for (const FDataflowOutput* const Output : SelectedDataflowNode->GetOutputs())
	{
		if (Output->GetType() == SelectedNodeOutputTypeName)
		{
			return SelectedNode;
		}
	}

	return nullptr;
}

UEdGraphNode* UClothEditorContextObject::CreateNewNode(const FName& NewNodeTypeName)
{
	const TSharedPtr<const SDataflowGraphEditor> PinnedDataflowGraphEditor = DataflowGraphEditor.Pin();
	if (!PinnedDataflowGraphEditor)
	{
		return nullptr;
	}

	const TSharedPtr<FAssetSchemaAction_Dataflow_CreateNode_DataflowEdNode> NodeAction =
		FAssetSchemaAction_Dataflow_CreateNode_DataflowEdNode::CreateAction(DataflowGraph, NewNodeTypeName);
	UEdGraphNode* const NewEdNode = NodeAction->PerformAction(DataflowGraph, nullptr, PinnedDataflowGraphEditor->GetPasteLocation(), true);

	return NewEdNode;
}

UEdGraphNode* UClothEditorContextObject::CreateAndConnectNewNode(
	const FName& NewNodeTypeName,
	UEdGraphNode& UpstreamNode,
	const FName& ConnectionTypeName)
{
	// First find the specified output of the upstream node, plus any pins it's connected to

	UEdGraphPin* UpstreamNodeOutputPin = nullptr;
	TArray<UEdGraphPin*> ExistingNodeInputPins;

	const UDataflowEdNode* const UpstreamDataflowEdNode = CastChecked<UDataflowEdNode>(&UpstreamNode);
	const TSharedPtr<const FDataflowNode> UpstreamDataflowNode = UpstreamDataflowEdNode->GetDataflowNode();

	for (const FDataflowOutput* const Output : UpstreamDataflowNode->GetOutputs())
	{
		if (Output->GetType() == ConnectionTypeName)
		{
			UpstreamNodeOutputPin = UpstreamDataflowEdNode->FindPin(*Output->GetName().ToString(), EGPD_Output);
			ExistingNodeInputPins = UpstreamNodeOutputPin->LinkedTo;
			break;
		}
	}

	// Add the new node 

	UEdGraphNode* const NewEdNode = CreateNewNode(NewNodeTypeName);
	checkf(NewEdNode, TEXT("Failed to create a new node in the DataflowGraph"));

	UDataflowEdNode* const NewDataflowEdNode = CastChecked<UDataflowEdNode>(NewEdNode);
	const TSharedPtr<FDataflowNode> NewDataflowNode = NewDataflowEdNode->GetDataflowNode();

	// Re-wire the graph

	if (UpstreamNodeOutputPin)
	{
		UEdGraphPin* NewNodeInputPin = nullptr;
		for (const FDataflowInput* const NewNodeInput : NewDataflowNode->GetInputs())
		{
			if (NewNodeInput->GetType() == ConnectionTypeName)
			{
				NewNodeInputPin = NewDataflowEdNode->FindPin(*NewNodeInput->GetName().ToString(), EGPD_Input);
			}
		}

		UEdGraphPin* NewNodeOutputPin = nullptr;
		for (const FDataflowOutput* const NewNodeOutput : NewDataflowNode->GetOutputs())
		{
			if (NewNodeOutput->GetType() == ConnectionTypeName)
			{
				NewNodeOutputPin = NewDataflowEdNode->FindPin(*NewNodeOutput->GetName().ToString(), EGPD_Output);
				break;
			}
		}

		check(NewNodeInputPin);
		check(NewNodeOutputPin);

		DataflowGraph->GetSchema()->TryCreateConnection(UpstreamNodeOutputPin, NewNodeInputPin);

		for (UEdGraphPin* DownstreamInputPin : ExistingNodeInputPins)
		{
			DataflowGraph->GetSchema()->TryCreateConnection(NewNodeOutputPin, DownstreamInputPin);
		}
	}

	DataflowGraph->NotifyGraphChanged();

	return NewEdNode;
}

void UClothEditorContextObject::SetClothCollection(UE::Chaos::ClothAsset::EClothPatternVertexType ViewMode, TWeakPtr<FManagedArrayCollection> ClothCollection)
{
	ConstructionViewMode = ViewMode;
	SelectedClothCollection = ClothCollection;
}