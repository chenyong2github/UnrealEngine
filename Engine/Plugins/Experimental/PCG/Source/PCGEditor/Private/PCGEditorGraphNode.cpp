// Copyright Epic Games, Inc. All Rights Reserved.

#include "PCGEditorGraphNode.h"

#include "PCGEditorGraphSchema.h"
#include "PCGNode.h"
#include "PCGSettings.h"

#include "EdGraph/EdGraphPin.h"
#include "EdGraph/EdGraph.h"
#include "Framework/Commands/GenericCommands.h"
#include "GraphEditorActions.h"
#include "ToolMenu.h"
#include "ToolMenuSection.h"

#define LOCTEXT_NAMESPACE "PCGEditorGraphNode"

void UPCGEditorGraphNode::BeginDestroy()
{
	if (PCGNode)
	{
		PCGNode->OnNodeSettingsChangedDelegate.RemoveAll(this);
	}

	Super::BeginDestroy();
}

void UPCGEditorGraphNode::Construct(UPCGNode* InPCGNode, EPCGEditorGraphNodeType InNodeType)
{
	check(InPCGNode);
	PCGNode = InPCGNode;
	InPCGNode->OnNodeSettingsChangedDelegate.AddUObject(this, &UPCGEditorGraphNode::OnNodeChanged);

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

		if (PCGNode)
		{
			for (const FName& OutLabel : PCGNode->OutLabels())
			{
				CreatePin(EEdGraphPinDirection::EGPD_Output, NAME_None, OutLabel);
			}
		}
	}

	if (NodeType == EPCGEditorGraphNodeType::Output || NodeType == EPCGEditorGraphNodeType::Settings)
	{
		CreatePin(EEdGraphPinDirection::EGPD_Input, NAME_None, FName(TEXT("In")));

		if (PCGNode)
		{
			for (const FName& InLabel : PCGNode->InLabels())
			{
				CreatePin(EEdGraphPinDirection::EGPD_Input, NAME_None, InLabel);
			}
		}
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

void UPCGEditorGraphNode::OnNodeChanged(UPCGNode* InNode)
{
	if (InNode == PCGNode)
	{
		ReconstructNode();
	}
}

void UPCGEditorGraphNode::ReconstructNode()
{
	if (!PCGNode)
	{
		return;
	}

	TArray<FName> InLabels = PCGNode->InLabels();
	TArray<FName> OutLabels = PCGNode->OutLabels();

	TArray<UEdGraphPin*> InputPins;
	TArray<UEdGraphPin*> OutputPins;

	for (UEdGraphPin* Pin : Pins)
	{
		if (Pin->Direction == EEdGraphPinDirection::EGPD_Input && Pin->PinName != TEXT("In"))
		{
			InputPins.Add(Pin);
		}
		else if (Pin->Direction == EEdGraphPinDirection::EGPD_Output && Pin->PinName != TEXT("Out"))
		{
			OutputPins.Add(Pin);
		}
	}

	auto UpdatePins = [this](TArray<UEdGraphPin*>& InPins, const TArray<FName>& InNames, EEdGraphPinDirection InPinDirection)
	{
		TArray<UEdGraphPin*> UnmatchedPins;

		for (UEdGraphPin* InPin : InPins)
		{
			if (!InNames.Contains(InPin->PinName))
			{
				UnmatchedPins.Add(InPin);
			}
		}

		TArray<FName> UnmatchedNames;

		for (const FName& InName : InNames)
		{
			if (InPins.FindByPredicate([&InName](UEdGraphPin* InPin) { return InPin->PinName == InName; }) == nullptr)
			{
				UnmatchedNames.Add(InName);
			}
		}

		// Use cases:
		// - Removing pin(s) : remove pin(s) & break links
		// - Remove all pins : remove all pins
		// - Adding new pin(s) : create new pin(s)
		// - Renaming a pin : find unmatched pin, unmatched name, rename
		// - Complete change : match on names
		if (UnmatchedNames.Num() == 1 && UnmatchedPins.Num() == 1)
		{
			UnmatchedPins[0]->PinName = UnmatchedNames[0];
		}
		else
		{
			for (UEdGraphPin* UnmatchedPin : UnmatchedPins)
			{
				UnmatchedPin->BreakAllPinLinks();
				RemovePin(UnmatchedPin);
			}

			for (const FName& UnmatchedName : UnmatchedNames)
			{
				CreatePin(InPinDirection, NAME_None, UnmatchedName);
			}
		}

		return UnmatchedNames.Num() > 0 || UnmatchedPins.Num() > 0;
	};

	bool bNeedsGraphUpdate = false;
	if (NodeType == EPCGEditorGraphNodeType::Output || NodeType == EPCGEditorGraphNodeType::Settings)
	{
		bNeedsGraphUpdate |= UpdatePins(InputPins, InLabels, EEdGraphPinDirection::EGPD_Input);
	}
	
	if (NodeType == EPCGEditorGraphNodeType::Input || NodeType == EPCGEditorGraphNodeType::Settings)
	{
		bNeedsGraphUpdate |= UpdatePins(OutputPins, OutLabels, EEdGraphPinDirection::EGPD_Output);
	}

	if (bNeedsGraphUpdate)
	{
		GetGraph()->NotifyGraphChanged();
	}
}

#undef LOCTEXT_NAMESPACE
