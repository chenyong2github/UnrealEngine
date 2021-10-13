// Copyright Epic Games, Inc. All Rights Reserved.

#include "ContextualAnimSceneAsset.h"
#include "AnimationRuntime.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimationPoseData.h"
#include "Animation/AnimTypes.h"
#include "ContextualAnimUtilities.h"
#include "ContextualAnimMetadata.h"
#include "UObject/ObjectSaveContext.h"
#include "ContextualAnimScenePivotProvider.h"
#include "Containers/ArrayView.h"

static FCompactPoseBoneIndex GetCompactPoseBoneIndexFromPose(const FCSPose<FCompactPose>& Pose, const FName& BoneName)
{
	const FBoneContainer& BoneContainer = Pose.GetPose().GetBoneContainer();
	for (int32 Idx = Pose.GetPose().GetNumBones() - 1; Idx >= 0; Idx--)
	{
		if (BoneContainer.GetReferenceSkeleton().GetBoneName(BoneContainer.GetBoneIndicesArray()[Idx]) == BoneName)
		{
			return FCompactPoseBoneIndex(Idx);
		}
	}

	checkf(false, TEXT("BoneName: %s Pose.Asset: %s Pose.NumBones: %d"), *BoneName.ToString(), *GetNameSafe(Pose.GetPose().GetBoneContainer().GetAsset()), Pose.GetPose().GetNumBones());
	return FCompactPoseBoneIndex(INDEX_NONE);
}

UContextualAnimSceneAsset::UContextualAnimSceneAsset(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bDisableCollisionBetweenActors = true;
	SampleRate = 15;
}

void UContextualAnimSceneAsset::PreSave(FObjectPreSaveContext ObjectSaveContext)
{
	// Necessary for FCompactPose that uses a FAnimStackAllocator (TMemStackAllocator) which allocates from FMemStack.
	// When allocating memory from FMemStack we need to explicitly use FMemMark to ensure items are freed when the scope exits. 
	// UWorld::Tick adds a FMemMark to catch any allocation inside the game tick 
	// but any allocation from outside the game tick (like here when generating the alignment tracks off-line) must explicitly add a mark to avoid a leak 
	FMemMark Mark(FMemStack::Get());

	Super::PreSave(ObjectSaveContext);

	// Set Index for each ContextualAnimData
	int32 NumAnimData = 0;
	for (auto& Entry : DataContainer)
	{
		NumAnimData = FMath::Max(NumAnimData, Entry.Value.AnimDataContainer.Num());
		for (int32 AnimDataIdx = 0; AnimDataIdx < Entry.Value.AnimDataContainer.Num(); AnimDataIdx++)
		{
			Entry.Value.AnimDataContainer[AnimDataIdx].Index = AnimDataIdx;
		}
	}

	// Generate scene pivot for each alignment section
	for (int32 AlignmentSectionIdx = 0; AlignmentSectionIdx < AlignmentSections.Num(); AlignmentSectionIdx++)
	{
		AlignmentSections[AlignmentSectionIdx].ScenePivots.Reset();

		// We need to calculate scene pivot for each set of animations
		for (int32 AnimDataIdx = 0; AnimDataIdx < NumAnimData; AnimDataIdx++)
		{
			if (AlignmentSections[AlignmentSectionIdx].ScenePivotProvider)
			{
				const FTransform ScenePivot = AlignmentSections[AlignmentSectionIdx].ScenePivotProvider->CalculateScenePivot_Source(AnimDataIdx);
				AlignmentSections[AlignmentSectionIdx].ScenePivots.Add(ScenePivot);
			}
			else
			{
				AlignmentSections[AlignmentSectionIdx].ScenePivots.Add(FTransform::Identity);
			}
		}
	}
	

	for (auto& Entry : DataContainer)
	{
		for (FContextualAnimData& Data : Entry.Value.AnimDataContainer)
		{
			// Generate alignment tracks relative to scene pivot
			GenerateAlignmentTracks(Entry.Value.Settings, Data);

			// Generate IK Targets
			GenerateIKTargetTracks(Entry.Value.Settings, Data);
		}
	}

	UpdateRadius();
}

