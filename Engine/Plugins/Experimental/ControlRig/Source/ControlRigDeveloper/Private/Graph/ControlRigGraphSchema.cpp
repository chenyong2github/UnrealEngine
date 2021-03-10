// Copyright Epic Games, Inc. All Rights Reserved.

#include "Graph/ControlRigGraphSchema.h"
#include "Graph/ControlRigGraph.h"
#include "Graph/ControlRigGraphNode.h"
#include "IControlRigEditorModule.h"
#include "UObject/UObjectIterator.h"
#include "Units/RigUnit.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "EdGraphNode_Comment.h"
#include "EdGraphSchema_K2_Actions.h"
#include "ScopedTransaction.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "GraphEditorActions.h"
#include "ControlRig.h"
#include "ControlRigBlueprint.h"
#include "ControlRigBlueprintGeneratedClass.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Framework/Notifications/NotificationManager.h"
#include "EulerTransform.h"
#include "Curves/CurveFloat.h"
#include "RigVMModel/Nodes/RigVMLibraryNode.h"
#include "RigVMModel/Nodes/RigVMCollapseNode.h"
#include "RigVMModel/Nodes/RigVMFunctionEntryNode.h"
#include "RigVMModel/Nodes/RigVMFunctionReturnNode.h"

#if WITH_EDITOR
#include "ControlRigEditor/Private/Editor/SControlRigFunctionLocalizationWidget.h"
#endif

#define LOCTEXT_NAMESPACE "ControlRigGraphSchema"

const FName UControlRigGraphSchema::GraphName_ControlRig(TEXT("Rig Graph"));

FReply FControlRigFunctionDragDropAction::DroppedOnPanel(const TSharedRef< class SWidget >& Panel, FVector2D ScreenPosition, FVector2D GraphPosition, UEdGraph& Graph)
{
	if (UControlRigGraph* TargetRigGraph = Cast<UControlRigGraph>(&Graph))
	{
		if (UControlRigBlueprint* TargetRigBlueprint = Cast<UControlRigBlueprint>(FBlueprintEditorUtils::FindBlueprintForGraph(TargetRigGraph)))
		{
			if (URigVMGraph* FunctionDefinitionGraph = SourceRigBlueprint->GetModel(SourceRigGraph))
			{
				if (URigVMLibraryNode* FunctionDefinitionNode = Cast<URigVMLibraryNode>(FunctionDefinitionGraph->GetOuter()))
				{
					if(URigVMController* TargetController = TargetRigBlueprint->GetController(TargetRigGraph))
					{
						if(URigVMFunctionLibrary* FunctionLibrary = Cast<URigVMFunctionLibrary>(FunctionDefinitionNode->GetOuter()))
						{
							if(UControlRigBlueprint* FunctionRigBlueprint = Cast<UControlRigBlueprint>(FunctionLibrary->GetOuter()))
							{
#if WITH_EDITOR
								if(FunctionRigBlueprint != TargetRigBlueprint)
								{
									if(!FunctionRigBlueprint->IsFunctionPublic(FunctionDefinitionNode->GetFName()))
									{
										TargetRigBlueprint->BroadcastRequestLocalizeFunctionDialog(FunctionDefinitionNode);
										FunctionDefinitionNode = TargetRigBlueprint->GetLocalFunctionLibrary()->FindPreviouslyLocalizedFunction(FunctionDefinitionNode);
									}
								}
#endif
								TargetController->AddFunctionReferenceNode(FunctionDefinitionNode, GraphPosition);
							}
						}
					}
				}
			}
		}
	}
	return FReply::Unhandled();
}

FReply FControlRigFunctionDragDropAction::DroppedOnPin(FVector2D ScreenPosition, FVector2D GraphPosition)
{
	return FReply::Unhandled();
}

FReply FControlRigFunctionDragDropAction::DroppedOnAction(TSharedRef<FEdGraphSchemaAction> Action)
{
	return FReply::Unhandled();
}

FReply FControlRigFunctionDragDropAction::DroppedOnCategory(FText Category)
{
	// todo
	/*
	if (SourceAction.IsValid())
	{
		SourceAction->MovePersistentItemToCategory(Category);
	}
	*/
	return FReply::Unhandled();
}

void FControlRigFunctionDragDropAction::HoverTargetChanged()
{
	// todo - see FMyBlueprintItemDragDropAction
	FGraphSchemaActionDragDropAction::HoverTargetChanged();

	// check for category + graph, everything else we won't allow for now.

	bDropTargetValid = true;
}

FControlRigFunctionDragDropAction::FControlRigFunctionDragDropAction()
	: FGraphSchemaActionDragDropAction()
	, SourceRigBlueprint(nullptr)
	, SourceRigGraph(nullptr)
	, bControlDrag(false)
	, bAltDrag(false)
{
}

