// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundEditorGraphSchema.h"

#include "EdGraph/EdGraph.h"
#include "EdGraphNode_Comment.h"
#include "Editor.h"
#include "Engine/Selection.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "GraphEditor.h"
#include "GraphEditorActions.h"
#include "GraphEditorSettings.h"
#include "Layout/SlateRect.h"
#include "Metasound.h"
#include "MetasoundEditor.h"
#include "MetasoundEditorGraph.h"
#include "MetasoundEditorGraphBuilder.h"
#include "MetasoundEditorGraphNode.h"
#include "MetasoundEditorModule.h"
#include "MetasoundFrontend.h"
#include "ScopedTransaction.h"
#include "Toolkits/ToolkitManager.h"
#include "ToolMenus.h"
#include "UObject/NoExportTypes.h"
#include "UObject/UObjectHash.h"
#include "UObject/UObjectIterator.h"
#include "../../MetasoundFrontend/Public/MetasoundAssetBase.h"

#define LOCTEXT_NAMESPACE "MetasoundEditor"


namespace Metasound
{
	namespace Editor
	{
		TSharedPtr<FEditor> GetEditorForGraph(const UObject& Metasound)
		{
			TSharedPtr<IToolkit> FoundAssetEditor = FToolkitManager::Get().FindEditorForAsset(CastChecked<const UObject>(&Metasound));
			return StaticCastSharedPtr<FEditor, IToolkit>(FoundAssetEditor);
		}

		TSharedPtr<FEditor> GetEditorForGraph(const UEdGraph& EdGraph)
		{
			const UMetasoundEditorGraph* MetasoundGraph = CastChecked<const UMetasoundEditorGraph>(&EdGraph);
			return GetEditorForGraph(MetasoundGraph->GetMetasoundChecked());
		}

		FLinearColor GetPinCategoryColor(const FEdGraphPinType& PinType)
		{
			const UGraphEditorSettings* Settings = GetDefault<UGraphEditorSettings>();
			check(Settings);

			if (PinType.PinCategory == FGraphBuilder::PinPrimitiveBoolean)
			{
				return Settings->BooleanPinTypeColor;
			}

			if (PinType.PinCategory == FGraphBuilder::PinPrimitiveFloat)
			{
				return Settings->FloatPinTypeColor;
			}

			if (PinType.PinCategory == FGraphBuilder::PinPrimitiveInteger)
			{
				return Settings->IntPinTypeColor;
			}

			if (PinType.PinCategory == FGraphBuilder::PinPrimitiveString)
			{
				return Settings->StringPinTypeColor;
			}

			return Settings->StructPinTypeColor;
		}

		FConnectionDrawingPolicy* FGraphConnectionDrawingPolicyFactory::CreateConnectionPolicy(
			const UEdGraphSchema* InSchema,
			int32 InBackLayerID,
			int32 InFrontLayerID,
			float InZoomFactor,
			const FSlateRect& InClippingRect,
			FSlateWindowElementList& InDrawElements,
			UEdGraph* InGraphObj) const
		{
			if (InSchema->IsA(UMetasoundEditorGraphSchema::StaticClass()))
			{
				return new FGraphConnectionDrawingPolicy(InBackLayerID, InFrontLayerID, InZoomFactor, InClippingRect, InDrawElements, InGraphObj);
			}
			return nullptr;
		}

		FGraphConnectionDrawingPolicy::FGraphConnectionDrawingPolicy(int32 InBackLayerID, int32 InFrontLayerID, float InZoomFactor, const FSlateRect& InClippingRect, FSlateWindowElementList& InDrawElements, UEdGraph* InGraphObj)
			: FConnectionDrawingPolicy(InBackLayerID, InFrontLayerID, InZoomFactor, InClippingRect, InDrawElements)
			, GraphObj(InGraphObj)
		{
			ActiveWireThickness = Settings->TraceAttackWireThickness;
			InactiveWireThickness = Settings->TraceReleaseWireThickness;
		}

		void FGraphConnectionDrawingPolicy::DetermineWiringStyle(UEdGraphPin* OutputPin, UEdGraphPin* InputPin, FConnectionParams& OutParams)
		{
			using namespace Frontend;

			if (!OutputPin || !InputPin || !GraphObj)
			{
				return;
			}

			OutParams.AssociatedPin1 = InputPin;
			OutParams.AssociatedPin2 = OutputPin;

			// Get the schema and grab the default color from it
			const UEdGraphSchema* Schema = GraphObj->GetSchema();
			FNodeHandle InputNodeHandle = CastChecked<UMetasoundEditorGraphNode>(InputPin->GetOwningNode())->GetNodeHandle();
			FNodeHandle OutputNodeHandle = CastChecked<UMetasoundEditorGraphNode>(OutputPin->GetOwningNode())->GetNodeHandle();

			OutParams.WireColor = GetPinCategoryColor(OutputPin->PinType);
			bool bExecuted = false;

			// Run through the predecessors, and on
			if (FExecPairingMap* PredecessorMap = PredecessorNodes.Find(OutputPin->GetOwningNode()))
			{
				if (FTimePair* Times = PredecessorMap->Find(InputPin->GetOwningNode()))
				{
					bExecuted = true;

					OutParams.WireThickness = ActiveWireThickness;
					OutParams.bDrawBubbles = true;
				}
			}

			if (!bExecuted)
			{
				OutParams.WireThickness = InactiveWireThickness;
			}
		}
	} // namespace Editor
} // namespace Metasound

UEdGraphNode* FMetasoundGraphSchemaAction_NewNode::PerformAction(UEdGraph* ParentGraph, UEdGraphPin* FromPin, const FVector2D Location, bool bSelectNewNode /* = true */)
{
	using namespace Metasound::Editor;
	using namespace Metasound::Frontend;

	const FScopedTransaction Transaction(LOCTEXT("MetasoundEditorNewNode", "Add New Metasound Node"));

	ParentGraph->Modify();

	UObject& ParentMetasound = CastChecked<UMetasoundEditorGraph>(ParentGraph)->GetMetasoundChecked();
	ParentMetasound.Modify();

	UEdGraphNode* NewGraphNode = FGraphBuilder::AddNode(ParentMetasound, Location, NodeClassInfo);

	return NewGraphNode;
}

FMetasoundGraphSchemaAction_NewInput::FMetasoundGraphSchemaAction_NewInput(FText InNodeCategory, FText InDisplayName, FName InTypeName, FText InToolTip, const int32 InGrouping)
	: FEdGraphSchemaAction(MoveTemp(InNodeCategory), MoveTemp(InDisplayName), MoveTemp(InToolTip), InGrouping)
	, NodeTypeName(InTypeName)
{
}

UEdGraphNode* FMetasoundGraphSchemaAction_NewInput::PerformAction(UEdGraph* ParentGraph, UEdGraphPin* FromPin, const FVector2D Location, bool bSelectNewNode /* = true */)
{
	using namespace Metasound::Editor;

	const FScopedTransaction Transaction(LOCTEXT("MetasoundEditorNewInput", "Add New Metasound Input"));

	ParentGraph->Modify();

	UObject& ParentMetasound = CastChecked<UMetasoundEditorGraph>(ParentGraph)->GetMetasoundChecked();
	ParentMetasound.Modify();

	FString NewNodeName = FGraphBuilder::GenerateUniqueInputName(ParentMetasound, NodeTypeName);
	UEdGraphNode* NewGraphNode = FGraphBuilder::AddInput(ParentMetasound, Location, NewNodeName, NodeTypeName, FText::GetEmpty());
	return NewGraphNode;
}

FMetasoundGraphSchemaAction_NewOutput::FMetasoundGraphSchemaAction_NewOutput(FText InNodeCategory, FText InDisplayName, FName InTypeName, FText InToolTip, const int32 InGrouping)
	: FEdGraphSchemaAction(MoveTemp(InNodeCategory), MoveTemp(InDisplayName), MoveTemp(InToolTip), InGrouping)
	, NodeTypeName(InTypeName)
{
}