void UContextualAnimSceneAsset::GenerateAlignmentTracks(const FContextualAnimTrackSettings& Settings, FContextualAnimData& AnimData)
{
	const FTransform MeshToComponentInverse = Settings.MeshToComponent.Inverse();
	const float SampleInterval = 1.f / SampleRate;

	// Initialize tracks for each alignment section
	const int32 TotalTracks = AlignmentSections.Num();
	AnimData.AlignmentData.Initialize(TotalTracks, SampleInterval);
	for (int32 Idx = 0; Idx < AlignmentSections.Num(); Idx++)
	{
		AnimData.AlignmentData.Tracks.TrackNames.Add(AlignmentSections[Idx].SectionName);
		AnimData.AlignmentData.Tracks.AnimationTracks.AddZeroed();
	}

	if (const UAnimMontage* Animation = AnimData.Animation)
	{
		float Time = 0.f;
		float EndTime = Animation->GetPlayLength();
		int32 SampleIndex = 0;
		while (Time < EndTime)
		{
			Time = FMath::Clamp(SampleIndex * SampleInterval, 0.f, EndTime);
			SampleIndex++;

			const FTransform RootTransform = MeshToComponentInverse * (UContextualAnimUtilities::ExtractRootTransformFromAnimation(Animation, Time) * AnimData.MeshToScene);

			for (int32 Idx = 0; Idx < AlignmentSections.Num(); Idx++)
			{
				FRawAnimSequenceTrack& SceneTrack = AnimData.AlignmentData.Tracks.AnimationTracks[Idx];

				const FTransform ScenePivotTransform = AlignmentSections[Idx].ScenePivots[AnimData.Index];
				const FTransform RootRelativeToScenePivot = RootTransform.GetRelativeTransform(ScenePivotTransform);

				SceneTrack.PosKeys.Add(FVector3f(RootRelativeToScenePivot.GetLocation()));
				SceneTrack.RotKeys.Add(FQuat4f(RootRelativeToScenePivot.GetRotation()));
			}
		}
	}
	else
	{
		for (int32 Idx = 0; Idx < AlignmentSections.Num(); Idx++)
		{
			AnimData.AlignmentData.Tracks.TrackNames.Add(AlignmentSections[Idx].SectionName);
			FRawAnimSequenceTrack& SceneTrack = AnimData.AlignmentData.Tracks.AnimationTracks.AddZeroed_GetRef();

			const FTransform RootTransform = MeshToComponentInverse * AnimData.MeshToScene;
			const FTransform ScenePivotTransform = AlignmentSections[Idx].ScenePivots[AnimData.Index];
			const FTransform RootRelativeToScenePivot = RootTransform.GetRelativeTransform(ScenePivotTransform);

			SceneTrack.PosKeys.Add(FVector3f(RootRelativeToScenePivot.GetLocation()));
			SceneTrack.RotKeys.Add(FQuat4f(RootRelativeToScenePivot.GetRotation()));
		}
	}
}