TSharedRef<FControlRigFunctionDragDropAction> FControlRigFunctionDragDropAction::New(TSharedPtr<FEdGraphSchemaAction> InAction, UControlRigBlueprint* InRigBlueprint, UControlRigGraph* InRigGraph)
{
	TSharedRef<FControlRigFunctionDragDropAction> Action = MakeShareable(new FControlRigFunctionDragDropAction);
	Action->SourceAction = InAction;
	Action->SourceRigBlueprint = InRigBlueprint;
	Action->SourceRigGraph = InRigGraph;
	Action->Construct();
	return Action;
}

UControlRigGraphSchema::UControlRigGraphSchema()
{
}

void UControlRigGraphSchema::GetGraphContextActions(FGraphContextMenuBuilder& ContextMenuBuilder) const
{

}

void UControlRigGraphSchema::GetContextMenuActions(class UToolMenu* Menu, class UGraphNodeContextMenuContext* Context) const
{
	/*
	// this seems to be taken care of by ControlRigGraphNode
#if WITH_EDITOR
	return IControlRigEditorModule::Get().GetContextMenuActions(this, Menu, Context);
#else
	check(0);
#endif
	*/
}

bool UControlRigGraphSchema::TryCreateConnection(UEdGraphPin* PinA, UEdGraphPin* PinB) const
{
#if WITH_EDITOR
	if (GEditor)
	{
		GEditor->CancelTransaction(0);
	}
#endif

	if (PinA == PinB)
	{
		return false;
	}

	if (PinA->GetOwningNode() == PinB->GetOwningNode())
	{
		return false;
	}

	UControlRigGraphSchema* MutableThis = (UControlRigGraphSchema*)this;
	MutableThis->LastPinForCompatibleCheck = nullptr;

	UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForNodeChecked(PinA->GetOwningNode());
	UControlRigBlueprint* RigBlueprint = Cast<UControlRigBlueprint>(Blueprint);
	if (RigBlueprint != nullptr)
	{
		if (URigVMController* Controller = RigBlueprint->GetOrCreateController(PinA->GetOwningNode()->GetGraph()))
		{
			if (PinA->Direction == EGPD_Input)
			{
				UEdGraphPin* Temp = PinA;
				PinA = PinB;
				PinB = Temp;
			}
			return Controller->AddLink(PinA->GetName(), PinB->GetName());
		}
	}
	return false;
}

static bool HasParentConnection_Recursive(const UEdGraphPin* InPin)
{
	if(InPin->ParentPin)
	{
		return InPin->ParentPin->LinkedTo.Num() > 0 || HasParentConnection_Recursive(InPin->ParentPin);
	}

	return false;
}

static bool HasChildConnection_Recursive(const UEdGraphPin* InPin)
{
	for(const UEdGraphPin* SubPin : InPin->SubPins)
	{
		if(SubPin->LinkedTo.Num() > 0 || HasChildConnection_Recursive(SubPin))
		{
			return true;
		}
	}

	return false;
}

const FPinConnectionResponse UControlRigGraphSchema::CanCreateConnection(const UEdGraphPin* A, const UEdGraphPin* B) const
{
	UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForNodeChecked(A->GetOwningNode());
	UControlRigBlueprint* RigBlueprint = Cast<UControlRigBlueprint>(Blueprint);
	if (RigBlueprint != nullptr)
	{
		UControlRigGraphNode* RigNodeA = Cast<UControlRigGraphNode>(A->GetOwningNode());
		UControlRigGraphNode* RigNodeB = Cast<UControlRigGraphNode>(B->GetOwningNode());

		if (RigNodeA && RigNodeB && RigNodeA != RigNodeB)
		{
			URigVMPin* PinA = RigNodeA->GetModelPinFromPinPath(A->GetName());
			if (PinA)
			{
				PinA = PinA->GetPinForLink();
				RigNodeA->GetModel()->PrepareCycleChecking(PinA, A->Direction == EGPD_Input);
			}

			URigVMPin* PinB = RigNodeB->GetModelPinFromPinPath(B->GetName());
			if (PinB)
			{
				PinB = PinB->GetPinForLink();
			}

			if (A->Direction == EGPD_Input)
			{
				URigVMPin* Temp = PinA;
				PinA = PinB;
				PinB = Temp;
			}

			const FRigVMByteCode* ByteCode = RigNodeA->GetController()->GetCurrentByteCode();

			FString FailureReason;
			bool bResult = RigNodeA->GetModel()->CanLink(PinA, PinB, &FailureReason, ByteCode);
			if (!bResult)
			{
				return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, FText::FromString(FailureReason));
			}
			return FPinConnectionResponse(CONNECT_RESPONSE_MAKE, LOCTEXT("ConnectResponse_Allowed", "Connect"));
		}
	}

	return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, LOCTEXT("ConnectResponse_Disallowed_Unexpected", "Unexpected error"));
}

