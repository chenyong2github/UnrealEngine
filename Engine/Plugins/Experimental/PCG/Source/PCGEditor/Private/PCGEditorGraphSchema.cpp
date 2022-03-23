// Copyright Epic Games, Inc. All Rights Reserved.

#include "PCGEditorGraphSchema.h"

#include "PCGEditorGraph.h"
#include "PCGEditorGraphNode.h"
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

bool UPCGEditorGraphSchema::TryCreateConnection(UEdGraphPin* A, UEdGraphPin* B) const
{
	bool bModified = Super::TryCreateConnection(A, B);

	if (bModified)
	{
		UEdGraphNode* NodeA = A->GetOwningNode();
		UEdGraphNode* NodeB = B->GetOwningNode();

		UPCGEditorGraphNode* PCGGraphNodeA = CastChecked<UPCGEditorGraphNode>(NodeA);
		UPCGEditorGraphNode* PCGGraphNodeB = CastChecked<UPCGEditorGraphNode>(NodeB);

		UPCGNode* PCGNodeA = PCGGraphNodeA->GetPCGNode();
		UPCGNode* PCGNodeB = PCGGraphNodeB->GetPCGNode();
		check(PCGNodeA && PCGNodeB);

		UPCGGraph* PCGGraph = PCGNodeA->GetGraph();
		check(PCGGraph);

		PCGGraph->AddEdge(PCGNodeA, PCGNodeB);
	}

	return bModified;
}

void UPCGEditorGraphSchema::BreakPinLinks(UEdGraphPin& TargetPin, bool bSendsNodeNotification) const
{
	Super::BreakPinLinks(TargetPin, bSendsNodeNotification);

	UEdGraphNode* GraphNode = TargetPin.GetOwningNode();

	UPCGEditorGraphNode* PCGGraphNode = CastChecked<UPCGEditorGraphNode>(GraphNode);

	UPCGNode* PCGNode = PCGGraphNode->GetPCGNode();
	check(PCGNode);

	UPCGGraph* PCGGraph = PCGNode->GetGraph();
	check(PCGGraph);

	if (TargetPin.Direction == EEdGraphPinDirection::EGPD_Input)
	{
		PCGGraph->RemoveInboundEdges(PCGNode);
	}
	else if (TargetPin.Direction == EEdGraphPinDirection::EGPD_Output)
	{
		PCGGraph->RemoveOutboundEdges(PCGNode);
	}
}

void UPCGEditorGraphSchema::BreakSinglePinLink(UEdGraphPin* SourcePin, UEdGraphPin* TargetPin) const
{
	Super::BreakSinglePinLink(SourcePin, TargetPin);

	UEdGraphNode* SourceGraphNode = SourcePin->GetOwningNode();
	UEdGraphNode* TargetGraphNode = TargetPin->GetOwningNode();

	UPCGEditorGraphNode* SourcePCGGraphNode = CastChecked<UPCGEditorGraphNode>(SourceGraphNode);
	UPCGEditorGraphNode* TargetPCGGraphNode = CastChecked<UPCGEditorGraphNode>(TargetGraphNode);

	UPCGNode* SourcePCGNode = SourcePCGGraphNode->GetPCGNode();
	UPCGNode* TargetPCGNode = TargetPCGGraphNode->GetPCGNode();
	check(SourcePCGNode && TargetPCGNode);

	UPCGGraph* PCGGraph = SourcePCGNode->GetGraph();
	PCGGraph->RemoveEdge(SourcePCGNode, TargetPCGNode);
}

#undef LOCTEXT_NAMESPACE