UEdGraphNode* FMetasoundGraphSchemaAction_NewOutput::PerformAction(UEdGraph* ParentGraph, UEdGraphPin* FromPin, const FVector2D Location, bool bSelectNewNode /* = true */)
{
	using namespace Metasound::Editor;

	const FScopedTransaction Transaction(LOCTEXT("MetasoundEditorNewOutput", "Add New Metasound Output"));

	ParentGraph->Modify();

	UObject& ParentMetasound = CastChecked<UMetasoundEditorGraph>(ParentGraph)->GetMetasoundChecked();
	ParentMetasound.Modify();

	FString NewNodeName = FGraphBuilder::GenerateUniqueInputName(ParentMetasound, NodeTypeName);

	UEdGraphNode* NewGraphNode = FGraphBuilder::AddOutput(ParentMetasound, Location, NewNodeName, NodeTypeName, FText::GetEmpty());
	return NewGraphNode;
}

void FMetasoundGraphSchemaAction_NewNode::ConnectToSelectedNodes(UMetasoundEditorGraphNode* NewGraphNode, UEdGraph* ParentGraph) const
{
	// TODO: Implement
}

UEdGraphNode* FMetasoundGraphSchemaAction_NewFromSelected::PerformAction(UEdGraph* ParentGraph, UEdGraphPin* FromPin, const FVector2D Location, bool bSelectNewNode/* = true*/)
{
	// TODO: Implement
	return nullptr;
}

UEdGraphNode* FMetasoundGraphSchemaAction_NewComment::PerformAction(UEdGraph* ParentGraph, UEdGraphPin* FromPin, const FVector2D Location, bool bSelectNewNode/* = true*/)
{
	using namespace Metasound::Editor;

	UEdGraphNode_Comment* CommentTemplate = NewObject<UEdGraphNode_Comment>();

	FSlateRect Bounds;
	FVector2D SpawnLocation = Location;
	TSharedPtr<FEditor> MetasoundEditor = GetEditorForGraph(*ParentGraph);

	if (MetasoundEditor.IsValid() && MetasoundEditor->GetBoundsForSelectedNodes(Bounds, 50.0f))
	{
		CommentTemplate->SetBounds(Bounds);
		SpawnLocation.X = CommentTemplate->NodePosX;
		SpawnLocation.Y = CommentTemplate->NodePosY;
	}

	return FEdGraphSchemaAction_NewNode::SpawnNodeFromTemplate<UEdGraphNode_Comment>(ParentGraph, CommentTemplate, SpawnLocation);
}

UEdGraphNode* FMetasoundGraphSchemaAction_Paste::PerformAction(UEdGraph* ParentGraph, UEdGraphPin* FromPin, const FVector2D Location, bool bSelectNewNode/* = true*/)
{
	using namespace Metasound::Editor;

	TSharedPtr<FEditor> MetasoundEditor = GetEditorForGraph(*ParentGraph);
	if (MetasoundEditor.IsValid())
	{
		MetasoundEditor->PasteNodes(&Location);
	}

	return nullptr;
}

