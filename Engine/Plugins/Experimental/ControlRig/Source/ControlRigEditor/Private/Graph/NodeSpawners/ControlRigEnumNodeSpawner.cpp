// Copyright Epic Games, Inc. All Rights Reserved.

#include "ControlRigEnumNodeSpawner.h"
#include "ControlRigUnitNodeSpawner.h"
#include "Graph/ControlRigGraph.h"
#include "Graph/ControlRigGraphNode.h"
#include "Graph/ControlRigGraphSchema.h"
#include "BlueprintNodeTemplateCache.h"
#include "RigVMModel/RigVMController.h"
#include "ControlRigBlueprint.h"
#include "ControlRigBlueprintUtils.h"

#include "RigVMModel/Nodes/RigVMEnumNode.h"

#if WITH_EDITOR
#include "Editor.h"
#endif

#define LOCTEXT_NAMESPACE "ControlRigEnumNodeSpawner"

UControlRigEnumNodeSpawner* UControlRigEnumNodeSpawner::CreateForEnum(UEnum* InEnum, const FText& InMenuDesc, const FText& InCategory, const FText& InTooltip)
{
	UControlRigEnumNodeSpawner* NodeSpawner = NewObject<UControlRigEnumNodeSpawner>(GetTransientPackage());
	NodeSpawner->NodeClass = UControlRigGraphNode::StaticClass();
	NodeSpawner->Enum = InEnum;

	FBlueprintActionUiSpec& MenuSignature = NodeSpawner->DefaultMenuSignature;
	
	MenuSignature.MenuName = InMenuDesc;
	MenuSignature.Tooltip  = InTooltip;
	MenuSignature.Category = InCategory;
	MenuSignature.Keywords = FText::FromString(TEXT("Enum"));
	MenuSignature.Icon = FSlateIcon(TEXT("ControlRigEditorStyle"), TEXT("ControlRig.RigUnit"));

	return NodeSpawner;
}

FBlueprintNodeSignature UControlRigEnumNodeSpawner::GetSpawnerSignature() const
{
	return FBlueprintNodeSignature(NodeClass);
}

FBlueprintActionUiSpec UControlRigEnumNodeSpawner::GetUiSpec(FBlueprintActionContext const& Context, FBindingSet const& Bindings) const
{
	UEdGraph* TargetGraph = (Context.Graphs.Num() > 0) ? Context.Graphs[0] : nullptr;
	FBlueprintActionUiSpec MenuSignature = PrimeDefaultUiSpec(TargetGraph);

	DynamicUiSignatureGetter.ExecuteIfBound(Context, Bindings, &MenuSignature);
	return MenuSignature;
}

UEdGraphNode* UControlRigEnumNodeSpawner::Invoke(UEdGraph* ParentGraph, FBindingSet const& Bindings, FVector2D const Location) const
{
	UControlRigGraphNode* NewNode = nullptr;

	bool const bIsTemplateNode = FBlueprintNodeTemplateCache::IsTemplateOuter(ParentGraph);

	if (bIsTemplateNode)
	{
		NewNode = NewObject<UControlRigGraphNode>(ParentGraph, TEXT("EnumNode"));
		ParentGraph->AddNode(NewNode, false);

		NewNode->CreateNewGuid();
		NewNode->PostPlacedNewNode();

		UEdGraphPin* OutputValuePin = UEdGraphPin::CreatePin(NewNode);
		NewNode->Pins.Add(OutputValuePin);

		OutputValuePin->PinType.PinCategory = TEXT("int32");
		OutputValuePin->Direction = EGPD_Output;
		NewNode->SetFlags(RF_Transactional);

		return NewNode;
	}

	bool const bUndo = !bIsTemplateNode;

	// First create a backing member for our node
	UControlRigGraph* RigGraph = Cast<UControlRigGraph>(ParentGraph);
	check(RigGraph);
	UControlRigBlueprint* RigBlueprint = Cast<UControlRigBlueprint>(ParentGraph->GetOuter());
	check(RigBlueprint);

#if WITH_EDITOR
	if (GEditor && !bIsTemplateNode)
	{
		GEditor->CancelTransaction(0);
	}
#endif

	URigVMController* Controller = bIsTemplateNode ? RigGraph->GetTemplateController() : RigBlueprint->Controller;

	FName Name = *URigVMEnumNode::EnumName;

	if (!bIsTemplateNode)
	{
		Controller->OpenUndoBracket(FString::Printf(TEXT("Add '%s' Node"), *Name.ToString()));
	}

	if (URigVMEnumNode* ModelNode = Controller->AddEnumNode(*Enum->GetPathName(), Location, Name.ToString(), bUndo))
	{
		NewNode = Cast<UControlRigGraphNode>(RigGraph->FindNodeForModelNodeName(ModelNode->GetFName()));

		if (NewNode && bUndo)
		{
			Controller->ClearNodeSelection(true);
			Controller->SelectNode(ModelNode, true, true);
		}

		if (bUndo)
		{
			Controller->CloseUndoBracket();
		}
	}
	else
	{
		if (bUndo)
		{
			Controller->CancelUndoBracket();
		}
	}


	return NewNode;
}

#undef LOCTEXT_NAMESPACE
