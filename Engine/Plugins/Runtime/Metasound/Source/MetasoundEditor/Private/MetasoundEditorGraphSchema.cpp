// Copyright Epic Games, Inc. All Rights Reserved.
#include "MetasoundEditorGraphSchema.h"

#include "EdGraph/EdGraph.h"
#include "EdGraphNode_Comment.h"
#include "EdGraphSchema_K2.h"
#include "Editor.h"
#include "Engine/Selection.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "GraphEditor.h"
#include "GraphEditorActions.h"
#include "GraphEditorSettings.h"
#include "Layout/SlateRect.h"
#include "Metasound.h"
#include "MetasoundAssetBase.h"
#include "MetasoundDataReference.h"
#include "MetasoundEditor.h"
#include "MetasoundEditorGraph.h"
#include "MetasoundEditorGraphBuilder.h"
#include "MetasoundEditorGraphNode.h"
#include "MetasoundEditorModule.h"
#include "MetasoundEditorSettings.h"
#include "MetasoundFrontend.h"
#include "ScopedTransaction.h"
#include "Toolkits/ToolkitManager.h"
#include "ToolMenus.h"
#include "UObject/NoExportTypes.h"
#include "UObject/UObjectHash.h"
#include "UObject/UObjectIterator.h"

#define LOCTEXT_NAMESPACE "MetasoundEditor"


namespace Metasound
{
	namespace Editor
	{
		bool TryConnectNewNodeToPin(UEdGraphNode& NewGraphNode, UEdGraphPin* FromPin)
		{
			using namespace Metasound::Frontend;

			if (!FromPin)
			{
				return false;
			}

			if (FromPin->Direction == EGPD_Input)
			{
				FInputHandle InputHandle = FGraphBuilder::GetInputHandleFromPin(FromPin);
				for (UEdGraphPin* Pin : NewGraphNode.Pins)
				{
					if (Pin->Direction == EGPD_Output)
					{
						FOutputHandle OutputHandle = FGraphBuilder::GetOutputHandleFromPin(Pin);
						if (OutputHandle->IsValid() && OutputHandle->GetDataType() == InputHandle->GetDataType())
						{
							if (ensure(FGraphBuilder::ConnectNodes(*FromPin, *Pin, true /* bConnectEdPins */)))
							{
								return true;
							}
						}
					}
				}
			}

			if (FromPin->Direction == EGPD_Output)
			{
				FOutputHandle OutputHandle = FGraphBuilder::GetOutputHandleFromPin(FromPin);
				for (UEdGraphPin* Pin : NewGraphNode.Pins)
				{
					if (Pin->Direction == EGPD_Input)
					{
						FInputHandle InputHandle = FGraphBuilder::GetInputHandleFromPin(Pin);
						if (InputHandle->IsValid() && InputHandle->GetDataType() == OutputHandle->GetDataType())
						{
							if (ensure(FGraphBuilder::ConnectNodes(*Pin, *FromPin, true /* bConnectEdPins */)))
							{
								return true;
							}
						}
					}
				}
			}

			return false;
		}

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
			const UMetasoundEditorSettings* Settings = GetDefault<UMetasoundEditorSettings>();
			check(Settings);

			if (PinType.PinCategory == FGraphBuilder::PinCategoryAudioFormat)
			{
				return Settings->AudioPinTypeColor;
			}

			if (PinType.PinCategory == FGraphBuilder::PinCategoryTrigger)
			{
				return Settings->TriggerPinTypeColor;
			}

			if (PinType.PinCategory == FGraphBuilder::PinCategoryBoolean)
			{
				return Settings->BooleanPinTypeColor;
			}

			if (PinType.PinCategory == FGraphBuilder::PinCategoryFloat)
			{
				if (PinType.PinSubCategory == FGraphBuilder::PinSubCategoryTime)
				{
					return Settings->TimePinTypeColor;
				}
				return Settings->FloatPinTypeColor;
			}

			if (PinType.PinCategory == FGraphBuilder::PinCategoryInt32)
			{
				return Settings->IntPinTypeColor;
			}

			if (PinType.PinCategory == FGraphBuilder::PinCategoryInt64)
			{
				return Settings->Int64PinTypeColor;
			}

			if (PinType.PinCategory == FGraphBuilder::PinCategoryString)
			{
				return Settings->StringPinTypeColor;
			}

			if (PinType.PinCategory == FGraphBuilder::PinCategoryDouble)
			{
				return Settings->DoublePinTypeColor;
			}