UMetasoundEditorGraphSchema::UMetasoundEditorGraphSchema(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

bool UMetasoundEditorGraphSchema::ConnectionCausesLoop(const UEdGraphPin* InputPin, const UEdGraphPin* OutputPin) const
{
	// TODO: Implement loop check
	return false;
}

void UMetasoundEditorGraphSchema::GetPaletteActions(FGraphActionMenuBuilder& ActionMenuBuilder) const
{
	GetAllMetasoundActions(ActionMenuBuilder, false /* bShowSelectedActions */);
	GetCommentAction(ActionMenuBuilder);
}

void UMetasoundEditorGraphSchema::GetGraphContextActions(FGraphContextMenuBuilder& ContextMenuBuilder) const
{
	using namespace Metasound::Editor;

	GetAllMetasoundActions(ContextMenuBuilder, true /* bShowSelectedActions */);
	GetCommentAction(ContextMenuBuilder, ContextMenuBuilder.CurrentGraph);

	if (!ContextMenuBuilder.FromPin)
	{
		TSharedPtr<FEditor> MetasoundEditor = GetEditorForGraph(*ContextMenuBuilder.CurrentGraph);
		if (MetasoundEditor.IsValid() && MetasoundEditor->CanPasteNodes())
		{
			TSharedPtr<FMetasoundGraphSchemaAction_Paste> NewAction = MakeShared<FMetasoundGraphSchemaAction_Paste>(FText::GetEmpty(), LOCTEXT("PasteHereAction", "Paste here"), FText::GetEmpty(), 0);
			ContextMenuBuilder.AddAction(NewAction);
		}
	}
}

void UMetasoundEditorGraphSchema::GetContextMenuActions(class UToolMenu* Menu, class UGraphNodeContextMenuContext* Context) const
{
	if (Context->Pin)
	{
		FToolMenuSection& Section = Menu->AddSection("MetasoundGraphSchemaPinActions", LOCTEXT("PinActionsMenuHeader", "Pin Actions"));

		// Only displays the 'Break Link' option if there is a link to break
		if (Context->Pin->LinkedTo.Num() > 0)
		{
			Section.AddMenuEntry(FGraphEditorCommands::Get().BreakPinLinks);
		}
	}
	else if (Context->Node)
	{
		const UMetasoundEditorGraphNode* SoundGraphNode = Cast<const UMetasoundEditorGraphNode>(Context->Node);
		{
			FToolMenuSection& Section = Menu->AddSection("MetasoundGraphSchemaNodeActions", LOCTEXT("NodeActionsMenuHeader", "Node Actions"));
			Section.AddMenuEntry(FGraphEditorCommands::Get().BreakNodeLinks);
		}
	}

	Super::GetContextMenuActions(Menu, Context);
}

void UMetasoundEditorGraphSchema::CreateDefaultNodesForGraph(UEdGraph& Graph) const
{
	const int32 RootNodeHeightOffset = -58;

	// Create the result node
	FGraphNodeCreator<UMetasoundEditorGraphNode> NodeCreator(Graph);
	UMetasoundEditorGraphNode* ResultRootNode = NodeCreator.CreateNode();
	ResultRootNode->NodePosY = RootNodeHeightOffset;
	NodeCreator.Finalize();
	SetNodeMetaData(ResultRootNode, FNodeMetadata::DefaultGraphNode);
}

const FPinConnectionResponse UMetasoundEditorGraphSchema::CanCreateConnection(const UEdGraphPin* PinA, const UEdGraphPin* PinB) const
{
	// Make sure the pins are not on the same node
	if (PinA->GetOwningNode() == PinB->GetOwningNode())
	{
		return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, LOCTEXT("ConnectionSameNode", "Both are on the same node"));
	}

	// Compare the directions
	const UEdGraphPin* InputPin = nullptr;
	const UEdGraphPin* OutputPin = nullptr;

	if (!CategorizePinsByDirection(PinA, PinB, InputPin, OutputPin))
	{
		return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, LOCTEXT("ConnectionIncompatible", "Directions are not compatible"));
	}

	if (ConnectionCausesLoop(InputPin, OutputPin))
	{
		return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, LOCTEXT("ConnectionLoop", "Connection would cause loop"));
	}

	if (InputPin->PinType.PinCategory != OutputPin->PinType.PinCategory)
	{
		return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, LOCTEXT("ConnectionTypeIncorrect", "Connection pin types do not match"));
	}

	UMetasoundEditorGraphNode* InputGraphNode = CastChecked<UMetasoundEditorGraphNode>(InputPin->GetOwningNode());
	Metasound::Frontend::FNodeHandle InputNodeHandle = InputGraphNode->GetNodeHandle();
	Metasound::Frontend::FInputHandle InputHandle = InputNodeHandle.GetInputWithName(InputPin->GetName());

	UMetasoundEditorGraphNode* OutputGraphNode = CastChecked<UMetasoundEditorGraphNode>(OutputPin->GetOwningNode());
	Metasound::Frontend::FNodeHandle OutputNodeHandle = OutputGraphNode->GetNodeHandle();
	Metasound::Frontend::FOutputHandle OutputHandle = OutputNodeHandle.GetOutputWithName(OutputPin->GetName());

	// TODO: Implement YesWithConverterNode to provide conversion options
	Metasound::Frontend::FConnectability Connectability = InputHandle.CanConnectTo(OutputHandle);
	if (Connectability.Connectable != Metasound::Frontend::FConnectability::EConnectable::Yes)
	{
		const FName InputType = InputHandle.GetInputType();
		const FName OutputType = OutputHandle.GetOutputType();
		return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, FText::Format(
			LOCTEXT("ConnectionTypeIncompatibleFormat", "Output pin of type '{0}' cannot be connected to input pin of type '{1}'"),
			FText::FromName(OutputType),
			FText::FromName(InputType)
		));
	}

	// Break existing connections on inputs only - multiple output connections are acceptable
	if (InputPin->LinkedTo.Num() > 0)
	{
		ECanCreateConnectionResponse ReplyBreakOutputs;
		if (InputPin == PinA)
		{
			ReplyBreakOutputs = CONNECT_RESPONSE_BREAK_OTHERS_A;
		}
		else
		{
			ReplyBreakOutputs = CONNECT_RESPONSE_BREAK_OTHERS_B;
		}
		return FPinConnectionResponse(ReplyBreakOutputs, LOCTEXT("ConnectionReplace", "Replace existing connections"));
	}

	return FPinConnectionResponse(CONNECT_RESPONSE_MAKE, TEXT(""));
}

