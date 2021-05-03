// Copyright Epic Games, Inc. All Rights Reserved.
#include "MetasoundEditorGraphSchema.h"

#include "EdGraph/EdGraph.h"
#include "EdGraphNode_Comment.h"
#include "EdGraphSchema_K2.h"
#include "Editor.h"
#include "Engine/Selection.h"
#include "Framework/Commands/GenericCommands.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "GraphEditor.h"
#include "GraphEditorActions.h"
#include "GraphEditorSettings.h"
#include "HAL/IConsoleManager.h"
#include "Layout/SlateRect.h"
#include "Metasound.h"
#include "MetasoundAssetBase.h"
#include "MetasoundDataReference.h"
#include "MetasoundEditor.h"
#include "MetasoundEditorCommands.h"
#include "MetasoundEditorGraph.h"
#include "MetasoundEditorGraphBuilder.h"
#include "MetasoundEditorGraphNode.h"
#include "MetasoundEditorModule.h"
#include "MetasoundEditorSettings.h"
#include "MetasoundFrontend.h"
#include "MetasoundFrontendSearchEngine.h"
#include "ScopedTransaction.h"
#include "Toolkits/ToolkitManager.h"
#include "ToolMenus.h"
#include "UObject/NoExportTypes.h"
#include "UObject/UObjectHash.h"
#include "UObject/UObjectIterator.h"

#define LOCTEXT_NAMESPACE "MetaSoundEditor"

static int32 EnableDeprecatedMetaSoundNodeClassCreationCVar = 0;
FAutoConsoleVariableRef CVarEnableDeprecatedMetaSoundNodeClassCreation(
	TEXT("au.Debug.Editor.MetaSounds.EnableDeprecatedNodeClassCreation"),
	EnableDeprecatedMetaSoundNodeClassCreationCVar,
	TEXT("Enable creating nodes major versions of deprecated MetaSound classes in the Editor.\n")
	TEXT("0: Disabled (default), !0: Enabled"),
	ECVF_Default);

namespace Metasound
{
	namespace Editor
	{
		namespace SchemaPrivate
		{
			static const FText InputDisplayNameFormat = LOCTEXT("DisplayNameAddInputFormat", "Get {0}");
			static const FText InputTooltipFormat = LOCTEXT("TooltipAddInputFormat", "Adds a getter for the input '{0}' to the graph.");

			static const FText OutputDisplayNameFormat = LOCTEXT("DisplayNameAddOutputFormat", "Set {0}");
			static const FText OutputTooltipFormat = LOCTEXT("TooltipAddOutputFormat", "Adds a setter for the output '{0}' to the graph.");

			enum class EPrimaryContextGroup
			{
				Default = 0,
				Inputs,
				Outputs,
				External
			};

			const FText& GetContextGroupDisplayName(EPrimaryContextGroup InContextGroup)
			{
				switch (InContextGroup)
				{
					case EPrimaryContextGroup::Inputs:
					{
						static const FText InputGroupName = LOCTEXT("InputActions", "Input Actions");
						return InputGroupName;
					}
					case EPrimaryContextGroup::Outputs:
					{
						static const FText OutputGroupName = LOCTEXT("OutputActions", "Output Actions");
						return OutputGroupName;
					}

					// External nodes & any other group other than those above should not use a primary context group display name
					case EPrimaryContextGroup::Default:
					case EPrimaryContextGroup::External:
					default:
					{
						checkNoEntry();
						return FText::GetEmpty();
					}
				}
			}

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

			struct FDataTypeActionQuery
			{
				FGraphActionMenuBuilder& ActionMenuBuilder;
				const TArray<Frontend::FConstNodeHandle>& NodeHandles;
				FInterfaceNodeFilterFunction Filter;
				EPrimaryContextGroup ContextGroup;
				const FText& DisplayNameFormat;
				const FText& TooltipFormat;
				bool bShowSelectedActions = false;
			};

