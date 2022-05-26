// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/DataflowEditorActions.h"
#include "Dataflow/DataflowEdNode.h"
#include "Dataflow/DataflowEPropertyCustomizations.h"
#include "Dataflow/DataflowNodeFactory.h"
#include "Dataflow/DataflowObject.h"


#define LOCTEXT_NAMESPACE "DataflowEditorCommands"

void FDataflowEditorCommandsImpl::RegisterCommands()
{
	UI_COMMAND(EvaluateNode, "Evaluate", "Trigger an evaluation of the selected node.", EUserInterfaceActionType::Button, FInputChord());

	if (Dataflow::FNodeFactory* Factory = Dataflow::FNodeFactory::GetInstance())
	{
		for (FName NodeName : Factory->RegisteredNodes())
		{
			TSharedPtr< FUICommandInfo > AddNode;
			FUICommandInfo::MakeCommandInfo(
				this->AsShared(),
				AddNode,
				NodeName, //FName("UseCreationFormToggle"),
				NSLOCTEXT("DataFlow", "DataflowButton", "New Dataflow Node"),
				NSLOCTEXT("DataFlow", "NewDataflowNodeTooltip", "New Dataflow Node Tooltip"),
				FSlateIcon(),
				EUserInterfaceActionType::Button,
				FInputChord()
			);
			CreateNodesMap.Add(NodeName, AddNode);
		}
	}
}

const FDataflowEditorCommandsImpl& FDataflowEditorCommands::Get()
{
	return FDataflowEditorCommandsImpl::Get();
}

void FDataflowEditorCommands::Register()
{
	return FDataflowEditorCommandsImpl::Register();
}

void FDataflowEditorCommands::Unregister()
{
	return FDataflowEditorCommandsImpl::Unregister();
}



void FDataflowEditorCommands::EvaluateNodes(const FGraphPanelSelectionSet& SelectedNodes, const Dataflow::FContext& InContext)
{
	for (UObject* Ode : SelectedNodes)
	{
		if (UDataflowEdNode* EdNode = dynamic_cast<UDataflowEdNode*>(Ode))
		{
			if (const TSharedPtr<Dataflow::FGraph> DataflowGraph = EdNode->GetDataflowGraph())
			{
				if (const TSharedPtr<Dataflow::FNode> DataflowNode = DataflowGraph->FindBaseNode(EdNode->GetDataflowNodeGuid()))
				{
					if (DataflowNode->GetOutputs().Num())
					{
						for (Dataflow::FConnection* NodeOutput : DataflowNode->GetOutputs())
						{
							DataflowNode->Evaluate(InContext, NodeOutput);
						}
					}
					else
					{
						DataflowNode->Evaluate(InContext, nullptr);
					}
				}
			}
		}
	}
}

void FDataflowEditorCommands::DeleteNodes(UDataflow* Graph, const FGraphPanelSelectionSet& SelectedNodes)
{
	for (UObject* Ode : SelectedNodes)
	{
		if (UDataflowEdNode* EdNode = dynamic_cast<UDataflowEdNode*>(Ode))
		{
			if (const TSharedPtr<Dataflow::FGraph> DataflowGraph = EdNode->GetDataflowGraph())
			{
				if (TSharedPtr<Dataflow::FNode> DataflowNode = DataflowGraph->FindBaseNode(EdNode->GetDataflowNodeGuid()))
				{
					Graph->RemoveNode(EdNode);
					DataflowGraph->RemoveNode(DataflowNode);
				}
			}
		}
	}
}

void FDataflowEditorCommands::OnSelectedNodesChanged(TSharedPtr<IDetailsView> PropertiesEditor, UObject* Asset, UDataflow* Graph, const TSet<UObject*>& NewSelection)
{
	if (Graph && PropertiesEditor)
	{
		if (const TSharedPtr<Dataflow::FGraph> DataflowGraph = Graph->GetDataflow())
		{
			PropertiesEditor->SetObject(Asset);
			FGraphPanelSelectionSet SelectedNodes = NewSelection;//GetSelectedNodes();
			if (SelectedNodes.Num())
			{
				TArray<UObject*> Objects;
				for (UObject* SelectedObject : SelectedNodes)
				{
					if (UDataflowEdNode* EdNode = Cast<UDataflowEdNode>(SelectedObject))
					{
						if (TSharedPtr<Dataflow::FNode> DataflowNode = DataflowGraph->FindBaseNode(EdNode->GetDataflowNodeGuid()))
						{
							const FName NodeName = MakeUniqueObjectName(Graph, UDataflow::StaticClass(), DataflowNode->GetName());
							UDataflowSEditorObject* Object = NewObject<UDataflowSEditorObject>(Asset, NodeName);
							Object->Node = DataflowNode;
							Objects.Add(Object);
						}
					}
				}
				PropertiesEditor->SetObjects(Objects);
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE
