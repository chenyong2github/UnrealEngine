// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundEditorGraphNode.h"

#include "EdGraph/EdGraphPin.h"
#include "Editor/EditorEngine.h"
#include "Engine/Font.h"
#include "Framework/Commands/GenericCommands.h"
#include "GraphEditorActions.h"
#include "Metasound.h"
#include "MetasoundEditorGraph.h"
#include "MetasoundEditorGraphBuilder.h"
#include "MetasoundEditorGraphSchema.h"
#include "MetasoundEditorCommands.h"
#include "MetasoundEditorModule.h"
#include "MetasoundFrontend.h"
#include "MetasoundUObjectRegistry.h"
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
	// TODO: Implement for nodes supporting variadic inputs
	if (ensure(false))
	{
		return;
	}

	FString PinName; // get from UMetasound
	UEdGraphPin* NewPin = CreatePin(EGPD_Input, TEXT("MetasoundEditorGraphNode"), *PinName);
	if (NewPin->PinName.IsNone())
	{
		// Pin must have a name for lookup purposes but is not user-facing
// 		NewPin->PinName = 
// 		NewPin->PinFriendlyName =
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

Metasound::Frontend::FConstGraphHandle UMetasoundEditorGraphNode::GetConstRootGraphHandle() const
{
	const FMetasoundAssetBase* ConstMetasoundAsset = Metasound::IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(&GetMetasoundChecked());
	FMetasoundAssetBase* MetasoundAsset = const_cast<FMetasoundAssetBase*>(ConstMetasoundAsset);
	check(MetasoundAsset);

	return MetasoundAsset->GetRootGraphHandle();
}

Metasound::Frontend::FGraphHandle UMetasoundEditorGraphNode::GetRootGraphHandle() const
{
	const FMetasoundAssetBase* ConstMetasoundAsset = Metasound::IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(&GetMetasoundChecked());
	FMetasoundAssetBase* MetasoundAsset = const_cast<FMetasoundAssetBase*>(ConstMetasoundAsset);
	check(MetasoundAsset);

	return MetasoundAsset->GetRootGraphHandle();
}

Metasound::Frontend::FConstNodeHandle UMetasoundEditorGraphNode::GetConstNodeHandle() const
{
	return GetConstRootGraphHandle()->GetNodeWithID(NodeID);
}

Metasound::Frontend::FNodeHandle UMetasoundEditorGraphNode::GetNodeHandle() const
{
	return GetRootGraphHandle()->GetNodeWithID(NodeID);
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

	ensureAlways(Pins.IsEmpty());
	Editor::FGraphBuilder::RebuildNodePins(*this);
}

void UMetasoundEditorGraphNode::ReconstructNode()
{
	using namespace Metasound;

	// Don't remove unused pins here. Reconstruction can occur while duplicating or pasting nodes,
	// and subsequent steps clean-up unused pins.  This can be called mid-copy, which means the node
	// handle may be invalid.  Setting to remove unused causes premature removal and then default values
	// are lost.
	// TODO: User will want to see dead pins as well for node definition changes. Label and color-code dead
	// pins (ex. red), and leave dead connections for reference like BP.
	Editor::FGraphBuilder::SynchronizeNodePins(*this, GetNodeHandle(), false /* bRemoveUnusedPins */);
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
			if (ECanCreateConnectionResponse::CONNECT_RESPONSE_MAKE == Response.Response) //-V1051
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
	const FMetasoundAssetBase* ConstMetasoundAsset = Metasound::IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(&GetMetasoundChecked());

	// TODO: create FConstGraphHandle so that this const cast is unnecessary.
	FMetasoundAssetBase* MetasoundAsset = const_cast<FMetasoundAssetBase*>(ConstMetasoundAsset);
	check(MetasoundAsset);

	Metasound::Frontend::FDocumentHandle DocumentHandle = MetasoundAsset->GetDocumentHandle();
	Metasound::Frontend::FNodeHandle NodeHandle = DocumentHandle->GetRootGraph()->GetNodeWithID(NodeID);

	const FString& NodeName = GetNodeHandle()->GetNodeName();
	if (DocumentHandle->IsRequiredInput(NodeName))
	{
		return false;
	}

	if (DocumentHandle->IsRequiredOutput(NodeName))
	{
		return false;
	}

	return true;
}

FString UMetasoundEditorGraphNode::GetDocumentationLink() const
{
	return TEXT("Shared/GraphNodes/Metasound");
}

FGuid UMetasoundEditorGraphNode::GetNodeID() const
{
	return NodeID;
}

FText UMetasoundEditorGraphNode::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	using namespace Metasound::Frontend;

	FConstNodeHandle NodeHandle = GetNodeHandle();
	switch (NodeHandle->GetClassType())
	{
		case EMetasoundFrontendClassType::Input:
		{
			// TODO: have INodeController determine display name
			TArray<FConstInputHandle> InputHandles = NodeHandle->GetConstInputs();
			if (InputHandles.Num() > 0)
			{
				return FText::Format(LOCTEXT("MetasoundGraphNode_InputTitleFormat", "Input {0}"), InputHandles[0]->GetDisplayName());
			}
			return LOCTEXT("MetasoundGraphNode_DefaultInputTitle", "Input");
		}
		break;

		case EMetasoundFrontendClassType::Output:
		{
			TArray<FConstOutputHandle> OutputHandles = NodeHandle->GetConstOutputs();
			if (OutputHandles.Num() > 0)
			{
				return FText::Format(LOCTEXT("MetasoundGraphNode_OutputTitleFormat", "Output {0}"), OutputHandles[0]->GetDisplayName());
			}
			return LOCTEXT("MetasoundGraphNode_DefaultOutputTitle", "Output");
		}
		break;

		default:
		case EMetasoundFrontendClassType::External:
		case EMetasoundFrontendClassType::Graph:
		{
			return NodeHandle->GetClassDisplayName();
		}
		break;
	}
}

void UMetasoundEditorGraphNode::PinDefaultValueChanged(UEdGraphPin* Pin)
{
	using namespace Metasound::Editor;
	using namespace Metasound::Frontend;

	if (Pin && Pin->Direction == EGPD_Input)
	{
		FNodeHandle NodeHandle = GetNodeHandle();
		IMetasoundEditorModule& EditorModule = FModuleManager::GetModuleChecked<IMetasoundEditorModule>("MetasoundEditor");
		FGraphBuilder::AddOrUpdateLiteralInput(GetMetasoundChecked(), NodeHandle, *Pin);
	}
}

void UMetasoundEditorGraphNode::PrepareForCopying()
{
}

void UMetasoundEditorGraphNode::PostEditImport()
{
}

void UMetasoundEditorGraphNode::PostEditChangeProperty(FPropertyChangedEvent& InEvent)
{
	const FName PropertyName = InEvent.GetPropertyName();
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UEdGraphNode, NodePosX)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(UEdGraphNode, NodePosY))
	{
		Metasound::Frontend::FNodeHandle NodeHandle = GetNodeHandle();
		FMetasoundFrontendNodeStyle Style = NodeHandle->GetNodeStyle();
		Style.Display.Location = FVector2D(NodePosX, NodePosY);
		GetNodeHandle()->SetNodeStyle(Style);
	}

	Super::PostEditChangeProperty(InEvent);
}

void UMetasoundEditorGraphNode::PostEditChangeChainProperty(FPropertyChangedChainEvent& InEvent)
{
	Super::PostEditChangeChainProperty(InEvent);
}

void UMetasoundEditorGraphNode::PostEditUndo()
{
	UEdGraphPin::ResolveAllPinReferences();

	// This can trigger and the handle is no longer valid if transaction
	// is being undone on a graph node that is orphaned.  If orphaned,
	// bail early.
	Metasound::Frontend::FNodeHandle NodeHandle = GetNodeHandle();
	if (!NodeHandle->IsValid())
	{
		return;
	}

	for (UEdGraphPin* Pin : Pins)
	{
		if (Pin && Pin->Direction == EGPD_Input)
		{
			UObject& Metasound = GetMetasoundChecked();
			Metasound::Editor::FGraphBuilder::AddOrUpdateLiteralInput(Metasound, NodeHandle, *Pin);
		}
	}
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
		if (Context->Pin->Direction == EGPD_Input)
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
	return GetRootGraphHandle()->GetNodeWithID(NodeID)->GetClassDescription();
}

FString UMetasoundEditorGraphNode::GetDocumentationExcerptName() const
{
	// Default the node to searching for an excerpt named for the C++ node class name, including the U prefix.
	// This is done so that the excerpt name in the doc file can be found by find-in-files when searching for the full class name.
	return FString::Printf(TEXT("%s%s"), UMetasound::StaticClass()->GetPrefixCPP(), *UMetasound::StaticClass()->GetName());
}
#undef LOCTEXT_NAMESPACE
