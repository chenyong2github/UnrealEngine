// Copyright Epic Games, Inc. All Rights Reserved.

#include "PCGEditorGraphSchema.h"

#include "PCGEditorGraph.h"
#include "PCGEditorGraphNodeBase.h"
#include "PCGEditorGraphSchemaActions.h"
#include "PCGSettings.h"
#include "PCGGraph.h"

#include "UObject/UObjectIterator.h"

#define LOCTEXT_NAMESPACE "PCGEditorGraphSchema"

void UPCGEditorGraphSchema::GetGraphContextActions(FGraphContextMenuBuilder& ContextMenuBuilder) const
{
	Super::GetGraphContextActions(ContextMenuBuilder);

	const UPCGEditorGraph* PCGEditorGraph = CastChecked<UPCGEditorGraph>(ContextMenuBuilder.CurrentGraph);

	TArray<UClass*> SettingsClasses;
	for (TObjectIterator<UClass> It; It; ++It)
	{
		UClass* Class = *It;
		
		if (Class->IsChildOf(UPCGSettings::StaticClass()) &&
			!Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_Hidden))
		{
			SettingsClasses.Add(Class);
		}
	}

	for (UClass* SettingsClass : SettingsClasses)
	{
		const FText MenuDesc = FText::FromName(SettingsClass->GetFName());

		TSharedPtr<FPCGEditorGraphSchemaAction_NewNode> NewAction(new FPCGEditorGraphSchemaAction_NewNode(FText::GetEmpty(), MenuDesc, FText::GetEmpty(), 0));
		NewAction->SettingsClass = SettingsClass;
		ContextMenuBuilder.AddAction(NewAction);
	}
}

FLinearColor UPCGEditorGraphSchema::GetPinTypeColor(const FEdGraphPinType& PinType) const
{
	return FLinearColor(1.0f, 0.0f, 1.0f, 1.0f);
}

const FPinConnectionResponse UPCGEditorGraphSchema::CanCreateConnection(const UEdGraphPin* A, const UEdGraphPin* B) const
{
	const UEdGraphNode* NodeA = A->GetOwningNode();
	const UEdGraphNode* NodeB = B->GetOwningNode();

	if (NodeA == NodeB)
	{
		return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, LOCTEXT("ConnectionSameNode", "Both pins are on same node"));
	}

	if (A->Direction == B->Direction)
	{
		return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, LOCTEXT("ConnectionSameDirection", "Both pins are the same direction"));
	}

	return FPinConnectionResponse();
}

bool UPCGEditorGraphSchema::TryCreateConnection(UEdGraphPin* InA, UEdGraphPin* InB) const
{
	bool bModified = Super::TryCreateConnection(InA, InB);

	if (bModified)
	{
		check(InA && InB);
		UEdGraphPin* A = (InA->Direction == EGPD_Output ? InA : InB);
		UEdGraphPin* B = (InA->Direction == EGPD_Input ? InA : InB);
		
		check(A->Direction == EGPD_Output && B->Direction == EGPD_Input);

		UEdGraphNode* NodeA = A->GetOwningNode();
		UEdGraphNode* NodeB = B->GetOwningNode();

		UPCGEditorGraphNodeBase* PCGGraphNodeA = CastChecked<UPCGEditorGraphNodeBase>(NodeA);
		UPCGEditorGraphNodeBase* PCGGraphNodeB = CastChecked<UPCGEditorGraphNodeBase>(NodeB);

		UPCGNode* PCGNodeA = PCGGraphNodeA->GetPCGNode();
		UPCGNode* PCGNodeB = PCGGraphNodeB->GetPCGNode();
		check(PCGNodeA && PCGNodeB);

		UPCGGraph* PCGGraph = PCGNodeA->GetGraph();
		check(PCGGraph);

		const FName& NodeAPinName = ((A->PinName == TEXT("Out") && PCGNodeA->HasDefaultOutLabel()) ? NAME_None : A->PinName);
		const FName& NodeBPinName = ((B->PinName == TEXT("In") && PCGNodeB->HasDefaultInLabel()) ? NAME_None : B->PinName);

		PCGGraph->AddLabeledEdge(PCGNodeA, NodeAPinName, PCGNodeB, NodeBPinName);
	}

	return bModified;
}

void UPCGEditorGraphSchema::BreakPinLinks(UEdGraphPin& TargetPin, bool bSendsNodeNotification) const
{
	Super::BreakPinLinks(TargetPin, bSendsNodeNotification);

	UEdGraphNode* GraphNode = TargetPin.GetOwningNode();

	UPCGEditorGraphNodeBase* PCGGraphNode = CastChecked<UPCGEditorGraphNodeBase>(GraphNode);

	UPCGNode* PCGNode = PCGGraphNode->GetPCGNode();
	check(PCGNode);

	UPCGGraph* PCGGraph = PCGNode->GetGraph();
	check(PCGGraph);

	if (TargetPin.Direction == EEdGraphPinDirection::EGPD_Input)
	{
		const FName& PinName = (TargetPin.PinName == TEXT("In") && PCGNode->HasDefaultInLabel()) ? NAME_None : TargetPin.PinName;
		PCGGraph->RemoveInboundEdges(PCGNode, PinName);
	}
	else if (TargetPin.Direction == EEdGraphPinDirection::EGPD_Output)
	{
		const FName& PinName = (TargetPin.PinName == TEXT("Out") && PCGNode->HasDefaultOutLabel()) ? NAME_None : TargetPin.PinName;
		PCGGraph->RemoveOutboundEdges(PCGNode, PinName);
	}
}

void UPCGEditorGraphSchema::BreakSinglePinLink(UEdGraphPin* SourcePin, UEdGraphPin* TargetPin) const
{
	Super::BreakSinglePinLink(SourcePin, TargetPin);

	UEdGraphNode* SourceGraphNode = SourcePin->GetOwningNode();
	UEdGraphNode* TargetGraphNode = TargetPin->GetOwningNode();

	UPCGEditorGraphNodeBase* SourcePCGGraphNode = CastChecked<UPCGEditorGraphNodeBase>(SourceGraphNode);
	UPCGEditorGraphNodeBase* TargetPCGGraphNode = CastChecked<UPCGEditorGraphNodeBase>(TargetGraphNode);

	UPCGNode* SourcePCGNode = SourcePCGGraphNode->GetPCGNode();
	UPCGNode* TargetPCGNode = TargetPCGGraphNode->GetPCGNode();
	check(SourcePCGNode && TargetPCGNode);

	const FName& SourcePinName = (SourcePin->PinName == TEXT("Out") ? NAME_None : SourcePin->PinName);
	const FName& TargetPinName = (TargetPin->PinName == TEXT("In") ? NAME_None : TargetPin->PinName);

	UPCGGraph* PCGGraph = SourcePCGNode->GetGraph();
	PCGGraph->RemoveEdge(SourcePCGNode, SourcePinName, TargetPCGNode, TargetPinName);
}

#undef LOCTEXT_NAMESPACE
