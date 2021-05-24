// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimGraphNode_LinkedAnimGraph.h"

#include "AnimGraphNode_AssetPlayerBase.h"
#include "BlueprintNodeSpawner.h"
#include "Animation/AnimBlueprint.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Blueprint.h"
#include "BlueprintActionDatabaseRegistrar.h"

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
				if(LinkedBlueprint->TargetSkeleton != ThisBlueprint->TargetSkeleton)
				{
					Node.InstanceClass = nullptr;
				}
			}
		}
	}
}

void UAnimGraphNode_LinkedAnimGraph::SetupFromAsset(const FAssetData& InAssetData, bool bInIsTemplateNode)
{
	InAssetData.GetTagValue("TargetSkeleton", SkeletonName);
	
	if(!bInIsTemplateNode)
	{
		UAnimBlueprint* AnimBlueprint = CastChecked<UAnimBlueprint>(InAssetData.GetAsset());
		Node.InstanceClass = AnimBlueprint->GeneratedClass.Get();
	}
}

void UAnimGraphNode_LinkedAnimGraph::GetMenuActions(FBlueprintActionDatabaseRegistrar& InActionRegistrar) const
{
	// Anim graph node base class will allow us to spawn an 'empty' node
	UAnimGraphNode_Base::GetMenuActions(InActionRegistrar);

	UAnimGraphNode_AssetPlayerBase::GetMenuActionsHelper(
		InActionRegistrar,
		GetClass(),
		{ UAnimBlueprint::StaticClass()},
		{ },
		[](const FAssetData& InAssetData)
		{
			return FText::Format(LOCTEXT("MenuDescFormat", "{0} - Linked Anim Graph"), FText::FromName(InAssetData.AssetName));
		},
		[](const FAssetData& InAssetData)
		{
			return FText::Format(LOCTEXT("MenuDescTooltipFormat", "Linked Anim Graph\n'{0}'"), FText::FromName(InAssetData.ObjectPath));
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
				if(!AnimBlueprint->TargetSkeleton->IsCompatibleSkeletonByAssetString(SkeletonName))
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