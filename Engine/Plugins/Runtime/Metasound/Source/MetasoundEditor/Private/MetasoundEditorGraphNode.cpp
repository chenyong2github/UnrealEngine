// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundEditorGraphNode.h"

#include "EdGraph/EdGraphPin.h"
#include "Editor/EditorEngine.h"
#include "Engine/Font.h"
#include "Framework/Commands/GenericCommands.h"
#include "GraphEditorActions.h"
#include "Metasound.h"
#include "MetasoundFrontend.h"
#include "MetasoundEditorGraph.h"
#include "MetasoundEditorGraphBuilder.h"
#include "MetasoundEditorGraphSchema.h"
#include "MetasoundEditorCommands.h"
#include "ScopedTransaction.h"
#include "ToolMenus.h"

#define LOCTEXT_NAMESPACE "MetasoundEditor"


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
				Pin->PinName = CreateUniquePinName("Input");
			}
			else
			{
				Pin->PinName = CreateUniquePinName("Output");
			}
			Pin->PinFriendlyName = FText::GetEmpty();
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

int32 UMetasoundEditorGraphNode::EstimateNodeWidth() const
{
	const FString NodeTitle = GetNodeTitle(ENodeTitleType::FullTitle).ToString();
	if (const UFont* Font = GetDefault<UEditorEngine>()->EditorFont)
	{
		return Font->GetStringSize(*NodeTitle);
	}
	else
	{
		static const int32 EstimatedCharWidth = 6;
		return NodeTitle.Len() * EstimatedCharWidth;
	}
}

UObject& UMetasoundEditorGraphNode::GetMetasoundChecked()
{
	UMetasoundEditorGraph* EdGraph = CastChecked<UMetasoundEditorGraph>(GetGraph());
	return EdGraph->GetMetasoundChecked();
}

const UObject& UMetasoundEditorGraphNode::GetMetasoundChecked() const
{
	UMetasoundEditorGraph* EdGraph = CastChecked<UMetasoundEditorGraph>(GetGraph());
	return EdGraph->GetMetasoundChecked();
}

Metasound::Frontend::FGraphHandle UMetasoundEditorGraphNode::GetRootGraphHandle() const
{
	const FMetasoundAssetBase* MetasoundAsset = Metasound::Frontend::GetObjectAsAssetBase(&GetMetasoundChecked());
	check(MetasoundAsset);

	return MetasoundAsset->GetRootGraphHandle();
}

Metasound::Frontend::FNodeHandle UMetasoundEditorGraphNode::GetNodeHandle() const
{
	return GetRootGraphHandle().GetNodeWithId(NodeID);
}

void UMetasoundEditorGraphNode::IteratePins(TUniqueFunction<void(UEdGraphPin* /* Pin */, int32 /* Index */)> InFunc, EEdGraphPinDirection InPinDirection)
{
	for (int32 PinIndex = 0; PinIndex < Pins.Num(); PinIndex++)
	{
		if (InPinDirection == EGPD_MAX || Pins[PinIndex]->Direction == InPinDirection)
		{
			InFunc(Pins[PinIndex], PinIndex);
		}
	}
}

void UMetasoundEditorGraphNode::AllocateDefaultPins()
{
	using namespace Metasound;

	ensureAlways(Pins.Num() == 0);

	Frontend::FNodeHandle NodeHandle = Frontend::FNodeHandle::InvalidHandle();
	if (NodeID != INDEX_NONE)
	{
		NodeHandle = GetNodeHandle();
	}
	Metasound::Editor::FGraphBuilder::RebuildNodePins(*this, NodeHandle, false /* bInRecordTransaction */);
}

void UMetasoundEditorGraphNode::ReconstructNode()
{
	using namespace Metasound;

	Frontend::FNodeHandle NodeHandle = Frontend::FNodeHandle::InvalidHandle();
	if (NodeID != INDEX_NONE)
	{
		NodeHandle = GetNodeHandle();
	}
	Editor::FGraphBuilder::RebuildNodePins(*this, NodeHandle);
}

void UMetasoundEditorGraphNode::AutowireNewNode(UEdGraphPin* FromPin)
{
	if (FromPin)
	{
		const UMetasoundEditorGraphSchema* Schema = CastChecked<UMetasoundEditorGraphSchema>(GetSchema());

		TSet<UEdGraphNode*> NodeList;

		// auto-connect from dragged pin to first compatible pin on the new node
		for (int32 i = 0; i < Pins.Num(); i++)
		{
			UEdGraphPin* Pin = Pins[i];
			check(Pin);
			FPinConnectionResponse Response = Schema->CanCreateConnection(FromPin, Pin);
			if (ECanCreateConnectionResponse::CONNECT_RESPONSE_MAKE == Response.Response)
			{
				if (Schema->TryCreateConnection(FromPin, Pin))
				{
					NodeList.Add(FromPin->GetOwningNode());
					NodeList.Add(this);
				}
				break;
			}
			else if (ECanCreateConnectionResponse::CONNECT_RESPONSE_BREAK_OTHERS_A == Response.Response)
			{
				// TODO: Implement default connections in GraphBuilder
				break;
			}
		}

		// Send all nodes that received a new pin connection a notification
		for (auto It = NodeList.CreateConstIterator(); It; ++It)
		{
			UEdGraphNode* Node = (*It);
			Node->NodeConnectionListChanged();
		}
	}
}

