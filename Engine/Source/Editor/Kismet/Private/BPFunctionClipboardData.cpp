// Copyright Epic Games, Inc. All Rights Reserved.

#include "BPFunctionClipboardData.h"
#include "K2Node_FunctionEntry.h"
#include "EdGraph/EdGraph.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "EdGraphUtilities.h"

FBPFunctionClipboardData::FBPFunctionClipboardData(const UEdGraph* InFuncGraph)
{
	SetFromGraph(InFuncGraph);
}

bool FBPFunctionClipboardData::IsValid() const
{
	// the only way to set these is by populating this struct with a graph or using *mostly* valid serialized data
	return FuncName != NAME_None && !NodesString.IsEmpty();
}

void FBPFunctionClipboardData::SetFromGraph(const UEdGraph* InFuncGraph)
{
	if (InFuncGraph)
	{
		FuncName = InFuncGraph->GetFName();

		// TODO: Make this look nicer with an overload of ExportNodesToText that takes a TArray?
		// construct a TSet of the nodes in the graph for ExportNodesToText
		TSet<UObject*> Nodes;
		Nodes.Reserve(InFuncGraph->Nodes.Num());
		for (UEdGraphNode* Node : InFuncGraph->Nodes)
		{
			Nodes.Add(Node);
		}
		FEdGraphUtilities::ExportNodesToText(Nodes, NodesString);
	}
}

UEdGraph* FBPFunctionClipboardData::CreateAndPopulateGraph(UBlueprint* InBlueprint, TSubclassOf<UEdGraphSchema> InSchema)
{
	if (InBlueprint && IsValid())
	{
		FName GraphName = FBlueprintEditorUtils::FindUniqueKismetName(InBlueprint, FuncName.ToString());
		UEdGraph* Graph = FBlueprintEditorUtils::CreateNewGraph(InBlueprint, GraphName, UEdGraph::StaticClass(), InSchema);

		if (Graph)
		{
			InBlueprint->FunctionGraphs.Add(Graph);
			PopulateGraph(Graph);
			
			TArray<UK2Node_FunctionEntry*> Entry;
			Graph->GetNodesOfClass<UK2Node_FunctionEntry>(Entry);
			if (ensure(Entry.Num() == 1))
			{
				// Discard category
				Entry[0]->MetaData.Category = FText::FromString(TEXT("Default"));

				// Add necessary function flags
				int32 AdditionalFunctionFlags = (FUNC_BlueprintEvent | FUNC_BlueprintCallable);
				if ((Entry[0]->GetExtraFlags() & FUNC_AccessSpecifiers) == FUNC_None)
				{
					AdditionalFunctionFlags |= FUNC_Public;
				}
				Entry[0]->AddExtraFlags(AdditionalFunctionFlags);

				Entry[0]->FunctionReference.SetExternalMember(Graph->GetFName(), nullptr);

				const UEdGraphSchema_K2* K2Schema = Cast<UEdGraphSchema_K2>(Graph->GetSchema());
				if (K2Schema)
				{
					// Mark graph as editable in case this came from a UserConstructionScript
					K2Schema->MarkFunctionEntryAsEditable(Graph, true);
				}
			}

			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(InBlueprint);

			return Graph;
		}
	}

	return nullptr;
}

void FBPFunctionClipboardData::PopulateGraph(UEdGraph* InFuncGraph)
{
	if (FEdGraphUtilities::CanImportNodesFromText(InFuncGraph, NodesString))
	{
		TSet<UEdGraphNode*> ImportedNodes;
		FEdGraphUtilities::ImportNodesFromText(InFuncGraph, NodesString, ImportedNodes);
	}
}
