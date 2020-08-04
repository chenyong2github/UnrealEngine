// Copyright Epic Games, Inc. All Rights Reserved.

#include "BPGraphClipboardData.h"
#include "K2Node_FunctionEntry.h"
#include "EdGraph/EdGraph.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "EdGraphUtilities.h"
#include "Kismet2/Kismet2NameValidators.h"

FBPGraphClipboardData::FBPGraphClipboardData()
	: GraphType(GT_MAX)
{
	// this results in an invalid GraphData, which will not be able to Paste
}

FBPGraphClipboardData::FBPGraphClipboardData(const UEdGraph* InFuncGraph)
{
	SetFromGraph(InFuncGraph);
}

bool FBPGraphClipboardData::IsValid() const
{
	// the only way to set these is by populating this struct with a graph or using *mostly* valid serialized data
	return GraphName != NAME_None && !NodesString.IsEmpty() && GraphType != GT_MAX;
}

bool FBPGraphClipboardData::IsFunction() const
{
	return GraphType == GT_Function;
}

bool FBPGraphClipboardData::IsMacro() const
{
	return GraphType == GT_Macro;
}

void FBPGraphClipboardData::SetFromGraph(const UEdGraph* InFuncGraph)
{
	if (InFuncGraph)
	{
		GraphName = InFuncGraph->GetFName();

		if (const UEdGraphSchema* Schema = InFuncGraph->GetSchema())
		{
			GraphType = Schema->GetGraphType(InFuncGraph);
		}

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

UEdGraph* FBPGraphClipboardData::CreateAndPopulateGraph(UBlueprint* InBlueprint, TSubclassOf<UEdGraphSchema> InSchema)
{
	if (InBlueprint && IsValid())
	{
		FKismetNameValidator Validator(InBlueprint);
		if (Validator.IsValid(GraphName) != EValidatorResult::Ok)
		{
			GraphName = FBlueprintEditorUtils::FindUniqueKismetName(InBlueprint, GraphName.GetPlainNameString());
		}
		UEdGraph* Graph = FBlueprintEditorUtils::CreateNewGraph(InBlueprint, GraphName, UEdGraph::StaticClass(), InSchema);

		if (Graph)
		{
			PopulateGraph(Graph);

			if (GraphType == GT_Function)
			{
				InBlueprint->FunctionGraphs.Add(Graph);

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
				}
			}
			else if (ensure(GraphType == GT_Macro))
			{
				InBlueprint->MacroGraphs.Add(Graph);
			}

			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(InBlueprint);

			return Graph;
		}
	}

	return nullptr;
}

void FBPGraphClipboardData::PopulateGraph(UEdGraph* InFuncGraph)
{
	if (FEdGraphUtilities::CanImportNodesFromText(InFuncGraph, NodesString))
	{
		TSet<UEdGraphNode*> ImportedNodes;
		FEdGraphUtilities::ImportNodesFromText(InFuncGraph, NodesString, ImportedNodes);
	}
}