void UContextualAnimSceneAsset::GenerateIKTargetTracks(const FContextualAnimTrackSettings& Settings, FContextualAnimData& AnimData)
{
	AnimData.IKTargetData.Empty();

	if (Settings.IKTargetDefinitions.Num() == 0)
	{
		return;
	}

	if (const UAnimMontage* Animation = AnimData.Animation)
	{
		UE_LOG(LogContextualAnim, Log, TEXT("%s Generating IK Target Tracks. Animation: %s"), *GetNameSafe(this), *GetNameSafe(Animation));

		const float SampleInterval = 1.f / SampleRate;

		TArray<FBoneIndexType> RequiredBoneIndexArray;

		// Helper structure to group pose extraction per target so we can extract the pose for all the bones that are relative to the same target in one pass
		struct FPoseExtractionHelper
		{
			const FContextualAnimData* TargetAnimDataPtr = nullptr;
			TArray<TTuple<FName, FName, int32, FName, int32>> BonesData; //0: GoalName, 1: MyBoneName 2: MyBoneIndex, 3: TargetBoneName, 4: TargetBoneIndex
		};
		TMap<FName, FPoseExtractionHelper> PoseExtractionHelperMap;
		PoseExtractionHelperMap.Reserve(Settings.IKTargetDefinitions.Num());

		int32 TotalTracks = 0;
		for (const FContextualAnimIKTargetDefinition& IKTargetDef : Settings.IKTargetDefinitions)
		{
			if (IKTargetDef.Provider == EContextualAnimIKTargetProvider::Autogenerated)
			{
				const FName TargetRole = IKTargetDef.AutoParams.TargetRole;
				FPoseExtractionHelper* DataPtr = PoseExtractionHelperMap.Find(TargetRole);
				if (DataPtr == nullptr)
				{
					// Find AnimData for target role. 
					const FContextualAnimData* TargetAnimDataPtr = GetAnimDataForRoleAtIndex(TargetRole, AnimData.Index);
					if (TargetAnimDataPtr == nullptr)
					{
						UE_LOG(LogContextualAnim, Warning, TEXT("\t Can't find AnimTrack for TargetRole '%s'"), *TargetRole.ToString());
						continue;
					}

					DataPtr = &PoseExtractionHelperMap.Add(TargetRole);
					DataPtr->TargetAnimDataPtr = TargetAnimDataPtr;
				}

				const FName BoneName = IKTargetDef.BoneName;
				const int32 BoneIndex = Animation->GetSkeleton()->GetReferenceSkeleton().FindBoneIndex(BoneName);
				if (BoneIndex == INDEX_NONE)
				{
					UE_LOG(LogContextualAnim, Warning, TEXT("\t Can't find BoneIndex. BoneName: %s Animation: %s Skel: %s"),
						*BoneName.ToString(), *GetNameSafe(Animation), *GetNameSafe(Animation->GetSkeleton()));

					continue;
				}

				// Find TargetBoneIndex. Note that we add TargetBoneIndexeven if it is INDEX_NONE. In this case, my bone will be relative to the origin of the target actor. 
				// This is to support cases where the target actor doesn't have animation or TargetBoneName is None
				FName TargetBoneName = IKTargetDef.AutoParams.BoneName;
				const UAnimMontage* TargetAnimation = DataPtr->TargetAnimDataPtr->Animation;
				const int32 TargetBoneIndex = TargetAnimation ? TargetAnimation->GetSkeleton()->GetReferenceSkeleton().FindBoneIndex(TargetBoneName) : INDEX_NONE;
				if (TargetBoneIndex == INDEX_NONE)
				{
					UE_LOG(LogContextualAnim, Log, TEXT("\t Can't find TargetBoneIndex. BoneName: %s Animation: %s Skel: %s. Track for this bone will be relative to the origin of the target role."),
						*TargetBoneName.ToString(), *GetNameSafe(TargetAnimation), TargetAnimation ? *GetNameSafe(TargetAnimation->GetSkeleton()) : nullptr);

					TargetBoneName = NAME_None;
				}

				RequiredBoneIndexArray.AddUnique(BoneIndex);

				DataPtr->BonesData.Add(MakeTuple(IKTargetDef.IKGoalName, BoneName, BoneIndex, TargetBoneName, TargetBoneIndex));
				TotalTracks++;

				UE_LOG(LogContextualAnim, Log, TEXT("\t Bone added for extraction. GoalName: %s BoneName: %s (%d) TargetRole: %s TargetAnimation: %s TargetBone: %s (%d)"),
					*IKTargetDef.IKGoalName.ToString(), *BoneName.ToString(), BoneIndex, *TargetRole.ToString(), *GetNameSafe(TargetAnimation), *TargetBoneName.ToString(), TargetBoneIndex);
			}
		}

		if (TotalTracks > 0)
		{
			// Complete bones chain and create bone container to extract pose from my animation
			Animation->GetSkeleton()->GetReferenceSkeleton().EnsureParentsExistAndSort(RequiredBoneIndexArray);
			FBoneContainer BoneContainer = FBoneContainer(RequiredBoneIndexArray, FCurveEvaluationOption(false), *Animation->GetSkeleton());

			// Initialize track container
			AnimData.IKTargetData.Initialize(TotalTracks, SampleInterval);

			// Initialize lookup map to go from track name to target role and bone (the bone this track is relative to)
			AnimData.IKTargetTrackLookupMap.Empty(TotalTracks);

			float Time = 0.f;
			float EndTime = Animation->GetPlayLength();
			int32 SampleIndex = 0;
			while (Time < EndTime)
			{
				Time = FMath::Clamp(SampleIndex * SampleInterval, 0.f, EndTime);
				SampleIndex++;

				// Extract pose from my animation
				FCSPose<FCompactPose> ComponentSpacePose;
				UContextualAnimUtilities::ExtractComponentSpacePose(Animation, BoneContainer, Time, false, ComponentSpacePose);

				// For each target role
				for (auto& Data : PoseExtractionHelperMap)
				{
					// Extract pose from target animation if any
					FCSPose<FCompactPose> OtherComponentSpacePose;
					TArray<FBoneIndexType> OtherRequiredBoneIndexArray;
					FBoneContainer OtherBoneContainer;
					const UAnimMontage* OtherAnimation = Data.Value.TargetAnimDataPtr->Animation;
					if (OtherAnimation)
					{
						// Prepare array with the indices of the bones to extract from target animation
						OtherRequiredBoneIndexArray.Reserve(Data.Value.BonesData.Num());
						for (int32 Idx = 0; Idx < Data.Value.BonesData.Num(); Idx++)
						{
							const int32 TargetBoneIndex = Data.Value.BonesData[Idx].Get<4>();
							if (TargetBoneIndex != INDEX_NONE)
							{
								OtherRequiredBoneIndexArray.AddUnique(TargetBoneIndex);
							}
						}

						if (OtherRequiredBoneIndexArray.Num() > 0)
						{
							// Complete bones chain and create bone container to extract pose form the target animation
							OtherAnimation->GetSkeleton()->GetReferenceSkeleton().EnsureParentsExistAndSort(OtherRequiredBoneIndexArray);
							OtherBoneContainer = FBoneContainer(OtherRequiredBoneIndexArray, FCurveEvaluationOption(false), *OtherAnimation->GetSkeleton());

							// Extract pose from target animation
							UContextualAnimUtilities::ExtractComponentSpacePose(OtherAnimation, OtherBoneContainer, Time, false, OtherComponentSpacePose);
						}
					}

					for (int32 Idx = 0; Idx < Data.Value.BonesData.Num(); Idx++)
					{
						const FName TrackName = Data.Value.BonesData[Idx].Get<0>();
						AnimData.IKTargetData.Tracks.TrackNames.Add(TrackName);
						AnimData.IKTargetData.Tracks.AnimationTracks.AddZeroed();

						// Add entry to the lookup table
						auto& NewItem = AnimData.IKTargetTrackLookupMap.FindOrAdd(TrackName);
						NewItem.RoleName = Data.Key;
						NewItem.BoneName = Data.Value.BonesData[Idx].Get<3>();

						// Get bone transform from my animation
						const FName BoneName = Data.Value.BonesData[Idx].Get<1>();
						const FCompactPoseBoneIndex BoneIndex = GetCompactPoseBoneIndexFromPose(ComponentSpacePose, BoneName);
						const FTransform BoneTransform = (ComponentSpacePose.GetComponentSpaceTransform(BoneIndex) * AnimData.MeshToScene);

						// Get bone transform from target animation
						FTransform OtherBoneTransform = Data.Value.TargetAnimDataPtr->MeshToScene;
						const FName TargetBoneName = Data.Value.BonesData[Idx].Get<3>();
						if (TargetBoneName != NAME_None)
						{
							const FCompactPoseBoneIndex OtherBoneIndex = GetCompactPoseBoneIndexFromPose(OtherComponentSpacePose, TargetBoneName);
							OtherBoneTransform = (OtherComponentSpacePose.GetComponentSpaceTransform(OtherBoneIndex) * Data.Value.TargetAnimDataPtr->MeshToScene);
						}

						// Get transform relative to target
						const FTransform BoneRelativeToOther = BoneTransform.GetRelativeTransform(OtherBoneTransform);

						// Add transform to the track
						FRawAnimSequenceTrack& NewTrack = AnimData.IKTargetData.Tracks.AnimationTracks[Idx];
						NewTrack.PosKeys.Add(FVector3f(BoneRelativeToOther.GetLocation()));
						NewTrack.RotKeys.Add(FQuat4f(BoneRelativeToOther.GetRotation()));

						UE_LOG(LogContextualAnim, Verbose, TEXT("\t\t Animation: %s Time: %f BoneName: %s (T: %s) Target Animation: %s TargetBoneName: %s (T: %s)"),
							*GetNameSafe(Animation), Time, *BoneName.ToString(), *BoneTransform.GetLocation().ToString(),
							*GetNameSafe(OtherAnimation), *TargetBoneName.ToString(), *OtherBoneTransform.GetLocation().ToString());
					}
				}
			}
		}
	}
}