bool UMetasoundEditorGraphSchema::TryCreateConnection(UEdGraphPin* PinA, UEdGraphPin* PinB) const
{
	UEdGraphPin* InputPin = nullptr;
	UEdGraphPin* OutputPin = nullptr;
	if (!CategorizePinsByDirection(PinA, PinB, InputPin, OutputPin))
	{
		return false;
	}

	const bool bModified = UEdGraphSchema::TryCreateConnection(PinA, PinB);
	if (bModified)
	{
		UMetasoundEditorGraphNode* InputGraphNode = CastChecked<UMetasoundEditorGraphNode>(InputPin->GetOwningNode());
		Metasound::Frontend::FNodeHandle InputNodeHandle = InputGraphNode->GetNodeHandle();
		Metasound::Frontend::FInputHandle InputHandle = InputNodeHandle.GetInputWithName(InputPin->GetName());

		UMetasoundEditorGraphNode* OutputGraphNode = CastChecked<UMetasoundEditorGraphNode>(OutputPin->GetOwningNode());
		Metasound::Frontend::FNodeHandle OutputNodeHandle = OutputGraphNode->GetNodeHandle();
		Metasound::Frontend::FOutputHandle OutputHandle = OutputNodeHandle.GetOutputWithName(OutputPin->GetName());

		// TODO: Implement YesWithConverterNode with selected conversion option
		if (!ensure(InputHandle.Connect(OutputHandle)))
		{
			InputPin->BreakLinkTo(PinB);
			return false;
		}
	}
	return bModified;
}

bool UMetasoundEditorGraphSchema::ShouldHidePinDefaultValue(UEdGraphPin* Pin) const
{
	// TODO: Determine if should be hidden from doc data
	return false;
}

FText UMetasoundEditorGraphSchema::GetPinDisplayName(const UEdGraphPin* Pin) const
{
	using namespace Metasound::Frontend;

	check(Pin);

	UMetasoundEditorGraphNode* Node = CastChecked<UMetasoundEditorGraphNode>(Pin->GetOwningNode());
	FNodeHandle NodeHandle = Node->GetNodeHandle();
	const EMetasoundClassType ClassType = NodeHandle.GetClassInfo().NodeType;

	switch (ClassType)
	{
		case EMetasoundClassType::Input:
		{
			FGraphHandle GraphHandle = Node->GetRootGraphHandle();
			return GraphHandle.GetInputDisplayName(NodeHandle.GetNodeName());
		}

		case EMetasoundClassType::Output:
		{
			FGraphHandle GraphHandle = Node->GetRootGraphHandle();
			return GraphHandle.GetOutputDisplayName(NodeHandle.GetNodeName());
		}

		case EMetasoundClassType::External:
		case EMetasoundClassType::MetasoundGraph:
		case EMetasoundClassType::Invalid:
		default:
		{
			static_assert(static_cast<int32>(EMetasoundClassType::Invalid) == 4, "Possible missing EMetasoundClassType case coverage");
			return Super::GetPinDisplayName(Pin);
		}
	}
}

FLinearColor UMetasoundEditorGraphSchema::GetPinTypeColor(const FEdGraphPinType& PinType) const
{
	return Metasound::Editor::GetPinCategoryColor(PinType);

}

