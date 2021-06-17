// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimGraphNode_LinkedAnimGraph.h"

#include "AnimGraphNode_AssetPlayerBase.h"
#include "AnimGraphNode_CallFunction.h"
#include "BlueprintNodeSpawner.h"
#include "Animation/AnimBlueprint.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Blueprint.h"
#include "BlueprintActionDatabaseRegistrar.h"
#include "IAnimBlueprintCopyTermDefaultsContext.h"
#include "KismetCompiler.h"

#define LOCTEXT_NAMESPACE "UAnimGraphNode_LinkedAnimGraph"

void UAnimGraphNode_LinkedAnimGraph::PostPasteNode()
{
	// Clear incompatible target class
	if(UClass* InstanceClass = GetTargetClass())
	{
		if(UAnimBlueprint* LinkedBlueprint = Cast<UAnimBlueprint>(UBlueprint::GetBlueprintFromClass(InstanceClass)))
		{
			if(UAnimBlueprint* ThisBlueprint = GetAnimBlueprint())
			{
				if(!LinkedBlueprint->bIsTemplate && !ThisBlueprint->bIsTemplate && LinkedBlueprint->TargetSkeleton != ThisBlueprint->TargetSkeleton)
				{
					Node.InstanceClass = nullptr;
				}
			}
		}
	}
}

TArray<UEdGraph*> UAnimGraphNode_LinkedAnimGraph::GetExternalGraphs() const
{
	if(UClass* InstanceClass = GetTargetClass())
	{
		if(UAnimBlueprint* LinkedBlueprint = Cast<UAnimBlueprint>(UBlueprint::GetBlueprintFromClass(InstanceClass)))
		{
			for(UEdGraph* Graph : LinkedBlueprint->FunctionGraphs)
			{
				if(Graph->GetFName() == UEdGraphSchema_K2::GN_AnimGraph)
				{
					return { Graph };
				}
			}
		}
	}

	return TArray<UEdGraph*>();
}

void UAnimGraphNode_LinkedAnimGraph::SetupFromAsset(const FAssetData& InAssetData, bool bInIsTemplateNode)
{
	if(InAssetData.IsValid())
	{
		InAssetData.GetTagValue("TargetSkeleton", SkeletonName);
		if(SkeletonName == TEXT("None"))
		{
			SkeletonName.Empty();
		}

		if(!bInIsTemplateNode)
		{
			UAnimBlueprint* AnimBlueprint = CastChecked<UAnimBlueprint>(InAssetData.GetAsset());
			Node.InstanceClass = AnimBlueprint->GeneratedClass.Get();
		}	
	}
}

void UAnimGraphNode_LinkedAnimGraph::GetMenuActions(FBlueprintActionDatabaseRegistrar& InActionRegistrar) const
{
	UAnimGraphNode_AssetPlayerBase::GetMenuActionsHelper(
		InActionRegistrar,
		GetClass(),
		{ UAnimBlueprint::StaticClass()},
		{ },
		[](const FAssetData& InAssetData, UClass* InClass)
		{
			if(InAssetData.IsValid())
			{
				return FText::Format(LOCTEXT("MenuDescFormat", "{0} - Linked Anim Graph"), FText::FromName(InAssetData.AssetName));
			}
			else
			{
				return LOCTEXT("MenuDesc", "Linked Anim Graph");
			}
		},
		[](const FAssetData& InAssetData, UClass* InClass)
		{
			if(InAssetData.IsValid())
			{
				return FText::Format(LOCTEXT("MenuDescTooltipFormat", "Linked Anim Graph\n'{0}'"), FText::FromName(InAssetData.ObjectPath));
			}
			else
			{
				return LOCTEXT("MenuDescTooltip", "Linked Anim Graph");
			}
		},
		[](UEdGraphNode* InNewNode, bool bInIsTemplateNode, const FAssetData InAssetData)
		{
			UAnimGraphNode_LinkedAnimGraph* GraphNode = CastChecked<UAnimGraphNode_LinkedAnimGraph>(InNewNode);
			GraphNode->SetupFromAsset(InAssetData, bInIsTemplateNode);
		});
}

bool UAnimGraphNode_LinkedAnimGraph::IsActionFilteredOut(class FBlueprintActionFilter const& Filter)
{
	bool bIsFilteredOut = false;

	if(!SkeletonName.IsEmpty())
	{
		FBlueprintActionContext const& FilterContext = Filter.Context;

		for (UBlueprint* Blueprint : FilterContext.Blueprints)
		{
			if (UAnimBlueprint* AnimBlueprint = Cast<UAnimBlueprint>(Blueprint))
			{
				if(AnimBlueprint->TargetSkeleton != nullptr && !AnimBlueprint->TargetSkeleton->IsCompatibleSkeletonByAssetString(SkeletonName))
				{
					bIsFilteredOut = true;
					break;
				}
			}
			else
			{
				// Not an animation Blueprint, cannot use
				bIsFilteredOut = true;
				break;
			}
		}
	}
	return bIsFilteredOut;
}

#undef LOCTEXT_NAMESPACE