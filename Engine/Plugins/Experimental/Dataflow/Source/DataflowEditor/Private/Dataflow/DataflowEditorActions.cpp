// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/DataflowEditorActions.h"
#include "Dataflow/DataflowNodeFactory.h"

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

void FDataflowEditorCommands::Register()
{
	return FDataflowEditorCommandsImpl::Register();
}

const FDataflowEditorCommandsImpl& FDataflowEditorCommands::Get()
{
	return FDataflowEditorCommandsImpl::Get();
}

void FDataflowEditorCommands::Unregister()
{
	return FDataflowEditorCommandsImpl::Unregister();
}

#undef LOCTEXT_NAMESPACE
