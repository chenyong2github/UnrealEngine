// Copyright Epic Games, Inc. All Rights Reserved.

#include "Graph/ControlRigGraphSchema.h"
#include "Graph/ControlRigGraph.h"
#include "Graph/ControlRigGraphNode.h"
#include "IControlRigEditorModule.h"
#include "UObject/UObjectIterator.h"
#include "Units/RigUnit.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "EdGraphNode_Comment.h"
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

#define LOCTEXT_NAMESPACE "ControlRigGraphSchema"

const FName UControlRigGraphSchema::GraphName_ControlRig(TEXT("Rig Graph"));

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
		if (PinA->Direction == EGPD_Input)
		{
			UEdGraphPin* Temp = PinA;
			PinA = PinB;
			PinB = Temp;
		}
		return RigBlueprint->Controller->AddLink(PinA->GetName(), PinB->GetName());
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

		if (RigNodeA && RigNodeB)
		{
			URigVMPin* PinA = RigNodeA->GetModelPinFromPinPath(A->GetName());
			if (PinA)
			{
				PinA = PinA->GetPinForLink();
				RigBlueprint->Model->PrepareCycleChecking(PinA, A->Direction == EGPD_Input);
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

			FString FailureReason;
			bool bResult = RigBlueprint->Model->CanLink(PinA, PinB, &FailureReason);
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
	UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForNodeChecked(TargetPin.GetOwningNode());
	const UControlRigBlueprint* RigBlueprint = Cast<UControlRigBlueprint>(Blueprint);
	if (RigBlueprint != nullptr)
	{
		RigBlueprint->Controller->BreakAllLinks(TargetPin.GetName(), TargetPin.Direction == EGPD_Input);
	}
}

void UControlRigGraphSchema::BreakSinglePinLink(UEdGraphPin* SourcePin, UEdGraphPin* TargetPin) const
{
	//const FScopedTransaction Transaction(LOCTEXT("GraphEd_BreakSinglePinLink", "Break Pin Link") );

	UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForNodeChecked(TargetPin->GetOwningNode());
	const UControlRigBlueprint* RigBlueprint = Cast<UControlRigBlueprint>(Blueprint);
	if (RigBlueprint != nullptr)
	{
		if (SourcePin->Direction == EGPD_Input)
		{
			UEdGraphPin* Temp = TargetPin;
			TargetPin = SourcePin;
			SourcePin = Temp;
		}
		
		RigBlueprint->Controller->BreakLink(SourcePin->GetName(), TargetPin->GetName());
	}
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
	UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForNodeChecked(Pin->GetOwningNode());
	const UControlRigBlueprint* RigBlueprint = Cast<UControlRigBlueprint>(Blueprint);
	if (RigBlueprint != nullptr)
	{
		if (URigVMPin* ModelPin = RigBlueprint->Model->FindPin(Pin->GetName()))
		{
			return ModelPin->RequiresWatch();
		}
	}
	return false;
}

void UControlRigGraphSchema::ClearPinWatch(UEdGraphPin const* Pin) const
{
	UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForNodeChecked(Pin->GetOwningNode());
	const UControlRigBlueprint* RigBlueprint = Cast<UControlRigBlueprint>(Blueprint);
	if (RigBlueprint != nullptr)
	{
		RigBlueprint->Controller->SetPinIsWatched(Pin->GetName(), false);
	}
}

void UControlRigGraphSchema::OnPinConnectionDoubleCicked(UEdGraphPin* PinA, UEdGraphPin* PinB, const FVector2D& GraphPosition) const
{
	UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForNodeChecked(PinA->GetOwningNode());
	const UControlRigBlueprint* RigBlueprint = Cast<UControlRigBlueprint>(Blueprint);
	if (RigBlueprint != nullptr)
	{
		if (URigVMLink* Link = RigBlueprint->Model->FindLink(FString::Printf(TEXT("%s -> %s"), *PinA->GetName(), *PinB->GetName())))
		{
			RigBlueprint->Controller->AddRerouteNodeOnLink(Link, false, GraphPosition);
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
		UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForNodeChecked(RigNode);
		UControlRigBlueprint* RigBlueprint = Cast<UControlRigBlueprint>(Blueprint);
		if (RigBlueprint != nullptr)
		{
			return RigBlueprint->Controller->RemoveNode(RigNode->GetModelNode());
		}
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

void UControlRigGraphSchema::EndGraphNodeInteraction(UEdGraphNode* InNode) const
{
#if WITH_EDITOR

	UControlRigGraph* Graph = Cast<UControlRigGraph>(InNode->GetOuter());
	if (Graph == nullptr)
	{
		return;
	}

	UControlRigBlueprint* Blueprint = Cast<UControlRigBlueprint>(Graph->GetOuter());
	if (Blueprint == nullptr)
	{
		return;
	}

	check(Blueprint->Controller);
	check(Blueprint->Model);

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

	Blueprint->Controller->OpenUndoBracket(TEXT("Move Nodes"));

	for (UEdGraphNode* NodeToMove : NodesToMove)
	{
		FName NodeName = NodeToMove->GetFName();
		if (URigVMNode* ModelNode = Blueprint->Model->FindNodeByName(NodeName))
		{
			FVector2D Position(NodeToMove->NodePosX, NodeToMove->NodePosY);
			if (Blueprint->Controller->SetNodePositionByName(NodeName, Position, true, false))
			{
				bMovedSomething = true;
			}
		}
	}

	if (bMovedSomething)
	{
		Blueprint->Controller->CloseUndoBracket();
	}
	else
	{
		Blueprint->Controller->CancelUndoBracket();
	}

#endif
}

#undef LOCTEXT_NAMESPACE