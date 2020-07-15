// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundEditorGraphNode.h"

#include "Editor/EditorEngine.h"
#include "Engine/Font.h"
#include "Framework/Commands/GenericCommands.h"
#include "GraphEditorActions.h"
#include "Metasound.h"
#include "MetasoundEditorGraph.h"
#include "MetasoundEditorCommands.h"
#include "ScopedTransaction.h"
#include "ToolMenus.h"

#define LOCTEXT_NAMESPACE "MetasoundEditorGraphNode"


UMetasoundEditorGraphNode::UMetasoundEditorGraphNode(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UMetasoundEditorGraphNode::PostLoad()
{
	Super::PostLoad();

	for (int32 Index = 0; Index < Pins.Num(); ++Index)
	{
		UEdGraphPin* Pin = Pins[Index];
		if (Pin->PinName.IsNone())
		{
			// Makes sure pin has a name for lookup purposes but user will never see it
			if (Pin->Direction == EGPD_Input)
			{
				Pin->PinName = CreateUniquePinName(TEXT("Input"));
			}
			else
			{
				Pin->PinName = CreateUniquePinName(TEXT("Output"));
			}
			Pin->PinFriendlyName = FText::FromString(TEXT(" "));
		}
	}
}

void UMetasoundEditorGraphNode::CreateInputPin()
{
	FString PinName; // get from UMetasound
	UEdGraphPin* NewPin = CreatePin(EGPD_Input, TEXT("MetasoundEditorGraphNode"), *PinName);
	if (NewPin->PinName.IsNone())
	{
		// Makes sure pin has a name for lookup purposes but user will never see it
		NewPin->PinName = CreateUniquePinName(TEXT("Input"));
		NewPin->PinFriendlyName = FText::FromString(TEXT(" "));
	}
}

void UMetasoundEditorGraphNode::AddInputPin()
{
	const FScopedTransaction Transaction( NSLOCTEXT("UnrealEd", "MetasoundEditorAddInput", "Add Metasound Input") );
	Modify();
	CreateInputPin();

// 	UMetasound* Metasound = CastChecked<UMetasoundEditorGraph>(GetGraph())->Metasound;
	// TODO: Add metasound input pin

	// Refresh the current graph, so the pins can be updated
	GetGraph()->NotifyGraphChanged();
}

void UMetasoundEditorGraphNode::RemoveInputPin(UEdGraphPin* InGraphPin)
{
	const FScopedTransaction Transaction( NSLOCTEXT("UnrealEd", "SoundCueEditorDeleteInput", "Delete Metasound Input") );
	Modify();

	TArray<UEdGraphPin*> InputPins;
	GetInputPins(InputPins);

	for (int32 InputIndex = 0; InputIndex < InputPins.Num(); InputIndex++)
	{
		if (InGraphPin == InputPins[InputIndex])
		{
			InGraphPin->MarkPendingKill();
			Pins.Remove(InGraphPin);

			// Remove pin from UMetasound
			break;
		}
	}

	// Refresh the current graph, so the pins can be updated
	GetGraph()->NotifyGraphChanged();
}

int32 UMetasoundEditorGraphNode::EstimateNodeWidth() const
{
	const int32 EstimatedCharWidth = 6;
	FString NodeTitle = GetNodeTitle(ENodeTitleType::FullTitle).ToString();
	UFont* Font = GetDefault<UEditorEngine>()->EditorFont;
	int32 Result = NodeTitle.Len()*EstimatedCharWidth;

	if (Font)
	{
		Result = Font->GetStringSize(*NodeTitle);
	}

	return Result;
}

bool UMetasoundEditorGraphNode::CanAddInputPin() const
{
	// Get from UMetasound
	return false;
}

FText UMetasoundEditorGraphNode::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	// Get from UMetasound
	return FText::GetEmpty();
}

void UMetasoundEditorGraphNode::PrepareForCopying()
{
}

void UMetasoundEditorGraphNode::PostCopyNode()
{
}

void UMetasoundEditorGraphNode::PostEditImport()
{
}

