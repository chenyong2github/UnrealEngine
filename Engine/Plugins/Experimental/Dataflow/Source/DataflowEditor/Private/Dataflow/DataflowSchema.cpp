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

const FPinConnectionResponse UDataflowSchema::CanCreateConnection(const UEdGraphPin* InPinA, const UEdGraphPin* InPinB) const
{
	bool bSwapped = false;
	const UEdGraphPin* PinA = InPinA;
	const UEdGraphPin* PinB = InPinB;
	if (PinA->Direction == EEdGraphPinDirection::EGPD_Input &&
		PinB->Direction == EEdGraphPinDirection::EGPD_Output)
	{
		bSwapped = true;
		PinA = InPinB; PinB = InPinA;
	}


	if (PinA->Direction == EEdGraphPinDirection::EGPD_Output)
	{
		if (PinB->Direction == EEdGraphPinDirection::EGPD_Input)
		{
			// Make sure the pins are not on the same node
			if (PinA->GetOwningNode() != PinB->GetOwningNode())
			{
				// Make sure types match. 
				if (PinA->PinType == PinB->PinType)
				{
					if (PinB->LinkedTo.Num())
					{
						return (bSwapped) ?
							FPinConnectionResponse(CONNECT_RESPONSE_BREAK_OTHERS_A, LOCTEXT("PinSteal", "Disconnect existing input and connect new input."))
							:
							FPinConnectionResponse(CONNECT_RESPONSE_BREAK_OTHERS_B, LOCTEXT("PinSteal", "Disconnect existing input and connect new input."));

					}
					return FPinConnectionResponse(CONNECT_RESPONSE_MAKE, LOCTEXT("PinConnect", "Connect input to output."));
				}
			}
		}
	}
	TArray<FText> NoConnectionResponse = {
		LOCTEXT("PinErrorSameNode_Nope", "Nope"),
		LOCTEXT("PinErrorSameNode_Sorry", "Sorry :("),
		LOCTEXT("PinErrorSameNode_NotGonnaWork", "Not gonna work."),
		LOCTEXT("PinErrorSameNode_StillNo", "Still no!"),
		LOCTEXT("PinErrorSameNode_TryAgain", "Try again?"),
	};
	return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, NoConnectionResponse[FMath::RandRange(0, NoConnectionResponse.Num()-1)]);
}


#undef LOCTEXT_NAMESPACE