			template <typename TAction>
			void GetDataTypeActions(const FDataTypeActionQuery& InQuery)
			{
				using namespace Editor;
				using namespace Frontend;

				for (const FConstNodeHandle& NodeHandle : InQuery.NodeHandles)
				{
					if (!InQuery.Filter || InQuery.Filter(NodeHandle))
					{
						const FText& GroupName = GetContextGroupDisplayName(InQuery.ContextGroup);
						const FText NodeDisplayName = NodeHandle->GetDisplayName();
						const FText Tooltip = FText::Format(InQuery.TooltipFormat, NodeDisplayName);
						const FText DisplayName = FText::Format(InQuery.DisplayNameFormat, NodeDisplayName);
						TSharedPtr<TAction> NewNodeAction = MakeShared<TAction>(GroupName, DisplayName, NodeHandle->GetID(), Tooltip, static_cast<int32>(InQuery.ContextGroup));
						InQuery.ActionMenuBuilder.AddAction(NewNodeAction);
					}
				}
			}
		}
	} // namespace Editor
} // namespace Metasound

UEdGraphNode* FMetasoundGraphSchemaAction_NewNode::PerformAction(UEdGraph* ParentGraph, UEdGraphPin* FromPin, const FVector2D Location, bool bSelectNewNode /* = true */)
{
	using namespace Metasound::Editor;
	using namespace Metasound::Frontend;

	const FScopedTransaction Transaction(LOCTEXT("AddNewNode", "Add New MetaSound Node"));
	UObject& ParentMetasound = CastChecked<UMetasoundEditorGraph>(ParentGraph)->GetMetasoundChecked();
	ParentMetasound.Modify();
	ParentGraph->Modify();

	if (UMetasoundEditorGraphExternalNode* NewGraphNode = FGraphBuilder::AddExternalNode(ParentMetasound, ClassMetadata, Location, bSelectNewNode))
	{
		NewGraphNode->Modify();
		SchemaPrivate::TryConnectNewNodeToPin(*NewGraphNode, FromPin);
		return NewGraphNode;
	}

	return nullptr;
}

FMetasoundGraphSchemaAction_NewInput::FMetasoundGraphSchemaAction_NewInput(FText InNodeCategory, FText InDisplayName, FGuid InNodeID, FText InToolTip, const int32 InGrouping)
	: FEdGraphSchemaAction(MoveTemp(InNodeCategory), MoveTemp(InDisplayName), MoveTemp(InToolTip), InGrouping)
	, NodeID(InNodeID)
{
}

UEdGraphNode* FMetasoundGraphSchemaAction_NewInput::PerformAction(UEdGraph* ParentGraph, UEdGraphPin* FromPin, const FVector2D InLocation, bool bSelectNewNode /* = true */)
{
	using namespace Metasound::Editor;
	using namespace Metasound::Frontend;


	UMetasoundEditorGraph* MetasoundGraph = CastChecked<UMetasoundEditorGraph>(ParentGraph);
	UObject& ParentMetasound = MetasoundGraph->GetMetasoundChecked();

	UMetasoundEditorGraphInput* Input = MetasoundGraph->FindInput(NodeID);
	if (!ensure(Input))
	{
		return nullptr;
	}

	FNodeHandle NodeHandle = Input->GetNodeHandle();
	if (!ensure(NodeHandle->IsValid()))
	{
		return nullptr;
	}

	const FScopedTransaction Transaction(LOCTEXT("AddNewInputNode", "Add New MetaSound Input Node"));
	ParentMetasound.Modify();
	MetasoundGraph->Modify();
	Input->Modify();

	if (UMetasoundEditorGraphInputNode* NewGraphNode = FGraphBuilder::AddInputNode(ParentMetasound, NodeHandle, InLocation))
	{
		NewGraphNode->Modify();
		UEdGraphNode* EdGraphNode = CastChecked<UEdGraphNode>(NewGraphNode);
		SchemaPrivate::TryConnectNewNodeToPin(*EdGraphNode, FromPin);
		return EdGraphNode;
	}

	return nullptr;
}

FMetasoundGraphSchemaAction_PromoteToInput::FMetasoundGraphSchemaAction_PromoteToInput(FText InNodeCategory, FText InDisplayName, FText InToolTip, const int32 InGrouping)
	: FEdGraphSchemaAction(MoveTemp(InNodeCategory), MoveTemp(InDisplayName), MoveTemp(InToolTip), InGrouping)
{
}

