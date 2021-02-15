// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimGraphNode_RotationOffsetBlendSpaceGraph.h"
#include "BlueprintNodeSpawner.h"
#include "BlueprintActionDatabaseRegistrar.h"
#include "Animation/AimOffsetBlendSpace.h"
#include "Animation/AimOffsetBlendSpace1D.h"
#include "AnimGraphNodeAlphaOptions.h"
#include "DetailLayoutBuilder.h"

#define LOCTEXT_NAMESPACE "UAnimGraphNode_RotationOffsetBlendSpaceGraph"

FText UAnimGraphNode_RotationOffsetBlendSpaceGraph::GetNodeTitle(ENodeTitleType::Type TitleType) const
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
			return FText::Format(LOCTEXT("AimOffsetListTitle", "AimOffset '{BlendSpaceName}'"), Args);
		}
		else
		{
			FFormatNamedArguments TitleArgs;
			TitleArgs.Add(TEXT("BlendSpaceName"), BlendSpaceName);
			FText Title = FText::Format(LOCTEXT("AimOffsetFullTitle", "{BlendSpaceName}\nAimOffset"), TitleArgs);

			if ((TitleType == ENodeTitleType::FullTitle) && (Node.GetGroupName() != NAME_None))
			{
				FFormatNamedArguments Args;
				Args.Add(TEXT("Title"), Title);
				Args.Add(TEXT("SyncGroupName"), FText::FromName(Node.GetGroupName()));
				Title = FText::Format(LOCTEXT("AimOffsetNodeGroupSubtitle", "{Title}\nSync group {SyncGroupName}"), Args);
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
		return LOCTEXT("EmptyBlendspaceListTitle", "AimOffset");
	}
}

void UAnimGraphNode_RotationOffsetBlendSpaceGraph::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
	struct GetMenuActions_Utils
	{
		static void SetNodeBlendSpace(UEdGraphNode* NewNode, bool bIsTemplateNode, TWeakObjectPtr<UBlendSpace> BlendSpace)
		{
			UAnimGraphNode_RotationOffsetBlendSpaceGraph* BlendSpaceNode = CastChecked<UAnimGraphNode_RotationOffsetBlendSpaceGraph>(NewNode);
			BlendSpaceNode->SetupFromAsset(BlendSpace.Get(), bIsTemplateNode);
		}

		static UBlueprintNodeSpawner* MakeBlendSpaceAction(TSubclassOf<UEdGraphNode> const NodeClass, const UBlendSpace* InBlendSpace)
		{
			UBlueprintNodeSpawner* NodeSpawner = nullptr;

			bool const bIsAimOffset = InBlendSpace->IsA(UAimOffsetBlendSpace::StaticClass()) ||
									  InBlendSpace->IsA(UAimOffsetBlendSpace1D::StaticClass());
			if (bIsAimOffset)
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
			UAnimGraphNode_RotationOffsetBlendSpaceGraph* BlendSpaceNode = CastChecked<UAnimGraphNode_RotationOffsetBlendSpaceGraph>(NewNode);
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
	ActionRegistrar.AddBlueprintAction(GetMenuActions_Utils::MakeBlendSpaceAction(GetClass(), UAimOffsetBlendSpace::StaticClass()));
	ActionRegistrar.AddBlueprintAction(GetMenuActions_Utils::MakeBlendSpaceAction(GetClass(), UAimOffsetBlendSpace1D::StaticClass()));

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

void UAnimGraphNode_RotationOffsetBlendSpaceGraph::BakeDataDuringCompilation(FCompilerResultsLog& MessageLog)
{
	UAnimBlueprint* AnimBlueprint = GetAnimBlueprint();
	AnimBlueprint->FindOrAddGroup(Node.GetGroupName());
}

void UAnimGraphNode_RotationOffsetBlendSpaceGraph::CustomizePinData(UEdGraphPin* Pin, FName SourcePropertyName, int32 ArrayIndex) const
{
	Super::CustomizePinData(Pin, SourcePropertyName, ArrayIndex);

	FAnimGraphNodeAlphaOptions::HandleCustomizePinData(Node, Pin);
}

void UAnimGraphNode_RotationOffsetBlendSpaceGraph::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	FAnimGraphNodeAlphaOptions::HandlePostEditChangeProperty(Node, this, PropertyChangedEvent);

	Super::PostEditChangeProperty(PropertyChangedEvent);
}

void UAnimGraphNode_RotationOffsetBlendSpaceGraph::CustomizeDetails(IDetailLayoutBuilder& InDetailBuilder)
{
	Super::CustomizeDetails(InDetailBuilder);

	TSharedRef<IPropertyHandle> NodeHandle = InDetailBuilder.GetProperty(TEXT("Node"), GetClass());

	FAnimGraphNodeAlphaOptions::HandleCustomizeDetails(Node, NodeHandle, InDetailBuilder);
}

#undef LOCTEXT_NAMESPACE