FLinearColor UControlRigGraphSchema::GetPinTypeColor(const FEdGraphPinType& PinType) const
{
	const FName& TypeName = PinType.PinCategory;
	if (TypeName == UEdGraphSchema_K2::PC_Struct)
	{
		if (UStruct* Struct = Cast<UStruct>(PinType.PinSubCategoryObject))
		{
			if (Struct->IsChildOf(FRigVMExecuteContext::StaticStruct()))
			{
				return FLinearColor::White;
			}
		}
	}
	return GetDefault<UEdGraphSchema_K2>()->GetPinTypeColor(PinType);
}

void UControlRigGraphSchema::BreakPinLinks(UEdGraphPin& TargetPin, bool bSendsNodeNotifcation) const
{
	//const FScopedTransaction Transaction( LOCTEXT("GraphEd_BreakPinLinks", "Break Pin Links") );

	// cache this here, as BreakPinLinks can trigger a node reconstruction invalidating the TargetPin referenceS
	if (UControlRigGraphNode* Node = Cast< UControlRigGraphNode>(TargetPin.GetOwningNode()))
	{
		Node->GetController()->BreakAllLinks(TargetPin.GetName(), TargetPin.Direction == EGPD_Input);
	}
}

void UControlRigGraphSchema::BreakSinglePinLink(UEdGraphPin* SourcePin, UEdGraphPin* TargetPin) const
{
	//const FScopedTransaction Transaction(LOCTEXT("GraphEd_BreakSinglePinLink", "Break Pin Link") );

	if (UControlRigGraphNode* Node = Cast< UControlRigGraphNode>(TargetPin->GetOwningNode()))
	{
		if (SourcePin->Direction == EGPD_Input)
		{
			UEdGraphPin* Temp = TargetPin;
			TargetPin = SourcePin;
			SourcePin = Temp;
		}
		
		Node->GetController()->BreakLink(SourcePin->GetName(), TargetPin->GetName());
	}
}

bool UControlRigGraphSchema::CanGraphBeDropped(TSharedPtr<FEdGraphSchemaAction> InAction) const
{
	if (!InAction.IsValid())
	{
		return false;
	}

	if (InAction->GetTypeId() == FEdGraphSchemaAction_K2Graph::StaticGetTypeId())
	{
		FEdGraphSchemaAction_K2Graph* FuncAction = (FEdGraphSchemaAction_K2Graph*)InAction.Get();
		if (UControlRigGraph* RigGraph = Cast<UControlRigGraph>((UEdGraph*)FuncAction->EdGraph))
		{
			return true;
		}
	}
	
	return false;
}

FReply UControlRigGraphSchema::BeginGraphDragAction(TSharedPtr<FEdGraphSchemaAction> InAction) const
{
	if (!InAction.IsValid())
	{
		return FReply::Unhandled();
	}

	if (InAction->GetTypeId() == FEdGraphSchemaAction_K2Graph::StaticGetTypeId())
	{
		FEdGraphSchemaAction_K2Graph* FuncAction = (FEdGraphSchemaAction_K2Graph*)InAction.Get();
if (UControlRigGraph* RigGraph = Cast<UControlRigGraph>(FuncAction->EdGraph))
{
	if (UControlRigBlueprint* RigBlueprint = Cast<UControlRigBlueprint>(FBlueprintEditorUtils::FindBlueprintForGraph(RigGraph)))
	{
		return FReply::Handled().BeginDragDrop(FControlRigFunctionDragDropAction::New(InAction, RigBlueprint, RigGraph));
	}
}
	}
	return FReply::Unhandled();
}

FConnectionDrawingPolicy* UControlRigGraphSchema::CreateConnectionDrawingPolicy(int32 InBackLayerID, int32 InFrontLayerID, float InZoomFactor, const FSlateRect& InClippingRect, class FSlateWindowElementList& InDrawElements, class UEdGraph* InGraphObj) const
{
#if WITH_EDITOR
	return IControlRigEditorModule::Get().CreateConnectionDrawingPolicy(InBackLayerID, InFrontLayerID, InZoomFactor, InClippingRect, InDrawElements, InGraphObj);
#else
	check(0);
	return nullptr;
#endif
}

bool UControlRigGraphSchema::ShouldHidePinDefaultValue(UEdGraphPin* Pin) const
{
	// we should hide default values if any of our parents are connected
	return HasParentConnection_Recursive(Pin);
}

bool UControlRigGraphSchema::IsPinBeingWatched(UEdGraphPin const* Pin) const
{
	if (UControlRigGraphNode* Node = Cast< UControlRigGraphNode>(Pin->GetOwningNode()))
	{
		if (URigVMPin* ModelPin = Node->GetModel()->FindPin(Pin->GetName()))
		{
			return ModelPin->RequiresWatch();
		}
	}
	return false;
}