void UContextualAnimSceneAsset::UpdateRadius()
{
	Radius = 0.f;
	ForEachAnimData([this](const FName& Role, const FContextualAnimData& Data)
		{
			Radius = FMath::Max(Radius, (Data.GetAlignmentTransformAtEntryTime().GetLocation().Size()));
			return EContextualAnimForEachResult::Continue;
		});
}

bool UContextualAnimSceneAsset::QueryCompositeTrack(const FContextualAnimCompositeTrack* Track, FContextualAnimQueryResult& OutResult, const FContextualAnimQueryParams& QueryParams, const FTransform& ToWorldTransform) const
{
	OutResult.Reset();

	const FTransform QueryTransform = QueryParams.Querier.IsValid() ? QueryParams.Querier->GetActorTransform() : QueryParams.QueryTransform;

	int32 DataIndex = INDEX_NONE;
	if (Track)
	{
		if (QueryParams.bComplexQuery)
		{
			for (int32 Idx = 0; Idx < Track->AnimDataContainer.Num(); Idx++)
			{
				const FContextualAnimData& Data = Track->AnimDataContainer[Idx];

				if (Data.Metadata)
				{
					const FTransform EntryTransform = Data.GetAlignmentTransformAtEntryTime() * ToWorldTransform;

					if (!Data.Metadata->DoesQuerierPassConditions(FContextualAnimQuerier(QueryTransform), FContextualAnimQueryContext(ToWorldTransform), EntryTransform))
					{
						continue;
					}
				}

				// Return the first item that passes all tests
				DataIndex = Idx;
				break;
			}
		}
		else // Simple Query
		{
			float BestDistanceSq = MAX_FLT;
			for (int32 Idx = 0; Idx < Track->AnimDataContainer.Num(); Idx++)
			{
				const FContextualAnimData& Data = Track->AnimDataContainer[Idx];

				//@TODO: Convert querier location to local space instead
				const FTransform EntryTransform = Data.GetAlignmentTransformAtEntryTime() * ToWorldTransform;
				const float DistSq = FVector::DistSquared2D(EntryTransform.GetLocation(), QueryTransform.GetLocation());
				if (DistSq < BestDistanceSq)
				{
					BestDistanceSq = DistSq;
					DataIndex = Idx;
				}
			}
		}

		if (DataIndex != INDEX_NONE)
		{
			const FContextualAnimData& ResultData = Track->AnimDataContainer[DataIndex];

			OutResult.DataIndex = DataIndex;
			OutResult.Animation = ResultData.Animation;
			OutResult.EntryTransform = ResultData.GetAlignmentTransformAtEntryTime() * ToWorldTransform;
			OutResult.SyncTransform = ResultData.GetAlignmentTransformAtSyncTime() * ToWorldTransform;

			if (QueryParams.bFindAnimStartTime)
			{
				const FVector LocalLocation = (QueryTransform.GetRelativeTransform(ToWorldTransform)).GetLocation();
				OutResult.AnimStartTime = ResultData.FindBestAnimStartTime(LocalLocation);
			}

			return true;
		}
	}

	return false;
}

