// Copyright Epic Games, Inc. All Rights Reserved.

#include "OptimusEditorClipboard.h"

#include "OptimusNode.h"
#include "OptimusNodeGraph.h"
#include "OptimusNodeLink.h"
#include "OptimusNodePin.h"
#include "UnrealExporter.h"
#include "Exporters/Exporter.h"
#include "HAL/PlatformApplicationMisc.h"


UOptimusClipboardContent* UOptimusClipboardContent::Create(
	const UOptimusNodeGraph* InGraph,
	const TArray<UOptimusNode*>& InNodes
	)
{
	for (const UOptimusNode* Node: InNodes)
	{
		if (Node->GetOwningGraph() != InGraph)
		{
			return nullptr;
		}
	}

	UOptimusClipboardContent* Content = NewObject<UOptimusClipboardContent>(GetTransientPackage());

	TMap<const UOptimusNode*, int32> NodeIndexMap;
	for (const UOptimusNode* Node: InNodes)
	{
		NodeIndexMap.Add(Node, Content->Nodes.Num());
		Content->Nodes.Add(Node);
	}

	// Make a copy of all the links that connect the nodes that are being copied.
	for (const UOptimusNodeLink* Link: InGraph->GetAllLinks())
	{
		if (ensure(Link->GetNodeOutputPin() != nullptr))
		{
			const UOptimusNode *OutputNode = Link->GetNodeOutputPin()->GetOwningNode();
			const UOptimusNode *InputNode = Link->GetNodeInputPin()->GetOwningNode();

			if (NodeIndexMap.Contains(OutputNode) && NodeIndexMap.Contains(InputNode))
			{
				FOptimusClipboardNodeLink ClipboardLink;
				ClipboardLink.NodeOutputIndex = NodeIndexMap[OutputNode];
				ClipboardLink.NodeOutputPinName = Link->GetNodeOutputPin()->GetUniqueName().ToString();
			
				ClipboardLink.NodeInputIndex = NodeIndexMap[InputNode];
				ClipboardLink.NodeInputPinName = Link->GetNodeOutputPin()->GetUniqueName().ToString();

				Content->NodeLinks.Add(ClipboardLink);
			}
		}
	}

	return Content;
}


UOptimusNodeGraph* UOptimusClipboardContent::GetGraphFromClipboardContent() const
{
	// Create a temporary graph that will hold the nodes that the caller will then duplicate
	// out of.
	UOptimusNodeGraph* Graph = NewObject<UOptimusNodeGraph>(GetTransientPackage(), FName("ClipboardGraph"), RF_Transient);
	
	// Duplicate the nodes into it.
	TMap<const UOptimusNode*, UOptimusNode*> OldNodeToNewNodeMap;
	for (const UOptimusNode* Node: Nodes)
	{
		UOptimusNode* NewNode = DuplicateObject(Node, Graph, Node->GetFName());
		Graph->AddNodeDirect(NewNode);
		OldNodeToNewNodeMap.Add(Node, NewNode);
	}

	// Link them up.
	for (const FOptimusClipboardNodeLink& LinkInfo: NodeLinks)
	{
		const UOptimusNode* OutputNode = OldNodeToNewNodeMap[Nodes[LinkInfo.NodeOutputIndex]];
		const UOptimusNode* InputNode = OldNodeToNewNodeMap[Nodes[LinkInfo.NodeInputIndex]];

		UOptimusNodePin* OutputPin = OutputNode->FindPin(LinkInfo.NodeOutputPinName);
		UOptimusNodePin* InputPin = InputNode->FindPin(LinkInfo.NodeInputPinName);

		Graph->AddLinkDirect(OutputPin, InputPin);
	}

	return Graph;
}


void FOptimusEditorClipboard::SetClipboardContent(UOptimusClipboardContent* InContent)
{
	// Clear the mark state for saving.
	UnMarkAllObjects(EObjectMark(OBJECTMARK_TagExp | OBJECTMARK_TagImp));

	// Export the clipboard to text.
	FStringOutputDevice Archive;
	const FExportObjectInnerContext Context;
	UExporter::ExportToOutputDevice(
		&Context,
		InContent,
		nullptr,
		Archive,
		TEXT("copy"),
		0,
		PPF_ExportsNotFullyQualified | PPF_Copy | PPF_Delimited,
		false,
		InContent->GetOuter());
	FPlatformApplicationMisc::ClipboardCopy(*Archive);
}


UOptimusClipboardContent* FOptimusEditorClipboard::GetClipboardContent() const
{
	// Get the text from the clipboard.
	FString ClipboardText;
	FPlatformApplicationMisc::ClipboardPaste(ClipboardText);

	// Try to create optimus clipboard content from that.
	FOptimusEditorClipboardContentTextObjectFactory ClipboardContentFactory;
	if (ClipboardContentFactory.CanCreateObjectsFromText(ClipboardText))
	{
		ClipboardContentFactory.ProcessBuffer(GetTransientPackage(), RF_Transactional, ClipboardText);
		return ClipboardContentFactory.ClipboardContent;
	}

	return nullptr;
}


bool FOptimusEditorClipboard::HasValidClipboardContent() const
{
	// Get the text from the clipboard.
	FString ClipboardText;
	FPlatformApplicationMisc::ClipboardPaste(ClipboardText);

	const FOptimusEditorClipboardContentTextObjectFactory ClipboardContentFactory;
	return ClipboardContentFactory.CanCreateObjectsFromText(ClipboardText);
}