void UControlRigGraphSchema::ClearPinWatch(UEdGraphPin const* Pin) const
{
	if (UControlRigGraphNode* Node = Cast< UControlRigGraphNode>(Pin->GetOwningNode()))
	{
		Node->GetController()->SetPinIsWatched(Pin->GetName(), false);
	}
}

void UControlRigGraphSchema::OnPinConnectionDoubleCicked(UEdGraphPin* PinA, UEdGraphPin* PinB, const FVector2D& GraphPosition) const
{
	if (UControlRigGraphNode* Node = Cast< UControlRigGraphNode>(PinA->GetOwningNode()))
	{
		if (URigVMLink* Link = Node->GetModel()->FindLink(FString::Printf(TEXT("%s -> %s"), *PinA->GetName(), *PinB->GetName())))
		{
			Node->GetController()->AddRerouteNodeOnLink(Link, false, GraphPosition);
		}
	}
}

bool UControlRigGraphSchema::MarkBlueprintDirtyFromNewNode(UBlueprint* InBlueprint, UEdGraphNode* InEdGraphNode) const
{
	if (InBlueprint == nullptr || InEdGraphNode == nullptr)
	{
		return false;
	}
	return true;
}

bool UControlRigGraphSchema::IsStructEditable(UStruct* InStruct) const
{
	if (InStruct == FRuntimeFloatCurve::StaticStruct())
	{
		return true;
	}
	return false;
}

void UControlRigGraphSchema::SetNodePosition(UEdGraphNode* Node, const FVector2D& Position) const
{
	if (UControlRigGraphNode* RigNode = Cast<UControlRigGraphNode>(Node))
	{
		RigNode->GetController()->SetNodePosition(RigNode->GetModelNode(), Position, true, false);
	}
}

void UControlRigGraphSchema::GetGraphDisplayInformation(const UEdGraph& Graph, /*out*/ FGraphDisplayInfo& DisplayInfo) const
{
	Super::GetGraphDisplayInformation(Graph, DisplayInfo);

	if (UControlRigGraph* RigGraph = Cast<UControlRigGraph>((UEdGraph*)&Graph))
	{
		TArray<FString> NodePathParts;
		if (URigVMNode::SplitNodePath(RigGraph->ModelNodePath, NodePathParts))
		{
			DisplayInfo.DisplayName = FText::FromString(NodePathParts.Last());
			DisplayInfo.PlainName = DisplayInfo.DisplayName;
		}
	}
}

FText UControlRigGraphSchema::GetGraphCategory(const UEdGraph* InGraph) const
{
	if (UControlRigGraph* RigGraph = Cast<UControlRigGraph>((UEdGraph*)InGraph))
	{
		if (URigVMGraph* Model = RigGraph->GetModel())
		{
			if (URigVMCollapseNode* CollapseNode = Cast<URigVMCollapseNode>(Model->GetOuter()))
			{
				return FText::FromString(CollapseNode->GetNodeCategory());
			}
		}
	}
	return FText();
}

FReply UControlRigGraphSchema::TrySetGraphCategory(const UEdGraph* InGraph, const FText& InCategory)
{
	if (UControlRigGraph* RigGraph = Cast<UControlRigGraph>((UEdGraph*)InGraph))
	{
		if (UControlRigBlueprint* RigBlueprint = Cast<UControlRigBlueprint>(FBlueprintEditorUtils::FindBlueprintForGraph(RigGraph)))
		{
			if (URigVMGraph* Model = RigGraph->GetModel())
			{
				if (URigVMCollapseNode* CollapseNode = Cast<URigVMCollapseNode>(Model->GetOuter()))
				{
					if (URigVMController* Controller = RigBlueprint->GetOrCreateController(CollapseNode->GetGraph()))
					{
						if (Controller->SetNodeCategory(CollapseNode, InCategory.ToString()))
						{
							return FReply::Handled();
						}
					}
				}
			}
		}
	}
	return FReply::Unhandled();
}

bool UControlRigGraphSchema::TryDeleteGraph(UEdGraph* GraphToDelete) const
{
	if (UControlRigGraph* RigGraph = Cast<UControlRigGraph>(GraphToDelete))
	{
		if (UControlRigBlueprint* RigBlueprint = Cast<UControlRigBlueprint>(FBlueprintEditorUtils::FindBlueprintForGraph(RigGraph)))
		{
			if (URigVMGraph* Model = RigBlueprint->GetModel(GraphToDelete))
			{
				if (URigVMCollapseNode* LibraryNode = Cast<URigVMCollapseNode>(Model->GetOuter()))
				{
					if (URigVMController* Controller = RigBlueprint->GetOrCreateController(LibraryNode->GetGraph()))
					{
						// check if there is a "bulk remove function" transaction going on.
						// which implies that a category is being deleted
						if (GEditor->CanTransact())
						{
							if (GEditor->Trans->GetQueueLength() > 0)
							{
								const FTransaction* LastTransaction = GEditor->Trans->GetTransaction(GEditor->Trans->GetQueueLength() - 1);
								if (LastTransaction)
								{
									if (LastTransaction->GetTitle().ToString() == TEXT("Bulk Remove Functions"))
									{
										// instead of deleting the graph, let's set its category to none
										// and thus moving it to the top of the tree
										return Controller->SetNodeCategory(LibraryNode, FString());
									}
								}
							}
						}
						
						return Controller->RemoveNode(LibraryNode);
					}
				}
			}
		}
	}
	return false;
}

