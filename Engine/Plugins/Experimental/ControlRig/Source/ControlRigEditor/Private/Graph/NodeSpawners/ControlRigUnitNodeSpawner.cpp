// Copyright Epic Games, Inc. All Rights Reserved.

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

#define LOCTEXT_NAMESPACE "ControlRigUnitNodeSpawner"

UControlRigUnitNodeSpawner* UControlRigUnitNodeSpawner::CreateFromStruct(UScriptStruct* InStruct, const FText& InMenuDesc, const FText& InCategory, const FText& InTooltip)
{
	UControlRigUnitNodeSpawner* NodeSpawner = NewObject<UControlRigUnitNodeSpawner>(GetTransientPackage());
	NodeSpawner->StructTemplate = InStruct;
	NodeSpawner->NodeClass = UControlRigGraphNode::StaticClass();

	FBlueprintActionUiSpec& MenuSignature = NodeSpawner->DefaultMenuSignature;
	
	MenuSignature.MenuName = InMenuDesc;
	MenuSignature.Tooltip  = InTooltip;
	MenuSignature.Category = InCategory;

	FString KeywordsMetadata, PrototypeNameMetadata;
	InStruct->GetStringMetaDataHierarchical(FRigVMStruct::KeywordsMetaName, &KeywordsMetadata);
	if(!PrototypeNameMetadata.IsEmpty())
	{
		if(KeywordsMetadata.IsEmpty())
		{
			KeywordsMetadata = PrototypeNameMetadata;
		}
		else
		{
			KeywordsMetadata = KeywordsMetadata + TEXT(",") + PrototypeNameMetadata;
		}
	}
	MenuSignature.Keywords = FText::FromString(KeywordsMetadata);

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

void UControlRigUnitNodeSpawner::Prime()
{
	// we expect that you don't need a node template to construct menu entries
	// from this, so we choose not to pre-cache one here
}

FBlueprintNodeSignature UControlRigUnitNodeSpawner::GetSpawnerSignature() const
{
	return FBlueprintNodeSignature(NodeClass);
}

FBlueprintActionUiSpec UControlRigUnitNodeSpawner::GetUiSpec(FBlueprintActionContext const& Context, FBindingSet const& Bindings) const
{
	UEdGraph* TargetGraph = (Context.Graphs.Num() > 0) ? Context.Graphs[0] : nullptr;
	FBlueprintActionUiSpec MenuSignature = PrimeDefaultUiSpec(TargetGraph);

	DynamicUiSignatureGetter.ExecuteIfBound(Context, Bindings, &MenuSignature);
	return MenuSignature;
}

UEdGraphNode* UControlRigUnitNodeSpawner::Invoke(UEdGraph* ParentGraph, FBindingSet const& Bindings, FVector2D const Location) const
{
	UControlRigGraphNode* NewNode = nullptr;

	if(StructTemplate)
	{
#if WITH_EDITOR
		if (GEditor)
		{
			GEditor->CancelTransaction(0);
		}
#endif

		UBlueprint* Blueprint = CastChecked<UBlueprint>(ParentGraph->GetOuter());
		NewNode = SpawnNode(ParentGraph, Blueprint, StructTemplate, Location);
	}

	return NewNode;
}

bool UControlRigUnitNodeSpawner::IsTemplateNodeFilteredOut(FBlueprintActionFilter const& Filter) const
{
	if (StructTemplate)
	{
		FString DeprecatedMetadata;
		StructTemplate->GetStringMetaDataHierarchical(FRigVMStruct::DeprecatedMetaName, &DeprecatedMetadata);
		if (!DeprecatedMetadata.IsEmpty())
		{
			return true;
		}
	}
	return Super::IsTemplateNodeFilteredOut(Filter);
}

UControlRigGraphNode* UControlRigUnitNodeSpawner::SpawnNode(UEdGraph* ParentGraph, UBlueprint* Blueprint, UScriptStruct* StructTemplate, FVector2D const Location)
{
	UControlRigGraphNode* NewNode = nullptr;
	UControlRigBlueprint* RigBlueprint = Cast<UControlRigBlueprint>(Blueprint);
	UControlRigGraph* RigGraph = Cast<UControlRigGraph>(ParentGraph);

	if (RigBlueprint != nullptr && RigGraph != nullptr)
	{
		bool const bIsTemplateNode = FBlueprintNodeTemplateCache::IsTemplateOuter(ParentGraph);
		bool const bUndo = !bIsTemplateNode;

		FName Name = bIsTemplateNode ? *StructTemplate->GetDisplayNameText().ToString() : FControlRigBlueprintUtils::ValidateName(RigBlueprint, StructTemplate->GetFName().ToString());
		URigVMController* Controller = bIsTemplateNode ? RigGraph->GetTemplateController() : RigBlueprint->Controller;

		if (!bIsTemplateNode)
		{
			Controller->OpenUndoBracket(FString::Printf(TEXT("Add '%s' Node"), *Name.ToString()));
		}

		if (URigVMStructNode* ModelNode = Controller->AddStructNode(StructTemplate, TEXT("Execute"), Location, Name.ToString(), bUndo))
		{
			NewNode = Cast<UControlRigGraphNode>(RigGraph->FindNodeForModelNodeName(ModelNode->GetFName()));

			if (NewNode && bUndo)
			{
				Controller->ClearNodeSelection(true);
				Controller->SelectNode(ModelNode, true, true);

				HookupMutableNode(ModelNode, RigBlueprint);
			}

#if WITH_EDITORONLY_DATA
			if (!bIsTemplateNode)
			{
				const FControlRigSettingsPerPinBool* ExpansionMapPtr = UControlRigSettings::Get()->RigUnitPinExpansion.Find(ModelNode->GetScriptStruct()->GetName());
				if (ExpansionMapPtr)
				{
					const FControlRigSettingsPerPinBool& ExpansionMap = *ExpansionMapPtr;

					for (const TPair<FString, bool>& Pair : ExpansionMap.Values)
					{
						FString PinPath = FString::Printf(TEXT("%s.%s"), *ModelNode->GetName(), *Pair.Key);
						Controller->SetPinExpansion(PinPath, Pair.Value, bUndo);
					}
				}

				FString UsedFilterString = SGraphActionMenu::LastUsedFilterText;
				if (!UsedFilterString.IsEmpty())
				{
					UsedFilterString = UsedFilterString.ToLower();

					if (UEnum* RigElementTypeEnum = StaticEnum<ERigElementType>())
					{
						ERigElementType UsedElementType = ERigElementType::None;
						int64 MaxEnumValue = RigElementTypeEnum->GetMaxEnumValue();

						for (int64 EnumValue = 0; EnumValue < MaxEnumValue; EnumValue++)
						{
							FString EnumText = RigElementTypeEnum->GetDisplayNameTextByValue(EnumValue).ToString().ToLower();
							if (UsedFilterString.Contains(EnumText))
							{
								UsedElementType = (ERigElementType)EnumValue;
								break;
							}
						}

						if (UsedElementType != ERigElementType::None)
						{
							TArray<URigVMPin*> ModelPins = ModelNode->GetAllPinsRecursively();
							for (URigVMPin* ModelPin : ModelPins)
							{
								if (ModelPin->GetCPPTypeObject() == FRigElementKey::StaticStruct())
								{
									if (URigVMPin* TypePin = ModelPin->FindSubPin(TEXT("Type")))
									{
										FString DefaultValue = RigElementTypeEnum->GetDisplayNameTextByValue((int64)UsedElementType).ToString();
										Controller->SetPinDefaultValue(TypePin->GetPinPath(), DefaultValue);
										break;
									}
								}
							}
						}
					}
				}
			}
#endif

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

void UControlRigUnitNodeSpawner::HookupMutableNode(URigVMNode* InModelNode, UControlRigBlueprint* InRigBlueprint)
{
	URigVMController* Controller = InRigBlueprint->Controller;

	Controller->ClearNodeSelection(true);
	Controller->SelectNode(InModelNode, true, true);

	// see if the node has an execute pin
	URigVMPin* ModelNodeExecutePin = nullptr;
	for (URigVMPin* Pin : InModelNode->GetPins())
	{
		if (Pin->GetScriptStruct())
		{
			if (Pin->GetScriptStruct()->IsChildOf(FRigVMExecuteContext::StaticStruct()))
			{
				if (Pin->GetDirection() == ERigVMPinDirection::IO || Pin->GetDirection() == ERigVMPinDirection::Input)
				{
					ModelNodeExecutePin = Pin;
					break;
				}
			}
		}
	}

	// we have an execute pin - so we have to hook it up
	if (ModelNodeExecutePin)
	{
		URigVMPin* ClosestOtherModelNodeExecutePin = nullptr;
		float ClosestDistance = FLT_MAX;

		const UControlRigGraphSchema* Schema = GetDefault<UControlRigGraphSchema>();
		if (Schema->LastPinForCompatibleCheck)
		{
			URigVMPin* FromPin = Controller->GetGraph()->FindPin(Schema->LastPinForCompatibleCheck->GetName());
			if (FromPin)
			{
				if (FromPin->IsExecuteContext() &&
					(FromPin->GetDirection() == ERigVMPinDirection::IO || FromPin->GetDirection() == ERigVMPinDirection::Output))
				{
					ClosestOtherModelNodeExecutePin = FromPin;
				}
			}

		}

		if (ClosestOtherModelNodeExecutePin == nullptr)
		{
			for (URigVMNode* OtherModelNode : Controller->GetGraph()->GetNodes())
			{
				if (OtherModelNode == InModelNode)
				{
					continue;
				}

				for (URigVMPin* Pin : OtherModelNode->GetPins())
				{
					if (Pin->GetScriptStruct())
					{
						if (Pin->GetScriptStruct()->IsChildOf(FRigVMExecuteContext::StaticStruct()))
						{
							if (Pin->GetDirection() == ERigVMPinDirection::IO || Pin->GetDirection() == ERigVMPinDirection::Output)
							{
								if (Pin->GetLinkedTargetPins().Num() == 0)
								{
									float Distance = (OtherModelNode->GetPosition() - InModelNode->GetPosition()).Size();
									if (Distance < ClosestDistance)
									{
										ClosestOtherModelNodeExecutePin = Pin;
										ClosestDistance = Distance;
										break;
									}
								}
							}
						}
					}
				}
			}
		}

		if (ClosestOtherModelNodeExecutePin)
		{
			Controller->AddLink(ClosestOtherModelNodeExecutePin->GetPinPath(), ModelNodeExecutePin->GetPinPath(), true);
		}
	}
}

#undef LOCTEXT_NAMESPACE