void UMetasoundEditorGraphNode::PostDuplicate(bool bDuplicateForPIE)
{
	Super::PostDuplicate(bDuplicateForPIE);

	if (!bDuplicateForPIE)
	{
		CreateNewGuid();
	}
}

void UMetasoundEditorGraphNode::CreateInputPins()
{
	// Get from UMetasound... archetype?
}

void UMetasoundEditorGraphNode::GetNodeContextMenuActions(UToolMenu* Menu, UGraphNodeContextMenuContext* Context) const
{
	if (Context->Pin)
	{
		// If on an input that can be deleted, show option
		if (Context->Pin->Direction == EGPD_Input /*&& SoundNode->ChildNodes.Num() > SoundNode->GetMinChildNodes()*/)
		{
			FToolMenuSection& Section = Menu->AddSection("MetasoundEditorGraphDeleteInput");
			Section.AddMenuEntry(FMetasoundEditorCommands::Get().DeleteInput);
		}
	}
	else if (Context->Node)
	{
		{
			FToolMenuSection& Section = Menu->AddSection("MetasoundEditorGraphNodeAlignment");
			Section.AddSubMenu("Alignment", LOCTEXT("AlignmentHeader", "Alignment"), FText(), FNewToolMenuDelegate::CreateLambda([](UToolMenu* SubMenu)
			{
				{
					FToolMenuSection& SubMenuSection = SubMenu->AddSection("EdGraphSchemaAlignment", LOCTEXT("AlignHeader", "Align"));
					SubMenuSection.AddMenuEntry(FGraphEditorCommands::Get().AlignNodesTop);
					SubMenuSection.AddMenuEntry(FGraphEditorCommands::Get().AlignNodesMiddle);
					SubMenuSection.AddMenuEntry(FGraphEditorCommands::Get().AlignNodesBottom);
					SubMenuSection.AddMenuEntry(FGraphEditorCommands::Get().AlignNodesLeft);
					SubMenuSection.AddMenuEntry(FGraphEditorCommands::Get().AlignNodesCenter);
					SubMenuSection.AddMenuEntry(FGraphEditorCommands::Get().AlignNodesRight);
					SubMenuSection.AddMenuEntry(FGraphEditorCommands::Get().StraightenConnections);
				}

				{
					FToolMenuSection& SubMenuSection = SubMenu->AddSection("EdGraphSchemaDistribution", LOCTEXT("DistributionHeader", "Distribution"));
					SubMenuSection.AddMenuEntry(FGraphEditorCommands::Get().DistributeNodesHorizontally);
					SubMenuSection.AddMenuEntry(FGraphEditorCommands::Get().DistributeNodesVertically);
				}
			}));
		}

		{
			FToolMenuSection& Section = Menu->AddSection("MetasoundEditorGraphNodeEdit");
			Section.AddMenuEntry(FGenericCommands::Get().Delete);
			Section.AddMenuEntry(FGenericCommands::Get().Cut);
			Section.AddMenuEntry(FGenericCommands::Get().Copy);
			Section.AddMenuEntry(FGenericCommands::Get().Duplicate);
		}

		{
			FToolMenuSection& Section = Menu->AddSection("MetasoundEditorGraphNodeAddPlaySync");
			if (CanAddInputPin())
			{
				Section.AddMenuEntry(FMetasoundEditorCommands::Get().AddInput);
			}
		}
	}
}

FText UMetasoundEditorGraphNode::GetTooltipText() const
{
	FText Tooltip = FText::GetEmpty();

	// TODO: Retrieve from UMetasound

	return Tooltip;
}

FString UMetasoundEditorGraphNode::GetDocumentationExcerptName() const
{
	// Default the node to searching for an excerpt named for the C++ node class name, including the U prefix.
	// This is done so that the excerpt name in the doc file can be found by find-in-files when searching for the full class name.
	return FString::Printf(TEXT("%s%s"), UMetasound::StaticClass()->GetPrefixCPP(), *UMetasound::StaticClass()->GetName());
}
#undef LOCTEXT_NAMESPACE
