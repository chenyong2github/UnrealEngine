// Copyright Epic Games, Inc. All Rights Reserved.

#include "ControlRigVariableNodeSpawner.h"
#include "Graph/ControlRigGraphNode.h"
#include "EdGraphSchema_K2.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Classes/EditorStyleSettings.h"
#include "Editor/EditorEngine.h"
#include "ObjectEditorUtils.h"
#include "EditorCategoryUtils.h"
#include "K2Node_Variable.h"
#include "BlueprintNodeTemplateCache.h"
#include "ControlRigController.h"
#include "ControlRigBlueprint.h"
#include "ControlRigBlueprintUtils.h"
#include "Units/RigUnit.h"

#if WITH_EDITOR
#include "Editor.h"
#endif

#define LOCTEXT_NAMESPACE "ControlRigVariableNodeSpawner"

const TArray<FString> GControlRigVariableNodeSpawnerAllowedStructTypes = {
	TEXT("FBox"),
	TEXT("FBox2D"),
	TEXT("FColor"),
	TEXT("FLinearColor"),
	TEXT("FVector"),
	TEXT("FVector2D"),
	TEXT("FVector4"),
	TEXT("FRotator"),
	TEXT("FQuat"),
	TEXT("FPlane"),
	TEXT("FMatrix"),
	TEXT("FRotationMatrix"),
	TEXT("FScaleMatrix"),
	TEXT("FTransform"),
	TEXT("FEulerTransform")
};

const TArray<FString> GControlRigVariableNodeSpawnerAllowedEnumTypes = {
};

UControlRigVariableNodeSpawner* UControlRigVariableNodeSpawner::CreateFromPinType(const FEdGraphPinType& InPinType, const FText& InMenuDesc, const FText& InCategory, const FText& InTooltip)
{
	UControlRigVariableNodeSpawner* NodeSpawner = NewObject<UControlRigVariableNodeSpawner>(GetTransientPackage());
	NodeSpawner->EdGraphPinType = InPinType;
	NodeSpawner->NodeClass = UControlRigGraphNode::StaticClass();

	FBlueprintActionUiSpec& MenuSignature = NodeSpawner->DefaultMenuSignature;
	
	MenuSignature.MenuName = InMenuDesc;
	MenuSignature.Tooltip  = InTooltip;
	MenuSignature.Category = InCategory;
	MenuSignature.Keywords = FText::FromString(TEXT("Variable"));
	MenuSignature.Icon = UK2Node_Variable::GetVarIconFromPinType(NodeSpawner->GetVarType(), MenuSignature.IconTint);

	return NodeSpawner;
}

void UControlRigVariableNodeSpawner::Prime()
{
	// we expect that you don't need a node template to construct menu entries
	// from this, so we choose not to pre-cache one here
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
	UBlueprint* Blueprint = CastChecked<UBlueprint>(ParentGraph->GetOuter());
	FName MemberName = NAME_None;
	if(!bIsTemplateNode)
	{
#if WITH_EDITOR
		if (GEditor)
		{
			GEditor->CancelTransaction(0);
		}
#endif

		if (UControlRigBlueprint* RigBlueprint = Cast<UControlRigBlueprint>(Blueprint))
		{
			FName DataType = EdGraphPinType.PinCategory;
			if (UStruct* Struct = Cast<UStruct>(EdGraphPinType.PinSubCategoryObject))
			{
				DataType = Struct->GetFName();
			}

			FName Name = FControlRigBlueprintUtils::ValidateName(RigBlueprint, DefaultMenuSignature.MenuName.ToString());
			if (RigBlueprint->ModelController->AddParameter(*Name.ToString(), DataType, EControlRigModelParameterType::Hidden, Location))
			{
				MemberName = RigBlueprint->LastNameFromNotification;
				for (UEdGraphNode* Node : ParentGraph->Nodes)
				{
					if (UControlRigGraphNode* RigNode = Cast<UControlRigGraphNode>(Node))
					{
						if (RigNode->GetPropertyName() == MemberName)
						{
							NewNode = RigNode;
							break;
						}
					}
				}
			}
		}
	}
	else
	{
		NewNode = FControlRigBlueprintUtils::InstantiateGraphNodeForProperty(ParentGraph, *DefaultMenuSignature.MenuName.ToString(), Location, EdGraphPinType);
	}

	return NewNode;
}

bool UControlRigVariableNodeSpawner::IsTemplateNodeFilteredOut(FBlueprintActionFilter const& Filter) const
{
	if (EdGraphPinType.PinCategory == UEdGraphSchema_K2::PC_Struct)
	{
		UStruct* Struct = Cast<UStruct>(EdGraphPinType.PinSubCategoryObject);
		if (Struct == nullptr)
		{
			return true;
		}
		if (Struct->IsChildOf(FRigUnit::StaticStruct()))
		{
			return true;
		}

		UScriptStruct* ScriptStruct = Cast<UScriptStruct>(Struct);
		if (ScriptStruct == nullptr)
		{
			// for now filter out anything which is not a script struct
			return true;
		}

		// check if it is any of the math types
		FString StructName = ScriptStruct->GetStructCPPName();
		if (!GControlRigVariableNodeSpawnerAllowedStructTypes.Contains(StructName))
		{
			return true;
		}
	}
	else if (EdGraphPinType.PinCategory == UEdGraphSchema_K2::PC_Enum || 
			EdGraphPinType.PinCategory == UEdGraphSchema_K2::PC_Byte)
	{
		UEnum* Enum = Cast<UEnum>(EdGraphPinType.PinSubCategoryObject);
		if (Enum == nullptr)
		{
			return true;
		}

		if (!GControlRigVariableNodeSpawnerAllowedEnumTypes.Contains(Enum->CppType))
		{
			return true;
		}
	}
	else if (EdGraphPinType.PinCategory == UEdGraphSchema_K2::AllObjectTypes ||
			EdGraphPinType.PinCategory == UEdGraphSchema_K2::PC_Object ||
			EdGraphPinType.PinCategory == UEdGraphSchema_K2::PC_Delegate || 
			EdGraphPinType.PinCategory == UEdGraphSchema_K2::PC_Interface)
	{
		// we don't allow objects, delegate or interfaces
		return true;
	}
	return Super::IsTemplateNodeFilteredOut(Filter);
}

FEdGraphPinType UControlRigVariableNodeSpawner::GetVarType() const
{
	return EdGraphPinType;
}

#undef LOCTEXT_NAMESPACE
