// Copyright Epic Games, Inc. All Rights Reserved.

#include "ControlRigRerouteNodeSpawner.h"
#include "Graph/ControlRigGraph.h"
#include "Graph/ControlRigGraphNode.h"
#include "Graph/ControlRigGraphSchema.h"
#include "EdGraphSchema_K2.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Classes/EditorStyleSettings.h"
#include "Editor/EditorEngine.h"
#include "ObjectEditorUtils.h"
#include "EditorCategoryUtils.h"
#include "K2Node_Variable.h"
#include "BlueprintNodeTemplateCache.h"
#include "RigVMModel/RigVMController.h"
#include "ControlRigBlueprint.h"
#include "ControlRigBlueprintUtils.h"
#include "RigVMModel/Nodes/RigVMRerouteNode.h"

#if WITH_EDITOR
#include "Editor.h"
#endif

#define LOCTEXT_NAMESPACE "ControlRigRerouteNodeSpawner"

UControlRigRerouteNodeSpawner* UControlRigRerouteNodeSpawner::CreateGeneric(const FText& InMenuDesc, const FText& InCategory, const FText& InTooltip)
{
	UControlRigRerouteNodeSpawner* NodeSpawner = NewObject<UControlRigRerouteNodeSpawner>(GetTransientPackage());
	NodeSpawner->NodeClass = UControlRigGraphNode::StaticClass();

	FBlueprintActionUiSpec& MenuSignature = NodeSpawner->DefaultMenuSignature;
	
	MenuSignature.MenuName = InMenuDesc;
	MenuSignature.Tooltip  = InTooltip;
	MenuSignature.Category = InCategory;
	MenuSignature.Keywords = FText::FromString(TEXT("Reroute,Elbow,Wire,Literal,Make Literal,Constant"));

	return NodeSpawner;
}

void UControlRigRerouteNodeSpawner::Prime()
{
	// we expect that you don't need a node template to construct menu entries
	// from this, so we choose not to pre-cache one here
}

FBlueprintNodeSignature UControlRigRerouteNodeSpawner::GetSpawnerSignature() const
{
	return FBlueprintNodeSignature(NodeClass);
}

FBlueprintActionUiSpec UControlRigRerouteNodeSpawner::GetUiSpec(FBlueprintActionContext const& Context, FBindingSet const& Bindings) const
{
	UEdGraph* TargetGraph = (Context.Graphs.Num() > 0) ? Context.Graphs[0] : nullptr;
	FBlueprintActionUiSpec MenuSignature = PrimeDefaultUiSpec(TargetGraph);

	DynamicUiSignatureGetter.ExecuteIfBound(Context, Bindings, &MenuSignature);
	return MenuSignature;
}

UEdGraphNode* UControlRigRerouteNodeSpawner::Invoke(UEdGraph* ParentGraph, FBindingSet const& Bindings, FVector2D const Location) const
{
	UControlRigGraphNode* NewNode = nullptr;

	bool const bIsTemplateNode = FBlueprintNodeTemplateCache::IsTemplateOuter(ParentGraph);

	// First create a backing member for our node
	UControlRigGraph* RigGraph = Cast<UControlRigGraph>(ParentGraph);
	check(RigGraph);
	UControlRigBlueprint* RigBlueprint = Cast<UControlRigBlueprint>(ParentGraph->GetOuter());
	check(RigBlueprint);

	FName MemberName = NAME_None;

#if WITH_EDITOR
	if (GEditor && !bIsTemplateNode)
	{
		GEditor->CancelTransaction(0);
	}
#endif

	if (bIsTemplateNode)
	{
		NewNode = NewObject<UControlRigGraphNode>(ParentGraph, TEXT("RerouteNode"));
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
	else
	{
		URigVMController* Controller = RigBlueprint->Controller;
		Controller->OpenUndoBracket(TEXT("Added Reroute Node."));

		FString PinPath;
		bool bIsInput = false;

		if (UControlRigGraphSchema* RigSchema = Cast<UControlRigGraphSchema>((UEdGraphSchema*)ParentGraph->GetSchema()))
		{
			bIsInput = RigSchema->bLastPinWasInput;

			if (const UEdGraphPin* LastPin = RigSchema->LastPinForCompatibleCheck)
			{
				if (URigVMPin* ModelPin = RigBlueprint->Model->FindPin(LastPin->GetName()))
				{
					PinPath = ModelPin->GetPinPath();
				}
			}
		}

		if (URigVMNode* ModelNode = Controller->AddRerouteNodeOnPin(PinPath, bIsInput, true, Location, FString(), true))
		{
			for (UEdGraphNode* Node : ParentGraph->Nodes)
			{
				if (UControlRigGraphNode* RigNode = Cast<UControlRigGraphNode>(Node))
				{
					if (RigNode->GetModelNodeName() == ModelNode->GetFName())
					{
						NewNode = RigNode;
						break;
					}
				}
			}

			if (NewNode)
			{
				Controller->ClearNodeSelection(true);
				Controller->SelectNode(ModelNode, true, true);
			}
			Controller->CloseUndoBracket();
		}
		else
		{
			Controller->CancelUndoBracket();
		}
	}

	return NewNode;
}

bool UControlRigRerouteNodeSpawner::IsTemplateNodeFilteredOut(FBlueprintActionFilter const& Filter) const
{
	for (UBlueprint* Blueprint : Filter.Context.Blueprints)
	{
		if (UControlRigBlueprint* RigBlueprint = Cast<UControlRigBlueprint>(Blueprint))
		{
			if (Filter.Context.Pins.Num() == 0)
			{
				return true;
			}
			for (UEdGraphPin* Pin : Filter.Context.Pins)
			{
				if (Pin->PinType.ContainerType != EPinContainerType::None)
				{
					return true;
				}
			}

			return false;
		}
	}
	return true;
}

#undef LOCTEXT_NAMESPACE