UEdGraphNode* FMetasoundGraphSchemaAction_PromoteToInput::PerformAction(UEdGraph* ParentGraph, UEdGraphPin* FromPin, const FVector2D InLocation, bool bSelectNewNode /* = true */)
{
	using namespace Metasound::Editor;
	using namespace Metasound::Frontend;

	FInputHandle InputHandle = FGraphBuilder::GetInputHandleFromPin(FromPin);
	if (!ensure(InputHandle->IsValid()))
	{
		return nullptr;
	}

	const FScopedTransaction Transaction(LOCTEXT("PromoteNodeInputToGraphInput", "Promote MetaSound Node Input to Graph Input"));
	UMetasoundEditorGraph* MetasoundGraph = CastChecked<UMetasoundEditorGraph>(ParentGraph);
	UObject& ParentMetasound = MetasoundGraph->GetMetasoundChecked();
	ParentMetasound.Modify();

	const FText& InputName = InputHandle->GetOwningNode()->GetDisplayName();
	const FText& InputNodeName = InputHandle->GetDisplayName();
	const FString InputNameBase = FText::Format(LOCTEXT("PromoteInputNameBaseFormat", "{0} {1}"), InputName, InputNodeName).ToString();
	FString NewNodeName = FGraphBuilder::GenerateUniqueInputName(ParentMetasound, &InputNameBase);

	FMetasoundFrontendLiteral DefaultValue;
	FGraphBuilder::GetPinDefaultLiteral(*FromPin, DefaultValue);

	FNodeHandle NodeHandle = FGraphBuilder::AddInputNodeHandle(ParentMetasound, NewNodeName, InputHandle->GetDataType(), FText::GetEmpty(), false /* bIsLiteralInput */, &DefaultValue);
	if (ensure(NodeHandle->IsValid()))
	{
		UMetasoundEditorGraphInput* Input = MetasoundGraph->FindOrAddInput(NodeHandle);
		if (ensure(Input))
		{
			if (UMetasoundEditorGraphInputNode* NewGraphNode = FGraphBuilder::AddInputNode(ParentMetasound, NodeHandle, InLocation))
			{
				UEdGraphNode* EdGraphNode = CastChecked<UEdGraphNode>(NewGraphNode);

				if (ensure(SchemaPrivate::TryConnectNewNodeToPin(*EdGraphNode, FromPin)))
				{
					TSharedPtr<FEditor> MetasoundEditor = FGraphBuilder::GetEditorForGraph(*ParentGraph);
					if (MetasoundEditor.IsValid())
					{
						MetasoundEditor->OnInputNameChanged(NewGraphNode->GetNodeID());
					}

					return EdGraphNode;
				}
			}
		}
	}

	return nullptr;
}

FMetasoundGraphSchemaAction_NewOutput::FMetasoundGraphSchemaAction_NewOutput(FText InNodeCategory, FText InDisplayName, FGuid InOutputNodeID, FText InToolTip, const int32 InGrouping)
	: FEdGraphSchemaAction(MoveTemp(InNodeCategory), MoveTemp(InDisplayName), MoveTemp(InToolTip), InGrouping)
	, NodeID(InOutputNodeID)
{
}

