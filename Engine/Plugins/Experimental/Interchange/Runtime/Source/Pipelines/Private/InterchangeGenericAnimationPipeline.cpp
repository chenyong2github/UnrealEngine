// Copyright Epic Games, Inc. All Rights Reserved. 
#include "InterchangeGenericAnimationPipeline.h"

#include "Animation/AnimationSettings.h"
#include "CoreMinimal.h"
#include "InterchangeAnimationTrackSetFactoryNode.h"
#include "InterchangeAnimationTrackSetNode.h"
#include "InterchangeAnimSequenceFactoryNode.h"
#include "InterchangeMeshNode.h"
#include "InterchangePipelineLog.h"
#include "InterchangeSceneNode.h"
#include "InterchangeSkeletalMeshFactoryNode.h"
#include "InterchangeSkeletalMeshLodDataNode.h"
#include "InterchangeSkeletonFactoryNode.h"
#include "InterchangeSkeletonHelper.h"
#include "InterchangeSourceData.h"
#include "Nodes/InterchangeAnimationAPI.h"
#include "Nodes/InterchangeBaseNode.h"
#include "Nodes/InterchangeBaseNodeContainer.h"
#include "Nodes/InterchangeSourceNode.h"
#include "Nodes/InterchangeUserDefinedAttribute.h"

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
			if (UInterchangeAnimationAPI::GetCustomIsNodeTransformAnimated(SceneNode, bIsAnimated) && bIsAnimated)
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

void UInterchangeGenericAnimationPipeline::AdjustSettingsForContext(EInterchangePipelineContext ImportType, TObjectPtr<UObject> ReimportAsset)
{
	Super::AdjustSettingsForContext(ImportType, ReimportAsset);

	check(!CommonSkeletalMeshesAndAnimationsProperties.IsNull());
	
	if (ImportType == EInterchangePipelineContext::AssetCustomLODImport
		|| ImportType == EInterchangePipelineContext::AssetCustomLODReimport
		|| ImportType == EInterchangePipelineContext::AssetAlternateSkinningImport
		|| ImportType == EInterchangePipelineContext::AssetAlternateSkinningReimport)
	{
		bImportAnimations = false;
		CommonSkeletalMeshesAndAnimationsProperties->bImportOnlyAnimations = false;
	}
	
	TArray<FString> HideCategories;
	if (ImportType == EInterchangePipelineContext::AssetReimport)
	{
		if (UAnimSequence* AnimSequence = Cast<UAnimSequence>(ReimportAsset))
		{
			//Set the skeleton to the current asset skeleton and re-import only the animation
			CommonSkeletalMeshesAndAnimationsProperties->Skeleton = AnimSequence->GetSkeleton();
			CommonSkeletalMeshesAndAnimationsProperties->bImportOnlyAnimations = true;
		}
		else
		{
			HideCategories.Add(TEXT("Animations"));
		}
	}

	if (UInterchangePipelineBase* OuterMostPipeline = GetMostPipelineOuter())
	{
		for (const FString& HideCategoryName : HideCategories)
		{
			HidePropertiesOfCategory(OuterMostPipeline, this, HideCategoryName);
		}
	}
}

