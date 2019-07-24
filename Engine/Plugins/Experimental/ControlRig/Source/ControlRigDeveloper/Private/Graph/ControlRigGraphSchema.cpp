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
#include "EulerTransform.h"

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

bool UControlRigGraphSchema::TryCreateConnection(UEdGraphPin* PinA, UEdGraphPin* PinB) const
{
#if WITH_EDITOR
	if (GEditor)
	{
		GEditor->CancelTransaction(0);
	}
#endif

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

		FString LeftA, LeftB, RightA, RightB;
		RigBlueprint->Model->SplitPinPath(PinA->GetName(), LeftA, RightA);
		RigBlueprint->Model->SplitPinPath(PinB->GetName(), LeftB, RightB);
		return RigBlueprint->ModelController->MakeLink(*LeftA, *RightA, *LeftB, *RightB);
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
		FString LeftA, LeftB, RightA, RightB;
		RigBlueprint->Model->SplitPinPath(A->GetName(), LeftA, RightA);
		RigBlueprint->Model->SplitPinPath(B->GetName(), LeftB, RightB);

		const FControlRigModelPin* PinA = RigBlueprint->Model->FindPin(*LeftA, *RightA);
		if (PinA)
		{
			RigBlueprint->ModelController->PrepareCycleCheckingForPin(*LeftA, *RightA, A->Direction == EGPD_Input);
		}

		if (A->Direction == EGPD_Input)
		{
			FString Temp = LeftA;
			LeftA = LeftB;
			LeftB = Temp;
			Temp = RightA;
			RightA = RightB;
			RightB = Temp;
		}

		FString FailureReason;
		bool bResult = RigBlueprint->ModelController->CanLink(*LeftA, *RightA, *LeftB, *RightB, &FailureReason);
		if (!bResult)
		{
			return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, FText::FromString(FailureReason));
		}
		return FPinConnectionResponse(CONNECT_RESPONSE_MAKE, LOCTEXT("ConnectResponse_Allowed", "Connect"));
	}

	return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, LOCTEXT("ConnectResponse_Disallowed_Unexpected", "Unexpected error"));
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
	//const FScopedTransaction Transaction( LOCTEXT("GraphEd_BreakPinLinks", "Break Pin Links") );

	// cache this here, as BreakPinLinks can trigger a node reconstruction invalidating the TargetPin referenceS
	UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForNodeChecked(TargetPin.GetOwningNode());
	const UControlRigBlueprint* RigBlueprint = Cast<UControlRigBlueprint>(Blueprint);
	if (RigBlueprint != nullptr)
	{
		FString Left, Right;
		RigBlueprint->Model->SplitPinPath(TargetPin.GetName(), Left, Right);
		RigBlueprint->ModelController->BreakLinks(*Left, *Right, TargetPin.Direction == EGPD_Input);
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
		
		FString SourceLeft, SourceRight, TargetLeft, TargetRight;
		RigBlueprint->Model->SplitPinPath(SourcePin->GetName(), SourceLeft, SourceRight);
		RigBlueprint->Model->SplitPinPath(TargetPin->GetName(), TargetLeft, TargetLeft);
		RigBlueprint->ModelController->BreakLink(*SourceLeft, *SourceRight, *TargetLeft, *TargetRight);
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

UControlRigGraphNode* UControlRigGraphSchema::CreateGraphNode(UControlRigGraph* InGraph, const FName& InPropertyName) const
{
	const bool bSelectNewNode = true;
	FGraphNodeCreator<UControlRigGraphNode> GraphNodeCreator(*InGraph);
	UControlRigGraphNode* ControlRigGraphNode = GraphNodeCreator.CreateNode(bSelectNewNode);
	ControlRigGraphNode->SetPropertyName(InPropertyName);
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


#undef LOCTEXT_NAMESPACE