UEdGraphNode* FMetasoundGraphSchemaAction_NewOutput::PerformAction(UEdGraph* ParentGraph, UEdGraphPin* FromPin, const FVector2D Location, bool bSelectNewNode /* = true */)
{
	using namespace Metasound::Editor;
	using namespace Metasound::Frontend;

	UMetasoundEditorGraph* MetasoundGraph = CastChecked<UMetasoundEditorGraph>(ParentGraph);
	UObject& ParentMetasound = MetasoundGraph->GetMetasoundChecked();

	UMetasoundEditorGraphOutput* Output = MetasoundGraph->FindOutput(NodeID);
	if (!ensure(Output))
	{
		return nullptr;
	}

	FNodeHandle NodeHandle = Output->GetNodeHandle();
	if (!ensure(NodeHandle->IsValid()))
	{
		return nullptr;
	}

	const FScopedTransaction Transaction(LOCTEXT("AddNewOutputNode", "Add New MetaSound Output Node"));
	ParentMetasound.Modify();
	ParentGraph->Modify();

	if (UMetasoundEditorGraphOutputNode* NewGraphNode = FGraphBuilder::AddOutputNode(ParentMetasound, NodeHandle, Location, bSelectNewNode))
	{
		SchemaPrivate::TryConnectNewNodeToPin(*NewGraphNode, FromPin);
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

	const FScopedTransaction Transaction(LOCTEXT("AddNewOutputNode", "Add Comment to MetaSound Graph"));
	ParentGraph->Modify();

	UEdGraphNode_Comment* CommentTemplate = NewObject<UEdGraphNode_Comment>();

	FSlateRect Bounds;
	FVector2D SpawnLocation = Location;
	TSharedPtr<FEditor> MetasoundEditor = FGraphBuilder::GetEditorForGraph(*ParentGraph);

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

	TSharedPtr<FEditor> MetasoundEditor = FGraphBuilder::GetEditorForGraph(*ParentGraph);
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
	using namespace Metasound::Editor;

	bool bCausesLoop = false;

	if ((nullptr != InputPin) && (nullptr != OutputPin))
	{
		UEdGraphNode* InputNode = InputPin->GetOwningNode();
		UEdGraphNode* OutputNode = OutputPin->GetOwningNode();

		// Sets bCausesLoop if the input node already has a path to the output node
		//
		FGraphBuilder::DepthFirstTraversal(InputNode, [&](UEdGraphNode* Node) -> TSet<UEdGraphNode*>
			{
				TSet<UEdGraphNode*> Children;

				if (OutputNode == Node)
				{
					// If the input node can already reach the output node, then this 
					// connection will cause a loop.
					bCausesLoop = true;
				}

				if (!bCausesLoop)
				{
					// Only produce children if no loop exists to avoid wasting unnecessary CPU
					if (nullptr != Node)
					{
						Node->ForEachNodeDirectlyConnectedToOutputs([&](UEdGraphNode* ChildNode) 
							{ 
								Children.Add(ChildNode);
							}
						);
					}
				}

				return Children;
			}
		);
	}
	
	return bCausesLoop;
}

void UMetasoundEditorGraphSchema::GetPaletteActions(FGraphActionMenuBuilder& ActionMenuBuilder) const
{
	GetCommentAction(ActionMenuBuilder);
	GetFunctionActions(ActionMenuBuilder);
}

void UMetasoundEditorGraphSchema::GetGraphContextActions(FGraphContextMenuBuilder& ContextMenuBuilder) const
{
	using namespace Metasound;
	using namespace Metasound::Editor;
	using namespace Metasound::Frontend;

	FActionClassFilters ClassFilters;
	if (const UEdGraphPin* FromPin = ContextMenuBuilder.FromPin)
	{
		if (FromPin->Direction == EGPD_Input)
		{
			FConstInputHandle InputHandle = FGraphBuilder::GetConstInputHandleFromPin(ContextMenuBuilder.FromPin);
			ClassFilters.OutputFilterFunction = [InputHandle](const FMetasoundFrontendClassOutput& InOutput)
			{
				return InOutput.TypeName == InputHandle->GetDataType();
			};

			// Show only input nodes as output nodes can only connected if FromPin is input
			FConstGraphHandle GraphHandle = InputHandle->GetOwningNode()->GetOwningGraph();
			GetDataTypeInputNodeActions(ContextMenuBuilder, GraphHandle, [InputHandle](FConstNodeHandle NodeHandle)
			{
				bool bHasOutputOfType = false;
				NodeHandle->IterateConstOutputs([&](FConstOutputHandle PotentialOutputHandle)
				{
					bHasOutputOfType |= PotentialOutputHandle->GetDataType() == InputHandle->GetDataType();
				});
				return bHasOutputOfType;
			});

			TSharedPtr<FMetasoundGraphSchemaAction_PromoteToInput> NewNodeAction = MakeShared<FMetasoundGraphSchemaAction_PromoteToInput>(
				SchemaPrivate::GetContextGroupDisplayName(SchemaPrivate::EPrimaryContextGroup::Inputs),
				LOCTEXT("PromoteToInputName", "Promote To Graph Input"),
				LOCTEXT("PromoteToInputTooltip", "Promotes node input to graph input"),
				static_cast<int32>(SchemaPrivate::EPrimaryContextGroup::Inputs));

			static_cast<FGraphActionMenuBuilder&>(ContextMenuBuilder).AddAction(NewNodeAction);
		}

		if (FromPin->Direction == EGPD_Output)
		{
			Frontend::FConstOutputHandle OutputHandle = Editor::FGraphBuilder::GetConstOutputHandleFromPin(FromPin);
			ClassFilters.InputFilterFunction = [OutputHandle](const FMetasoundFrontendClassInput& InInput)
			{
				return InInput.TypeName == OutputHandle->GetDataType();
			};

			// Show only output nodes as input nodes can only connected if FromPin is output
			FConstGraphHandle GraphHandle = OutputHandle->GetOwningNode()->GetOwningGraph();
			GetDataTypeOutputNodeActions(ContextMenuBuilder, GraphHandle, [OutputHandle](FConstNodeHandle NodeHandle)
			{
				bool bHasInputOfType = false;
				NodeHandle->IterateConstInputs([&](FConstInputHandle PotentialInputHandle)
				{
					bHasInputOfType |= PotentialInputHandle->GetDataType() == OutputHandle->GetDataType();
				});
				return bHasInputOfType;
			});
		}
	}
	else
	{
		TSharedPtr<FEditor> MetasoundEditor = FGraphBuilder::GetEditorForGraph(*ContextMenuBuilder.CurrentGraph);
		if (MetasoundEditor.IsValid() && MetasoundEditor->CanPasteNodes())
		{
			TSharedPtr<FMetasoundGraphSchemaAction_Paste> NewAction = MakeShared<FMetasoundGraphSchemaAction_Paste>(FText::GetEmpty(), LOCTEXT("PasteHereAction", "Paste here"), FText::GetEmpty(), 0);
			ContextMenuBuilder.AddAction(NewAction);
		}

		GetCommentAction(ContextMenuBuilder, ContextMenuBuilder.CurrentGraph);
		if (UObject* Metasound = MetasoundEditor->GetMetasoundObject())
		{
			FMetasoundAssetBase* MetasoundAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(Metasound);
			check(MetasoundAsset);
			FGraphHandle GraphHandle = MetasoundAsset->GetRootGraphHandle();

			GetDataTypeInputNodeActions(ContextMenuBuilder, GraphHandle);
			GetDataTypeOutputNodeActions(ContextMenuBuilder, GraphHandle);
		}
	}

	GetFunctionActions(ContextMenuBuilder, ClassFilters);
	GetConversionActions(ContextMenuBuilder, ClassFilters);
}

void UMetasoundEditorGraphSchema::GetContextMenuActions(class UToolMenu* Menu, class UGraphNodeContextMenuContext* Context) const
{
	using namespace Metasound::Editor;

	if (Context->Pin)
	{
		FToolMenuSection& Section = Menu->AddSection("MetasoundGraphSchemaPinActions", LOCTEXT("PinActionsMenuHeader", "Pin Actions"));

		// Only displays the 'Break Link' option if there is a link to break
		if (Context->Pin->LinkedTo.Num() > 0)
		{
			Section.AddMenuEntry(FGraphEditorCommands::Get().BreakPinLinks);
		}
	}
	else if (Context->Node && Context->Node->IsA<UMetasoundEditorGraphNode>())
	{
		FToolMenuSection& Section = Menu->AddSection("MetasoundGraphSchemaNodeActions", LOCTEXT("NodeActionsMenuHeader", "Node Actions"));
		Section.AddMenuEntry(FGenericCommands::Get().Delete);
		Section.AddMenuEntry(FGenericCommands::Get().Cut);
		Section.AddMenuEntry(FGenericCommands::Get().Copy);
		Section.AddMenuEntry(FGenericCommands::Get().Duplicate);
		Section.AddMenuEntry(FGraphEditorCommands::Get().BreakNodeLinks);

		// Only display update ability if node is of type external
		// and node registry is reporting a major update is available.
		if (const UMetasoundEditorGraphExternalNode* ExternalNode = Cast<UMetasoundEditorGraphExternalNode>(Context->Node))
		{
			FMetasoundFrontendVersionNumber MajorUpdateVersion = ExternalNode->GetMajorUpdateAvailable();
			if (MajorUpdateVersion.IsValid())
			{
				Section.AddMenuEntry(FEditorCommands::Get().UpdateNodes);
			}
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

	// Check if nodes being connected are in error/out-of-date version state before checking if can be connected as 
	// nodes with errors can contain invalid pins/handles).
	bool bConnectingNodesWithErrors = false;
	UEdGraphNode* InputNode = InputPin->GetOwningNode();
	if (ensure(InputNode))
	{
		if (InputNode->ErrorType == EMessageSeverity::Error)
		{
			bConnectingNodesWithErrors = true;
		}
	}
	UEdGraphNode* OutputNode = InputPin->GetOwningNode();
	if (ensure(OutputNode))
	{
		if (OutputNode->ErrorType == EMessageSeverity::Error)
		{
			bConnectingNodesWithErrors = true;
		}
	}
	if (bConnectingNodesWithErrors)
	{
		return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, LOCTEXT("ConnectionCannotContainErrorNode", "Cannot create new connections with node containing errors."));
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

	// Must mark Metasound object as modified to avoid desync issues ***before*** attempting to create a connection
	// so that transaction stack observes Frontend changes last if rolled back (i.e. undone).  UEdGraphSchema::TryCreateConnection
	// intrinsically marks the respective pin EdGraphNodes as modified.
	UEdGraphNode* PinANode = PinA->GetOwningNode();
	UMetasoundEditorGraph* Graph = CastChecked<UMetasoundEditorGraph>(PinANode->GetGraph());
	Graph->GetMetasoundChecked().Modify();

	// This call to parent takes care of marking respective nodes for modification.
	if (!UEdGraphSchema::TryCreateConnection(PinA, PinB))
	{
		return false;
	}

	if (!Metasound::Editor::FGraphBuilder::ConnectNodes(*InputPin, *OutputPin, false /* bConnectEdPins */))
	{
		return false;
	}

	return true;
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
	const EMetasoundFrontendClassType ClassType = NodeHandle->GetClassMetadata().Type;

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
	return Metasound::Editor::FGraphBuilder::GetPinCategoryColor(PinType);

}

void UMetasoundEditorGraphSchema::BreakNodeLinks(UEdGraphNode& TargetNode) const
{
	BreakNodeLinks(TargetNode, true /* bShouldActuallyTransact */);
}

void UMetasoundEditorGraphSchema::BreakNodeLinks(UEdGraphNode& TargetNode, bool bShouldActuallyTransact) const
{
	using namespace Metasound::Editor;

	const FScopedTransaction Transaction(LOCTEXT("BreakNodeLinks", "Break Node Links"), bShouldActuallyTransact);
	UMetasoundEditorGraph* Graph = CastChecked<UMetasoundEditorGraph>(TargetNode.GetGraph());
	Graph->GetMetasoundChecked().Modify();
	TargetNode.Modify();

	TArray<UEdGraphPin*> Pins = TargetNode.GetAllPins();
	for (UEdGraphPin* Pin : Pins)
	{
		FGraphBuilder::DisconnectPin(*Pin);
		Super::BreakPinLinks(*Pin, false /* bSendsNodeNotifcation */);
	}
	Super::BreakNodeLinks(TargetNode);
}

void UMetasoundEditorGraphSchema::BreakPinLinks(UEdGraphPin& TargetPin, bool bSendsNodeNotifcation) const
{
	using namespace Metasound::Editor;
	using namespace Metasound::Frontend;

	const FScopedTransaction Transaction(LOCTEXT("BreakPinLinks", "Break Pin Links"));
	UMetasoundEditorGraph* Graph = CastChecked<UMetasoundEditorGraph>(TargetPin.GetOwningNode()->GetGraph());
	Graph->GetMetasoundChecked().Modify();
	TargetPin.Modify();

	FGraphBuilder::DisconnectPin(TargetPin);
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
	const bool bIncludeDeprecated = static_cast<bool>(EnableDeprecatedMetaSoundNodeClassCreationCVar);
	const TArray<FMetasoundFrontendClass> FrontendClasses = Frontend::ISearchEngine::Get().FindAllClasses(bIncludeDeprecated);
	for (const FMetasoundFrontendClass& FrontendClass : FrontendClasses)
	{
		if (InFilters.InputFilterFunction && !FrontendClass.Interface.Inputs.ContainsByPredicate(InFilters.InputFilterFunction))
		{
			continue;
		}

		if (InFilters.OutputFilterFunction && !FrontendClass.Interface.Outputs.ContainsByPredicate(InFilters.OutputFilterFunction))
		{
			continue;
		}

		const FMetasoundFrontendClassMetadata& Metadata = FrontendClass.Metadata;
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

			NewNodeAction->ClassMetadata = Metadata;
			ActionMenuBuilder.AddAction(NewNodeAction);

		}
	}
}

void UMetasoundEditorGraphSchema::GetDataTypeInputNodeActions(FGraphContextMenuBuilder& ActionMenuBuilder, Metasound::Frontend::FConstGraphHandle InGraphHandle, Metasound::Editor::FInterfaceNodeFilterFunction InFilter, bool bShowSelectedActions) const
{
	using namespace Metasound::Editor;
	using namespace Metasound::Frontend;

	TArray<FConstNodeHandle> Inputs = InGraphHandle->GetConstInputNodes();
	for (int32 i = Inputs.Num() - 1; i >= 0; --i)
	{
		const FConstNodeHandle& NodeHandle = Inputs[i];
		if (NodeHandle->GetNodeStyle().Display.Visibility == EMetasoundFrontendNodeStyleDisplayVisibility::Hidden)
		{
			Inputs.RemoveAtSwap(i, 1, false);
		}
	}

	const SchemaPrivate::FDataTypeActionQuery ActionQuery
	{
		ActionMenuBuilder,
		Inputs,
		InFilter,
		SchemaPrivate::EPrimaryContextGroup::Inputs,
		SchemaPrivate::InputDisplayNameFormat,
		SchemaPrivate::InputTooltipFormat,
		bShowSelectedActions
	};
	SchemaPrivate::GetDataTypeActions<FMetasoundGraphSchemaAction_NewInput>(ActionQuery);
}

void UMetasoundEditorGraphSchema::GetDataTypeOutputNodeActions(FGraphContextMenuBuilder& ActionMenuBuilder, Metasound::Frontend::FConstGraphHandle InGraphHandle, Metasound::Editor::FInterfaceNodeFilterFunction InFilter, bool bShowSelectedActions) const
{
	using namespace Metasound::Editor;
	using namespace Metasound::Frontend;

	TArray<FConstNodeHandle> Outputs = InGraphHandle->GetConstOutputNodes();

	// Prune and only add actions for outputs that are not already represented in the graph
	// (as there should only be one output reference node ever to avoid confusion with which
	// is handling active input)
	if (const UMetasoundEditorGraph* Graph = Cast<UMetasoundEditorGraph>(ActionMenuBuilder.CurrentGraph))
	{
		for (int32 i = Outputs.Num() - 1; i >= 0; --i)
		{
			if (UMetasoundEditorGraphOutput* Output = Graph->FindOutput(Outputs[i]->GetID()))
			{
				if (!Output->GetNodes().IsEmpty())
				{
					Outputs.RemoveAtSwap(i, 1, false /* bAllowShrinking */);
				}
			}
		}
	}

	const SchemaPrivate::FDataTypeActionQuery ActionQuery
	{
		ActionMenuBuilder,
		Outputs,
		InFilter,
		SchemaPrivate::EPrimaryContextGroup::Outputs,
		SchemaPrivate::OutputDisplayNameFormat,
		SchemaPrivate::OutputTooltipFormat,
		bShowSelectedActions
	};
	SchemaPrivate::GetDataTypeActions<FMetasoundGraphSchemaAction_NewOutput>(ActionQuery);
}

void UMetasoundEditorGraphSchema::GetFunctionActions(FGraphActionMenuBuilder& ActionMenuBuilder, Metasound::Editor::FActionClassFilters InFilters, bool bShowSelectedActions) const
{
	using namespace Metasound::Editor;
	using namespace Metasound::Frontend;

	const FText MenuJoinFormat = LOCTEXT("MetasoundActionsFormatSubCategory", "{0}|{1}");

	const bool bIncludeDeprecated = static_cast<bool>(EnableDeprecatedMetaSoundNodeClassCreationCVar);
	const TArray<FMetasoundFrontendClass> FrontendClasses = ISearchEngine::Get().FindAllClasses(bIncludeDeprecated);
	for (const FMetasoundFrontendClass& FrontendClass : FrontendClasses)
	{
		if (InFilters.InputFilterFunction && !FrontendClass.Interface.Inputs.ContainsByPredicate(InFilters.InputFilterFunction))
		{
			continue;
		}

		if (InFilters.OutputFilterFunction && !FrontendClass.Interface.Outputs.ContainsByPredicate(InFilters.OutputFilterFunction))
		{
			continue;
		}

		const FMetasoundFrontendClassMetadata& Metadata = FrontendClass.Metadata;
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

			NewNodeAction->ClassMetadata = Metadata;
			ActionMenuBuilder.AddAction(NewNodeAction);
		}
	}
}

void UMetasoundEditorGraphSchema::GetCommentAction(FGraphActionMenuBuilder& ActionMenuBuilder, const UEdGraph* CurrentGraph) const
{
	using namespace Metasound::Editor;

	if (!ActionMenuBuilder.FromPin && CurrentGraph)
	{
		TSharedPtr<FEditor> MetasoundEditor = FGraphBuilder::GetEditorForGraph(*CurrentGraph);
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

	TSharedPtr<FEditor> MetasoundEditor = FGraphBuilder::GetEditorForGraph(*Graph);
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