void UMetasoundEditorGraphSchema::BreakNodeLinks(UEdGraphNode& TargetNode) const
{
	using namespace Metasound::Frontend;

	const FScopedTransaction Transaction(NSLOCTEXT("UnrealEd", "GraphEd_BreakPinLinks", "Break Pin Links"));

	Super::BreakNodeLinks(TargetNode);

	FNodeHandle NodeHandle = CastChecked<UMetasoundEditorGraphNode>(&TargetNode)->GetNodeHandle();
	const uint32 NodeID = NodeHandle.GetNodeID();

	FGraphHandle GraphHandle = CastChecked<UMetasoundEditorGraphNode>(&TargetNode)->GetRootGraphHandle();
	TArray<FNodeHandle> AllNodes = GraphHandle.GetAllNodes();
	for (Metasound::Frontend::FNodeHandle& IterNode : AllNodes)
	{
		if (NodeID != IterNode.GetNodeID())
		{
			TArray<FInputHandle> Inputs = IterNode.GetAllInputs();
			for (FInputHandle& Input : Inputs)
			{
				FOutputHandle Output = Input.GetCurrentlyConnectedOutput();
				if (Output.GetOwningNodeID() == NodeID)
				{
					Input.Disconnect(Output);
				}
			}
		}
	}
}

void UMetasoundEditorGraphSchema::BreakPinLinks(UEdGraphPin& TargetPin, bool bSendsNodeNotifcation) const
{
	using namespace Metasound::Frontend;

	const FScopedTransaction Transaction(NSLOCTEXT("UnrealEd", "GraphEd_BreakPinLinks", "Break Pin Links"));

	if (TargetPin.Direction == EGPD_Input)
	{
		FNodeHandle NodeHandle = CastChecked<UMetasoundEditorGraphNode>(TargetPin.GetOwningNode())->GetNodeHandle();
		FInputHandle InputHandle = NodeHandle.GetInputWithName(TargetPin.GetName());
		InputHandle.Disconnect();
	}
	else
	{
		check(TargetPin.Direction == EGPD_Output);
		for (UEdGraphPin* Pin : TargetPin.LinkedTo)
		{
			FNodeHandle NodeHandle = CastChecked<UMetasoundEditorGraphNode>(Pin->GetOwningNode())->GetNodeHandle();
			FInputHandle InputHandle = NodeHandle.GetInputWithName(Pin->GetName());
			InputHandle.Disconnect();
		}
	}

	Super::BreakPinLinks(TargetPin, bSendsNodeNotifcation);
}

void UMetasoundEditorGraphSchema::GetAssetsGraphHoverMessage(const TArray<FAssetData>& Assets, const UEdGraph* HoverGraph, FString& OutTooltipText, bool& bOutOkIcon) const
{
	bOutOkIcon = false;

	// TODO: Implement
}

void UMetasoundEditorGraphSchema::DroppedAssetsOnGraph(const TArray<FAssetData>& Assets, const FVector2D& GraphPosition, UEdGraph* Graph) const
{
	// TODO: Implement
}

void UMetasoundEditorGraphSchema::DroppedAssetsOnNode(const TArray<FAssetData>& Assets, const FVector2D& GraphPosition, UEdGraphNode* Node) const
{
	// TODO: Implement for metasound references
}

