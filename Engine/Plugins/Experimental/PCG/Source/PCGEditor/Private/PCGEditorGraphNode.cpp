// Copyright Epic Games, Inc. All Rights Reserved.

#include "PCGEditorGraphNode.h"

#include "PCGEditorGraphSchema.h"
#include "PCGNode.h"
#include "PCGSettings.h"

#include "EdGraph/EdGraphPin.h"
#include "Framework/Commands/GenericCommands.h"
#include "GraphEditorActions.h"
#include "ToolMenu.h"
#include "ToolMenuSection.h"

#define LOCTEXT_NAMESPACE "PCGEditorGraphNode"

void UPCGEditorGraphNode::Construct(UPCGNode* InPCGNode, EPCGEditorGraphNodeType InNodeType)
{
	check(InPCGNode);
	PCGNode = InPCGNode;

	NodePosX = InPCGNode->PositionX;
	NodePosY = InPCGNode->PositionY;
	
	NodeType = InNodeType;
}

FText UPCGEditorGraphNode::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	if (PCGNode && PCGNode->DefaultSettings)
	{
		if (PCGNode->DefaultSettings->AdditionalTaskName() != NAME_None)
		{
			return FText::FromName(PCGNode->DefaultSettings->AdditionalTaskName());
		}
		else
		{
			return FText::FromName(PCGNode->DefaultSettings->GetDefaultNodeName());
		}
	}
	else
	{
		return FText::FromName(TEXT("Unnamed node"));
	}
}

void UPCGEditorGraphNode::GetNodeContextMenuActions(UToolMenu* Menu, class UGraphNodeContextMenuContext* Context) const
{
	if (!Context->Node)
	{
		return;
	}

	{
		FToolMenuSection& Section = Menu->AddSection("EdGraphSchemaNodeActions", LOCTEXT("NodeActionsHeader", "Node Actions"));
		Section.AddMenuEntry(FGraphEditorCommands::Get().BreakNodeLinks);
	}

	{
		FToolMenuSection& Section = Menu->AddSection("EdGraphSchemaGeneral", LOCTEXT("GeneralHeader", "General"));
		Section.AddMenuEntry(FGenericCommands::Get().Delete);
	}

	{
		FToolMenuSection& Section = Menu->AddSection("EdGraphSchemaOrganization", LOCTEXT("OrganizationHeader", "Organization"));
		Section.AddSubMenu("Alignment", LOCTEXT("AlignmentHeader", "Alignment"), FText(), FNewToolMenuDelegate::CreateLambda([](UToolMenu* AlignmentMenu)
		{
			{
				FToolMenuSection& SubSection = AlignmentMenu->AddSection("EdGraphSchemaAlignment", LOCTEXT("AlignHeader", "Align"));
				SubSection.AddMenuEntry(FGraphEditorCommands::Get().AlignNodesTop);
				SubSection.AddMenuEntry(FGraphEditorCommands::Get().AlignNodesMiddle);
				SubSection.AddMenuEntry(FGraphEditorCommands::Get().AlignNodesBottom);
				SubSection.AddMenuEntry(FGraphEditorCommands::Get().AlignNodesLeft);
				SubSection.AddMenuEntry(FGraphEditorCommands::Get().AlignNodesCenter);
				SubSection.AddMenuEntry(FGraphEditorCommands::Get().AlignNodesRight);
				SubSection.AddMenuEntry(FGraphEditorCommands::Get().StraightenConnections);
			}

			{
				FToolMenuSection& SubSection = AlignmentMenu->AddSection("EdGraphSchemaDistribution", LOCTEXT("DistributionHeader", "Distribution"));
				SubSection.AddMenuEntry(FGraphEditorCommands::Get().DistributeNodesHorizontally);
				SubSection.AddMenuEntry(FGraphEditorCommands::Get().DistributeNodesVertically);
			}
		}));
	}
}

void UPCGEditorGraphNode::AllocateDefaultPins()
{
	if (NodeType == EPCGEditorGraphNodeType::Input || NodeType == EPCGEditorGraphNodeType::Settings)
	{
		CreatePin(EEdGraphPinDirection::EGPD_Output, NAME_None, FName(TEXT("Out")));
	}

	if (NodeType == EPCGEditorGraphNodeType::Output || NodeType == EPCGEditorGraphNodeType::Settings)
	{
		CreatePin(EEdGraphPinDirection::EGPD_Input, NAME_None, FName(TEXT("In")));
	}
}

void UPCGEditorGraphNode::AutowireNewNode(UEdGraphPin* FromPin)
{
	if (FromPin->Direction == EEdGraphPinDirection::EGPD_Output)
	{
		UEdGraphPin* ToPin = FindPinChecked(TEXT("In"), EEdGraphPinDirection::EGPD_Input);
		GetSchema()->TryCreateConnection(FromPin, ToPin);
	}
	else if (FromPin->Direction == EEdGraphPinDirection::EGPD_Input)
	{
		UEdGraphPin* ToPin = FindPinChecked(TEXT("Out"), EEdGraphPinDirection::EGPD_Output);
		GetSchema()->TryCreateConnection(FromPin, ToPin);
	}
	NodeConnectionListChanged();
}

bool UPCGEditorGraphNode::CanUserDeleteNode() const
{
	return NodeType != EPCGEditorGraphNodeType::Input && NodeType != EPCGEditorGraphNodeType::Output;
}

#undef LOCTEXT_NAMESPACE