			if (PinType.PinCategory == FGraphBuilder::PinCategoryObject)
			{
				return Settings->ObjectPinTypeColor;
			}

			return Settings->DefaultPinTypeColor;
		}

		template <typename TAction>
		void GetDataTypeActions(FGraphActionMenuBuilder& ActionMenuBuilder, FDataTypeFilterFunction InFilter, const FText& InCategory, const FText& InTooltipFormat, bool bShowSelectedActions)
		{
			using namespace Metasound::Editor;
			using namespace Metasound::Frontend;

			static const FText MenuJoinFormat = LOCTEXT("MetasoundActionsFormatSubCategory", "{0}|{1}");

			IMetasoundEditorModule& EditorModule = FModuleManager::GetModuleChecked<IMetasoundEditorModule>("MetasoundEditor");
			EditorModule.IterateDataTypes([&](const FEditorDataType& DataType)
			{
				if (InFilter && !InFilter(DataType))
				{
					return;
				}

				const FName DataTypeName = DataType.RegistryInfo.DataTypeName;
				const FText DataTypeDisplayName = FText::FromString(FGraphBuilder::GetDataTypeDisplayName(DataTypeName));
				const FText DataTypeTextName = FText::FromName(DataTypeName);
				const TArray<FString> Categories = FGraphBuilder::GetDataTypeNameCategories(DataTypeName);
				const FText CategoriesText = FText::FromString(FString::Join(Categories, TEXT("|")));
				TSharedPtr<TAction> NewNodeAction = MakeShared<TAction>
				(
					FText::Format(MenuJoinFormat, InCategory, CategoriesText),
					DataTypeDisplayName,
					DataTypeName,
					FText::Format(InTooltipFormat, DataTypeTextName),
					0
				);

				ActionMenuBuilder.AddAction(NewNodeAction);
			});
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
				if (InputPin->PinType.PinCategory == FGraphBuilder::PinCategoryTrigger)
				{
					OutParams.WireThickness = Settings->DefaultExecutionWireThickness;
				}
				else if (InputPin->PinType.PinCategory == FGraphBuilder::PinCategoryAudioFormat)
				{
					OutParams.bDrawBubbles = true;
				}
				else
				{
					OutParams.WireThickness = InactiveWireThickness;
				}
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

	FMetasoundFrontendNodeStyle Style;
	Style.Display.Location = Location;
	if (UEdGraphNode* NewGraphNode = FGraphBuilder::AddNode(ParentMetasound, NodeClassInfo, Style))
	{
		TryConnectNewNodeToPin(*NewGraphNode, FromPin);
		return NewGraphNode;
	}

	return nullptr;
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

	FMetasoundFrontendNodeStyle Style;
	Style.Display.Location = Location;

	if (UEdGraphNode* NewGraphNode = FGraphBuilder::AddInput(ParentMetasound, NewNodeName, NodeTypeName, Style, FText::GetEmpty()))
	{
		TryConnectNewNodeToPin(*NewGraphNode, FromPin);
		return NewGraphNode;
	}

	return nullptr;
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

	FMetasoundFrontendNodeStyle Style;
	Style.Display.Location = Location;

	if (UEdGraphNode* NewGraphNode = FGraphBuilder::AddOutput(ParentMetasound, NewNodeName, NodeTypeName, Style, FText::GetEmpty()))
	{
		TryConnectNewNodeToPin(*NewGraphNode, FromPin);
		return NewGraphNode;
	}

	return nullptr;
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
	GetCommentAction(ActionMenuBuilder);
	GetFunctionActions(ActionMenuBuilder);
	GetDataTypeInputNodeActions(ActionMenuBuilder);
	GetDataTypeOutputNodeActions(ActionMenuBuilder);
}

void UMetasoundEditorGraphSchema::GetGraphContextActions(FGraphContextMenuBuilder& ContextMenuBuilder) const
{
	using namespace Metasound;
	using namespace Metasound::Editor;

	FActionClassFilters ClassFilters;
	if (ContextMenuBuilder.FromPin)
	{
		if (ContextMenuBuilder.FromPin->Direction == EGPD_Input)
		{
			Frontend::FConstInputHandle InputHandle = Editor::FGraphBuilder::GetConstInputHandleFromPin(ContextMenuBuilder.FromPin);
			ClassFilters.OutputFilterFunction = [InputHandle](const FMetasoundFrontendClassOutput& InOutput)
			{
				return InOutput.TypeName == InputHandle->GetDataType();
			};

			// Show only input nodes as output nodes can only connected if FromPin is input
			GetDataTypeInputNodeActions(ContextMenuBuilder, [InputHandle](const Editor::FEditorDataType& InDataType)
			{
				return InDataType.RegistryInfo.DataTypeName == InputHandle->GetDataType();
			});
		}

		if (ContextMenuBuilder.FromPin->Direction == EGPD_Output)
		{
			Frontend::FConstOutputHandle OutputHandle = Editor::FGraphBuilder::GetConstOutputHandleFromPin(ContextMenuBuilder.FromPin);
			ClassFilters.InputFilterFunction = [OutputHandle](const FMetasoundFrontendClassInput& InInput)
			{
				return InInput.TypeName == OutputHandle->GetDataType();
			};

			FDataTypeFilterFunction DataTypeFilter = [OutputHandle](const Editor::FEditorDataType& InDataType)
			{
				return InDataType.RegistryInfo.DataTypeName == OutputHandle->GetDataType();
			};

			// Show only output nodes as input nodes can only connected if FromPin is output
			GetDataTypeOutputNodeActions(ContextMenuBuilder, DataTypeFilter);
		}
	}
	else
	{
		TSharedPtr<Editor::FEditor> MetasoundEditor = Editor::GetEditorForGraph(*ContextMenuBuilder.CurrentGraph);
		if (MetasoundEditor.IsValid() && MetasoundEditor->CanPasteNodes())
		{
			TSharedPtr<FMetasoundGraphSchemaAction_Paste> NewAction = MakeShared<FMetasoundGraphSchemaAction_Paste>(FText::GetEmpty(), LOCTEXT("PasteHereAction", "Paste here"), FText::GetEmpty(), 0);
			ContextMenuBuilder.AddAction(NewAction);
		}

		GetCommentAction(ContextMenuBuilder, ContextMenuBuilder.CurrentGraph);
		GetDataTypeInputNodeActions(ContextMenuBuilder);
		GetDataTypeOutputNodeActions(ContextMenuBuilder);
	}

	GetFunctionActions(ContextMenuBuilder, ClassFilters);
	GetConversionActions(ContextMenuBuilder, ClassFilters);
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
	using namespace Metasound;

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

	Frontend::FInputHandle InputHandle = Editor::FGraphBuilder::GetInputHandleFromPin(InputPin);
	Frontend::FOutputHandle OutputHandle = Editor::FGraphBuilder::GetOutputHandleFromPin(OutputPin);

	if (InputHandle->IsValid() && OutputHandle->IsValid())
	{
		// TODO: Implement YesWithConverterNode to provide conversion options
		Frontend::FConnectability Connectability = InputHandle->CanConnectTo(*OutputHandle);
		if (Connectability.Connectable != Frontend::FConnectability::EConnectable::Yes)
		{
			const FName InputType = InputHandle->GetDataType();
			const FName OutputType = OutputHandle->GetDataType();
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

	return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, LOCTEXT("ConnectionInternalError", "Internal error. Metasound node vertex handle mismatch."));
}

bool UMetasoundEditorGraphSchema::TryCreateConnection(UEdGraphPin* PinA, UEdGraphPin* PinB) const
{
	using namespace Metasound::Frontend;

	if (!ensure(PinA && PinB))
	{
		return false;
	}

	UEdGraphPin* InputPin = nullptr;
	UEdGraphPin* OutputPin = nullptr;
	if (!CategorizePinsByDirection(PinA, PinB, InputPin, OutputPin))
	{
		return false;
	}

	if (!ensure(InputPin && OutputPin))
	{
		return false;
	}

	// TODO: Implement YesWithConverterNode with selected conversion option

	bool bModified = UEdGraphSchema::TryCreateConnection(PinA, PinB);
	if (bModified)
	{
		bModified = Metasound::Editor::FGraphBuilder::ConnectNodes(*InputPin, *OutputPin, false);
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
	const EMetasoundFrontendClassType ClassType = NodeHandle->GetClassType();

	switch (ClassType)
	{
		case EMetasoundFrontendClassType::Input:
		{
			TArray<FOutputHandle> OutputHandles = NodeHandle->GetOutputsWithVertexName(Pin->GetName());
			if (ensure(OutputHandles.Num() > 0))
			{
				return OutputHandles[0]->GetDisplayName();
			}
			else
			{
				return Super::GetPinDisplayName(Pin);
			}
		}

		case EMetasoundFrontendClassType::Output:
		{
			TArray<FInputHandle> InputHandles = NodeHandle->GetInputsWithVertexName(Pin->GetName());
			if (ensure(InputHandles.Num() > 0))
			{
				return InputHandles[0]->GetDisplayName();
			}
			else
			{
				return Super::GetPinDisplayName(Pin);
			}
		}

		case EMetasoundFrontendClassType::External:
		case EMetasoundFrontendClassType::Graph:
		case EMetasoundFrontendClassType::Invalid:
		default:
		{
			static_assert(static_cast<int32>(EMetasoundFrontendClassType::Invalid) == 4, "Possible missing EMetasoundFrontendClassType case coverage");
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

	const FScopedTransaction Transaction(NSLOCTEXT("UnrealEd", "GraphEd_BreakNodeLinks", "Break Node Links"));

	TArray<UEdGraphPin*> Pins = TargetNode.GetAllPins();
	for (UEdGraphPin* Pin : Pins)
	{
		BreakPinLinks(*Pin, false);
	}

	Super::BreakNodeLinks(TargetNode);
}

void UMetasoundEditorGraphSchema::BreakPinLinks(UEdGraphPin& TargetPin, bool bSendsNodeNotifcation) const
{
	using namespace Metasound::Editor;
	using namespace Metasound::Frontend;

	const FScopedTransaction Transaction(NSLOCTEXT("UnrealEd", "GraphEd_BreakPinLinks", "Break Pin Links"));

	TArray<FInputHandle> InputHandles;
	TArray<UEdGraphPin*> InputPins;

	UObject& Metasound = CastChecked<UMetasoundEditorGraphNode>(TargetPin.GetOwningNode())->GetMetasoundChecked();

	if (TargetPin.Direction == EGPD_Input)
	{
		FNodeHandle NodeHandle = CastChecked<UMetasoundEditorGraphNode>(TargetPin.GetOwningNode())->GetNodeHandle();
		InputHandles = NodeHandle->GetInputsWithVertexName(TargetPin.GetName());
		InputPins.Add(&TargetPin);
	}
	else
	{
		check(TargetPin.Direction == EGPD_Output);
		for (UEdGraphPin* Pin : TargetPin.LinkedTo)
		{
			FNodeHandle NodeHandle = CastChecked<UMetasoundEditorGraphNode>(Pin->GetOwningNode())->GetNodeHandle();
			InputHandles.Append(NodeHandle->GetInputsWithVertexName(Pin->GetName()));
			InputPins.Add(Pin);
		}
	}

	IMetasoundEditorModule& EditorModule = FModuleManager::GetModuleChecked<IMetasoundEditorModule>("MetasoundEditor");
	for (int32 i = 0; i < InputHandles.Num(); ++i)
	{
		FInputHandle Handle = InputHandles[i];
		Handle->Disconnect();

		FNodeHandle NodeHandle = Handle->GetOwningNode();
		FGraphBuilder::AddOrUpdateLiteralInput(Metasound, NodeHandle, *InputPins[i]);
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

void UMetasoundEditorGraphSchema::GetConversionActions(FGraphActionMenuBuilder& ActionMenuBuilder, Metasound::Editor::FActionClassFilters InFilters, bool bShowSelectedActions) const
{
	using namespace Metasound;

	const FText MenuJoinFormat = LOCTEXT("MetasoundActionsFormatSubCategory", "{0}|{1}");
	const TArray<Frontend::FNodeClassInfo> ClassInfos = Frontend::GetAllAvailableNodeClasses();
	for (const Frontend::FNodeClassInfo& ClassInfo : ClassInfos)
	{
		const FMetasoundFrontendClass ClassDescription = Frontend::GenerateClassDescription(ClassInfo);
		if (InFilters.InputFilterFunction && !ClassDescription.Interface.Inputs.ContainsByPredicate(InFilters.InputFilterFunction))
		{
			continue;
		}

		if (InFilters.OutputFilterFunction && !ClassDescription.Interface.Outputs.ContainsByPredicate(InFilters.OutputFilterFunction))
		{
			continue;
		}

		const FMetasoundFrontendClassMetadata Metadata = ClassDescription.Metadata;
		const FText Tooltip = Metadata.Author.IsEmpty()
			? Metadata.Description
			: FText::Format(LOCTEXT("MetasoundTooltipAuthorFormat", "{0}\nAuthor: {1}"), Metadata.Description, Metadata.Author);

		if (!Metadata.CategoryHierarchy.IsEmpty() && !Metadata.CategoryHierarchy[0].CompareTo(Editor::FGraphBuilder::ConvertMenuName))
		{
			TSharedPtr<FMetasoundGraphSchemaAction_NewNode> NewNodeAction = MakeShared<FMetasoundGraphSchemaAction_NewNode>
			(
				Editor::FGraphBuilder::ConvertMenuName,
				Metadata.DisplayName,
				Tooltip,
				0
			);

			NewNodeAction->NodeClassInfo = ClassInfo;
			ActionMenuBuilder.AddAction(NewNodeAction);

		}
	}
}

void UMetasoundEditorGraphSchema::GetDataTypeInputNodeActions(FGraphActionMenuBuilder& ActionMenuBuilder, Metasound::Editor::FDataTypeFilterFunction InFilter, bool bShowSelectedActions) const
{
	const FText InputMenuName = LOCTEXT("MetasoundActionsInputsMenu", "Inputs");
	const FText InputTooltipFormat = LOCTEXT("MetasoundTooltipAddInputFormat", "Adds an input of type {0} to the Metasound");
	Metasound::Editor::GetDataTypeActions<FMetasoundGraphSchemaAction_NewInput>(ActionMenuBuilder, InFilter, InputMenuName, InputTooltipFormat, bShowSelectedActions);
}

void UMetasoundEditorGraphSchema::GetDataTypeOutputNodeActions(FGraphActionMenuBuilder& ActionMenuBuilder, Metasound::Editor::FDataTypeFilterFunction InFilter, bool bShowSelectedActions) const
{
	const FText OutputMenuName = LOCTEXT("MetasoundActionsOutputsMenu", "Outputs");
	const FText OutputTooltipFormat = LOCTEXT("MetasoundTooltipAddOutputFormat", "Adds an output of type {0} to the Metasound");
	Metasound::Editor::GetDataTypeActions<FMetasoundGraphSchemaAction_NewOutput>(ActionMenuBuilder, InFilter, OutputMenuName, OutputTooltipFormat, bShowSelectedActions);
}

void UMetasoundEditorGraphSchema::GetFunctionActions(FGraphActionMenuBuilder& ActionMenuBuilder, Metasound::Editor::FActionClassFilters InFilters, bool bShowSelectedActions) const
{
	using namespace Metasound::Editor;
	using namespace Metasound::Frontend;

	const FText MenuJoinFormat = LOCTEXT("MetasoundActionsFormatSubCategory", "{0}|{1}");

	const TArray<FNodeClassInfo> ClassInfos = GetAllAvailableNodeClasses();
	for (const FNodeClassInfo& ClassInfo : ClassInfos)
	{
		const FMetasoundFrontendClass ClassDescription = GenerateClassDescription(ClassInfo);
		if (InFilters.InputFilterFunction && !ClassDescription.Interface.Inputs.ContainsByPredicate(InFilters.InputFilterFunction))
		{
			continue;
		}

		if (InFilters.OutputFilterFunction && !ClassDescription.Interface.Outputs.ContainsByPredicate(InFilters.OutputFilterFunction))
		{
			continue;
		}

		const FMetasoundFrontendClassMetadata Metadata = ClassDescription.Metadata;
		const FText Tooltip = Metadata.Author.IsEmpty()
			? Metadata.Description
			: FText::Format(LOCTEXT("MetasoundTooltipAuthorFormat", "{0}\nAuthor: {1}"), Metadata.Description, Metadata.Author);

		if (Metadata.CategoryHierarchy.IsEmpty() || Metadata.CategoryHierarchy[0].CompareTo(FGraphBuilder::ConvertMenuName))
		{
			const FText CategoriesText = FText::Join(LOCTEXT("MetasoundActionsCategoryDelim", "|"), Metadata.CategoryHierarchy);
			TSharedPtr<FMetasoundGraphSchemaAction_NewNode> NewNodeAction = MakeShared<FMetasoundGraphSchemaAction_NewNode>
			(
				FText::Format(MenuJoinFormat, FGraphBuilder::FunctionMenuName, CategoriesText),
				Metadata.DisplayName,
				Tooltip,
				0
			);

			NewNodeAction->NodeClassInfo = ClassInfo;
			ActionMenuBuilder.AddAction(NewNodeAction);
		}
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