const FContextualAnimTrackSettings* UContextualAnimSceneAsset::GetTrackSettings(const FName& Role) const
{
	const FContextualAnimCompositeTrack* Track = DataContainer.Find(Role);
	return Track ? &Track->Settings : nullptr;
}

const FContextualAnimData* UContextualAnimSceneAsset::GetAnimDataForRoleAtIndex(const FName& Role, int32 Index) const
{
	if(const FContextualAnimCompositeTrack* Track = DataContainer.Find(Role))
	{
		if(Track->AnimDataContainer.IsValidIndex(Index))
		{
			return &Track->AnimDataContainer[Index];
		}
	}

	return nullptr;
}

void UContextualAnimSceneAsset::ForEachAnimData(FForEachAnimDataFunction Function) const
{
	for (const auto& Pair : DataContainer)
	{
		for (const FContextualAnimData& Data : Pair.Value.AnimDataContainer)
		{
			if (Function(Pair.Key, Data) == EContextualAnimForEachResult::Break)
			{
				return;
			}
		}
	}
}

TArray<FName> UContextualAnimSceneAsset::GetRoles() const
{
	TArray<FName> Roles;
	DataContainer.GetKeys(Roles);
	return Roles;
}

bool UContextualAnimSceneAsset::Query(const FName& Role, FContextualAnimQueryResult& OutResult, const FContextualAnimQueryParams& QueryParams, const FTransform& ToWorldTransform) const
{
	const FContextualAnimCompositeTrack* Track = DataContainer.Find(Role);
	return Track ? QueryCompositeTrack(Track, OutResult, QueryParams, ToWorldTransform) : false;
}

