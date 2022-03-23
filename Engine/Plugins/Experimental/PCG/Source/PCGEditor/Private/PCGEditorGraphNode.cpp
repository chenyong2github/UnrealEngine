// Copyright Epic Games, Inc. All Rights Reserved.

#include "PCGEditorGraphNode.h"

#include "EdGraph/EdGraphPin.h"
#include "GraphEditorActions.h"
#include "ToolMenu.h"
#include "ToolMenuSection.h"

#include "PCGNode.h"
#include "PCGSettings.h"
#include "PCGEditorGraphSchema.h"

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
	check(PCGNode && PCGNode->DefaultSettings);
	return FText::FromName(PCGNode->DefaultSettings->GetDefaultNodeName());
}

void UPCGEditorGraphNode::GetNodeContextMenuActions(UToolMenu* Menu, class UGraphNodeContextMenuContext* Context) const
{
	if (!Context->Node)
	{
		return;
	}

	FToolMenuSection& NodeSection = Menu->AddSection("EdGraphSchemaNodeActions", LOCTEXT("NodeActionsHeader", "Node Actions"));
	{
		NodeSection.AddMenuEntry(FGraphEditorCommands::Get().BreakNodeLinks);
	}

	FToolMenuSection& Section = Menu->AddSection("EdGraphSchemaOrganization", LOCTEXT("OrganizationHeader", "Organization"));
	Section.AddSubMenu("Alignment", LOCTEXT("AlignmentHeader", "Alignment"), FText(), FNewToolMenuDelegate::CreateLambda([](UToolMenu* AlignmentMenu)
		{
			{
				FToolMenuSection& InSection = AlignmentMenu->AddSection("EdGraphSchemaAlignment", LOCTEXT("AlignHeader", "Align"));
				InSection.AddMenuEntry(FGraphEditorCommands::Get().AlignNodesTop);
				InSection.AddMenuEntry(FGraphEditorCommands::Get().AlignNodesMiddle);
				InSection.AddMenuEntry(FGraphEditorCommands::Get().AlignNodesBottom);
				InSection.AddMenuEntry(FGraphEditorCommands::Get().AlignNodesLeft);
				InSection.AddMenuEntry(FGraphEditorCommands::Get().AlignNodesCenter);
				InSection.AddMenuEntry(FGraphEditorCommands::Get().AlignNodesRight);
				InSection.AddMenuEntry(FGraphEditorCommands::Get().StraightenConnections);
			}

			{
				FToolMenuSection& InSection = AlignmentMenu->AddSection("EdGraphSchemaDistribution", LOCTEXT("DistributionHeader", "Distribution"));
				InSection.AddMenuEntry(FGraphEditorCommands::Get().DistributeNodesHorizontally);
				InSection.AddMenuEntry(FGraphEditorCommands::Get().DistributeNodesVertically);
			}
		}));
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

#undef LOCTEXT_NAMESPACE
