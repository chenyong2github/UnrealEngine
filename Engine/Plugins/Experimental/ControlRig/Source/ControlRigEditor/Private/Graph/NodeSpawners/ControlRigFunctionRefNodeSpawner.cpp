// Copyright Epic Games, Inc. All Rights Reserved.

#include "ControlRigFunctionRefNodeSpawner.h"
#include "ControlRigUnitNodeSpawner.h"
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
#include "ControlRigBlueprintUtils.h"
#include "ScopedTransaction.h"
#include "ControlRig/Private/Units/Execution/RigUnit_BeginExecution.h"
#include "ControlRig.h"
#include "Settings/ControlRigSettings.h"

#if WITH_EDITOR
#include "Editor.h"
#include "SGraphActionMenu.h"
#endif

#define LOCTEXT_NAMESPACE "ControlRigFunctionRefNodeSpawner"

UControlRigFunctionRefNodeSpawner* UControlRigFunctionRefNodeSpawner::CreateFromFunction(URigVMLibraryNode* InFunction)
{
	check(InFunction);

	UControlRigFunctionRefNodeSpawner* NodeSpawner = NewObject<UControlRigFunctionRefNodeSpawner>(GetTransientPackage());
	NodeSpawner->ReferencedFunctionPtr = InFunction;
	NodeSpawner->NodeClass = UControlRigGraphNode::StaticClass();

	FBlueprintActionUiSpec& MenuSignature = NodeSpawner->DefaultMenuSignature;

	FString Category = InFunction->GetNodeCategory();
	FString CategoryPrefix;

	if (UControlRigBlueprint* Blueprint = Cast<UControlRigBlueprint>(InFunction->GetLibrary()->GetOuter()))
	{
		/*if(Blueprint->IsFunctionLibrary())
		{
			// todo
			ensure(false);
		}
		else
		*/
		{
			CategoryPrefix = TEXT("Local Functions");
		}
	}

	if (!Category.IsEmpty() && !CategoryPrefix.IsEmpty())
	{
		CategoryPrefix += TEXT("|");
	}

	MenuSignature.MenuName = FText::FromString(InFunction->GetName());
	MenuSignature.Tooltip = InFunction->GetToolTipText();
	MenuSignature.Category = FText::FromString(CategoryPrefix + Category);
	MenuSignature.Keywords = FText::FromString(InFunction->GetNodeKeywords());

	// add at least one character, so that PrimeDefaultUiSpec() doesn't 
	// attempt to query the template node
	if (MenuSignature.Keywords.IsEmpty())
	{
		// want to set it to something so we won't end up back in this condition
		MenuSignature.Keywords = FText::FromString(TEXT(" "));
	}

	MenuSignature.Icon = FSlateIcon("EditorStyle", "Kismet.AllClasses.FunctionIcon");

	return NodeSpawner;
}

void UControlRigFunctionRefNodeSpawner::Prime()
{
	// we expect that you don't need a node template to construct menu entries
	// from this, so we choose not to pre-cache one here
}

FBlueprintNodeSignature UControlRigFunctionRefNodeSpawner::GetSpawnerSignature() const
{
	if(ReferencedFunctionPtr.IsValid())
	{
		return FBlueprintNodeSignature(TEXT("RigFunction=") + ReferencedFunctionPtr->GetPathName());
	}
	return FBlueprintNodeSignature(TEXT("Invalid RigFunction"));
}

FBlueprintActionUiSpec UControlRigFunctionRefNodeSpawner::GetUiSpec(FBlueprintActionContext const& Context, FBindingSet const& Bindings) const
{
	UEdGraph* TargetGraph = (Context.Graphs.Num() > 0) ? Context.Graphs[0] : nullptr;
	FBlueprintActionUiSpec MenuSignature = PrimeDefaultUiSpec(TargetGraph);

	DynamicUiSignatureGetter.ExecuteIfBound(Context, Bindings, &MenuSignature);
	return MenuSignature;
}

UEdGraphNode* UControlRigFunctionRefNodeSpawner::Invoke(UEdGraph* ParentGraph, FBindingSet const& Bindings, FVector2D const Location) const
{
	UControlRigGraphNode* NewNode = nullptr;

	if(ReferencedFunctionPtr.IsValid())
	{
#if WITH_EDITOR
		if (GEditor)
		{
			GEditor->CancelTransaction(0);
		}
#endif

		UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForGraph(ParentGraph);
		NewNode = SpawnNode(ParentGraph, Blueprint, ReferencedFunctionPtr.Get(), Location);
	}

	return NewNode;
}

UControlRigGraphNode* UControlRigFunctionRefNodeSpawner::SpawnNode(UEdGraph* ParentGraph, UBlueprint* Blueprint, URigVMLibraryNode* InFunction, FVector2D const Location)
{
	UControlRigGraphNode* NewNode = nullptr;
	UControlRigBlueprint* RigBlueprint = Cast<UControlRigBlueprint>(Blueprint);
	UControlRigGraph* RigGraph = Cast<UControlRigGraph>(ParentGraph);

	if (RigBlueprint != nullptr && RigGraph != nullptr)
	{
		bool const bIsTemplateNode = FBlueprintNodeTemplateCache::IsTemplateOuter(ParentGraph);
		bool const bUndo = !bIsTemplateNode;

		FName Name = bIsTemplateNode ? *InFunction->GetName() : FControlRigBlueprintUtils::ValidateName(RigBlueprint, InFunction->GetName());
		URigVMController* Controller = bIsTemplateNode ? RigGraph->GetTemplateController() : RigBlueprint->GetController(ParentGraph);

		if (!bIsTemplateNode)
		{
			Controller->OpenUndoBracket(FString::Printf(TEXT("Add '%s' Node"), *Name.ToString()));
		}

		if (URigVMFunctionReferenceNode* ModelNode = Controller->AddFunctionReferenceNode(InFunction, Location, Name.ToString(), bUndo))
		{
			NewNode = Cast<UControlRigGraphNode>(RigGraph->FindNodeForModelNodeName(ModelNode->GetFName()));
			check(NewNode);

			if (NewNode && bUndo)
			{
				Controller->ClearNodeSelection(true);
				Controller->SelectNode(ModelNode, true, true);

				UControlRigUnitNodeSpawner::HookupMutableNode(ModelNode, RigBlueprint);
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
	}
	return NewNode;
}


#undef LOCTEXT_NAMESPACE