void UMetasoundEditorGraphSchema::GetAllMetasoundActions(FGraphActionMenuBuilder& ActionMenuBuilder, bool bShowSelectedActions) const
{
	using namespace Metasound::Editor;
	using namespace Metasound::Frontend;

	const FText InputMenuName = LOCTEXT("MetasoundAddInputMenu", "Add Input");
	const FText OutputMenuName = LOCTEXT("MetasoundAddOutputMenu", "Add Output");

	IMetasoundEditorModule& EditorModule = FModuleManager::GetModuleChecked<IMetasoundEditorModule>("MetasoundEditor");
	EditorModule.IterateDataTypes([InputMenuName, OutputMenuName, InMenuBuilder = &ActionMenuBuilder](const FEditorDataType& DataType)
	{
		const FName DataTypeName = DataType.RegistryInfo.DataTypeName;
		const FText DataTypeDisplayName = FText::FromString(FGraphBuilder::GetDataTypeDisplayName(DataTypeName));
		const FText DataTypeTextName = FText::FromName(DataTypeName);
		const FText MenuJoinFormat = LOCTEXT("MetasoundFormatNodeSubCategory", "{0}|{1}");

		const TArray<FString> Categories = FGraphBuilder::GetDataTypeNameCategories(DataTypeName);
		const FText CategoriesText = FText::FromString(FString::Join(Categories, TEXT("|")));


		TSharedPtr<FMetasoundGraphSchemaAction_NewInput> AddInputNodeAction = MakeShared<FMetasoundGraphSchemaAction_NewInput>
		(
			FText::Format(MenuJoinFormat, InputMenuName, CategoriesText),
			DataTypeDisplayName,
			DataTypeName,
			FText::Format(LOCTEXT("MetasoundTooltipAddInputFormat", "Adds an input of type {0} to the Metasound"), DataTypeTextName),
			0
		);
		InMenuBuilder->AddAction(AddInputNodeAction);

		TSharedPtr<FMetasoundGraphSchemaAction_NewOutput> AddOutputNodeAction = MakeShared<FMetasoundGraphSchemaAction_NewOutput>
		(
			FText::Format(MenuJoinFormat, OutputMenuName, CategoriesText),
			DataTypeDisplayName,
			DataTypeName,
			FText::Format(LOCTEXT("MetasoundTooltipAddOutputFormat", "Adds an output of type {0} to the Metasound"), DataTypeTextName),
			0
		);
		InMenuBuilder->AddAction(AddOutputNodeAction);
	});

	const FText NodeMenuName = LOCTEXT("MetasoundNodesMenu", "Add Node");
	const TArray<FNodeClassInfo> ClassInfos = GetAllAvailableNodeClasses();
	for (const FNodeClassInfo& ClassInfo : ClassInfos)
	{
		const FMetasoundClassDescription ClassDescription = GenerateClassDescription(ClassInfo);
		const FMetasoundClassMetadata Metadata = ClassDescription.Metadata;
		const FText Tooltip = Metadata.AuthorName.IsEmpty()
			? Metadata.MetasoundDescription
			: FText::Format(LOCTEXT("MetasoundTooltipAuthorFormat", "{0}\nAuthor: {1}"), Metadata.MetasoundDescription, Metadata.AuthorName);

		TSharedPtr<FMetasoundGraphSchemaAction_NewNode> NewNodeAction = MakeShared<FMetasoundGraphSchemaAction_NewNode>
		(
			NodeMenuName,
			FText::FromString(ClassInfo.NodeName),
			Tooltip,
			0
		);

		NewNodeAction->NodeClassInfo = ClassInfo;
		ActionMenuBuilder.AddAction(NewNodeAction);
	}
}

void UMetasoundEditorGraphSchema::GetCommentAction(FGraphActionMenuBuilder& ActionMenuBuilder, const UEdGraph* CurrentGraph) const
{
	using namespace Metasound::Editor;

	if (!ActionMenuBuilder.FromPin && CurrentGraph)
	{
		TSharedPtr<FEditor> MetasoundEditor = GetEditorForGraph(*CurrentGraph);
		if (MetasoundEditor.IsValid())
		{
			const int32 NumSelected = MetasoundEditor->GetNumNodesSelected();
			const FText MenuDescription = NumSelected > 0 ? LOCTEXT("CreateCommentAction", "Create Comment from Selection") : LOCTEXT("AddCommentAction", "Add Comment...");
			const FText ToolTip = LOCTEXT("CreateCommentToolTip", "Creates a comment.");

			TSharedPtr<FMetasoundGraphSchemaAction_NewComment> NewAction(new FMetasoundGraphSchemaAction_NewComment(FText::GetEmpty(), MenuDescription, ToolTip, 0));
			ActionMenuBuilder.AddAction(NewAction);
		}
	}
}

int32 UMetasoundEditorGraphSchema::GetNodeSelectionCount(const UEdGraph* Graph) const
{
	using namespace Metasound::Editor;

	TSharedPtr<FEditor> MetasoundEditor = GetEditorForGraph(*Graph);
	if (MetasoundEditor.IsValid())
	{
		return MetasoundEditor->GetNumNodesSelected();
	}

	return 0;
}

TSharedPtr<FEdGraphSchemaAction> UMetasoundEditorGraphSchema::GetCreateCommentAction() const
{
	TSharedPtr<FMetasoundGraphSchemaAction_NewComment> Comment = MakeShared<FMetasoundGraphSchemaAction_NewComment>();
	return StaticCastSharedPtr<FEdGraphSchemaAction, FMetasoundGraphSchemaAction_NewComment>(Comment);
}
#undef LOCTEXT_NAMESPACE
