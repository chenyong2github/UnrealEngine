// Copyright Epic Games, Inc. All Rights Reserved.
#include "MetasoundEditorGraph.h"

#include "EdGraph/EdGraphNode.h"
#include "MetasoundEditorGraphBuilder.h"
#include "MetasoundEditorGraphNode.h"
#include "MetasoundEditorGraphInputNodes.h"
#include "MetasoundEditorModule.h"


UMetasoundEditorGraphInputNode* UMetasoundEditorGraph::CreateInputNode(EMetasoundFrontendLiteralType LiteralType, UClass* InLiteralObjectClass, bool bInSelectNewNode)
{
	using namespace Metasound::Editor;

	IMetasoundEditorModule & EditorModule = FModuleManager::GetModuleChecked<IMetasoundEditorModule>("MetasoundEditor");

	TSubclassOf<UMetasoundEditorGraphInputNode> NodeClass = EditorModule.FindNodeInputClass(LiteralType);

	if (ensure(NodeClass))
	{
		UMetasoundEditorGraphInputNode* NewGraphNode = CastChecked<UMetasoundEditorGraphInputNode>(CreateNode(NodeClass, bInSelectNewNode));

		NewGraphNode->CreateNewGuid();
		NewGraphNode->PostPlacedNewNode();

		if (NewGraphNode->Pins.IsEmpty())
		{
			NewGraphNode->AllocateDefaultPins();
		}

		PostEditChange();
		MarkPackageDirty();

		return NewGraphNode;
	}

	return nullptr;
}

UObject* UMetasoundEditorGraph::GetMetasound() const
{
	return ParentMetasound;
}

UObject& UMetasoundEditorGraph::GetMetasoundChecked() const
{
	check(ParentMetasound);
	return *ParentMetasound;
}

void UMetasoundEditorGraph::Synchronize()
{
	if (ParentMetasound)
	{
		Metasound::Editor::FGraphBuilder::SynchronizeGraph(*ParentMetasound);
	}
}