bool UControlRigGraphSchema::TryRenameGraph(UEdGraph* GraphToRename, const FName& InNewName) const
{
	if (UControlRigGraph* RigGraph = Cast<UControlRigGraph>(GraphToRename))
	{
		if (UControlRigBlueprint* RigBlueprint = Cast<UControlRigBlueprint>(FBlueprintEditorUtils::FindBlueprintForGraph(RigGraph)))
		{
			if (URigVMGraph* Model = RigBlueprint->GetModel())
			{
				URigVMLibraryNode* LibraryNode = Cast<URigVMLibraryNode>(Model->FindNode(RigGraph->ModelNodePath));
				if (LibraryNode == nullptr && RigBlueprint->GetLocalFunctionLibrary())
				{
					LibraryNode = Cast<URigVMLibraryNode>(RigBlueprint->GetLocalFunctionLibrary()->FindFunction(*RigGraph->ModelNodePath));
				}

				if (LibraryNode)
				{
					if (URigVMController* Controller = RigBlueprint->GetOrCreateController(LibraryNode->GetGraph()))
					{
						Controller->RenameNode(LibraryNode, InNewName);
						return true;
					}
				}
			}
		}
	}
	return false;
}

UEdGraphPin* UControlRigGraphSchema::DropPinOnNode(UEdGraphNode* InTargetNode, const FName& InSourcePinName, const FEdGraphPinType& InSourcePinType, EEdGraphPinDirection InSourcePinDirection) const
{
	FString NewPinName;

	if (UControlRigBlueprint* RigBlueprint = Cast<UControlRigBlueprint>(FBlueprintEditorUtils::FindBlueprintForNode(InTargetNode)))
	{
		if (UControlRigGraphNode* RigNode = Cast<UControlRigGraphNode>(InTargetNode))
		{
			if (URigVMNode* ModelNode = RigNode->GetModelNode())
			{
				URigVMGraph* Model = nullptr;
				ERigVMPinDirection PinDirection = InSourcePinDirection == EGPD_Input ? ERigVMPinDirection::Input : ERigVMPinDirection::Output;

				if (URigVMCollapseNode* CollapseNode = Cast<URigVMCollapseNode>(ModelNode))
				{
					Model = CollapseNode->GetContainedGraph();
					PinDirection = PinDirection == ERigVMPinDirection::Output ? ERigVMPinDirection::Input : ERigVMPinDirection::Output;
				}
				else if (ModelNode->IsA<URigVMFunctionEntryNode>() ||
					ModelNode->IsA<URigVMFunctionReturnNode>())
				{
					Model = ModelNode->GetGraph();
				}

				if (Model)
				{
					ensure(!Model->IsTopLevelGraph());

					FRigVMExternalVariable ExternalVar = UControlRig::GetExternalVariableFromPinType(InSourcePinName, InSourcePinType);
					if (ExternalVar.IsValid(true /* allow null memory */))
					{
						if (URigVMController* Controller = RigBlueprint->GetController(Model))
						{
							FString TypeName = ExternalVar.TypeName.ToString();
							FName TypeObjectPathName = NAME_None;
							if (ExternalVar.TypeObject)
							{
								TypeObjectPathName = *ExternalVar.TypeObject->GetPathName();
							}

							FString DefaultValue;
							if (PinBeingDropped)
							{
								if (UControlRigGraphNode* SourceNode = Cast<UControlRigGraphNode>(PinBeingDropped->GetOwningNode()))
								{
									if (URigVMPin* SourcePin = SourceNode->GetModelPinFromPinPath(PinBeingDropped->GetName()))
									{
										DefaultValue = SourcePin->GetDefaultValue();
									}
								}
							}

							FName ExposedPinName = Controller->AddExposedPin(
								InSourcePinName,
								PinDirection,
								TypeName,
								TypeObjectPathName,
								DefaultValue
							);
							
							if (!ExposedPinName.IsNone())
							{
								NewPinName = ExposedPinName.ToString();
							}
						}
					}
				}

				if (!NewPinName.IsEmpty())
				{
					if (URigVMPin* NewModelPin = ModelNode->FindPin(NewPinName))
					{
						return RigNode->FindPin(*NewModelPin->GetPinPath());
					}
				}
			}
		}
	}

	return nullptr;
}

