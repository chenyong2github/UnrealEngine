// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Graph/ControlRigGraphSchema.h"
#include "Graph/ControlRigGraph.h"
#include "Graph/ControlRigGraphNode.h"
#include "IControlRigEditorModule.h"
#include "UObject/UObjectIterator.h"
#include "Units/RigUnit.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "ScopedTransaction.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "GraphEditorActions.h"
#include "ControlRig.h"
#include "ControlRigDAG.h"
#include "ControlRigBlueprint.h"
#include "ControlRigBlueprintGeneratedClass.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Framework/Notifications/NotificationManager.h"

#define LOCTEXT_NAMESPACE "ControlRigGraphSchema"

const FName UControlRigGraphSchema::GraphName_ControlRig(TEXT("Rig Graph"));

UControlRigGraphSchema::UControlRigGraphSchema()
{
}

void UControlRigGraphSchema::GetGraphContextActions(FGraphContextMenuBuilder& ContextMenuBuilder) const
{

}

void UControlRigGraphSchema::GetContextMenuActions(const UEdGraph* CurrentGraph, const UEdGraphNode* InGraphNode, const UEdGraphPin* InGraphPin, FMenuBuilder* MenuBuilder, bool bIsDebugging) const
{
#if WITH_EDITOR
	return IControlRigEditorModule::Get().GetContextMenuActions(this, CurrentGraph, InGraphNode, InGraphPin, MenuBuilder, bIsDebugging);
#else
	check(0);
#endif
}

bool UControlRigGraphSchema::TryCreateConnection_DetectCycle(UEdGraphPin* PinA, UEdGraphPin* PinB) const
{
	check(PinA);
	check(PinB);
	if (PinA->GetOwningNode() == PinB->GetOwningNode())
	{
		return true;
	}

	const UEdGraphNode* NodeA = PinA->GetOwningNode();
	const UEdGraphNode* NodeB = PinB->GetOwningNode();

	FControlRigDAG DAG;
	const UEdGraph* Graph = NodeA->GetGraph();
	for (const UEdGraphNode* Node : Graph->Nodes)
	{
		DAG.AddNode();
	}
	for (int32 NodeIndex = 0; NodeIndex < Graph->Nodes.Num(); NodeIndex++)
	{
		const UEdGraphNode* Node = Graph->Nodes[NodeIndex];
		for (int32 PinIndex = 0; PinIndex < Node->Pins.Num(); PinIndex++)
		{
			const UEdGraphPin* Pin = Node->Pins[PinIndex];
			if (Pin->Direction != EGPD_Output)
			{
				continue;
			}
			for (const UEdGraphPin* LinkedPin : Pin->LinkedTo)
			{
				const UEdGraphNode* LinkedNode = LinkedPin->GetOwningNode();
				int32 LinkedNodeIndex = Graph->Nodes.IndexOfByKey(LinkedNode);
				int32 LinkedPinIndex = LinkedNode->Pins.IndexOfByKey(LinkedPin);
				DAG.AddLink(NodeIndex, LinkedNodeIndex, PinIndex, LinkedPinIndex);
			}
		}
	}

	// finally add the link we are going to make
	int32 NodeAIndex = Graph->Nodes.IndexOfByKey(NodeA);
	int32 NodeBIndex = Graph->Nodes.IndexOfByKey(NodeB);
	int32 PinAIndex = NodeA->Pins.IndexOfByKey(PinA);
	int32 PinBIndex = NodeA->Pins.IndexOfByKey(PinB);
	if (PinA->Direction == EGPD_Output)
	{
		DAG.AddLink(NodeAIndex, NodeBIndex, PinAIndex, PinBIndex);
	}
	else
	{
		DAG.AddLink(NodeBIndex, NodeAIndex, PinBIndex, PinAIndex);
	}

	return DAG.FindCycle().Num() > 0;
}