bool UMetasoundEditorGraphNode::CanCreateUnderSpecifiedSchema(const UEdGraphSchema* Schema) const
{
	return Schema->IsA(UMetasoundEditorGraphSchema::StaticClass());
}

bool UMetasoundEditorGraphNode::CanUserDeleteNode() const
{
	const FString& NodeName = GetNodeHandle().GetNodeName();
	Metasound::Frontend::FGraphHandle GraphHandle = GetRootGraphHandle();
	if (GraphHandle.IsRequiredInput(NodeName))
	{
		return false;
	}

	if (GraphHandle.IsRequiredOutput(NodeName))
	{
		return false;
	}

	return true;
}

FString UMetasoundEditorGraphNode::GetDocumentationLink() const
{
	return TEXT("Shared/GraphNodes/Metasound");
}

void UMetasoundEditorGraphNode::SetNodeID(uint32 InNodeID)
{
	NodeID = InNodeID;
}

uint32 UMetasoundEditorGraphNode::GetNodeID() const
{
	return NodeID;
}

FText UMetasoundEditorGraphNode::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	using namespace Metasound::Frontend;

	FNodeHandle NodeHandle = GetNodeHandle();
	switch (NodeHandle.GetNodeType())
	{
		case EMetasoundClassType::Input:
		{
			FGraphHandle GraphHandle = GetRootGraphHandle();
			return FText::Format(LOCTEXT("MetasoundGraphNode_TitleFormat", "Input {0}"), GraphHandle.GetInputDisplayName(NodeHandle.GetNodeName()));
		}
		break;

		case EMetasoundClassType::Output:
		{
			FGraphHandle GraphHandle = GetRootGraphHandle();
			return FText::Format(LOCTEXT("MetasoundGraphNode_TitleFormat", "Output {0}"), GraphHandle.GetOutputDisplayName(NodeHandle.GetNodeName()));
		}
		break;

		default:
		case EMetasoundClassType::External:
		case EMetasoundClassType::MetasoundGraph:
		{
			return FText::FromString(NodeHandle.GetNodeClassName());
		}
		break;
	}
}

void UMetasoundEditorGraphNode::PrepareForCopying()
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

void UMetasoundEditorGraphNode::GetNodeContextMenuActions(UToolMenu* Menu, UGraphNodeContextMenuContext* Context) const
{
	using namespace Metasound::Editor;

	if (Context->Pin)
	{
		// If on an input that can be deleted, show option
		if (Context->Pin->Direction == EGPD_Input /*&& SoundNode->ChildNodes.Num() > SoundNode->GetMinChildNodes()*/)
		{
			FToolMenuSection& Section = Menu->AddSection("MetasoundEditorGraphDeleteInput");
			Section.AddMenuEntry(FEditorCommands::Get().DeleteInput);
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
	}
}

FText UMetasoundEditorGraphNode::GetTooltipText() const
{
	using namespace Metasound::Frontend;

	FNodeHandle NodeHandle = GetNodeHandle();
	switch (NodeHandle.GetNodeType())
	{
		case EMetasoundClassType::Input:
		{
			FGraphHandle GraphHandle = GetRootGraphHandle();
			return GraphHandle.GetInputToolTip(NodeHandle.GetNodeName());
		}
		break;

		case EMetasoundClassType::Output:
		{
			FGraphHandle GraphHandle = GetRootGraphHandle();
			return GraphHandle.GetOutputToolTip(NodeHandle.GetNodeName());
		}
		break;

		default:
		case EMetasoundClassType::External:
		case EMetasoundClassType::MetasoundGraph:
		{
			return GenerateClassDescription(NodeHandle.GetClassInfo()).Metadata.MetasoundDescription;
		}
		break;
	}
}

FString UMetasoundEditorGraphNode::GetDocumentationExcerptName() const
{
	// Default the node to searching for an excerpt named for the C++ node class name, including the U prefix.
	// This is done so that the excerpt name in the doc file can be found by find-in-files when searching for the full class name.
	return FString::Printf(TEXT("%s%s"), UMetasound::StaticClass()->GetPrefixCPP(), *UMetasound::StaticClass()->GetName());
}
#undef LOCTEXT_NAMESPACE
