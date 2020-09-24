// Copyright Epic Games, Inc. All Rights Reserved.

#include "ControlRigVariableNodeSpawner.h"
#include "Graph/ControlRigGraph.h"
#include "Graph/ControlRigGraphNode.h"
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
#include "RigVMModel/Nodes/RigVMVariableNode.h"

#if WITH_EDITOR
#include "Editor.h"
#endif

#define LOCTEXT_NAMESPACE "ControlRigVariableNodeSpawner"

UControlRigVariableNodeSpawner* UControlRigVariableNodeSpawner::CreateFromExternalVariable(UControlRigBlueprint* InBlueprint, const FRigVMExternalVariable& InExternalVariable, bool bInIsGetter, const FText& InMenuDesc, const FText& InCategory, const FText& InTooltip)
{
	UControlRigVariableNodeSpawner* NodeSpawner = NewObject<UControlRigVariableNodeSpawner>(GetTransientPackage());
	NodeSpawner->Blueprint = InBlueprint;
	NodeSpawner->ExternalVariable = InExternalVariable;
	NodeSpawner->bIsGetter = bInIsGetter;
	NodeSpawner->NodeClass = UControlRigGraphNode::StaticClass();

	FBlueprintActionUiSpec& MenuSignature = NodeSpawner->DefaultMenuSignature;
	
	MenuSignature.MenuName = FText::FromString(FString::Printf(TEXT("%s %s"), bInIsGetter ? TEXT("Get") : TEXT("Set"), *InMenuDesc.ToString()));
	MenuSignature.Tooltip  = InTooltip;
	MenuSignature.Category = InCategory;
	MenuSignature.Keywords = FText::FromString(TEXT("Variable"));

	FEdGraphPinType PinType = UControlRig::GetPinTypeFromExternalVariable(InExternalVariable);
	MenuSignature.Icon = UK2Node_Variable::GetVarIconFromPinType(PinType, MenuSignature.IconTint);

	return NodeSpawner;
}

void UControlRigVariableNodeSpawner::Prime()
{
	// we expect that you don't need a node template to construct menu entries
	// from this, so we choose not to pre-cache one here
}

bool UControlRigVariableNodeSpawner::IsTemplateNodeFilteredOut(FBlueprintActionFilter const& Filter) const
{
	if (Blueprint.IsValid())
	{
		if (!Filter.Context.Blueprints.Contains(Blueprint.Get()))
		{
			return true;
		}
	}
	return Super::IsTemplateNodeFilteredOut(Filter);
}

FBlueprintNodeSignature UControlRigVariableNodeSpawner::GetSpawnerSignature() const
{
	return FBlueprintNodeSignature(NodeClass);
}

FBlueprintActionUiSpec UControlRigVariableNodeSpawner::GetUiSpec(FBlueprintActionContext const& Context, FBindingSet const& Bindings) const
{
	UEdGraph* TargetGraph = (Context.Graphs.Num() > 0) ? Context.Graphs[0] : nullptr;
	FBlueprintActionUiSpec MenuSignature = PrimeDefaultUiSpec(TargetGraph);

	DynamicUiSignatureGetter.ExecuteIfBound(Context, Bindings, &MenuSignature);
	return MenuSignature;
}

UEdGraphNode* UControlRigVariableNodeSpawner::Invoke(UEdGraph* ParentGraph, FBindingSet const& Bindings, FVector2D const Location) const
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

	URigVMController* Controller = bIsTemplateNode ? RigGraph->GetTemplateController() : RigBlueprint->Controller;

	if (!bIsTemplateNode)
	{
		Controller->OpenUndoBracket(TEXT("Add Variable"));
	}

	FString ObjectPath;
	if (ExternalVariable.TypeObject)
	{
		ObjectPath = ExternalVariable.TypeObject->GetPathName();
	}

	FString TypeName = ExternalVariable.TypeName.ToString();
	if (ExternalVariable.bIsArray)
	{
		TypeName = FString::Printf(TEXT("TArray<%s>"), *TypeName);
	}

	if (URigVMNode* ModelNode = Controller->AddVariableNodeFromObjectPath(ExternalVariable.Name, TypeName, ObjectPath, bIsGetter, FString(), Location, FString(), !bIsTemplateNode))
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

		if (!bIsTemplateNode)
		{
			if (NewNode)
			{
				Controller->ClearNodeSelection(true);
				Controller->SelectNode(ModelNode, true, true);
			}
			Controller->CloseUndoBracket();
		}
	}
	else
	{
		if (!bIsTemplateNode)
		{
			Controller->CancelUndoBracket();
		}
	}


	return NewNode;
}

#undef LOCTEXT_NAMESPACE