bool UControlRigGraphSchema::TryCreateConnection_Extended(UEdGraphPin* PinA, UEdGraphPin* PinB) const
{
	const FControlRigPinConnectionResponse Response = CanCreateConnection_Extended(PinA, PinB);
	bool bModified = false;

	struct Local
	{
		static void BreakParentConnections_Recursive(UEdGraphPin* InPin, const UControlRigGraphSchema* This)
		{
			This->ResetPinDefaultsRecursive(InPin);

			if(InPin->ParentPin)
			{
				InPin->ParentPin->Modify();
				InPin->ParentPin->BreakAllPinLinks();
				InPin->GetOwningNode()->PinConnectionListChanged(InPin->ParentPin);
				BreakParentConnections_Recursive(InPin->ParentPin, This);
			}
		}

		static void BreakChildConnections_Recursive(UEdGraphPin* InPin, const UControlRigGraphSchema* This)
		{
			for(UEdGraphPin* SubPin : InPin->SubPins)
			{
				if(SubPin->LinkedTo.Num() > 0)
				{
					SubPin->Modify();
					SubPin->BreakAllPinLinks();
					SubPin->GetOwningNode()->PinConnectionListChanged(SubPin);
				}

				BreakChildConnections_Recursive(SubPin, This);
			}
		}
	};

	// build a temporary dag to disallow cycles.
	// we do this here only once since it's a costly calculation
	// and we don't want to do it for every possible pin.
	if (Response.Response.Response == CONNECT_RESPONSE_MAKE)
	{
		if (TryCreateConnection_DetectCycle(PinA, PinB))
		{
#if WITH_EDITOR
			FNotificationInfo Info(LOCTEXT("ConnectResponse_Disallowed_Cycle", "Connection not allowed to avoid cycle."));
			Info.Image = FCoreStyle::Get().GetBrush(TEXT("MessageLog.Warning"));
			Info.bFireAndForget = true;
			Info.FadeOutDuration = 5.0f;
			Info.ExpireDuration = 0.0f;
			TSharedPtr<SNotificationItem> NotificationPtr = FSlateNotificationManager::Get().AddNotification(Info);
			NotificationPtr->SetCompletionState(SNotificationItem::CS_Success);
#endif
			//PinA->GetOwningNode()->PinConnectionListChanged(PinA);
			//PinB->GetOwningNode()->PinConnectionListChanged(PinB);
			return false;
		}
	}

	switch (Response.Response.Response)
	{
	case CONNECT_RESPONSE_MAKE:
		PinA->Modify();
		PinB->Modify();
		PinA->MakeLinkTo(PinB);
		bModified = true;
		switch(Response.ExtendedResponse)
		{
		case ECanCreateConnectionResponse_Extended::None:
			break;
		case ECanCreateConnectionResponse_Extended::BreakChildren:
			if(PinA->Direction == EGPD_Input)
			{
				Local::BreakChildConnections_Recursive(PinA, this);
			}
			else if(PinB->Direction == EGPD_Input)
			{
				Local::BreakChildConnections_Recursive(PinB, this);
			}
			break;
		case ECanCreateConnectionResponse_Extended::BreakParent:
			if(PinA->Direction == EGPD_Input)
			{
				Local::BreakParentConnections_Recursive(PinA, this);
			}
			else if(PinB->Direction == EGPD_Input)
			{
				Local::BreakParentConnections_Recursive(PinB, this);
			}
			break;
		}
		PinA->GetOwningNode()->PinConnectionListChanged(PinA);
		PinB->GetOwningNode()->PinConnectionListChanged(PinB);
		break;

	default:
		bModified = UEdGraphSchema::TryCreateConnection(PinA, PinB);
		break;
	}

	return bModified;
}