bool UControlRigGraphSchema::SupportsDropPinOnNode(UEdGraphNode* InTargetNode, const FEdGraphPinType& InSourcePinType, EEdGraphPinDirection InSourcePinDirection, FText& OutErrorMessage) const
{
	if (UControlRigGraphNode* RigNode = Cast<UControlRigGraphNode>(InTargetNode))
	{
		if(URigVMNode* ModelNode = RigNode->GetModelNode())
		{
			if (ModelNode->IsA<URigVMFunctionEntryNode>())
			{
				if (InSourcePinDirection == EGPD_Output)
				{
					OutErrorMessage = LOCTEXT("AddPinToReturnNode", "Add Pin to Return Node");
					return false;
				}
				return true;
			}
			else if (ModelNode->IsA<URigVMFunctionReturnNode>())
			{
				if (InSourcePinDirection == EGPD_Input)
				{
					OutErrorMessage = LOCTEXT("AddPinToEntryNode", "Add Pin to Entry Node");
					return false;
				}
				return true;
			}
			else if (ModelNode->IsA<URigVMCollapseNode>())
			{
				return true;
			}
		}
	}

	return false;
}

UControlRigGraphNode* UControlRigGraphSchema::CreateGraphNode(UControlRigGraph* InGraph, const FName& InPropertyName) const
{
	const bool bSelectNewNode = true;
	FGraphNodeCreator<UControlRigGraphNode> GraphNodeCreator(*InGraph);
	UControlRigGraphNode* ControlRigGraphNode = GraphNodeCreator.CreateNode(bSelectNewNode);
	ControlRigGraphNode->ModelNodePath = InPropertyName.ToString();
	GraphNodeCreator.Finalize();

	return ControlRigGraphNode;
}

void UControlRigGraphSchema::TrySetDefaultValue(UEdGraphPin& InPin, const FString& InNewDefaultValue, bool bMarkAsModified) const
{
#if WITH_EDITOR
	if (GEditor)
	{
		GEditor->CancelTransaction(0);
	}
#endif
	GetDefault<UEdGraphSchema_K2>()->TrySetDefaultValue(InPin, InNewDefaultValue, false);
}

void UControlRigGraphSchema::TrySetDefaultObject(UEdGraphPin& InPin, UObject* InNewDefaultObject, bool bMarkAsModified) const
{
#if WITH_EDITOR
	if (GEditor)
	{
		GEditor->CancelTransaction(0);
	}
#endif
	GetDefault<UEdGraphSchema_K2>()->TrySetDefaultObject(InPin, InNewDefaultObject, false);
}

void UControlRigGraphSchema::TrySetDefaultText(UEdGraphPin& InPin, const FText& InNewDefaultText, bool bMarkAsModified) const
{
#if WITH_EDITOR
	if (GEditor)
	{
		GEditor->CancelTransaction(0);
	}
#endif
	GetDefault<UEdGraphSchema_K2>()->TrySetDefaultText(InPin, InNewDefaultText, false);
}

bool UControlRigGraphSchema::ArePinsCompatible(const UEdGraphPin* PinA, const UEdGraphPin* PinB, const UClass* CallingContext, bool bIgnoreArray /*= false*/) const
{
	// filter out pins which have a parent
	if (PinB->ParentPin != nullptr)
	{
		return false;
	}

	if (UControlRigGraphNode* GraphNode = Cast<UControlRigGraphNode>(PinB->GetOwningNode()))
	{
	}

	// for reroute nodes - always allow it
	if (PinA->PinType.PinCategory == TEXT("POLYMORPH"))
	{
		UControlRigGraphSchema* MutableThis = (UControlRigGraphSchema*)this;
		MutableThis->LastPinForCompatibleCheck = PinB;
		MutableThis->bLastPinWasInput = PinB->Direction == EGPD_Input;
		return true;
	}
	if (PinB->PinType.PinCategory == TEXT("POLYMORPH"))
	{
		UControlRigGraphSchema* MutableThis = (UControlRigGraphSchema*)this;
		MutableThis->LastPinForCompatibleCheck = PinA;
		MutableThis->bLastPinWasInput = PinA->Direction == EGPD_Input;
		return true;
	}

	struct Local
	{
		static FString GetCPPTypeFromPinType(const FEdGraphPinType& InPinType)
		{
			return FString();
		}
	};

	if (PinA->PinType.PinCategory.IsNone() && PinB->PinType.PinCategory.IsNone())
	{
		return true;
	}
	else if (PinA->PinType.PinCategory.IsNone() && !PinB->PinType.PinCategory.IsNone())
	{
		if (UControlRigGraphNode* RigNode = Cast<UControlRigGraphNode>(PinA->GetOwningNode()))
		{
			if (URigVMPrototypeNode* PrototypeNode = Cast<URigVMPrototypeNode>(RigNode->GetModelNode()))
			{
				FString CPPType = Local::GetCPPTypeFromPinType(PinB->PinType);
				FString Left, Right;
				URigVMPin::SplitPinPathAtStart(PinA->GetName(), Left, Right);
				if (URigVMPin* ModelPin = PrototypeNode->FindPin(Right))
				{
					return PrototypeNode->SupportsType(ModelPin, CPPType);
				}
			}
		}
	}
	else if (!PinA->PinType.PinCategory.IsNone() && PinB->PinType.PinCategory.IsNone())
	{
		if (UControlRigGraphNode* RigNode = Cast<UControlRigGraphNode>(PinB->GetOwningNode()))
		{
			if (URigVMPrototypeNode* PrototypeNode = Cast<URigVMPrototypeNode>(RigNode->GetModelNode()))
			{
				FString CPPType = Local::GetCPPTypeFromPinType(PinA->PinType);
				FString Left, Right;
				URigVMPin::SplitPinPathAtStart(PinB->GetName(), Left, Right);
				if (URigVMPin* ModelPin = PrototypeNode->FindPin(Right))
				{
					return PrototypeNode->SupportsType(ModelPin, CPPType);
				}
			}
		}
	}

	return GetDefault<UEdGraphSchema_K2>()->ArePinsCompatible(PinA, PinB, CallingContext, bIgnoreArray);
}

