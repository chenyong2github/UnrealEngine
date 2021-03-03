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
	NodeSpawner->bIsLocalFunction = true;

	FBlueprintActionUiSpec& MenuSignature = NodeSpawner->DefaultMenuSignature;

	const FString Category = InFunction->GetNodeCategory();
	FString CategoryPrefix = TEXT("Local Functions");

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

UControlRigFunctionRefNodeSpawner* UControlRigFunctionRefNodeSpawner::CreateFromAssetData(const FAssetData& InAssetData, const FControlRigPublicFunctionData& InPublicFunction)
{
	UControlRigFunctionRefNodeSpawner* NodeSpawner = NewObject<UControlRigFunctionRefNodeSpawner>(GetTransientPackage());
	NodeSpawner->ReferencedAssetObjectPath = InAssetData.ObjectPath;
	NodeSpawner->ReferencedPublicFunctionData = InPublicFunction;
	NodeSpawner->NodeClass = UControlRigGraphNode::StaticClass();
	NodeSpawner->bIsLocalFunction = false;

	FBlueprintActionUiSpec& MenuSignature = NodeSpawner->DefaultMenuSignature;

	const FString Category = InPublicFunction.Category;
	FString CategoryPrefix =  InAssetData.ToSoftObjectPath().GetAssetName();

	if (!Category.IsEmpty() && !CategoryPrefix.IsEmpty())
	{
		CategoryPrefix += TEXT("|");
	}

	MenuSignature.MenuName = FText::FromName(InPublicFunction.Name);
	MenuSignature.Category = FText::FromString(CategoryPrefix + Category);
	MenuSignature.Keywords = FText::FromString(InPublicFunction.Keywords);

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
	FString SignatureString = TEXT("Invalid RigFunction");
	if(ReferencedFunctionPtr.IsValid())
	{
		if(UObject* FunctionLibrary = ReferencedFunctionPtr->GetOuter())
		{
			if(UObject* Blueprint = FunctionLibrary->GetOuter())
			{
				SignatureString = 
					FString::Printf(TEXT("RigFunction=%s::%s"),
					*Blueprint->GetPathName(),
					*ReferencedFunctionPtr->GetName());
			}
		}
	}
	else if(ReferencedAssetObjectPath.IsValid() && ReferencedPublicFunctionData.Name.IsValid())
	{
		SignatureString = 
			FString::Printf(TEXT("RigFunction=%s::%s"),
			*ReferencedAssetObjectPath.ToString(),
			*ReferencedPublicFunctionData.Name.ToString());
	}

	if(bIsLocalFunction)
	{
		SignatureString += TEXT(" (local)");
	}

	return FBlueprintNodeSignature(SignatureString);
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

	// if we are trying to build the real function ref - but we haven't loaded the asset yet...
	if(!FBlueprintNodeTemplateCache::IsTemplateOuter(ParentGraph))
	{
		if(!ReferencedFunctionPtr.IsValid() && !ReferencedAssetObjectPath.IsNone() && !ReferencedPublicFunctionData.Name.IsNone())
		{
			UControlRigBlueprint* ReferencedBlueprint = LoadObject<UControlRigBlueprint>(nullptr, *ReferencedAssetObjectPath.ToString());
			if(ReferencedBlueprint == nullptr)
			{
				return nullptr;
			}

			URigVMFunctionLibrary* FunctionLibrary = ReferencedBlueprint->GetLocalFunctionLibrary();
			if(FunctionLibrary == nullptr)
			{
				return nullptr;
			}

			URigVMLibraryNode* FunctionNode = FunctionLibrary->FindFunction(ReferencedPublicFunctionData.Name);
			if(FunctionNode == nullptr)
			{
				return nullptr;
			}

			ReferencedFunctionPtr = FunctionNode;
		}
	}

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
	else
	{
		// we are only going to get here if we are spawning a template node
		NewNode = NewObject<UControlRigGraphNode>(ParentGraph);
		ParentGraph->AddNode(NewNode, false);

		NewNode->CreateNewGuid();
		NewNode->PostPlacedNewNode();

		for(const FControlRigPublicFunctionArg& Arg : ReferencedPublicFunctionData.Arguments)
		{
			if(Arg.Direction ==  ERigVMPinDirection::Input ||
				Arg.Direction ==  ERigVMPinDirection::IO)
			{
				UEdGraphPin* InputPin = UEdGraphPin::CreatePin(NewNode);
				NewNode->Pins.Add(InputPin);

				InputPin->Direction = EGPD_Input;
				InputPin->PinType = Arg.GetPinType();
			}

			if(Arg.Direction ==  ERigVMPinDirection::Output ||
				Arg.Direction ==  ERigVMPinDirection::IO)
			{
				UEdGraphPin* OutputPin = UEdGraphPin::CreatePin(NewNode);
				NewNode->Pins.Add(OutputPin);

				OutputPin->Direction = EGPD_Output;
				OutputPin->PinType = Arg.GetPinType();
			}
		}

		NewNode->SetFlags(RF_Transactional);
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

bool UControlRigFunctionRefNodeSpawner::IsTemplateNodeFilteredOut(FBlueprintActionFilter const& Filter) const
{
	if(bIsLocalFunction)
	{
		if(ReferencedFunctionPtr.IsValid())
		{
			for (UBlueprint* Blueprint : Filter.Context.Blueprints)
			{
				if(Blueprint->GetOutermost() != ReferencedFunctionPtr.Get()->GetOutermost())
				{
					return true;
				}
			}
		}
	}
	else if(ReferencedAssetObjectPath.IsValid())
	{
		const FString ReferencedAssetObjectPathString = ReferencedAssetObjectPath.ToString();
		for (UBlueprint* Blueprint : Filter.Context.Blueprints)
		{
			if(Blueprint->GetPathName() == ReferencedAssetObjectPathString)
			{
				return true;
			}
		}
	}
	return false;
}

#undef LOCTEXT_NAMESPACE
