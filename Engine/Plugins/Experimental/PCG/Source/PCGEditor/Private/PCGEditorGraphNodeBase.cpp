// Copyright Epic Games, Inc. All Rights Reserved.

#include "PCGEditorGraphNodeBase.h"

#include "PCGEditorGraph.h"
#include "PCGEditorGraphSchema.h"
#include "PCGEditorSettings.h"
#include "PCGGraph.h"
#include "PCGNode.h"
#include "PCGSettings.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "Framework/Commands/GenericCommands.h"
#include "GraphEditorActions.h"
#include "GraphEditorSettings.h"
#include "ToolMenu.h"
#include "ToolMenuSection.h"
#include "Widgets/Colors/SColorPicker.h"

#define LOCTEXT_NAMESPACE "PCGEditorGraphNodeBase"

void UPCGEditorGraphNodeBase::BeginDestroy()
{
	if (PCGNode)
	{
		PCGNode->OnNodeSettingsChangedDelegate.RemoveAll(this);
	}

	Super::BeginDestroy();
}

void UPCGEditorGraphNodeBase::Construct(UPCGNode* InPCGNode, EPCGEditorGraphNodeType InNodeType)
{
	check(InPCGNode);
	PCGNode = InPCGNode;
	InPCGNode->OnNodeSettingsChangedDelegate.AddUObject(this, &UPCGEditorGraphNodeBase::OnNodeChanged);

	NodePosX = InPCGNode->PositionX;
	NodePosY = InPCGNode->PositionY;
	
	NodeType = InNodeType;

	bCanRenameNode = (InNodeType == EPCGEditorGraphNodeType::Settings);
}

void UPCGEditorGraphNodeBase::GetNodeContextMenuActions(UToolMenu* Menu, class UGraphNodeContextMenuContext* Context) const
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
		FToolMenuSection& Section = Menu->AddSection("EdGraphSchemaOrganization", LOCTEXT("OrganizationHeader", "Organization"));
		Section.AddMenuEntry(
			"PCGNode_SetColor",
			LOCTEXT("PCGNode_SetColor", "Set Node Color"),
			LOCTEXT("PCGNode_SetColorTooltip", "Sets a specific color on the given node. Note that white maps to the default value"),
			FSlateIcon(FEditorStyle::GetStyleSetName(), "ColorPicker.Mode"),
			FUIAction(FExecuteAction::CreateUObject(const_cast<UPCGEditorGraphNodeBase*>(this), &UPCGEditorGraphNodeBase::OnPickColor)));

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

void UPCGEditorGraphNodeBase::AutowireNewNode(UEdGraphPin* FromPin)
{
	if (FromPin->Direction == EEdGraphPinDirection::EGPD_Output)
	{
		FName InPinName = TEXT("In");
		if (PCGNode && !PCGNode->HasDefaultInLabel() && !PCGNode->InLabels().IsEmpty())
		{
			InPinName = PCGNode->InLabels()[0];
		}

		UEdGraphPin* ToPin = FindPinChecked(InPinName, EEdGraphPinDirection::EGPD_Input);
		GetSchema()->TryCreateConnection(FromPin, ToPin);
	}
	else if (FromPin->Direction == EEdGraphPinDirection::EGPD_Input)
	{
		FName OutPinName = TEXT("Out");
		if (PCGNode && !PCGNode->HasDefaultOutLabel() && !PCGNode->OutLabels().IsEmpty())
		{
			OutPinName = PCGNode->OutLabels()[0];
		}

		UEdGraphPin* ToPin = FindPinChecked(OutPinName, EEdGraphPinDirection::EGPD_Output);
		GetSchema()->TryCreateConnection(FromPin, ToPin);
	}
	NodeConnectionListChanged();
}

void UPCGEditorGraphNodeBase::PrepareForCopying()
{
	if (PCGNode)
	{
		// Temporarily take ownership of the MaterialExpression, so that it is not deleted when cutting
		PCGNode->Rename(nullptr, this, REN_DontCreateRedirectors | REN_DoNotDirty);
	}
}

bool UPCGEditorGraphNodeBase::CanCreateUnderSpecifiedSchema(const UEdGraphSchema* Schema) const
{
	return Schema->IsA(UPCGEditorGraphSchema::StaticClass());
}

void UPCGEditorGraphNodeBase::PostCopy()
{
	if (PCGNode)
	{
		UPCGEditorGraph* PCGEditorGraph = CastChecked<UPCGEditorGraph>(GetGraph());
		UPCGGraph* PCGGraph = PCGEditorGraph->GetPCGGraph();
		check(PCGGraph);
		PCGNode->Rename(nullptr, PCGGraph, REN_DontCreateRedirectors | REN_DoNotDirty);
	}
}

void UPCGEditorGraphNodeBase::PostPaste()
{
	if (PCGNode)
	{
		RebuildEdgesFromPins();

		PCGNode->OnNodeSettingsChangedDelegate.AddUObject(this, &UPCGEditorGraphNodeBase::OnNodeChanged);
		PCGNode->PositionX = NodePosX;
		PCGNode->PositionY = NodePosY;
	}
}