void UControlRigGraphSchema::RenameNode(UControlRigGraphNode* Node, const FName& InNewNodeName) const
{
	Node->NodeTitle = FText::FromName(InNewNodeName);
	Node->Modify();
}

void UControlRigGraphSchema::ResetPinDefaultsRecursive(UEdGraphPin* InPin) const
{
	UControlRigGraphNode* RigNode = Cast<UControlRigGraphNode>(InPin->GetOwningNode());
	if (RigNode == nullptr)
	{
		return;
	}

	RigNode->CopyPinDefaultsToModel(InPin);
	for (UEdGraphPin* SubPin : InPin->SubPins)
	{
		ResetPinDefaultsRecursive(SubPin);
	}
}

void UControlRigGraphSchema::GetVariablePinTypes(TArray<FEdGraphPinType>& PinTypes) const
{
	PinTypes.Add(FEdGraphPinType(UEdGraphSchema_K2::PC_Boolean, FName(NAME_None), nullptr, EPinContainerType::None, false, FEdGraphTerminalType()));
	PinTypes.Add(FEdGraphPinType(UEdGraphSchema_K2::PC_Float, FName(NAME_None), nullptr, EPinContainerType::None, false, FEdGraphTerminalType()));
	PinTypes.Add(FEdGraphPinType(UEdGraphSchema_K2::PC_Int, FName(NAME_None), nullptr, EPinContainerType::None, false, FEdGraphTerminalType()));
	PinTypes.Add(FEdGraphPinType(UEdGraphSchema_K2::PC_Struct, FName(NAME_None), TBaseStructure<FVector>::Get(), EPinContainerType::None, false, FEdGraphTerminalType()));
	PinTypes.Add(FEdGraphPinType(UEdGraphSchema_K2::PC_Struct, FName(NAME_None), TBaseStructure<FVector2D>::Get(), EPinContainerType::None, false, FEdGraphTerminalType()));
	PinTypes.Add(FEdGraphPinType(UEdGraphSchema_K2::PC_Struct, FName(NAME_None), TBaseStructure<FRotator>::Get(), EPinContainerType::None, false, FEdGraphTerminalType()));
	PinTypes.Add(FEdGraphPinType(UEdGraphSchema_K2::PC_Struct, FName(NAME_None), TBaseStructure<FTransform>::Get(), EPinContainerType::None, false, FEdGraphTerminalType()));
	PinTypes.Add(FEdGraphPinType(UEdGraphSchema_K2::PC_Struct, FName(NAME_None), TBaseStructure<FEulerTransform>::Get(), EPinContainerType::None, false, FEdGraphTerminalType()));
	PinTypes.Add(FEdGraphPinType(UEdGraphSchema_K2::PC_Struct, FName(NAME_None), TBaseStructure<FLinearColor>::Get(), EPinContainerType::None, false, FEdGraphTerminalType()));
}

bool UControlRigGraphSchema::SafeDeleteNodeFromGraph(UEdGraph* Graph, UEdGraphNode* Node) const
{
	if (UControlRigGraphNode* RigNode = Cast<UControlRigGraphNode>(Node))
	{
		return RigNode->GetController()->RemoveNode(RigNode->GetModelNode());
	}
	return false;
}

bool UControlRigGraphSchema::CanVariableBeDropped(UEdGraph* InGraph, FProperty* InVariableToDrop) const
{
	FRigVMExternalVariable ExternalVariable = FRigVMExternalVariable::Make(InVariableToDrop, nullptr);
	return ExternalVariable.IsValid(true /* allow nullptr */);
}

