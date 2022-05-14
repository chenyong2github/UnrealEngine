// Copyright Epic Games, Inc. All Rights Reserved.

#include "EvalGraph/EvalGraphSchema.h"

#include "EvalGraph/EvalGraphEdNode.h"
#include "EvalGraph/EvalGraphEditorActions.h"
#include "EvalGraph/EvalGraphEdNode.h"
#include "EvalGraph/EvalGraphSNode.h"
#include "EvalGraph/EvalGraphNodeFactory.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraph.h"
#include "Framework/Commands/GenericCommands.h"
#include "Logging/LogMacros.h"
#include "ToolMenuSection.h"
#include "ToolMenu.h"
#include "GraphEditorActions.h"

#define LOCTEXT_NAMESPACE "EvalGraphNode"

UEvalGraphSchema::UEvalGraphSchema()
{
}

void UEvalGraphSchema::GetContextMenuActions(class UToolMenu* Menu, class UGraphNodeContextMenuContext* Context) const
{
	if (Context->Node)
	{
		FToolMenuSection& Section = Menu->AddSection("TestGraphSchemaNodeActions", LOCTEXT("ClassActionsMenuHeader", "Node Actions"));
		{
			Section.AddMenuEntry(FGenericCommands::Get().Delete);
			Section.AddMenuEntry(FGenericCommands::Get().Cut);
			Section.AddMenuEntry(FGenericCommands::Get().Copy);
			Section.AddMenuEntry(FGenericCommands::Get().Duplicate);
			Section.AddMenuEntry(FGraphEditorCommands::Get().BreakNodeLinks);
			Section.AddMenuEntry(FEvalGraphEditorCommands::Get().EvaluateNode);
		}
	}

	Super::GetContextMenuActions(Menu, Context);
}

void UEvalGraphSchema::GetGraphContextActions(FGraphContextMenuBuilder& ContextMenuBuilder) const
{
	if (Eg::FNodeFactory* Factory = Eg::FNodeFactory::GetInstance())
	{
		for (FName NodeName : Factory->RegisteredNodes())
		{
			if (FEvalGraphEditorCommands::Get().CreateNodesMap.Contains(NodeName))
			{
				ContextMenuBuilder.AddAction(FAssetSchemaAction_EvalGraph_CreateNode_EvalGraphEdNode::CreateAction(ContextMenuBuilder.OwnerOfTemporaries, NodeName));
			}
		}
	}
}
#undef LOCTEXT_NAMESPACE
