// Copyright Epic Games, Inc. All Rights Reserved. 
#include "InterchangeGenericAnimationPipeline.h"

#include "CoreMinimal.h"
#include "InterchangeAnimSequenceFactoryNode.h"
#include "InterchangePipelineLog.h"
#include "InterchangeSceneNode.h"
#include "InterchangeSkeletalMeshFactoryNode.h"
#include "InterchangeSkeletonFactoryNode.h"
#include "InterchangeSkeletonHelper.h"
#include "InterchangeSourceData.h"
#include "Nodes/InterchangeBaseNode.h"
#include "Nodes/InterchangeBaseNodeContainer.h"
#include "Nodes/InterchangeSourceNode.h"


namespace UE::Interchange::Private
{
	/**
	 * Return true if there is one animated node in the scene node hierarchy under NodeUid
	 */
	bool IsSkeletonAnimatedRecursive(const FString& NodeUid, UInterchangeBaseNodeContainer* BaseNodeContainer)
	{
		if (const UInterchangeSceneNode* SceneNode = Cast<UInterchangeSceneNode>(BaseNodeContainer->GetNode(NodeUid)))
		{
			bool bIsAnimated = false;
			if (SceneNode->GetCustomIsNodeTransformAnimated(bIsAnimated) && bIsAnimated)
			{
				return true;
			}
		}
		TArray<FString> Children = BaseNodeContainer->GetNodeChildrenUids(NodeUid);
		for (const FString& ChildUid : Children)
		{
			if (IsSkeletonAnimatedRecursive(ChildUid, BaseNodeContainer))
			{
				return true;
			}
		}
		return false;
	}
}

void UInterchangeGenericAnimationPipeline::AdjustSettingsForReimportType(EInterchangeReimportType ImportType, TObjectPtr<UObject> ReimportAsset)
{
	check(!CommonSkeletalMeshesAndAnimationsProperties.IsNull());
	if (ImportType == EInterchangeReimportType::AssetCustomLODImport
		|| ImportType == EInterchangeReimportType::AssetCustomLODReimport
		|| ImportType == EInterchangeReimportType::AssetAlternateSkinningImport
		|| ImportType == EInterchangeReimportType::AssetAlternateSkinningReimport)
	{
		bImportAnimations = false;
		CommonSkeletalMeshesAndAnimationsProperties->bImportOnlyAnimations = false;
	}
	else if(ImportType == EInterchangeReimportType::AssetReimport)
	{
		if (UAnimSequence* AnimSequence = Cast<UAnimSequence>(ReimportAsset))
		{
			//Set the skeleton to the current asset skeleton and re-import only the animation
			CommonSkeletalMeshesAndAnimationsProperties->Skeleton = AnimSequence->GetSkeleton();
			CommonSkeletalMeshesAndAnimationsProperties->bImportOnlyAnimations = true;
		}
	}
}