bool UControlRigGraphSchema::RequestVariableDropOnPanel(UEdGraph* InGraph, FProperty* InVariableToDrop, const FVector2D& InDropPosition, const FVector2D& InScreenPosition)
{
#if WITH_EDITOR
	if (CanVariableBeDropped(InGraph, InVariableToDrop))
	{
		FRigVMExternalVariable ExternalVariable = FRigVMExternalVariable::Make(InVariableToDrop, nullptr);

		UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForGraph(InGraph);
		UControlRigBlueprint* RigBlueprint = Cast<UControlRigBlueprint>(Blueprint);
		if (RigBlueprint != nullptr)
		{
			RigBlueprint->OnVariableDropped().Broadcast(InGraph, InVariableToDrop, InDropPosition, InScreenPosition);
			return true;
		}
	}
#endif

	return false;
}

bool UControlRigGraphSchema::RequestVariableDropOnPin(UEdGraph* InGraph, FProperty* InVariableToDrop, UEdGraphPin* InPin, const FVector2D& InDropPosition, const FVector2D& InScreenPosition)
{
#if WITH_EDITOR
	if (CanVariableBeDropped(InGraph, InVariableToDrop))
	{
		if(UControlRigGraph* Graph = Cast<UControlRigGraph>(InGraph))
		{
			if (URigVMPin* ModelPin = Graph->GetModel()->FindPin(InPin->GetName()))
			{
				FRigVMExternalVariable ExternalVariable = FRigVMExternalVariable::Make(InVariableToDrop, nullptr);
				if (ModelPin->CanBeBoundToVariable(ExternalVariable))
				{
					FModifierKeysState KeyState = FSlateApplication::Get().GetModifierKeys();
					if (KeyState.IsAltDown())
					{
						return Graph->GetController()->BindPinToVariable(ModelPin->GetPinPath(), InVariableToDrop->GetName());
					}
					else
					{
						Graph->GetController()->OpenUndoBracket(TEXT("Bind Variable to Pin"));
						if (URigVMVariableNode* VariableNode = Graph->GetController()->AddVariableNode(ExternalVariable.Name, ExternalVariable.TypeName.ToString(), ExternalVariable.TypeObject, true, FString(), InDropPosition + FVector2D(0.f, -34.f)))
						{
							Graph->GetController()->AddLink(VariableNode->FindPin(TEXT("Value"))->GetPinPath(), ModelPin->GetPinPath(), true);
						}
						Graph->GetController()->CloseUndoBracket();
						return true;
					}
				}
			}
		}
	}
#endif

	return false;
}

void UControlRigGraphSchema::EndGraphNodeInteraction(UEdGraphNode* InNode) const
{
#if WITH_EDITOR

	UControlRigGraph* Graph = Cast<UControlRigGraph>(InNode->GetOuter());
	if (Graph == nullptr)
	{
		return;
	}

	check(Graph->GetController());
	check(Graph->GetModel());

	TArray<UEdGraphNode*> NodesToMove;
	NodesToMove.Add(InNode);

	for (UEdGraphNode* SelectedGraphNode : Graph->Nodes)
	{
		if (SelectedGraphNode->IsSelected())
		{
			NodesToMove.AddUnique(SelectedGraphNode);
		}
	}

	for (int32 NodeIndex = 0; NodeIndex < NodesToMove.Num(); NodeIndex++)
	{
		if (UEdGraphNode_Comment* CommentNode = Cast<UEdGraphNode_Comment>(NodesToMove[NodeIndex]))
		{
			if (CommentNode->MoveMode == ECommentBoxMode::GroupMovement)
			{
				for (FCommentNodeSet::TConstIterator NodeIt(CommentNode->GetNodesUnderComment()); NodeIt; ++NodeIt)
				{
					if (UEdGraphNode* NodeUnderComment = Cast<UEdGraphNode>(*NodeIt))
					{
						NodesToMove.AddUnique(NodeUnderComment);
					}
				}
			}
		}
	}

	bool bMovedSomething = false;

	Graph->GetController()->OpenUndoBracket(TEXT("Move Nodes"));

	for (UEdGraphNode* NodeToMove : NodesToMove)
	{
		FName NodeName = NodeToMove->GetFName();
		if (URigVMNode* ModelNode = Graph->GetModel()->FindNodeByName(NodeName))
		{
			FVector2D Position(NodeToMove->NodePosX, NodeToMove->NodePosY);
			if (Graph->GetController()->SetNodePositionByName(NodeName, Position, true, false))
			{
				bMovedSomething = true;
			}
		}
	}

	if (bMovedSomething)
	{
		Graph->GetController()->CloseUndoBracket();
	}
	else
	{
		Graph->GetController()->CancelUndoBracket();
	}

#endif
}

#undef LOCTEXT_NAMESPACE