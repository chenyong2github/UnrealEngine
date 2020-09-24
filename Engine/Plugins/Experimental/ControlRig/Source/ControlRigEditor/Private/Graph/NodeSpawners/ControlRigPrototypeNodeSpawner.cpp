// Copyright Epic Games, Inc. All Rights Reserved.

#include "ControlRigPrototypeNodeSpawner.h"
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
#include "ControlRig.h"

#if WITH_EDITOR
#include "Editor.h"
#endif

#define LOCTEXT_NAMESPACE "ControlRigPrototypeNodeSpawner"

UControlRigPrototypeNodeSpawner* UControlRigPrototypeNodeSpawner::CreateFromNotation(const FName& InNotation, const FText& InMenuDesc, const FText& InCategory, const FText& InTooltip)
{
	UControlRigPrototypeNodeSpawner* NodeSpawner = NewObject<UControlRigPrototypeNodeSpawner>(GetTransientPackage());
	NodeSpawner->PrototypeNotation = InNotation;
	NodeSpawner->NodeClass = UControlRigGraphNode::StaticClass();

	FBlueprintActionUiSpec& MenuSignature = NodeSpawner->DefaultMenuSignature;
	
	MenuSignature.MenuName = InMenuDesc;
	MenuSignature.Tooltip  = InTooltip;
	MenuSignature.Category = InCategory;

	if (const FRigVMPrototype* Prototype = FRigVMRegistry::Get().FindPrototype(InNotation))
	{
#if WITH_EDITOR
		FString KeywordsMetadata = Prototype->GetKeywords();
		MenuSignature.Keywords = FText::FromString(KeywordsMetadata);
#endif
	}

	// add at least one character, so that PrimeDefaultUiSpec() doesn't 
	// attempt to query the template node
	//
	// @TODO: maybe UPROPERTY() fields should have keyword metadata like functions
	if (MenuSignature.Keywords.IsEmpty())
	{
		// want to set it to something so we won't end up back in this condition
		MenuSignature.Keywords = FText::FromString(TEXT(" "));
	}

	// @TODO: should use details customization-like extensibility system to provide editor only data like this
	MenuSignature.Icon = FSlateIcon(TEXT("ControlRigEditorStyle"), TEXT("ControlRig.RigUnit"));

	return NodeSpawner;
}

void UControlRigPrototypeNodeSpawner::Prime()
{
	// we expect that you don't need a node template to construct menu entries
	// from this, so we choose not to pre-cache one here
}

FBlueprintNodeSignature UControlRigPrototypeNodeSpawner::GetSpawnerSignature() const
{
	return FBlueprintNodeSignature(NodeClass);
}

FBlueprintActionUiSpec UControlRigPrototypeNodeSpawner::GetUiSpec(FBlueprintActionContext const& Context, FBindingSet const& Bindings) const
{
	UEdGraph* TargetGraph = (Context.Graphs.Num() > 0) ? Context.Graphs[0] : nullptr;
	FBlueprintActionUiSpec MenuSignature = PrimeDefaultUiSpec(TargetGraph);

	DynamicUiSignatureGetter.ExecuteIfBound(Context, Bindings, &MenuSignature);
	return MenuSignature;
}

UEdGraphNode* UControlRigPrototypeNodeSpawner::Invoke(UEdGraph* ParentGraph, FBindingSet const& Bindings, FVector2D const Location) const
{
	UControlRigGraphNode* NewNode = nullptr;

	if(!PrototypeNotation.IsNone())
	{
#if WITH_EDITOR
		if (GEditor)
		{
			GEditor->CancelTransaction(0);
		}
#endif

		UBlueprint* Blueprint = CastChecked<UBlueprint>(ParentGraph->GetOuter());
		NewNode = SpawnNode(ParentGraph, Blueprint, PrototypeNotation, Location);
	}

	return NewNode;
}

UControlRigGraphNode* UControlRigPrototypeNodeSpawner::SpawnNode(UEdGraph* ParentGraph, UBlueprint* Blueprint, const FName& InNotation, FVector2D const Location)
{
	UControlRigGraphNode* NewNode = nullptr;
	UControlRigBlueprint* RigBlueprint = Cast<UControlRigBlueprint>(Blueprint);
	UControlRigGraph* RigGraph = Cast<UControlRigGraph>(ParentGraph);

	if (RigBlueprint != nullptr && RigGraph != nullptr)
	{
		bool const bIsTemplateNode = FBlueprintNodeTemplateCache::IsTemplateOuter(ParentGraph);
		bool const bUndo = !bIsTemplateNode;

		const FRigVMPrototype* Prototype = FRigVMRegistry::Get().FindPrototype(InNotation);
		if (Prototype == nullptr)
		{
			return nullptr;
		}

		FName Name = bIsTemplateNode ? Prototype->GetName() : FControlRigBlueprintUtils::ValidateName(RigBlueprint, Prototype->GetName().ToString());
		URigVMController* Controller = bIsTemplateNode ? RigGraph->GetTemplateController() : RigBlueprint->Controller;

		if (!bIsTemplateNode)
		{
			Controller->OpenUndoBracket(FString::Printf(TEXT("Add '%s' Node"), *Name.ToString()));
		}

		if (URigVMPrototypeNode* ModelNode = Controller->AddPrototypeNode(InNotation, Location, Name.ToString(), bUndo))
		{
			NewNode = Cast<UControlRigGraphNode>(RigGraph->FindNodeForModelNodeName(ModelNode->GetFName()));

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