void UInterchangeGenericAnimationPipeline::ExecutePreImportPipeline(UInterchangeBaseNodeContainer* InBaseNodeContainer, const TArray<UInterchangeSourceData*>& InSourceDatas)
{
	check(!CommonSkeletalMeshesAndAnimationsProperties.IsNull());
	if (!InBaseNodeContainer)
	{
		UE_LOG(LogInterchangePipeline, Warning, TEXT("UInterchangeGenericAnimationPipeline: Cannot execute pre-import pipeline because InBaseNodeContrainer is null"));
		return;
	}

	if (CommonSkeletalMeshesAndAnimationsProperties->bImportOnlyAnimations && CommonSkeletalMeshesAndAnimationsProperties->Skeleton.IsNull())
	{
		UE_LOG(LogInterchangePipeline, Warning, TEXT("UInterchangeGenericAnimationPipeline: Cannot execute pre-import pipeline because we cannot import animation only but not specify any valid skeleton"));
		return;
	}

	BaseNodeContainer = InBaseNodeContainer;
	SourceDatas.Empty(InSourceDatas.Num());
	for (const UInterchangeSourceData* SourceData : InSourceDatas)
	{
		SourceDatas.Add(SourceData);
	}

	double SampleRate = 30.0;
	double RangeStart = 0;
	double RangeStop = 0;
	bool bRangeIsValid = false;
	const UInterchangeSourceNode* SourceNode = UInterchangeSourceNode::GetUniqueInstance(BaseNodeContainer);
	if (SourceNode)
	{
		if (bImportBoneTracks)
		{
			int32 Numerator, Denominator;
			if (!bUse30HzToBakeBoneAnimation && CustomBoneAnimationSampleRate == 0 && SourceNode->GetCustomSourceFrameRateNumerator(Numerator))
			{
				if (SourceNode->GetCustomSourceFrameRateDenominator(Denominator) && Denominator > 0 && Numerator > 0)
				{
					SampleRate = static_cast<double>(Numerator) / static_cast<double>(Denominator);
				}
			}
			else if ((!bUse30HzToBakeBoneAnimation && CustomBoneAnimationSampleRate > 0))
			{
				SampleRate = static_cast<double>(CustomBoneAnimationSampleRate);
			}

			if (AnimationRange == EInterchangeAnimationRange::Timeline)
			{
				if (SourceNode->GetCustomSourceTimelineStart(RangeStart))
				{
					if (SourceNode->GetCustomSourceTimelineEnd(RangeStop))
					{
						bRangeIsValid = true;
					}
				}
			}
			else if (AnimationRange == EInterchangeAnimationRange::Animated)
			{
				if (SourceNode->GetCustomAnimatedTimeStart(RangeStart))
				{
					if (SourceNode->GetCustomAnimatedTimeEnd(RangeStop))
					{
						bRangeIsValid = true;
					}
				}
			}
			else if (AnimationRange == EInterchangeAnimationRange::SetRange)
			{
				RangeStart = static_cast<double>(FrameImportRange.Min) / SampleRate;
				RangeStop = static_cast<double>(FrameImportRange.Max) / SampleRate;
				bRangeIsValid = true;
			}
		}
	}
	else
	{
		if (bImportBoneTracks)
		{
			if ((!bUse30HzToBakeBoneAnimation && CustomBoneAnimationSampleRate > 0))
			{
				SampleRate = CustomBoneAnimationSampleRate;
			}
			//Find the range by iteration the scene node
			TArray<FString> SceneNodeUids;
			BaseNodeContainer->GetNodes(UInterchangeSceneNode::StaticClass(), SceneNodeUids);
			for (const FString& SceneNodeUid : SceneNodeUids)
			{
				if (const UInterchangeSceneNode* SceneNode = Cast<const UInterchangeSceneNode>(BaseNodeContainer->GetNode(SceneNodeUid)))
				{
					double SceneNodeAnimStart;
					double SceneNodeAnimStop;
					if (SceneNode->GetCustomNodeTransformAnimationStartTime(SceneNodeAnimStart))
					{
						if (SceneNode->GetCustomNodeTransformAnimationEndTime(SceneNodeAnimStop))
						{
							if (RangeStart > SceneNodeAnimStart)
							{
								RangeStart = SceneNodeAnimStart;
							}
							if (RangeStop < SceneNodeAnimStop)
							{
								RangeStop = SceneNodeAnimStop;
							}
							bRangeIsValid = true;
						}
					}
				}
			}
		}
	}


	//Retrieve all skeletons
	TArray<UInterchangeSkeletonFactoryNode*> Skeletons;
	BaseNodeContainer->IterateNodes([&Skeletons, LocalNodeContainer = BaseNodeContainer](const FString& NodeUid, UInterchangeBaseNode* Node)
		{
			if (UInterchangeSkeletonFactoryNode* SkeletonFactoryNode = Cast<UInterchangeSkeletonFactoryNode>(Node))
			{
				FString RootSceneNodeUid;
				SkeletonFactoryNode->GetCustomRootJointUid(RootSceneNodeUid);
				if(UE::Interchange::Private::IsSkeletonAnimatedRecursive(RootSceneNodeUid, LocalNodeContainer))
				{
					Skeletons.Add(SkeletonFactoryNode);
				}
			}
		});
	//for each animated skeleton create one anim sequence factory node
	for (UInterchangeSkeletonFactoryNode* SkeletonFactoryNode : Skeletons)
	{
		const FString AnimSequenceUid = TEXT("\\AnimSequence") + SkeletonFactoryNode->GetUniqueID();
		FString AnimSequenceName = SkeletonFactoryNode->GetDisplayLabel();
		if (AnimSequenceName.EndsWith(TEXT("_Skeleton")))
		{
			AnimSequenceName.LeftChopInline(9);
		}
		AnimSequenceName += TEXT("_Anim");
		UInterchangeAnimSequenceFactoryNode* AnimSequenceFactoryNode = NewObject<UInterchangeAnimSequenceFactoryNode>(BaseNodeContainer, NAME_None);
		AnimSequenceFactoryNode->InitializeAnimSequenceNode(AnimSequenceUid, AnimSequenceName);
		
		AnimSequenceFactoryNode->SetCustomSkeletonFactoryNodeUid(SkeletonFactoryNode->GetUniqueID());
		AnimSequenceFactoryNode->SetCustomImportBoneTracks(bImportBoneTracks);
		AnimSequenceFactoryNode->SetCustomImportBoneTracksSampleRate(SampleRate);
		if (bRangeIsValid)
		{
			AnimSequenceFactoryNode->SetCustomImportBoneTracksRangeStart(RangeStart);
			AnimSequenceFactoryNode->SetCustomImportBoneTracksRangeStop(RangeStop);
		}

		//USkeleton cannot be created without a valid skeletalmesh
		const FString SkeletonUid = SkeletonFactoryNode->GetUniqueID();
		AnimSequenceFactoryNode->AddFactoryDependencyUid(SkeletonUid);

		FString RootJointUid;
		if(SkeletonFactoryNode->GetCustomRootJointUid(RootJointUid))
		{
#if WITH_EDITOR
			//Iterate all joints to set the meta data value in the anim sequence factory node
			UE::Interchange::Private::FSkeletonHelper::RecursiveAddSkeletonMetaDataValues(BaseNodeContainer, AnimSequenceFactoryNode, RootJointUid);
#endif //WITH_EDITOR
		}

		const bool bImportOnlyAnimation = CommonSkeletalMeshesAndAnimationsProperties->bImportOnlyAnimations;
		
		//Iterate dependencies
		{
			TArray<FString> SkeletalMeshNodeUids;
			BaseNodeContainer->GetNodes(UInterchangeSkeletalMeshFactoryNode::StaticClass(), SkeletalMeshNodeUids);
			for (const FString& SkelMeshFactoryNodeUid : SkeletalMeshNodeUids)
			{
				if (const UInterchangeSkeletalMeshFactoryNode* SkeletalMeshFactoryNode = Cast<const UInterchangeSkeletalMeshFactoryNode>(BaseNodeContainer->GetFactoryNode(SkelMeshFactoryNodeUid)))
				{
					TArray<FString> SkeletalMeshDependencies;
					SkeletalMeshFactoryNode->GetFactoryDependencies(SkeletalMeshDependencies);
					for (const FString& SkeletalMeshDependencyUid : SkeletalMeshDependencies)
					{
						if (SkeletonUid.Equals(SkeletalMeshDependencyUid))
						{
							AnimSequenceFactoryNode->AddFactoryDependencyUid(SkelMeshFactoryNodeUid);
							break;
						}
					}
				}
			}
		}

		if (!CommonSkeletalMeshesAndAnimationsProperties->Skeleton.IsNull())
		{
			bool bSkeletonCompatible = true;

			//TODO: support skeleton helper in runtime
#if WITH_EDITOR
			bSkeletonCompatible = UE::Interchange::Private::FSkeletonHelper::IsCompatibleSkeleton(CommonSkeletalMeshesAndAnimationsProperties->Skeleton, RootJointUid, BaseNodeContainer);
#endif
			if(bSkeletonCompatible)
			{
				FSoftObjectPath SkeletonSoftObjectPath(CommonSkeletalMeshesAndAnimationsProperties->Skeleton.Get());
				AnimSequenceFactoryNode->SetCustomSkeletonSoftObjectPath(SkeletonSoftObjectPath);
			}
			else
			{
				UInterchangeResultError_Generic* Message = AddMessage<UInterchangeResultError_Generic>();
				Message->Text = FText::Format(NSLOCTEXT("UInterchangeGenericAnimationPipeline", "IncompatibleSkeleton", "Incompatible skeleton {0} when importing AnimSequence {1}."),
					FText::FromString(CommonSkeletalMeshesAndAnimationsProperties->Skeleton->GetName()),
					FText::FromString(AnimSequenceName));
			}
		}
		BaseNodeContainer->AddNode(AnimSequenceFactoryNode);
	}
}

void UInterchangeGenericAnimationPipeline::ExecutePostImportPipeline(const UInterchangeBaseNodeContainer* InBaseNodeContainer, const FString& NodeKey, UObject* CreatedAsset, bool bIsAReimport)
{
	//We do not use the provided base container since ExecutePreImportPipeline cache it
	//We just make sure the same one is pass in parameter
	if (!InBaseNodeContainer || !ensure(BaseNodeContainer == InBaseNodeContainer) || !CreatedAsset)
	{
		return;
	}

	const UInterchangeBaseNode* Node = BaseNodeContainer->GetNode(NodeKey);
	if (!Node)
	{
		return;
	}
}