bool UControlRigGraphSchema::TryCreateConnection(UEdGraphPin* PinA, UEdGraphPin* PinB) const
{
	UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForNodeChecked(PinA->GetOwningNode());

	bool bModified = TryCreateConnection_Extended(PinA, PinB);

	if (bModified && !PinA->IsPendingKill())
	{
		FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
	}

	return bModified;	
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

const FControlRigPinConnectionResponse UControlRigGraphSchema::CanCreateConnection_Extended(const UEdGraphPin* A, const UEdGraphPin* B) const
{
	check(A != nullptr);
	check(B != nullptr);

	// Deal with basic connections (same pins, same node, differing types etc.)
	if(A == B)
	{
		return FControlRigPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, LOCTEXT("ConnectResponse_Disallowed_Self", "Cannot link a pin to itself"));
	}

	if(A->Direction == B->Direction)
	{
		return FControlRigPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, A->Direction == EGPD_Input ? LOCTEXT("ConnectResponse_Disallowed_Direction_Input", "Cannot link input pin to input pin") : LOCTEXT("ConnectResponse_Disallowed_Direction_Output", "Cannot link output pin to output pin"));
	}

	if(A->GetOwningNode() == B->GetOwningNode())
	{
		return FControlRigPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, LOCTEXT("ConnectResponse_Disallowed_SameNode", "Cannot link two pins on the same node"));
	}

	if(A->PinType != B->PinType)
	{
		return FControlRigPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, LOCTEXT("ConnectResponse_Disallowed_Different_Types", "Cannot link pins of differing types"));
	}

	struct Local
	{
		static bool HasNotConnectableMetaData(const UEdGraphPin* PinToCheck)
		{
			if (PinToCheck->ParentPin)
			{
				return HasNotConnectableMetaData(PinToCheck->ParentPin);
			}

			UControlRigGraphNode* RigNode = CastChecked<UControlRigGraphNode>(PinToCheck->GetOwningNode());
			UScriptStruct* UnitStruct = RigNode->GetUnitScriptStruct();
			if (UnitStruct)
			{
				FString PropertyName = PinToCheck->GetName();
				int32 PeriodIndex = PropertyName.Find(TEXT("."));
				if (PeriodIndex != INDEX_NONE)
				{
					PropertyName = PropertyName.Mid(PeriodIndex + 1);
				}
				UProperty* TargetProperty = UnitStruct->FindPropertyByName(*PropertyName);
				if (TargetProperty)
				{
					return TargetProperty->HasMetaData(UControlRig::ConstantMetaName);
				}
			}

			return false;
		}
	};

	// check if this property can be connected to based on metadata
	if (A->Direction == EGPD_Input)
	{
		if (Local::HasNotConnectableMetaData(A))
		{
			if (!Local::HasNotConnectableMetaData(B))
			{
				return FControlRigPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, LOCTEXT("ConnectResponse_Disallowed_Constant", "This pin is defined as constant."));
			}
		}
	}
	if (B->Direction == EGPD_Input)
	{
		if (Local::HasNotConnectableMetaData(B))
		{
			if (!Local::HasNotConnectableMetaData(A))
			{
				return FControlRigPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, LOCTEXT("ConnectResponse_Disallowed_Constant", "This pin is defined as constant."));
			}
		}
	}

	// Deal with many-to-one and one to many connections
	if(A->Direction == EGPD_Input && A->LinkedTo.Num() > 0)
	{
		return FControlRigPinConnectionResponse(CONNECT_RESPONSE_BREAK_OTHERS_A, LOCTEXT("ConnectResponse_Replace_Input", "Replace connection"));
	}
	else if(B->Direction == EGPD_Input && B->LinkedTo.Num() > 0)
	{
		return FControlRigPinConnectionResponse(CONNECT_RESPONSE_BREAK_OTHERS_B, LOCTEXT("ConnectResponse_Replace_Input", "Replace connection"));
	}

	// Deal with sub-struct pins

	if(A->Direction == EGPD_Input && HasParentConnection_Recursive(A))
	{
		return FControlRigPinConnectionResponse(CONNECT_RESPONSE_MAKE, LOCTEXT("ConnectResponse_Replace_Parent", "Replace parent connection"), ECanCreateConnectionResponse_Extended::BreakParent);
	}
	else if(B->Direction == EGPD_Input && HasParentConnection_Recursive(B))
	{
		return FControlRigPinConnectionResponse(CONNECT_RESPONSE_MAKE, LOCTEXT("ConnectResponse_Replace_Parent", "Replace parent connection"), ECanCreateConnectionResponse_Extended::BreakParent);
	}

	if(A->Direction == EGPD_Input && HasChildConnection_Recursive(A))
	{
		return FControlRigPinConnectionResponse(CONNECT_RESPONSE_MAKE, LOCTEXT("ConnectResponse_Replace_Child", "Replace child connection(s)"), ECanCreateConnectionResponse_Extended::BreakChildren);
	}
	else if(B->Direction == EGPD_Input && HasChildConnection_Recursive(B))
	{
		return FControlRigPinConnectionResponse(CONNECT_RESPONSE_MAKE, LOCTEXT("ConnectResponse_Replace_Child", "Replace child connection(s)"), ECanCreateConnectionResponse_Extended::BreakChildren);
	}

	return FControlRigPinConnectionResponse(CONNECT_RESPONSE_MAKE, LOCTEXT("ConnectResponse_Allowed", "Connect"));	
}

const FPinConnectionResponse UControlRigGraphSchema::CanCreateConnection(const UEdGraphPin* A, const UEdGraphPin* B) const
{
	const FControlRigPinConnectionResponse Response = CanCreateConnection_Extended(A, B);
	return Response.Response;
}

FLinearColor UControlRigGraphSchema::GetPinTypeColor(const FEdGraphPinType& PinType) const
{
	const FName& TypeName = PinType.PinCategory;
	if (TypeName == UEdGraphSchema_K2::PC_Struct)
	{
		if (PinType.PinSubCategoryObject == FControlRigExecuteContext::StaticStruct())
		{
			return FLinearColor::White;
		}
	}
	return GetDefault<UEdGraphSchema_K2>()->GetPinTypeColor(PinType);
}

