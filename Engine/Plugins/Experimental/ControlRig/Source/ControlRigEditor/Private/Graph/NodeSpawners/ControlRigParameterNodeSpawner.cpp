// Copyright Epic Games, Inc. All Rights Reserved.

#include "ControlRigParameterNodeSpawner.h"
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
#include "RigVMModel/Nodes/RigVMParameterNode.h"

#if WITH_EDITOR
#include "Editor.h"
#endif

#define LOCTEXT_NAMESPACE "ControlRigParameterNodeSpawner"

UControlRigParameterNodeSpawner* UControlRigParameterNodeSpawner::CreateFromPinType(const FEdGraphPinType& InPinType, bool bInIsGetter, const FText& InMenuDesc, const FText& InCategory, const FText& InTooltip)
{
	UControlRigParameterNodeSpawner* NodeSpawner = NewObject<UControlRigParameterNodeSpawner>(GetTransientPackage());
	NodeSpawner->EdGraphPinType = InPinType;
	NodeSpawner->bIsGetter = bInIsGetter;
	NodeSpawner->NodeClass = UControlRigGraphNode::StaticClass();

	FBlueprintActionUiSpec& MenuSignature = NodeSpawner->DefaultMenuSignature;
	
	MenuSignature.MenuName = FText::FromString(FString::Printf(TEXT("%s %s"), bInIsGetter ? TEXT("Get") : TEXT("Set"), *InMenuDesc.ToString()));
	MenuSignature.Tooltip  = InTooltip;
	MenuSignature.Category = InCategory;
	MenuSignature.Keywords = FText::FromString(TEXT("Parameter"));
	MenuSignature.Icon = UK2Node_Variable::GetVarIconFromPinType(InPinType, MenuSignature.IconTint);

	return NodeSpawner;
}

void UControlRigParameterNodeSpawner::Prime()
{
	// we expect that you don't need a node template to construct menu entries
	// from this, so we choose not to pre-cache one here
}

FBlueprintNodeSignature UControlRigParameterNodeSpawner::GetSpawnerSignature() const
{
	return FBlueprintNodeSignature(NodeClass);
}

FBlueprintActionUiSpec UControlRigParameterNodeSpawner::GetUiSpec(FBlueprintActionContext const& Context, FBindingSet const& Bindings) const
{
	UEdGraph* TargetGraph = (Context.Graphs.Num() > 0) ? Context.Graphs[0] : nullptr;
	FBlueprintActionUiSpec MenuSignature = PrimeDefaultUiSpec(TargetGraph);

	DynamicUiSignatureGetter.ExecuteIfBound(Context, Bindings, &MenuSignature);
	return MenuSignature;
}

UEdGraphNode* UControlRigParameterNodeSpawner::Invoke(UEdGraph* ParentGraph, FBindingSet const& Bindings, FVector2D const Location) const
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

	FName DataType = EdGraphPinType.PinCategory;
	if (DataType == UEdGraphSchema_K2::PC_Int)
	{
		DataType = TEXT("int32");
	}
	else if (DataType == UEdGraphSchema_K2::PC_Name)
	{
		DataType = TEXT("FName");
	}
	else if (DataType == UEdGraphSchema_K2::PC_String)
	{
		DataType = TEXT("FString");
	}
	else if (UStruct* Struct = Cast<UStruct>(EdGraphPinType.PinSubCategoryObject))
	{
		DataType = *FString::Printf(TEXT("F%s"), *Struct->GetFName().ToString());
	}
	else if (UEnum* Enum = Cast<UEnum>(EdGraphPinType.PinSubCategoryObject))
	{
		DataType = *FString::Printf(TEXT("E%s"), *Enum->GetName());
	}

	FString DataTypeForParameterName = DataType.ToString();
	if (DataTypeForParameterName.StartsWith(TEXT("F"), ESearchCase::CaseSensitive) || DataTypeForParameterName.StartsWith(TEXT("E"), ESearchCase::CaseSensitive))
	{
		DataTypeForParameterName = DataTypeForParameterName.RightChop(1);
	}
	DataTypeForParameterName = DataTypeForParameterName.Left(1).ToUpper() + DataTypeForParameterName.RightChop(1);
	FString ParameterNamePrefix = FString::Printf(TEXT("%sParam"), *DataTypeForParameterName);

	TMap<FName, int32> NameToIndex;
	TArray<FRigVMGraphParameterDescription> ExistingParameters = Controller->GetGraph()->GetParameterDescriptions();
	for (int32 ParameterIndex = 0; ParameterIndex < ExistingParameters.Num(); ParameterIndex++)
	{
		NameToIndex.Add(ExistingParameters[ParameterIndex].Name, ParameterIndex);
	}

	FName ParameterName = URigVMController::GetUniqueName(*ParameterNamePrefix, [NameToIndex](const FName& InName) {
		return !NameToIndex.Contains(InName);
	});

	if (!bIsTemplateNode)
	{
		Controller->OpenUndoBracket(FString::Printf(TEXT("Add '%s' Parameter"), *DataType.ToString()));
	}

	if (URigVMNode* ModelNode = Controller->AddParameterNodeFromObjectPath(ParameterName, DataType.ToString(), FString(), bIsGetter, FString(), Location, FString(), !bIsTemplateNode))
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
