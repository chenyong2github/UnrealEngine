// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimGraphNode_BlendSpaceEvaluator.h"
#include "ToolMenus.h"
#include "AnimGraphCommands.h"
#include "BlueprintNodeSpawner.h"
#include "Kismet2/CompilerResultsLog.h"
#include "IAnimBlueprintNodeOverrideAssetsContext.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "BlueprintActionDatabaseRegistrar.h"
#include "BlueprintNodeTemplateCache.h"

/////////////////////////////////////////////////////
// UAnimGraphNode_BlendSpaceEvaluator

#define LOCTEXT_NAMESPACE "A3Nodes"

UAnimGraphNode_BlendSpaceEvaluator::UAnimGraphNode_BlendSpaceEvaluator(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

FText UAnimGraphNode_BlendSpaceEvaluator::GetNodeTitleForBlendSpace(ENodeTitleType::Type TitleType, UBlendSpace* InBlendSpace) const
{
	const FText BlendSpaceName = FText::FromString(InBlendSpace->GetName());

	if (TitleType == ENodeTitleType::ListView || TitleType == ENodeTitleType::MenuTitle)
	{
		FFormatNamedArguments Args;
		Args.Add(TEXT("BlendSpaceName"), BlendSpaceName);

		// FText::Format() is slow, so we cache this to save on performance
		CachedNodeTitles.SetCachedTitle(TitleType, FText::Format(LOCTEXT("BlendSpaceEvaluatorListTitle", "Blendspace Evaluator '{BlendSpaceName}'"), Args), this);
	}
	else
	{
		FFormatNamedArguments TitleArgs;
		TitleArgs.Add(TEXT("BlendSpaceName"), BlendSpaceName);
		FText Title = FText::Format(LOCTEXT("BlendSpaceEvaluatorFullTitle", "{BlendSpaceName}\nBlendspace Evaluator"), TitleArgs);

		if ((TitleType == ENodeTitleType::FullTitle) && (Node.GetGroupName() != NAME_None))
		{
			FFormatNamedArguments Args;
			Args.Add(TEXT("Title"), Title);
			Args.Add(TEXT("SyncGroupName"), FText::FromName(Node.GetGroupName()));
			Title = FText::Format(LOCTEXT("BlendSpaceNodeGroupSubtitle", "{Title}\nSync group {SyncGroupName}"), Args);
		}
		// FText::Format() is slow, so we cache this to save on performance
		CachedNodeTitles.SetCachedTitle(TitleType, Title, this);
	}

	return CachedNodeTitles[TitleType];
}

FText UAnimGraphNode_BlendSpaceEvaluator::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	if (Node.GetBlendSpace() == nullptr)
	{
		// we may have a valid variable connected or default pin value
		UEdGraphPin* BlendSpacePin = FindPin(GET_MEMBER_NAME_STRING_CHECKED(FAnimNode_BlendSpacePlayer, BlendSpace));
		if (BlendSpacePin && BlendSpacePin->LinkedTo.Num() > 0)
		{
			return LOCTEXT("BlendSpaceEvaluator_Variable_Title", "Blendspace Evaluator");
		}
		else if (BlendSpacePin && BlendSpacePin->DefaultObject != nullptr)
		{
			return GetNodeTitleForBlendSpace(TitleType, CastChecked<UBlendSpace>(BlendSpacePin->DefaultObject));
		}
		else
		{
			if (TitleType == ENodeTitleType::ListView || TitleType == ENodeTitleType::MenuTitle)
			{
				return LOCTEXT("BlendSpaceEvaluator_NONE_ListTitle", "Blendspace Evaluator '(None)'");
			}
			else
			{
				return LOCTEXT("BlendSpaceEvaluator_NONE_Title", "(None)\nBlendspace Evaluator");
			}
		}
	}
	// @TODO: the bone can be altered in the property editor, so we have to 
	//        choose to mark this dirty when that happens for this to properly work
	else //if (!CachedNodeTitles.IsTitleCached(TitleType, this))
	{
		return GetNodeTitleForBlendSpace(TitleType, Node.GetBlendSpace());
	}
}

void UAnimGraphNode_BlendSpaceEvaluator::GetMenuActions(FBlueprintActionDatabaseRegistrar& InActionRegistrar) const
{
	GetMenuActionsHelper(
		InActionRegistrar,
		GetClass(),
		{ UBlendSpace::StaticClass() },
		{ },
		[](const FAssetData& InAssetData)
		{
			return FText::Format(LOCTEXT("MenuDescFormat", "Blendspace Evaluator '{0}'"), FText::FromName(InAssetData.AssetName));
		},
		[](const FAssetData& InAssetData)
		{
			return FText::Format(LOCTEXT("MenuDescTooltipFormat", "Blendspace Evaluator\n'{0}'"), FText::FromName(InAssetData.ObjectPath));
		},
		[](UEdGraphNode* InNewNode, bool bInIsTemplateNode, const FAssetData InAssetData)
		{
			UAnimGraphNode_AssetPlayerBase::SetupNewNode(InNewNode, bInIsTemplateNode, InAssetData);
		});	
}