void UInterchangeGenericAnimationPipeline::ExecutePreImportPipeline(UInterchangeBaseNodeContainer* InBaseNodeContainer, const TArray<UInterchangeSourceData*>& InSourceDatas)
{
	if (!InBaseNodeContainer)
	{
		UE_LOG(LogInterchangePipeline, Warning, TEXT("UInterchangeGenericAnimationPipeline: Cannot execute pre-import pipeline because InBaseNodeContrainer is null"));
		return;
	}

	BaseNodeContainer = InBaseNodeContainer;

	if (!bImportAnimations)
	{
		//Nothing to import
		return;
	}

	TArray<UInterchangeAnimationTrackSetNode*> TrackSetNodes;
	BaseNodeContainer->IterateNodesOfType<UInterchangeAnimationTrackSetNode>([&](const FString& NodeUid, UInterchangeAnimationTrackSetNode* Node)
		{
			TrackSetNodes.Add(Node);
		});

	for (UInterchangeAnimationTrackSetNode* TrackSetNode : TrackSetNodes)
	{
		if (TrackSetNode)
		{
			CreateAnimationTrackSetFactoryNode(*TrackSetNode);
		}
	}

	if (CommonSkeletalMeshesAndAnimationsProperties.IsNull())
	{
		return;
	}

	if (CommonSkeletalMeshesAndAnimationsProperties->bImportOnlyAnimations && CommonSkeletalMeshesAndAnimationsProperties->Skeleton.IsNull())
	{
		UE_LOG(LogInterchangePipeline, Warning, TEXT("UInterchangeGenericAnimationPipeline: Cannot execute pre-import pipeline because we cannot import animation only but not specify any valid skeleton"));
		return;
	}
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
					if (UInterchangeAnimationAPI::GetCustomNodeTransformAnimationStartTime(SceneNode, SceneNodeAnimStart))
					{
						if (UInterchangeAnimationAPI::GetCustomNodeTransformAnimationEndTime(SceneNode, SceneNodeAnimStop))
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

	//Retrieve all skeletons and all morph target anim data
	TMap<UInterchangeSkeletonFactoryNode*, TArray<const UInterchangeMeshNode*>> MorphTargetsPerSkeletons;

	BaseNodeContainer->IterateNodes([&MorphTargetsPerSkeletons, LocalNodeContainer = BaseNodeContainer](const FString& NodeUid, UInterchangeBaseNode* Node)
		{
			if (UInterchangeSkeletonFactoryNode* SkeletonFactoryNode = Cast<UInterchangeSkeletonFactoryNode>(Node))
			{
				const UInterchangeSkeletalMeshFactoryNode* SkeletalMeshFactoryNode = nullptr;
				//Iterate dependencies
				TArray<FString> SkeletalMeshNodeUids;
				LocalNodeContainer->GetNodes(UInterchangeSkeletalMeshFactoryNode::StaticClass(), SkeletalMeshNodeUids);
				for (const FString& SkelMeshFactoryNodeUid : SkeletalMeshNodeUids)
				{
					if (const UInterchangeSkeletalMeshFactoryNode* CurrentSkeletalMeshFactoryNode = Cast<const UInterchangeSkeletalMeshFactoryNode>(LocalNodeContainer->GetFactoryNode(SkelMeshFactoryNodeUid)))
					{
						TArray<FString> SkeletalMeshDependencies;
						CurrentSkeletalMeshFactoryNode->GetFactoryDependencies(SkeletalMeshDependencies);
						for (const FString& SkeletalMeshDependencyUid : SkeletalMeshDependencies)
						{
							if (NodeUid.Equals(SkeletalMeshDependencyUid))
							{
								SkeletalMeshFactoryNode = CurrentSkeletalMeshFactoryNode;
								break;
							}
						}
					}
				}

				FString RootSceneNodeUid;
				SkeletonFactoryNode->GetCustomRootJointUid(RootSceneNodeUid);
				if(UE::Interchange::Private::IsSkeletonAnimatedRecursive(RootSceneNodeUid, LocalNodeContainer))
				{
					MorphTargetsPerSkeletons.FindOrAdd(SkeletonFactoryNode);
				}
				
				if (SkeletalMeshFactoryNode)
				{
					//Find the skeletalmesh morph targets
					FString SkeletalMeshFactoryNodeUid = SkeletalMeshFactoryNode->GetUniqueID();
					//Get the LOD data factory node
					TArray<FString> LodDataChildren;
					SkeletalMeshFactoryNode->GetLodDataUniqueIds(LodDataChildren);
					for (const FString& ChildUid : LodDataChildren)
					{
						if (const UInterchangeSkeletalMeshLodDataNode* LodData = Cast<UInterchangeSkeletalMeshLodDataNode>(LocalNodeContainer->GetNode(ChildUid)))
						{
							TArray<FString> MeshUids;
							LodData->GetMeshUids(MeshUids);
							for (const FString& MeshUid : MeshUids)
							{
								if (const UInterchangeBaseNode* BaseNode = LocalNodeContainer->GetNode(MeshUid))
								{
									const UInterchangeMeshNode* MeshNode = nullptr;
									FString RealMeshUid;
									if (const UInterchangeSceneNode* SceneNode = Cast<UInterchangeSceneNode>(BaseNode))
									{
										SceneNode->GetCustomAssetInstanceUid(RealMeshUid);
									}
									else
									{
										RealMeshUid = MeshUid;
									}
									MeshNode = Cast<UInterchangeMeshNode>(LocalNodeContainer->GetNode(RealMeshUid));
									if (MeshNode)
									{
										TArray<FString> MorphTargetUids;
										MeshNode->GetMorphTargetDependencies(MorphTargetUids);
										bool bIsMorphTargetAnimated = false;
										TArray<const UInterchangeMeshNode*> MorphTargets;
										for (const FString& MorphTargetUid : MorphTargetUids)
										{
											if (const UInterchangeMeshNode* MorphTargetNode = Cast<UInterchangeMeshNode>(LocalNodeContainer->GetNode(MorphTargetUid)))
											{
												MorphTargets.AddUnique(MorphTargetNode);
												TOptional<FString> MorphTargetAnimationPayloadKey = MorphTargetNode->GetAnimationCurvePayLoadKey();
												if (MorphTargetAnimationPayloadKey.IsSet())
												{
													bIsMorphTargetAnimated = true;
												}
											}
										}
										if (bIsMorphTargetAnimated)
										{
											TArray<const UInterchangeMeshNode*>& MorphTargetNodes = MorphTargetsPerSkeletons.FindOrAdd(SkeletonFactoryNode);
											MorphTargetNodes.Reset(MorphTargets.Num());
											MorphTargetNodes.Append(MorphTargets);
										}
									}
								}
							}
						}
					}
					
					//If we do not already create an anim sequence for this skeleton, add the anim sequence if we have
					//at least one animated user defined attribute.
					if (!MorphTargetsPerSkeletons.Contains(SkeletonFactoryNode))
					{
						//Add anim sequence if we have at least one animated user defined attributes
						LocalNodeContainer->BreakableIterateNodeChildren(RootSceneNodeUid, [&MorphTargetsPerSkeletons, SkeletonFactoryNode](const UInterchangeBaseNode* Node)
							{
								if (const UInterchangeSceneNode* SceneNode = Cast<UInterchangeSceneNode>(Node))
								{
									TArray<FInterchangeUserDefinedAttributeInfo> AttributeInfos;
									UInterchangeUserDefinedAttributesAPI::GetUserDefinedAttributeInfos(SceneNode, AttributeInfos);
									for (const FInterchangeUserDefinedAttributeInfo& AttributeInfo : AttributeInfos)
									{
										if (AttributeInfo.PayloadKey.IsSet())
										{
											MorphTargetsPerSkeletons.FindOrAdd(SkeletonFactoryNode);
											return true;
										}
									}
								}
								return false;
							});
					}
				}
			}
		});

	const TArray<FString> CustomAttributeNamesToImport = UAnimationSettings::Get()->GetBoneCustomAttributeNamesToImport();

	//for each animated skeleton create one anim sequence factory node
	for (const TPair<UInterchangeSkeletonFactoryNode*, TArray<const UInterchangeMeshNode*>>& SkeletonAndMorphTargets : MorphTargetsPerSkeletons)
	{
		UInterchangeSkeletonFactoryNode* SkeletonFactoryNode = SkeletonAndMorphTargets.Key;
		const FString AnimSequenceUid = TEXT("\\AnimSequence") + SkeletonFactoryNode->GetUniqueID();
		FString AnimSequenceName = SkeletonFactoryNode->GetDisplayLabel();
		if (AnimSequenceName.EndsWith(TEXT("_Skeleton")))
		{
			AnimSequenceName.LeftChopInline(9);
		}
		AnimSequenceName += TEXT("_Anim");

		if (bImportBoneTracks)
		{
			FFrameRate FrameRate = UE::Interchange::Animation::ConvertSampleRatetoFrameRate(SampleRate);

			const double SequenceLength = FMath::Max<double>(RangeStop - RangeStart, MINIMUM_ANIMATION_LENGTH);

			const float SubFrame = FrameRate.AsFrameTime(SequenceLength).GetSubFrame();

			if (!FMath::IsNearlyZero(SubFrame, KINDA_SMALL_NUMBER) && !FMath::IsNearlyEqual(SubFrame, 1.0f, KINDA_SMALL_NUMBER))
			{
				UInterchangeResultError_Generic* Message = AddMessage<UInterchangeResultError_Generic>();
				Message->SourceAssetName = SourceDatas[0]->GetFilename();
				Message->DestinationAssetName = AnimSequenceName;
				Message->AssetType = UAnimSequence::StaticClass();
				Message->Text = FText::Format(NSLOCTEXT("UInterchangeGenericAnimationPipeline", "WrongSequenceLength", "Animation length {0} is not compatible with import frame-rate {1} (sub frame {2}), animation has to be frame-border aligned."),
					FText::AsNumber(SequenceLength), FrameRate.ToPrettyText(), FText::AsNumber(SubFrame));
				//Skip this anim sequence factory node
				continue;
			}
		}

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

		AnimSequenceFactoryNode->SetCustomImportAttributeCurves(bImportCustomAttribute);
		AnimSequenceFactoryNode->SetCustomDoNotImportCurveWithZero(bDoNotImportCurveWithZero);
		AnimSequenceFactoryNode->SetCustomRemoveCurveRedundantKeys(bRemoveCurveRedundantKeys);
		AnimSequenceFactoryNode->SetCustomDeleteExistingMorphTargetCurves(bDeleteExistingMorphTargetCurves);
		AnimSequenceFactoryNode->SetCustomDeleteExistingCustomAttributeCurves(bDeleteExistingCustomAttributeCurves);
		AnimSequenceFactoryNode->SetCustomDeleteExistingNonCurveCustomAttributes(bDeleteExistingNonCurveCustomAttributes);

		AnimSequenceFactoryNode->SetCustomMaterialDriveParameterOnCustomAttribute(bSetMaterialDriveParameterOnCustomAttribute);
		for (const FString& MaterialSuffixe : MaterialCurveSuffixes)
		{
			AnimSequenceFactoryNode->SetAnimatedMaterialCurveSuffixe(MaterialSuffixe);
		}

		//Add the animated morph targets uid so the factory can import them
		if (SkeletonAndMorphTargets.Value.Num() > 0)
		{
			for (const UInterchangeMeshNode* MorphTargetNode : SkeletonAndMorphTargets.Value)
			{
				AnimSequenceFactoryNode->SetAnimatedMorphTargetDependencyUid(MorphTargetNode->GetUniqueID());
			}
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
			BaseNodeContainer->IterateNodeChildren(RootJointUid, [&AnimSequenceFactoryNode, &CustomAttributeNamesToImport](const UInterchangeBaseNode* Node)
			{
				if (const UInterchangeSceneNode* SceneNode = Cast<UInterchangeSceneNode>(Node))
				{
					FString BoneName = SceneNode->GetDisplayLabel();
					bool bImportAllAttributesOnBone = UAnimationSettings::Get()->BoneNamesWithCustomAttributes.Contains(BoneName);
						
					TArray<FInterchangeUserDefinedAttributeInfo> AttributeInfos;
					UInterchangeUserDefinedAttributesAPI::GetUserDefinedAttributeInfos(SceneNode, AttributeInfos);
					for (const FInterchangeUserDefinedAttributeInfo& AttributeInfo : AttributeInfos)
					{
						if (AttributeInfo.PayloadKey.IsSet())
						{
							bool bDecimalType = false;
							switch (AttributeInfo.Type)
							{
							case UE::Interchange::EAttributeTypes::Float:
							case UE::Interchange::EAttributeTypes::Float16:
							case UE::Interchange::EAttributeTypes::Double:
								{
									bDecimalType = true;
								}
								break;
							}

							const bool bForceImportBoneCustomAttribute = CustomAttributeNamesToImport.Contains(AttributeInfo.Name);
							//Material attribute curve
							if (!bImportAllAttributesOnBone && bDecimalType && !bForceImportBoneCustomAttribute)
							{
								AnimSequenceFactoryNode->SetAnimatedAttributeCurveName(AttributeInfo.Name);
							}
							else if (bForceImportBoneCustomAttribute || bImportAllAttributesOnBone)
							{
								AnimSequenceFactoryNode->SetAnimatedAttributeStepCurveName(AttributeInfo.Name);
							}
						}
					}
				}
			});
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

void UInterchangeGenericAnimationPipeline::CreateAnimationTrackSetFactoryNode(UInterchangeAnimationTrackSetNode& TranslatedNode)
{
	const FString FactoryNodeUid = UInterchangeFactoryBaseNode::BuildFactoryNodeUid(TranslatedNode.GetUniqueID());

	UInterchangeAnimationTrackSetFactoryNode* FactoryNode = NewObject<UInterchangeAnimationTrackSetFactoryNode>(BaseNodeContainer, NAME_None);

	FactoryNode->InitializeNode(FactoryNodeUid, TranslatedNode.GetDisplayLabel(), EInterchangeNodeContainerType::FactoryData);
	FactoryNode->SetEnabled(true);

	TArray<FString> AnimationTrackUids;
	TranslatedNode.GetCustomAnimationTrackUids(AnimationTrackUids);

	for (const FString& AnimationTrackUid : AnimationTrackUids)
	{
		FactoryNode->AddCustomAnimationTrackUid(AnimationTrackUid);

		// Update factory's dependencies
		if (const UInterchangeAnimationTrackBaseNode* TrackNode = Cast<UInterchangeAnimationTrackBaseNode>(BaseNodeContainer->GetNode(AnimationTrackUid)))
		{
			if (const UInterchangeTransformAnimationTrackNode* TransformTrackNode = Cast<UInterchangeTransformAnimationTrackNode>(TrackNode))
			{
				FString ActorNodeUid;
				if (TransformTrackNode->GetCustomActorDependencyUid(ActorNodeUid))
				{
					const FString ActorFactoryNodeUid = TEXT("Factory_") + ActorNodeUid;
					FactoryNode->AddFactoryDependencyUid(ActorFactoryNodeUid);
				}
			}
			else if (const UInterchangeAnimationTrackSetInstanceNode* InstanceTrackNode = Cast<UInterchangeAnimationTrackSetInstanceNode>(TrackNode))
			{
				FString TrackSetNodeUid;
				if (InstanceTrackNode->GetCustomTrackSetDependencyUid(TrackSetNodeUid))
				{
					const FString TrackSetFactoryNodeUid = TEXT("Factory_") + TrackSetNodeUid;
					FactoryNode->AddFactoryDependencyUid(TrackSetFactoryNodeUid);
				}

			}
		}
	}

	float FrameRate;
	if (TranslatedNode.GetCustomFrameRate(FrameRate))
	{
		FactoryNode->SetCustomFrameRate(FrameRate);
	}

	UInterchangeUserDefinedAttributesAPI::DuplicateAllUserDefinedAttribute(&TranslatedNode, FactoryNode, false);

	FactoryNode->AddTargetNodeUid(TranslatedNode.GetUniqueID());
	TranslatedNode.AddTargetNodeUid(FactoryNode->GetUniqueID());

	BaseNodeContainer->AddNode(FactoryNode);
}

