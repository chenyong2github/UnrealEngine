// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

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

#define LOCTEXT_NAMESPACE "ControlRigUnitNodeSpawner"

UControlRigUnitNodeSpawner* UControlRigUnitNodeSpawner::CreateFromStruct(UStruct* InStruct, const FText& InMenuDesc, const FText& InCategory, const FText& InTooltip)
{
	UControlRigUnitNodeSpawner* NodeSpawner = NewObject<UControlRigUnitNodeSpawner>(GetTransientPackage());
	NodeSpawner->StructTemplate = InStruct;
	NodeSpawner->NodeClass = UControlRigGraphNode::StaticClass();

	FBlueprintActionUiSpec& MenuSignature = NodeSpawner->DefaultMenuSignature;
	
	MenuSignature.MenuName = InMenuDesc;
	MenuSignature.Tooltip  = InTooltip;
	MenuSignature.Category = InCategory;

	FString KeywordsMetadata, PrototypeNameMetadata;
	InStruct->GetStringMetaDataHierarchical(UControlRig::KeywordsMetaName, &KeywordsMetadata);
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
		const FScopedTransaction Transaction(LOCTEXT("AddRigUnitNode", "Add Rig Unit Node"));

		UBlueprint* Blueprint = CastChecked<UBlueprint>(ParentGraph->GetOuter());
		NewNode = SpawnNode(ParentGraph, Blueprint, StructTemplate, Location);

		bool const bIsTemplateNode = FBlueprintNodeTemplateCache::IsTemplateOuter(ParentGraph);
		if (NewNode != nullptr && !bIsTemplateNode)
		{
			const TArray<TSharedRef<FControlRigField>>& NewExecutionInfos = NewNode->GetExecutionVariableInfo();
			if(NewExecutionInfos.Num() > 0)
			{
				float ClosestDistance = FLT_MAX;
				UControlRigGraphNode* ClosestRigNode = nullptr;
				UEdGraphPin* ClosestExecutionPin = nullptr;

				// try to hook up the rig execution pin automatically for the user
				for (UEdGraphNode* Node : ParentGraph->Nodes)
				{
					if (UControlRigGraphNode* RigNode = Cast<UControlRigGraphNode>(Node))
					{
						if (RigNode == NewNode)
						{
							continue;
						}

						TArray<TSharedRef<FControlRigField>> CurrentExecutionInfos = RigNode->GetExecutionVariableInfo();
						CurrentExecutionInfos.Append(RigNode->GetInputOutputVariableInfo());
						CurrentExecutionInfos.Append(RigNode->GetOutputVariableInfo());
						for(const TSharedRef<FControlRigField>& ExecutionInfo : CurrentExecutionInfos)
						{
							if (ExecutionInfo->OutputPin != nullptr)
							{
								if (ExecutionInfo->OutputPin->LinkedTo.Num() == 0 &&
									ExecutionInfo->OutputPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Struct &&
									ExecutionInfo->OutputPin->PinType.PinSubCategoryObject == FControlRigExecuteContext::StaticStruct())
								{
									float Distance = (Location - FVector2D((float)RigNode->NodePosX, (float)RigNode->NodePosY)).SizeSquared();
									if (Distance < ClosestDistance)
									{
										ClosestDistance = Distance;
										ClosestRigNode = RigNode;
										ClosestExecutionPin = CurrentExecutionInfos[0]->OutputPin;
									}
								}
							}
						}
					}
				}

				// if we didn't find a closest rig node with an execution pin - let's create one
				if (ClosestRigNode == nullptr)
				{
					ClosestRigNode = SpawnNode(ParentGraph, Blueprint, FRigUnit_BeginExecution::StaticStruct(), Location - FVector2D(200.f, 0.f));
					ClosestExecutionPin = ClosestRigNode->Pins[0];
				}

				UControlRigGraph* RigGraph = CastChecked<UControlRigGraph>(ParentGraph);
				const UControlRigGraphSchema* ControlRigSchema = RigGraph->GetControlRigGraphSchema();
				for (const TSharedRef<FControlRigField>& NewExecutionInfo : NewExecutionInfos)
				{
					if (NewExecutionInfo->InputPin != nullptr)
					{
						ControlRigSchema->TryCreateConnection(ClosestExecutionPin, NewExecutionInfo->InputPin);
					}
				}
			}
		}
	}

	return NewNode;
}

bool UControlRigUnitNodeSpawner::IsTemplateNodeFilteredOut(FBlueprintActionFilter const& Filter) const
{
	if (StructTemplate)
	{
		FString DeprecatedMetadata;
		StructTemplate->GetStringMetaDataHierarchical(UControlRig::DeprecatedMetaName, &DeprecatedMetadata);
		if (!DeprecatedMetadata.IsEmpty())
		{
			return true;
		}
	}
	return Super::IsTemplateNodeFilteredOut(Filter);
}

UControlRigGraphNode* UControlRigUnitNodeSpawner::SpawnNode(UEdGraph* ParentGraph, UBlueprint* Blueprint, UStruct* StructTemplate, FVector2D const Location)
{
	UControlRigGraphNode* NewNode = nullptr;

	bool const bIsTemplateNode = FBlueprintNodeTemplateCache::IsTemplateOuter(ParentGraph);

	// First create a backing member for our node
	FName MemberName = NAME_None;

	if (!bIsTemplateNode)
	{
		MemberName = FControlRigBlueprintUtils::AddUnitMember(Blueprint, StructTemplate);
	}
	else
	{
		MemberName = StructTemplate->GetFName();
	}

	if (MemberName != NAME_None)
	{
		NewNode = FControlRigBlueprintUtils::InstantiateGraphNodeForProperty(ParentGraph, MemberName, Location);
	}

	return NewNode;
}

#undef LOCTEXT_NAMESPACE