void UAnimGraphNode_BlendSpaceEvaluator::ValidateAnimNodeDuringCompilation(class USkeleton* ForSkeleton, class FCompilerResultsLog& MessageLog)
{
	Super::ValidateAnimNodeDuringCompilation(ForSkeleton, MessageLog);

	UBlendSpace* BlendSpaceToCheck = Node.GetBlendSpace();
	UEdGraphPin* BlendSpacePin = FindPin(GET_MEMBER_NAME_STRING_CHECKED(FAnimNode_BlendSpaceEvaluator, BlendSpace));
	if (BlendSpacePin != nullptr && BlendSpaceToCheck == nullptr)
	{
		BlendSpaceToCheck = Cast<UBlendSpace>(BlendSpacePin->DefaultObject);
	}

	if (BlendSpaceToCheck == nullptr)
	{
		// Check for bindings
		bool bHasBinding = false;
		if(BlendSpacePin != nullptr)
		{
			if (FAnimGraphNodePropertyBinding* BindingPtr = PropertyBindings.Find(BlendSpacePin->GetFName()))
			{
				bHasBinding = true;
			}
		}

		// we may have a connected node or binding
		if (BlendSpacePin == nullptr || (BlendSpacePin->LinkedTo.Num() == 0 && !bHasBinding))
		{
			MessageLog.Error(TEXT("@@ references an unknown blend space"), this);
		}
	}
	else 
	{
		USkeleton* BlendSpaceSkeleton = BlendSpaceToCheck->GetSkeleton();
		if (BlendSpaceSkeleton&& // if blend space doesn't have skeleton, it might be due to blend space not loaded yet, @todo: wait with anim blueprint compilation until all assets are loaded?
			!ForSkeleton->IsCompatible(BlendSpaceSkeleton))
		{
			MessageLog.Error(TEXT("@@ references blendspace that uses an incompatible skeleton @@"), this, BlendSpaceSkeleton);
		}
	}
}

void UAnimGraphNode_BlendSpaceEvaluator::BakeDataDuringCompilation(class FCompilerResultsLog& MessageLog)
{
	UAnimBlueprint* AnimBlueprint = GetAnimBlueprint();
	AnimBlueprint->FindOrAddGroup(Node.GetGroupName());
}

void UAnimGraphNode_BlendSpaceEvaluator::GetNodeContextMenuActions(UToolMenu* Menu, UGraphNodeContextMenuContext* Context) const
{
	if (!Context->bIsDebugging)
	{
		// add an option to convert to single frame
		{
			FToolMenuSection& Section = Menu->AddSection("AnimGraphNodeBlendSpacePlayer", NSLOCTEXT("A3Nodes", "BlendSpaceHeading", "Blend Space"));
			Section.AddMenuEntry(FAnimGraphCommands::Get().OpenRelatedAsset);
			Section.AddMenuEntry(FAnimGraphCommands::Get().ConvertToBSPlayer);
		}
	}
}

void UAnimGraphNode_BlendSpaceEvaluator::SetAnimationAsset(UAnimationAsset* Asset)
{
	if (UBlendSpace* BlendSpace = Cast<UBlendSpace>(Asset))
	{
		Node.SetBlendSpace(BlendSpace);
	}
}

void UAnimGraphNode_BlendSpaceEvaluator::OnOverrideAssets(IAnimBlueprintNodeOverrideAssetsContext& InContext) const
{
	if(InContext.GetAssets().Num() > 0)
	{
		if (UBlendSpace* BlendSpace = Cast<UBlendSpace>(InContext.GetAssets()[0]))
		{
			FAnimNode_BlendSpaceEvaluator& AnimNode = InContext.GetAnimNode<FAnimNode_BlendSpaceEvaluator>();
			AnimNode.SetBlendSpace(BlendSpace);
		}
	}
}

bool UAnimGraphNode_BlendSpaceEvaluator::DoesSupportTimeForTransitionGetter() const
{
	return true;
}

UAnimationAsset* UAnimGraphNode_BlendSpaceEvaluator::GetAnimationAsset() const 
{
	UBlendSpace* BlendSpace = Node.GetBlendSpace();
	UEdGraphPin* BlendSpacePin = FindPin(GET_MEMBER_NAME_STRING_CHECKED(FAnimNode_BlendSpaceEvaluator, BlendSpace));
	if (BlendSpacePin != nullptr && BlendSpace == nullptr)
	{
		BlendSpace = Cast<UBlendSpace>(BlendSpacePin->DefaultObject);
	}

	return BlendSpace;
}

const TCHAR* UAnimGraphNode_BlendSpaceEvaluator::GetTimePropertyName() const 
{
	return TEXT("InternalTimeAccumulator");
}

UScriptStruct* UAnimGraphNode_BlendSpaceEvaluator::GetTimePropertyStruct() const 
{
	return FAnimNode_BlendSpaceEvaluator::StaticStruct();
}

EAnimAssetHandlerType UAnimGraphNode_BlendSpaceEvaluator::SupportsAssetClass(const UClass* AssetClass) const
{
	if (AssetClass->IsChildOf(UBlendSpace::StaticClass()) && !IsAimOffsetBlendSpace(AssetClass))
	{
		return EAnimAssetHandlerType::Supported;
	}
	else
	{
		return EAnimAssetHandlerType::NotSupported;
	}
}

#undef LOCTEXT_NAMESPACE
