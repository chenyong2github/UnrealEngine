// Copyright Epic Games, Inc. All Rights Reserved.

#include "ControlRigSelectNodeSpawner.h"
#include "ControlRigUnitNodeSpawner.h"
#include "Graph/ControlRigGraph.h"
#include "Graph/ControlRigGraphNode.h"
#include "Graph/ControlRigGraphSchema.h"
#include "BlueprintNodeTemplateCache.h"
#include "RigVMModel/RigVMController.h"
#include "ControlRigBlueprint.h"
#include "ControlRigBlueprintUtils.h"

#include "RigVMModel/Nodes/RigVMSelectNode.h"

#if WITH_EDITOR
#include "Editor.h"
#endif

#define LOCTEXT_NAMESPACE "ControlRigSelectNodeSpawner"

UControlRigSelectNodeSpawner* UControlRigSelectNodeSpawner::CreateGeneric(const FText& InMenuDesc, const FText& InCategory, const FText& InTooltip)
{
	UControlRigSelectNodeSpawner* NodeSpawner = NewObject<UControlRigSelectNodeSpawner>(GetTransientPackage());
	NodeSpawner->NodeClass = UControlRigGraphNode::StaticClass();

	FBlueprintActionUiSpec& MenuSignature = NodeSpawner->DefaultMenuSignature;
	
	MenuSignature.MenuName = InMenuDesc;
	MenuSignature.Tooltip  = InTooltip;
	MenuSignature.Category = InCategory;
	MenuSignature.Keywords = FText::FromString(TEXT("Select,Pick,Sequence,If"));
	MenuSignature.Icon = FSlateIcon(TEXT("ControlRigEditorStyle"), TEXT("ControlRig.RigUnit"));

	return NodeSpawner;
}

FBlueprintNodeSignature UControlRigSelectNodeSpawner::GetSpawnerSignature() const
{
	return FBlueprintNodeSignature(NodeClass);
}

FBlueprintActionUiSpec UControlRigSelectNodeSpawner::GetUiSpec(FBlueprintActionContext const& Context, FBindingSet const& Bindings) const
{
	UEdGraph* TargetGraph = (Context.Graphs.Num() > 0) ? Context.Graphs[0] : nullptr;
	FBlueprintActionUiSpec MenuSignature = PrimeDefaultUiSpec(TargetGraph);

	DynamicUiSignatureGetter.ExecuteIfBound(Context, Bindings, &MenuSignature);
	return MenuSignature;
}

UEdGraphNode* UControlRigSelectNodeSpawner::Invoke(UEdGraph* ParentGraph, FBindingSet const& Bindings, FVector2D const Location) const
{
	UControlRigGraphNode* NewNode = nullptr;

	bool const bIsTemplateNode = FBlueprintNodeTemplateCache::IsTemplateOuter(ParentGraph);

	if (bIsTemplateNode)
	{
		NewNode = NewObject<UControlRigGraphNode>(ParentGraph, TEXT("SelectNode"));
		ParentGraph->AddNode(NewNode, false);

		NewNode->CreateNewGuid();
		NewNode->PostPlacedNewNode();

		UEdGraphPin* InputValuePin = UEdGraphPin::CreatePin(NewNode);
		UEdGraphPin* OutputValuePin = UEdGraphPin::CreatePin(NewNode);
		NewNode->Pins.Add(InputValuePin);
		NewNode->Pins.Add(OutputValuePin);

		InputValuePin->PinType.PinCategory = TEXT("POLYMORPH");
		OutputValuePin->PinType.PinCategory = TEXT("POLYMORPH");
		InputValuePin->Direction = EGPD_Input;
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

	FString CPPType = "int32";
	FName CPPTypeObjectPath = NAME_None;

	if (UControlRigGraphSchema* RigSchema = Cast<UControlRigGraphSchema>((UEdGraphSchema*)ParentGraph->GetSchema()))
	{
		if (const UEdGraphPin* LastPin = RigSchema->LastPinForCompatibleCheck)
		{
			if (URigVMPin* ModelPin = RigBlueprint->Model->FindPin(LastPin->GetName()))
			{
				CPPType = ModelPin->GetCPPType();
				if (ModelPin->GetCPPTypeObject())
				{
					CPPTypeObjectPath = *ModelPin->GetCPPTypeObject()->GetPathName();
				}
			}
		}
	}

	FName MemberName = NAME_None;

#if WITH_EDITOR
	if (GEditor && !bIsTemplateNode)
	{
		GEditor->CancelTransaction(0);
	}
#endif

	URigVMController* Controller = bIsTemplateNode ? RigGraph->GetTemplateController() : RigBlueprint->Controller;

	FName Name = *URigVMSelectNode::SelectName;

	if (!bIsTemplateNode)
	{
		Controller->OpenUndoBracket(FString::Printf(TEXT("Add '%s' Node"), *Name.ToString()));
	}

	if (URigVMSelectNode* ModelNode = Controller->AddSelectNode(CPPType, CPPTypeObjectPath, Location, Name.ToString(), bUndo))
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

bool UControlRigSelectNodeSpawner::IsTemplateNodeFilteredOut(FBlueprintActionFilter const& Filter) const
{
	for (UBlueprint* Blueprint : Filter.Context.Blueprints)
	{
		if (UControlRigBlueprint* RigBlueprint = Cast<UControlRigBlueprint>(Blueprint))
		{
			if (Filter.Context.Pins.Num() == 0)
			{
				return true;
			}

			FString PinPath = Filter.Context.Pins[0]->GetName();
			if (URigVMPin* ModelPin = RigBlueprint->Model->FindPin(PinPath))
			{
				return ModelPin->IsExecuteContext();
			}
		}
	}

	return false;
}

#undef LOCTEXT_NAMESPACE
