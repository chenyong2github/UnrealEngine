// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimGraphNode_BlendSpaceGraph.h"
#include "BlueprintNodeSpawner.h"
#include "BlueprintActionDatabaseRegistrar.h"
#include "Animation/AimOffsetBlendSpace.h"
#include "Animation/AimOffsetBlendSpace1D.h"

#define LOCTEXT_NAMESPACE "UAnimGraphNode_BlendSpaceGraph"

FText UAnimGraphNode_BlendSpaceGraph::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	if(BlendSpaceGraph || BlendSpace)
	{
		const FText BlendSpaceName = FText::FromString(BlendSpaceGraph ? GetBlendSpaceGraphName() : GetBlendSpaceName());

		if(TitleType == ENodeTitleType::EditableTitle)
		{
			return BlendSpaceName;
		}
		else if (TitleType == ENodeTitleType::ListView || TitleType == ENodeTitleType::MenuTitle)
		{
			FFormatNamedArguments Args;
			Args.Add(TEXT("BlendSpaceName"), BlendSpaceName);
			return FText::Format(LOCTEXT("BlendspaceListTitle", "Blendspace '{BlendSpaceName}'"), Args);
		}
		else
		{
			FFormatNamedArguments TitleArgs;
			TitleArgs.Add(TEXT("BlendSpaceName"), BlendSpaceName);
			FText Title = FText::Format(LOCTEXT("BlendSpaceFullTitle", "{BlendSpaceName}\nBlendspace"), TitleArgs);

			if ((TitleType == ENodeTitleType::FullTitle) && (Node.GetGroupName() != NAME_None))
			{
				FFormatNamedArguments Args;
				Args.Add(TEXT("Title"), Title);
				Args.Add(TEXT("SyncGroupName"), FText::FromName(Node.GetGroupName()));
				Title = FText::Format(LOCTEXT("BlendSpaceNodeGroupSubtitle", "{Title}\nSync group {SyncGroupName}"), Args);
			}

			return Title;
		}
	}
	else if(BlendSpaceClass.Get())
	{
		return BlendSpaceClass.Get()->GetDisplayNameText();
	}
	else
	{
		return LOCTEXT("EmptyBlendspaceListTitle", "Blendspace");
	}
}

void UAnimGraphNode_BlendSpaceGraph::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
	struct GetMenuActions_Utils
	{
		static void SetNodeBlendSpace(UEdGraphNode* NewNode, bool bIsTemplateNode, TWeakObjectPtr<UBlendSpace> BlendSpace)
		{
			UAnimGraphNode_BlendSpaceGraph* BlendSpaceNode = CastChecked<UAnimGraphNode_BlendSpaceGraph>(NewNode);
			BlendSpaceNode->SetupFromAsset(BlendSpace.Get(), bIsTemplateNode);
		}

		static UBlueprintNodeSpawner* MakeBlendSpaceAction(TSubclassOf<UEdGraphNode> const NodeClass, const UBlendSpace* InBlendSpace)
		{
			UBlueprintNodeSpawner* NodeSpawner = nullptr;

			bool const bIsAimOffset = InBlendSpace->IsA(UAimOffsetBlendSpace::StaticClass()) ||
									  InBlendSpace->IsA(UAimOffsetBlendSpace1D::StaticClass());
			if (!bIsAimOffset)
			{
				NodeSpawner = UBlueprintNodeSpawner::Create(NodeClass);
				check(NodeSpawner != nullptr);

				TWeakObjectPtr<UBlendSpace> BlendSpacePtr = MakeWeakObjectPtr(const_cast<UBlendSpace*>(InBlendSpace));
				NodeSpawner->CustomizeNodeDelegate = UBlueprintNodeSpawner::FCustomizeNodeDelegate::CreateStatic(GetMenuActions_Utils::SetNodeBlendSpace, BlendSpacePtr);
			}	
			return NodeSpawner;
		}

		static void SetNodeBlendSpaceClass(UEdGraphNode* NewNode, bool bIsTemplateNode, TSubclassOf<UBlendSpace> InBlendSpaceClass)
		{
			UAnimGraphNode_BlendSpaceGraph* BlendSpaceNode = CastChecked<UAnimGraphNode_BlendSpaceGraph>(NewNode);
			BlendSpaceNode->SetupFromClass(InBlendSpaceClass, bIsTemplateNode);
		}

		static UBlueprintNodeSpawner* MakeBlendSpaceAction(TSubclassOf<UEdGraphNode> const NodeClass, TSubclassOf<UBlendSpace> InBlendSpaceClass)
		{
			UBlueprintNodeSpawner* NodeSpawner = UBlueprintNodeSpawner::Create(NodeClass);
			check(NodeSpawner != nullptr);

			NodeSpawner->CustomizeNodeDelegate = UBlueprintNodeSpawner::FCustomizeNodeDelegate::CreateStatic(GetMenuActions_Utils::SetNodeBlendSpaceClass, InBlendSpaceClass);
	
			return NodeSpawner;
		}
	};

	// Add the non-asset based cases
	ActionRegistrar.AddBlueprintAction(GetMenuActions_Utils::MakeBlendSpaceAction(GetClass(), UBlendSpace::StaticClass()));
	ActionRegistrar.AddBlueprintAction(GetMenuActions_Utils::MakeBlendSpaceAction(GetClass(), UBlendSpace1D::StaticClass()));

	if (const UObject* RegistrarTarget = ActionRegistrar.GetActionKeyFilter())
	{
		if (const UBlendSpace* TargetBlendSpace = Cast<UBlendSpace>(RegistrarTarget))
		{
			if(TargetBlendSpace->IsAsset())
			{
				if (UBlueprintNodeSpawner* NodeSpawner = GetMenuActions_Utils::MakeBlendSpaceAction(GetClass(), TargetBlendSpace))
				{
					ActionRegistrar.AddBlueprintAction(TargetBlendSpace, NodeSpawner);
				}
			}
		}
		// else, the Blueprint database is specifically looking for actions pertaining to something different (not a BlendSpace asset)
	}
	else
	{
		UClass* NodeClass = GetClass();
		for (TObjectIterator<UBlendSpace> BlendSpaceIt; BlendSpaceIt; ++BlendSpaceIt)
		{
			if(BlendSpaceIt->IsAsset())
			{
				if (UBlueprintNodeSpawner* NodeSpawner = GetMenuActions_Utils::MakeBlendSpaceAction(NodeClass, *BlendSpaceIt))
				{
					ActionRegistrar.AddBlueprintAction(*BlendSpaceIt, NodeSpawner);
				}
			}
		}
	}
}

void UAnimGraphNode_BlendSpaceGraph::BakeDataDuringCompilation(class FCompilerResultsLog& MessageLog)
{
	UAnimBlueprint* AnimBlueprint = GetAnimBlueprint();
	AnimBlueprint->FindOrAddGroup(Node.GetGroupName());
}

#undef LOCTEXT_NAMESPACE
