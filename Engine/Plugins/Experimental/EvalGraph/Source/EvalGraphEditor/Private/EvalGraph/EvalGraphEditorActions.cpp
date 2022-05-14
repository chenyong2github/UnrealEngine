// Copyright Epic Games, Inc. All Rights Reserved.

#include "EvalGraph/EvalGraphEditorActions.h"
#include "EvalGraph/EvalGraphNodeFactory.h"

#define LOCTEXT_NAMESPACE "EvalGraphEditorCommands"

void FEvalGraphEditorCommandsImpl::RegisterCommands()
{
	UI_COMMAND(EvaluateNode, "Evaluate", "Trigger an evaluation of the selected node.", EUserInterfaceActionType::Button, FInputChord());

	if (Eg::FNodeFactory* Factory = Eg::FNodeFactory::GetInstance())
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
			//UI_COMMAND(AddNode,"Name Here", "Add Node", EUserInterfaceActionType::Button, FInputChord());
			CreateNodesMap.Add(NodeName, AddNode);
		}
	}
}

void FEvalGraphEditorCommands::Register()
{
	return FEvalGraphEditorCommandsImpl::Register();
}

const FEvalGraphEditorCommandsImpl& FEvalGraphEditorCommands::Get()
{
	return FEvalGraphEditorCommandsImpl::Get();
}

void FEvalGraphEditorCommands::Unregister()
{
	return FEvalGraphEditorCommandsImpl::Unregister();
}

#undef LOCTEXT_NAMESPACE