void UControlRigGraphSchema::BreakPinLinks(UEdGraphPin& TargetPin, bool bSendsNodeNotifcation) const
{
	const FScopedTransaction Transaction( LOCTEXT("GraphEd_BreakPinLinks", "Break Pin Links") );

	// cache this here, as BreakPinLinks can trigger a node reconstruction invalidating the TargetPin referenceS
	UBlueprint* const Blueprint = FBlueprintEditorUtils::FindBlueprintForNodeChecked(TargetPin.GetOwningNode());

	TArray<UEdGraphPin*> LinkedTo;
	LinkedTo.Append(TargetPin.LinkedTo);

	Super::BreakPinLinks(TargetPin, bSendsNodeNotifcation);

	if (TargetPin.Direction == EEdGraphPinDirection::EGPD_Input)
	{
		ResetPinDefaultsRecursive(&TargetPin);
	}
	else
	{
		for (UEdGraphPin* LinkedPin : LinkedTo)
		{
			if (LinkedPin->Direction == EEdGraphPinDirection::EGPD_Input)
			{
				ResetPinDefaultsRecursive(LinkedPin);
			}
		}
	}

	FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);	
}

void UControlRigGraphSchema::BreakSinglePinLink(UEdGraphPin* SourcePin, UEdGraphPin* TargetPin) const
{
	const FScopedTransaction Transaction(LOCTEXT("GraphEd_BreakSinglePinLink", "Break Pin Link") );

	UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForNodeChecked(TargetPin->GetOwningNode());

	Super::BreakSinglePinLink(SourcePin, TargetPin);
	if (SourcePin->Direction == EEdGraphPinDirection::EGPD_Input)
	{
		ResetPinDefaultsRecursive(SourcePin);
	}
	else if (TargetPin->Direction == EEdGraphPinDirection::EGPD_Input)
	{
		ResetPinDefaultsRecursive(TargetPin);
	}

	FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
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

UControlRigGraphNode* UControlRigGraphSchema::CreateGraphNode(UControlRigGraph* InGraph, const FName& InPropertyName) const
{
	const bool bSelectNewNode = true;
	FGraphNodeCreator<UControlRigGraphNode> GraphNodeCreator(*InGraph);
	UControlRigGraphNode* ControlRigGraphNode = GraphNodeCreator.CreateNode(bSelectNewNode);
	ControlRigGraphNode->SetPropertyName(InPropertyName);
	GraphNodeCreator.Finalize();

	return ControlRigGraphNode;
}

void UControlRigGraphSchema::TrySetDefaultValue(UEdGraphPin& InPin, const FString& InNewDefaultValue) const
{
	GetDefault<UEdGraphSchema_K2>()->TrySetDefaultValue(InPin, InNewDefaultValue);
}

void UControlRigGraphSchema::TrySetDefaultObject(UEdGraphPin& InPin, UObject* InNewDefaultObject) const
{
	GetDefault<UEdGraphSchema_K2>()->TrySetDefaultObject(InPin, InNewDefaultObject);
}

void UControlRigGraphSchema::TrySetDefaultText(UEdGraphPin& InPin, const FText& InNewDefaultText) const
{
	GetDefault<UEdGraphSchema_K2>()->TrySetDefaultText(InPin, InNewDefaultText);
}

bool UControlRigGraphSchema::ArePinsCompatible(const UEdGraphPin* PinA, const UEdGraphPin* PinB, const UClass* CallingContext, bool bIgnoreArray /*= false*/) const
{
	// filter out pins which have a parent
	if (PinB->ParentPin != nullptr)
	{
		return false;
	}
	return GetDefault<UEdGraphSchema_K2>()->ArePinsCompatible(PinA, PinB, CallingContext, bIgnoreArray);
}

void UControlRigGraphSchema::RenameNode(UControlRigGraphNode* Node, const FName& InNewNodeName) const
{
	Node->NodeTitleFull = Node->NodeTitle = FText::FromName(InNewNodeName);
	Node->Modify();
}

void UControlRigGraphSchema::ResetPinDefaultsRecursive(UEdGraphPin* InPin) const
{
	UControlRigGraphNode* RigNode = Cast<UControlRigGraphNode>(InPin->GetOwningNode());
	if (RigNode == nullptr)
	{
		return;
	}

	RigNode->CopyPinDefaultsToProperties(InPin, true, false);

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
	PinTypes.Add(FEdGraphPinType(UEdGraphSchema_K2::PC_Struct, FName(NAME_None), TBaseStructure<FLinearColor>::Get(), EPinContainerType::None, false, FEdGraphTerminalType()));
}


#undef LOCTEXT_NAMESPACE