void UPCGEditorGraphNodeBase::RebuildEdgesFromPins()
{
	check(PCGNode)
	
	for (UEdGraphPin* Pin : Pins)
	{
		if (Pin->Direction == EEdGraphPinDirection::EGPD_Output)
		{
			for (UEdGraphPin* ConnectedPin : Pin->LinkedTo)
			{
				UEdGraphNode* ConnectedGraphNode = ConnectedPin->GetOwningNode();
				UPCGEditorGraphNodeBase* ConnectedPCGGraphNode = CastChecked<UPCGEditorGraphNodeBase>(ConnectedGraphNode);

				if (UPCGNode* ConnectedPCGNode = ConnectedPCGGraphNode->GetPCGNode())
				{
					const FName& NodeAPinName = ((Pin->PinName == TEXT("Out") && PCGNode->HasDefaultOutLabel()) ? NAME_None : Pin->PinName);
					const FName& NodeBPinName = ((ConnectedPin->PinName == TEXT("In") && ConnectedPCGNode->HasDefaultInLabel()) ? NAME_None : ConnectedPin->PinName);

					PCGNode->AddEdgeTo(NodeAPinName, ConnectedPCGNode, NodeBPinName);
				}
			}
		}
	}
}

void UPCGEditorGraphNodeBase::OnNodeChanged(UPCGNode* InNode, bool bSettingsChanged)
{
	if (InNode == PCGNode)
	{
		ReconstructNode();
	}
}

void UPCGEditorGraphNodeBase::OnPickColor()
{
	FColorPickerArgs PickerArgs;
	PickerArgs.bIsModal = true;
	PickerArgs.bUseAlpha = false;
	PickerArgs.InitialColorOverride = GetNodeTitleColor();
	PickerArgs.OnColorCommitted = FOnLinearColorValueChanged::CreateUObject(this, &UPCGEditorGraphNodeBase::OnColorPicked);

	OpenColorPicker(PickerArgs);
}

void UPCGEditorGraphNodeBase::OnColorPicked(FLinearColor NewColor)
{
	if (PCGNode && GetNodeTitleColor() != NewColor)
	{
		PCGNode->Modify();
		PCGNode->NodeTitleColor = NewColor;
	}
}

void UPCGEditorGraphNodeBase::ReconstructNode()
{
	if (!PCGNode)
	{
		return;
	}

	TArray<FName> InLabels = PCGNode->InLabels();
	TArray<FName> OutLabels = PCGNode->OutLabels();

	TArray<UEdGraphPin*> InputPins;
	TArray<UEdGraphPin*> OutputPins;

	// Reallocate the default pins if they had been removed previously
	if ((NodeType == EPCGEditorGraphNodeType::Output || NodeType == EPCGEditorGraphNodeType::Settings) && PCGNode->HasDefaultInLabel() && !FindPin(TEXT("In"), EEdGraphPinDirection::EGPD_Input))
	{
		FCreatePinParams FirstPinParams;
		FirstPinParams.Index = 0;
		CreatePin(EEdGraphPinDirection::EGPD_Input, NAME_None, FName(TEXT("In")), FirstPinParams);
	}

	if ((NodeType == EPCGEditorGraphNodeType::Input || NodeType == EPCGEditorGraphNodeType::Settings) && PCGNode->HasDefaultOutLabel() && !FindPin(TEXT("Out"), EEdGraphPinDirection::EGPD_Output))
	{
		FCreatePinParams FirstPinParams;
		FirstPinParams.Index = 0;
		CreatePin(EEdGraphPinDirection::EGPD_Output, NAME_None, FName(TEXT("Out")), FirstPinParams);
	}

	for (UEdGraphPin* Pin : Pins)
	{
		if (Pin->Direction == EEdGraphPinDirection::EGPD_Input && (Pin->PinName != TEXT("In") || !PCGNode->HasDefaultInLabel()))
		{
			InputPins.Add(Pin);
		}
		else if (Pin->Direction == EEdGraphPinDirection::EGPD_Output && (Pin->PinName != TEXT("Out") || !PCGNode->HasDefaultOutLabel()))
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

	if (NodeType == EPCGEditorGraphNodeType::Output || NodeType == EPCGEditorGraphNodeType::Settings)
	{
		UpdatePins(InputPins, InLabels, EEdGraphPinDirection::EGPD_Input);
	}
	
	if (NodeType == EPCGEditorGraphNodeType::Input || NodeType == EPCGEditorGraphNodeType::Settings)
	{
		UpdatePins(OutputPins, OutLabels, EEdGraphPinDirection::EGPD_Output);
	}

	OnNodeChangedDelegate.ExecuteIfBound();
}

FLinearColor UPCGEditorGraphNodeBase::GetNodeTitleColor() const
{
	if (PCGNode)
	{
		if (PCGNode->NodeTitleColor != FLinearColor::White)
		{
			return PCGNode->NodeTitleColor;
		}
		else if (PCGNode->DefaultSettings)
		{
			FLinearColor SettingsColor = PCGNode->DefaultSettings->GetNodeTitleColor();
			if (SettingsColor == FLinearColor::White)
			{
				SettingsColor = GetDefault<UPCGEditorSettings>()->GetColor(PCGNode->DefaultSettings);
			}

			if (SettingsColor != FLinearColor::White)
			{
				return SettingsColor;
			}
		}
	}

	return GetDefault<UPCGEditorSettings>()->DefaultNodeColor;
}

#undef LOCTEXT_NAMESPACE
