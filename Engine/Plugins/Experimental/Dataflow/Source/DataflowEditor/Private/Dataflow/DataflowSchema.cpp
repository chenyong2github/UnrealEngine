// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/DataflowSchema.h"

#include "Dataflow/DataflowEdNode.h"
#include "Dataflow/DataflowEditorActions.h"
#include "Dataflow/DataflowEdNode.h"
#include "Dataflow/DataflowSNode.h"
#include "Dataflow/DataflowNodeFactory.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraph.h"
#include "Framework/Commands/GenericCommands.h"
#include "Logging/LogMacros.h"
#include "ToolMenuSection.h"
#include "ToolMenu.h"
#include "GraphEditorActions.h"

#define LOCTEXT_NAMESPACE "DataflowNode"

UDataflowSchema::UDataflowSchema()
{
}

void UDataflowSchema::GetContextMenuActions(class UToolMenu* Menu, class UGraphNodeContextMenuContext* Context) const
{
	if (Context->Node)
	{
		FToolMenuSection& Section = Menu->AddSection("TestGraphSchemaNodeActions", LOCTEXT("ClassActionsMenuHeader", "Node Actions"));
		{
			Section.AddMenuEntry(FGenericCommands::Get().Delete);
			Section.AddMenuEntry(FGraphEditorCommands::Get().BreakNodeLinks);
			Section.AddMenuEntry(FDataflowEditorCommands::Get().EvaluateNode);
		}
	}

	Super::GetContextMenuActions(Menu, Context);
}

void UDataflowSchema::GetGraphContextActions(FGraphContextMenuBuilder& ContextMenuBuilder) const
{
	if (Dataflow::FNodeFactory* Factory = Dataflow::FNodeFactory::GetInstance())
	{
		for (FName NodeTypeName : Factory->RegisteredNodes())
		{
			if (FDataflowEditorCommands::Get().CreateNodesMap.Contains(NodeTypeName))
			{
				ContextMenuBuilder.AddAction(FAssetSchemaAction_Dataflow_CreateNode_DataflowEdNode::CreateAction(ContextMenuBuilder.OwnerOfTemporaries, NodeTypeName));
			}
		}
	}
}

const FPinConnectionResponse UDataflowSchema::CanCreateConnection(const UEdGraphPin* PinA, const UEdGraphPin* PinB) const
{
	// Make sure the pins are not on the same node
	if (PinA->GetOwningNode() != PinB->GetOwningNode())
	{
		// Make sure types match. 
		if (PinA->PinType == PinB->PinType)
		{
			return FPinConnectionResponse(CONNECT_RESPONSE_MAKE, LOCTEXT("PinConnect", "Connect nodes"));
		}
	}

	return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, LOCTEXT("PinErrorSameNode", "Both are on the same node"));
}


#undef LOCTEXT_NAMESPACE