UAnimMontage* UContextualAnimSceneAsset::GetAnimationForRoleAtIndex(FName Role, int32 Index) const
{
	if(const FContextualAnimCompositeTrack* Track = DataContainer.Find(Role))
	{
		if(Track->AnimDataContainer.IsValidIndex(Index))
		{
			return Track->AnimDataContainer[Index].Animation;
		}
	}

	return nullptr;
}

FTransform UContextualAnimSceneAsset::ExtractAlignmentTransformAtTime(FName Role, int32 AnimDataIndex, float Time) const
{
	if (const FContextualAnimCompositeTrack* Track = DataContainer.Find(Role))
	{
		if (Track->AnimDataContainer.IsValidIndex(AnimDataIndex))
		{
			return Track->AnimDataContainer[AnimDataIndex].GetAlignmentTransformAtTime(Time);
		}
	}

	return FTransform::Identity;
}

FTransform UContextualAnimSceneAsset::ExtractIKTargetTransformAtTime(FName Role, int32 AnimDataIndex, FName TrackName, float Time) const
{
	if (const FContextualAnimCompositeTrack* Track = DataContainer.Find(Role))
	{
		if (Track->AnimDataContainer.IsValidIndex(AnimDataIndex))
		{
			return Track->AnimDataContainer[AnimDataIndex].IKTargetData.ExtractTransformAtTime(TrackName, Time);
		}
	}

	return FTransform::Identity;
}

int32 UContextualAnimSceneAsset::FindAnimIndex(FName Role, UAnimMontage* Animation) const
{
	int32 Result = INDEX_NONE;

	if (const FContextualAnimCompositeTrack* Track = DataContainer.Find(Role))
	{
		for(int32 Index = 0; Index < Track->AnimDataContainer.Num(); Index++)
		{
			if(Track->AnimDataContainer[Index].Animation == Animation)
			{
				Result = Index;
				break;
			}
		}
	}

	return Result;
}