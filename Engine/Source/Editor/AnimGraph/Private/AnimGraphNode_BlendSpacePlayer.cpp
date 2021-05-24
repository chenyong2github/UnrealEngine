// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimGraphNode_BlendSpacePlayer.h"
#include "UObject/UObjectHash.h"
#include "UObject/UObjectIterator.h"
#include "ToolMenus.h"
#include "GraphEditorActions.h"
#include "Kismet2/CompilerResultsLog.h"
#include "BlueprintNodeSpawner.h"
#include "BlueprintActionDatabaseRegistrar.h"
#include "Animation/AimOffsetBlendSpace.h"
#include "Animation/AimOffsetBlendSpace1D.h"
#include "AnimGraphCommands.h"
#include "IAnimBlueprintCopyTermDefaultsContext.h"
#include "IAnimBlueprintNodeOverrideAssetsContext.h"
#include "AssetRegistry/AssetRegistryModule.h"

/////////////////////////////////////////////////////
// UAnimGraphNode_BlendSpacePlayer

#define LOCTEXT_NAMESPACE "A3Nodes"

UAnimGraphNode_BlendSpacePlayer::UAnimGraphNode_BlendSpacePlayer(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

FText UAnimGraphNode_BlendSpacePlayer::GetNodeTitleForBlendSpace(ENodeTitleType::Type TitleType, UBlendSpace* InBlendSpace) const
{
	const FText BlendSpaceName = FText::FromString(InBlendSpace->GetName());

	if (TitleType == ENodeTitleType::ListView || TitleType == ENodeTitleType::MenuTitle)
	{
		FFormatNamedArguments Args;
		Args.Add(TEXT("BlendSpaceName"), BlendSpaceName);
		// FText::Format() is slow, so we cache this to save on performance
		CachedNodeTitles.SetCachedTitle(TitleType, FText::Format(LOCTEXT("BlendspacePlayer", "Blendspace Player '{BlendSpaceName}'"), Args), this);
	}
	else
	{
		FFormatNamedArguments TitleArgs;
		TitleArgs.Add(TEXT("BlendSpaceName"), BlendSpaceName);
		FText Title = FText::Format(LOCTEXT("BlendSpacePlayerFullTitle", "{BlendSpaceName}\nBlendspace Player"), TitleArgs);

		if (TitleType == ENodeTitleType::FullTitle)
		{
			FFormatNamedArguments Args;
			Args.Add(TEXT("Title"), Title);

			if(Node.GetGroupMethod() == EAnimSyncMethod::SyncGroup)
			{
				Args.Add(TEXT("SyncGroupName"), FText::FromName(Node.GetGroupName()));
				Title = FText::Format(LOCTEXT("BlendSpaceNodeGroupSubtitle", "{Title}\nSync group {SyncGroupName}"), Args);
			}
			else if(Node.GetGroupMethod() == EAnimSyncMethod::Graph)
			{
				Title = FText::Format(LOCTEXT("BlendSpaceNodeGroupSubtitle", "{Title}\nGraph sync group"), Args);

				UObject* ObjectBeingDebugged = GetAnimBlueprint()->GetObjectBeingDebugged();
				UAnimBlueprintGeneratedClass* GeneratedClass = GetAnimBlueprint()->GetAnimBlueprintGeneratedClass();
				if (ObjectBeingDebugged && GeneratedClass)
				{
					int32 NodeIndex = GeneratedClass->GetNodeIndexFromGuid(NodeGuid);
					if(NodeIndex != INDEX_NONE)
					{
						if(const FName* SyncGroupNamePtr = GeneratedClass->GetAnimBlueprintDebugData().NodeSyncsThisFrame.Find(NodeIndex))
						{
							Args.Add(TEXT("SyncGroupName"), FText::FromName(*SyncGroupNamePtr));
							Title = FText::Format(LOCTEXT("BlendSpaceNodeGraphGroupSubtitle", "{Title}\nGraph sync group {SyncGroupName}"), Args);
						}
					}
				}
			}
		}
		// FText::Format() is slow, so we cache this to save on performance
		CachedNodeTitles.SetCachedTitle(TitleType, Title, this);
	}

	return CachedNodeTitles[TitleType];
}

FText UAnimGraphNode_BlendSpacePlayer::GetNodeTitle(ENodeTitleType::Type TitleType) const
{	
	if (Node.GetBlendSpace() == nullptr)
	{
		// we may have a valid variable connected or default pin value
		UEdGraphPin* BlendSpacePin = FindPin(GET_MEMBER_NAME_STRING_CHECKED(FAnimNode_BlendSpacePlayer, BlendSpace));
		if (BlendSpacePin && BlendSpacePin->LinkedTo.Num() > 0)
		{
			return LOCTEXT("BlendspacePlayer_Variable_Title", "Blendspace Player");
		}
		else if (BlendSpacePin && BlendSpacePin->DefaultObject != nullptr)
		{
			return GetNodeTitleForBlendSpace(TitleType, CastChecked<UBlendSpace>(BlendSpacePin->DefaultObject));
		}
		else
		{
			if (TitleType == ENodeTitleType::ListView || TitleType == ENodeTitleType::MenuTitle)
			{
				return LOCTEXT("BlendspacePlayer_NONE_ListTitle", "Blendspace Player '(None)'");
			}
			else
			{
				return LOCTEXT("BlendspacePlayer_NONE_Title", "(None)\nBlendspace Player");
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

void UAnimGraphNode_BlendSpacePlayer::ValidateAnimNodeDuringCompilation(class USkeleton* ForSkeleton, class FCompilerResultsLog& MessageLog)
{
	Super::ValidateAnimNodeDuringCompilation(ForSkeleton, MessageLog);

	UBlendSpace* BlendSpaceToCheck = Node.GetBlendSpace();
	UEdGraphPin* BlendSpacePin = FindPin(GET_MEMBER_NAME_STRING_CHECKED(FAnimNode_BlendSpacePlayer, BlendSpace));
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
		if (BlendSpaceSkeleton && // if blend space doesn't have skeleton, it might be due to blend space not loaded yet, @todo: wait with anim blueprint compilation until all assets are loaded?
			!ForSkeleton->IsCompatible(BlendSpaceSkeleton))
		{
			MessageLog.Error(TEXT("@@ references blendspace that uses an incompatible skeleton @@"), this, BlendSpaceSkeleton);
		}
	}
}

void UAnimGraphNode_BlendSpacePlayer::BakeDataDuringCompilation(class FCompilerResultsLog& MessageLog)
{
	UAnimBlueprint* AnimBlueprint = GetAnimBlueprint();
	AnimBlueprint->FindOrAddGroup(Node.GetGroupName());
}

void UAnimGraphNode_BlendSpacePlayer::GetNodeContextMenuActions(UToolMenu* Menu, UGraphNodeContextMenuContext* Context) const
{
	if (!Context->bIsDebugging)
	{
		// add an option to convert to single frame
		{
			FToolMenuSection& Section = Menu->AddSection("AnimGraphNodeBlendSpaceEvaluator", NSLOCTEXT("A3Nodes", "BlendSpaceHeading", "Blend Space"));
			Section.AddMenuEntry(FAnimGraphCommands::Get().OpenRelatedAsset);
			Section.AddMenuEntry(FAnimGraphCommands::Get().ConvertToBSEvaluator);
			Section.AddMenuEntry(FAnimGraphCommands::Get().ConvertToBSGraph);
		}
	}
}

void UAnimGraphNode_BlendSpacePlayer::GetMenuActions(FBlueprintActionDatabaseRegistrar& InActionRegistrar) const
{
	GetMenuActionsHelper(
		InActionRegistrar,
		GetClass(),
		{ UBlendSpace::StaticClass() },
		{ },
		[](const FAssetData& InAssetData)
		{
			return FText::Format(LOCTEXT("MenuDescFormat", "Blendspace Player '{0}'"), FText::FromName(InAssetData.AssetName));
		},
		[](const FAssetData& InAssetData)
		{
			return FText::Format(LOCTEXT("MenuDescTooltipFormat", "Blendspace Player\n'{0}'"), FText::FromName(InAssetData.ObjectPath));
		},
		[](UEdGraphNode* InNewNode, bool bInIsTemplateNode, const FAssetData InAssetData)
		{
			UAnimGraphNode_AssetPlayerBase::SetupNewNode(InNewNode, bInIsTemplateNode, InAssetData);
		});
}

FBlueprintNodeSignature UAnimGraphNode_BlendSpacePlayer::GetSignature() const
{
	FBlueprintNodeSignature NodeSignature = Super::GetSignature();
	NodeSignature.AddSubObject(Node.GetBlendSpace());

	return NodeSignature;
}

void UAnimGraphNode_BlendSpacePlayer::SetAnimationAsset(UAnimationAsset* Asset)
{
	if (UBlendSpace* BlendSpace = Cast<UBlendSpace>(Asset))
	{
		Node.SetBlendSpace(BlendSpace);
	}
}

void UAnimGraphNode_BlendSpacePlayer::OnOverrideAssets(IAnimBlueprintNodeOverrideAssetsContext& InContext) const
{
	if(InContext.GetAssets().Num() > 0)
	{
		if (UBlendSpace* BlendSpace = Cast<UBlendSpace>(InContext.GetAssets()[0]))
		{
			FAnimNode_BlendSpacePlayer& AnimNode = InContext.GetAnimNode<FAnimNode_BlendSpacePlayer>();
			AnimNode.SetBlendSpace(BlendSpace);
		}
	}
}

void UAnimGraphNode_BlendSpacePlayer::GetAllAnimationSequencesReferred(TArray<UAnimationAsset*>& AnimationAssets) const
{
	if(Node.GetBlendSpace())
	{
		HandleAnimReferenceCollection(Node.BlendSpace, AnimationAssets);
	}
}

void UAnimGraphNode_BlendSpacePlayer::ReplaceReferredAnimations(const TMap<UAnimationAsset*, UAnimationAsset*>& AnimAssetReplacementMap)
{
	HandleAnimReferenceReplacement(Node.BlendSpace, AnimAssetReplacementMap);
}

bool UAnimGraphNode_BlendSpacePlayer::DoesSupportTimeForTransitionGetter() const
{
	return true;
}

UAnimationAsset* UAnimGraphNode_BlendSpacePlayer::GetAnimationAsset() const 
{
	UBlendSpace* BlendSpace = Node.GetBlendSpace();
	UEdGraphPin* BlendSpacePin = FindPin(GET_MEMBER_NAME_STRING_CHECKED(FAnimNode_BlendSpacePlayer, BlendSpace));
	if (BlendSpacePin != nullptr && BlendSpace == nullptr)
	{
		BlendSpace = Cast<UBlendSpace>(BlendSpacePin->DefaultObject);
	}

	return BlendSpace;
}

const TCHAR* UAnimGraphNode_BlendSpacePlayer::GetTimePropertyName() const 
{
	return TEXT("InternalTimeAccumulator");
}

UScriptStruct* UAnimGraphNode_BlendSpacePlayer::GetTimePropertyStruct() const 
{
	return FAnimNode_BlendSpacePlayer::StaticStruct();
}

EAnimAssetHandlerType UAnimGraphNode_BlendSpacePlayer::SupportsAssetClass(const UClass* AssetClass) const
{
	if (AssetClass->IsChildOf(UBlendSpace::StaticClass()) && !IsAimOffsetBlendSpace(AssetClass))
	{
		return EAnimAssetHandlerType::PrimaryHandler;
	}
	else
	{
		return EAnimAssetHandlerType::NotSupported;
	}
}

#undef LOCTEXT_NAMESPACE

