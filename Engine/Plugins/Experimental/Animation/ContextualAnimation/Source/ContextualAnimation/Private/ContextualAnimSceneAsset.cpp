// Copyright Epic Games, Inc. All Rights Reserved.

#include "ContextualAnimSceneAsset.h"
#include "AnimationRuntime.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimationPoseData.h"
#include "Animation/AnimTypes.h"
#include "ContextualAnimUtilities.h"
#include "ContextualAnimSelectionCriterion.h"
#include "UObject/ObjectSaveContext.h"
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

	AlignmentSections.Add(FContextualAnimAlignmentSectionData());
}

#if WITH_EDITOR
void UContextualAnimSceneAsset::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName PropertyName = (PropertyChangedEvent.Property != nullptr) ? PropertyChangedEvent.Property->GetFName() : NAME_None;
	const FName MemberPropertyName = (PropertyChangedEvent.MemberProperty != nullptr) ? PropertyChangedEvent.MemberProperty->GetFName() : NAME_None;

	UE_LOG(LogContextualAnim, Log, TEXT("%s::PostEditChangeProperty MemberPropertyName: %s PropertyName: %s"),
		*GetNameSafe(this), *MemberPropertyName.ToString(), *PropertyName.ToString());

	if(MemberPropertyName == GET_MEMBER_NAME_CHECKED(UContextualAnimSceneAsset, AlignmentSections))
	{
		GenerateAlignmentTracks();
	}
}
#endif

void UContextualAnimSceneAsset::PreSave(FObjectPreSaveContext ObjectSaveContext)
{
	Super::PreSave(ObjectSaveContext);

	PrecomputeData();
}

void UContextualAnimSceneAsset::PrecomputeData()
{
	// Set VariantIdx for each AnimTrack
	for (int32 VariantIdx = 0; VariantIdx < Variants.Num(); VariantIdx++)
	{
		for (FContextualAnimTrack& AnimTrack : Variants[VariantIdx].Tracks)
		{
			AnimTrack.VariantIdx = VariantIdx;
		}
	}

	// Generate alignment tracks relative to scene pivot
	GenerateAlignmentTracks();

	// Generate IK Targets
	GenerateIKTargetTracks();

	UpdateRadius();
}

void UContextualAnimSceneAsset::GenerateAlignmentTracks()
{
	// Necessary for FCompactPose that uses a FAnimStackAllocator (TMemStackAllocator) which allocates from FMemStack.
	// When allocating memory from FMemStack we need to explicitly use FMemMark to ensure items are freed when the scope exits. 
	// UWorld::Tick adds a FMemMark to catch any allocation inside the game tick 
	// but any allocation from outside the game tick (like here when generating the alignment tracks off-line) must explicitly add a mark to avoid a leak 
	FMemMark Mark(FMemStack::Get());

	for (FContextualAnimTracksContainer& Variant : Variants)
	{
		Variant.ScenePivots.Reset();

		// Generate scene pivot for each alignment section
		for (const FContextualAnimAlignmentSectionData& AligmentSection : AlignmentSections)
		{
			FTransform ScenePivot = FTransform::Identity;
			FContextualAnimTrack* AnimTrack = Variant.Tracks.FindByPredicate([&AligmentSection](const FContextualAnimTrack& AnimTrack) { return AnimTrack.Role == AligmentSection.Origin; });
			if (AnimTrack)
			{
				if (AligmentSection.bAlongClosestDistance)
				{
					FContextualAnimTrack* OtherAnimTrack = Variant.Tracks.FindByPredicate([&AligmentSection](const FContextualAnimTrack& AnimTrack) { return AnimTrack.Role == AligmentSection.OtherRole; });
					if (OtherAnimTrack)
					{
						FTransform T1 = AnimTrack->Animation ? UContextualAnimUtilities::ExtractRootTransformFromAnimation(AnimTrack->Animation, 0.f) : FTransform::Identity;
						T1 = (GetMeshToComponentForRole(AnimTrack->Role).Inverse() * (T1 * AnimTrack->MeshToScene));

						FTransform T2 = OtherAnimTrack->Animation ? UContextualAnimUtilities::ExtractRootTransformFromAnimation(OtherAnimTrack->Animation, 0.f) : FTransform::Identity;
						T2 = (GetMeshToComponentForRole(OtherAnimTrack->Role).Inverse() * (T2 * OtherAnimTrack->MeshToScene));

						ScenePivot.SetLocation(FMath::Lerp<FVector>(T1.GetLocation(), T2.GetLocation(), AligmentSection.Weight));
						ScenePivot.SetRotation((T2.GetLocation() - T1.GetLocation()).GetSafeNormal2D().ToOrientationQuat());
					}
				}
				else
				{
					const FTransform RootTransform = AnimTrack->Animation ? UContextualAnimUtilities::ExtractRootTransformFromAnimation(AnimTrack->Animation, 0.f) : FTransform::Identity;
					ScenePivot = (GetMeshToComponentForRole(AnimTrack->Role).Inverse() * (RootTransform * AnimTrack->MeshToScene));
				}
			}

			Variant.ScenePivots.Add(ScenePivot);
		}

		// Generate alignment tracks relative to scene pivot
		for (FContextualAnimTrack& AnimTrack : Variant.Tracks)
		{
			UE_LOG(LogContextualAnim, Log, TEXT("%s Generating AlignmentTracks Tracks. Animation: %s"), *GetNameSafe(this), *GetNameSafe(AnimTrack.Animation));

			const FTransform MeshToComponentInverse = GetMeshToComponentForRole(AnimTrack.Role).Inverse();
			const float SampleInterval = 1.f / SampleRate;

			// Initialize tracks for each alignment section
			const int32 TotalTracks = AlignmentSections.Num();
			AnimTrack.AlignmentData.Initialize(TotalTracks, SampleInterval);
			for (int32 Idx = 0; Idx < TotalTracks; Idx++)
			{
				AnimTrack.AlignmentData.Tracks.TrackNames.Add(AlignmentSections[Idx].WarpTargetName);
				AnimTrack.AlignmentData.Tracks.AnimationTracks.AddZeroed();
			}

			if (const UAnimSequenceBase* Animation = AnimTrack.Animation)
			{
				float Time = 0.f;
				float EndTime = Animation->GetPlayLength();
				int32 SampleIndex = 0;
				while (Time < EndTime)
				{
					Time = FMath::Clamp(SampleIndex * SampleInterval, 0.f, EndTime);
					SampleIndex++;

					const FTransform RootTransform = MeshToComponentInverse * (UContextualAnimUtilities::ExtractRootTransformFromAnimation(Animation, Time) * AnimTrack.MeshToScene);

					for (int32 Idx = 0; Idx < TotalTracks; Idx++)
					{
						FRawAnimSequenceTrack& AlignmentTrack = AnimTrack.AlignmentData.Tracks.AnimationTracks[Idx];

						const FTransform ScenePivotTransform = Variants[AnimTrack.VariantIdx].ScenePivots[Idx];
						const FTransform RootRelativeToScenePivot = RootTransform.GetRelativeTransform(ScenePivotTransform);

						AlignmentTrack.PosKeys.Add(FVector3f(RootRelativeToScenePivot.GetLocation()));
						AlignmentTrack.RotKeys.Add(FQuat4f(RootRelativeToScenePivot.GetRotation()));
					}
				}
			}
			else
			{
				const FTransform RootTransform = MeshToComponentInverse * AnimTrack.MeshToScene;

				for (int32 Idx = 0; Idx < TotalTracks; Idx++)
				{
					FRawAnimSequenceTrack& SceneTrack = AnimTrack.AlignmentData.Tracks.AnimationTracks[Idx];

					const FTransform ScenePivotTransform = Variants[AnimTrack.VariantIdx].ScenePivots[Idx];
					const FTransform RootRelativeToScenePivot = RootTransform.GetRelativeTransform(ScenePivotTransform);

					SceneTrack.PosKeys.Add(FVector3f(RootRelativeToScenePivot.GetLocation()));
					SceneTrack.RotKeys.Add(FQuat4f(RootRelativeToScenePivot.GetRotation()));
				}
			}
		}
	}
}

void UContextualAnimSceneAsset::GenerateIKTargetTracks()
{
	FMemMark Mark(FMemStack::Get());

	for (FContextualAnimTracksContainer& Variant : Variants)
	{
		for (FContextualAnimTrack& AnimTrack : Variant.Tracks)
		{
			AnimTrack.IKTargetData.Empty();

			const FContextualAnimIKTargetDefContainer* IKTargetDefContainerPtr = RoleToIKTargetDefsMap.Find(AnimTrack.Role);
			if (IKTargetDefContainerPtr == nullptr || IKTargetDefContainerPtr->IKTargetDefs.Num() == 0)
			{
				continue;
			}

			if (const UAnimSequenceBase* Animation = AnimTrack.Animation)
			{
				UE_LOG(LogContextualAnim, Log, TEXT("%s Generating IK Target Tracks. Animation: %s"), *GetNameSafe(this), *GetNameSafe(Animation));

				const float SampleInterval = 1.f / SampleRate;

				TArray<FBoneIndexType> RequiredBoneIndexArray;

				// Helper structure to group pose extraction per target so we can extract the pose for all the bones that are relative to the same target in one pass
				struct FPoseExtractionHelper
				{
					const FContextualAnimTrack* TargetAnimTrackPtr = nullptr;
					TArray<TTuple<FName, FName, int32, FName, int32>> BonesData; //0: GoalName, 1: MyBoneName 2: MyBoneIndex, 3: TargetBoneName, 4: TargetBoneIndex
				};
				TMap<FName, FPoseExtractionHelper> PoseExtractionHelperMap;
				PoseExtractionHelperMap.Reserve(IKTargetDefContainerPtr->IKTargetDefs.Num());

				int32 TotalTracks = 0;
				for (const FContextualAnimIKTargetDefinition& IKTargetDef : IKTargetDefContainerPtr->IKTargetDefs)
				{
					if (IKTargetDef.Provider == EContextualAnimIKTargetProvider::Autogenerated)
					{
						const FName TargetRole = IKTargetDef.TargetRoleName;
						FPoseExtractionHelper* DataPtr = PoseExtractionHelperMap.Find(TargetRole);
						if (DataPtr == nullptr)
						{
							// Find track for target role. 
							const FContextualAnimTrack* TargetAnimTrackPtr = GetAnimTrack(TargetRole, AnimTrack.VariantIdx);
							if (TargetAnimTrackPtr == nullptr)
							{
								UE_LOG(LogContextualAnim, Warning, TEXT("\t Can't find AnimTrack for TargetRole '%s'"), *TargetRole.ToString());
								continue;
							}

							DataPtr = &PoseExtractionHelperMap.Add(TargetRole);
							DataPtr->TargetAnimTrackPtr = TargetAnimTrackPtr;
						}

						const FName BoneName = IKTargetDef.BoneName;
						const int32 BoneIndex = Animation->GetSkeleton()->GetReferenceSkeleton().FindBoneIndex(BoneName);
						if (BoneIndex == INDEX_NONE)
						{
							UE_LOG(LogContextualAnim, Warning, TEXT("\t Can't find BoneIndex. BoneName: %s Animation: %s Skel: %s"),
								*BoneName.ToString(), *GetNameSafe(Animation), *GetNameSafe(Animation->GetSkeleton()));

							continue;
						}

						// Find TargetBoneIndex. Note that we add TargetBoneIndex even if it is INDEX_NONE. In this case, my bone will be relative to the origin of the target actor. 
						// This is to support cases where the target actor doesn't have animation or TargetBoneName is None
						FName TargetBoneName = IKTargetDef.TargetBoneName;
						const UAnimSequenceBase* TargetAnimation = DataPtr->TargetAnimTrackPtr->Animation;
						const int32 TargetBoneIndex = TargetAnimation ? TargetAnimation->GetSkeleton()->GetReferenceSkeleton().FindBoneIndex(TargetBoneName) : INDEX_NONE;
						if (TargetBoneIndex == INDEX_NONE)
						{
							UE_LOG(LogContextualAnim, Log, TEXT("\t Can't find TargetBoneIndex. BoneName: %s Animation: %s Skel: %s. Track for this bone will be relative to the origin of the target role."),
								*TargetBoneName.ToString(), *GetNameSafe(TargetAnimation), TargetAnimation ? *GetNameSafe(TargetAnimation->GetSkeleton()) : nullptr);

							TargetBoneName = NAME_None;
						}

						RequiredBoneIndexArray.AddUnique(BoneIndex);

						DataPtr->BonesData.Add(MakeTuple(IKTargetDef.GoalName, BoneName, BoneIndex, TargetBoneName, TargetBoneIndex));
						TotalTracks++;

						UE_LOG(LogContextualAnim, Log, TEXT("\t Bone added for extraction. GoalName: %s BoneName: %s (%d) TargetRole: %s TargetAnimation: %s TargetBone: %s (%d)"),
							*IKTargetDef.GoalName.ToString(), *BoneName.ToString(), BoneIndex, *TargetRole.ToString(), *GetNameSafe(TargetAnimation), *TargetBoneName.ToString(), TargetBoneIndex);
					}
				}

				if (TotalTracks > 0)
				{
					// Complete bones chain and create bone container to extract pose from my animation
					Animation->GetSkeleton()->GetReferenceSkeleton().EnsureParentsExistAndSort(RequiredBoneIndexArray);
					FBoneContainer BoneContainer = FBoneContainer(RequiredBoneIndexArray, FCurveEvaluationOption(false), *Animation->GetSkeleton());

					// Initialize track container
					AnimTrack.IKTargetData.Initialize(TotalTracks, SampleInterval);

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
							const UAnimSequenceBase* OtherAnimation = Data.Value.TargetAnimTrackPtr->Animation;
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
								AnimTrack.IKTargetData.Tracks.TrackNames.Add(TrackName);
								AnimTrack.IKTargetData.Tracks.AnimationTracks.AddZeroed();

								// Get bone transform from my animation
								const FName BoneName = Data.Value.BonesData[Idx].Get<1>();
								const FCompactPoseBoneIndex BoneIndex = GetCompactPoseBoneIndexFromPose(ComponentSpacePose, BoneName);
								const FTransform BoneTransform = (ComponentSpacePose.GetComponentSpaceTransform(BoneIndex) * AnimTrack.MeshToScene);

								// Get bone transform from target animation
								FTransform OtherBoneTransform = Data.Value.TargetAnimTrackPtr->MeshToScene;
								const FName TargetBoneName = Data.Value.BonesData[Idx].Get<3>();
								if (TargetBoneName != NAME_None)
								{
									const FCompactPoseBoneIndex OtherBoneIndex = GetCompactPoseBoneIndexFromPose(OtherComponentSpacePose, TargetBoneName);
									OtherBoneTransform = (OtherComponentSpacePose.GetComponentSpaceTransform(OtherBoneIndex) * Data.Value.TargetAnimTrackPtr->MeshToScene);
								}

								// Get transform relative to target
								const FTransform BoneRelativeToOther = BoneTransform.GetRelativeTransform(OtherBoneTransform);

								// Add transform to the track
								FRawAnimSequenceTrack& NewTrack = AnimTrack.IKTargetData.Tracks.AnimationTracks[Idx];
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
	}
}

void UContextualAnimSceneAsset::UpdateRadius()
{
	Radius = 0.f;
	ForEachAnimTrack([this](const FContextualAnimTrack& AnimTrack)
		{
			Radius = FMath::Max(Radius, (AnimTrack.GetAlignmentTransformAtEntryTime().GetLocation().Size()));
			return UE::ContextualAnim::EForEachResult::Continue;
		});
}

FName UContextualAnimSceneAsset::FindRoleByAnimation(const UAnimSequenceBase* Animation) const
{
	FName Result = NAME_None;

	ForEachAnimTrack([&Result, Animation](const FContextualAnimTrack& AnimTrack)
		{
			if(AnimTrack.Animation == Animation)
			{
				Result = AnimTrack.Role;
				return UE::ContextualAnim::EForEachResult::Break;
			}

			return UE::ContextualAnim::EForEachResult::Continue;
		});

	return Result;
}

const FContextualAnimIKTargetDefContainer& UContextualAnimSceneAsset::GetIKTargetDefsForRole(const FName& Role) const
{
	if (const FContextualAnimIKTargetDefContainer* ContainerPtr = RoleToIKTargetDefsMap.Find(Role))
	{
		return *ContainerPtr;
	}

	return FContextualAnimIKTargetDefContainer::EmptyContainer;
}

const FTransform& UContextualAnimSceneAsset::GetMeshToComponentForRole(const FName& Role) const
{
	if (RolesAsset)
	{
		if (const FContextualAnimRoleDefinition* RoleDef = RolesAsset->FindRoleDefinitionByName(Role))
		{
			return RoleDef->MeshToComponent;
		}
	}

	return FTransform::Identity;
}

const FContextualAnimTrack* UContextualAnimSceneAsset::GetAnimTrack(const FName& Role, int32 VariantIdx) const
{
	if(Variants.IsValidIndex(VariantIdx))
	{
		return Variants[VariantIdx].Tracks.FindByPredicate([Role](const FContextualAnimTrack& AnimTrack){ return AnimTrack.Role == Role;});
	}

	return nullptr;
}

void UContextualAnimSceneAsset::ForEachAnimTrack(FForEachAnimTrackFunction Function) const
{
	for (const FContextualAnimTracksContainer& TracksContainer : Variants)
	{
		for (const FContextualAnimTrack& AnimTrack : TracksContainer.Tracks)
		{
			if (Function(AnimTrack) == UE::ContextualAnim::EForEachResult::Break)
			{
				return;
			}
		}
	}
}

void UContextualAnimSceneAsset::ForEachAnimTrack(int32 VariantIdx, FForEachAnimTrackFunction Function) const
{
	if(Variants.IsValidIndex(VariantIdx))
	{
		const TArray<FContextualAnimTrack>& Tracks = Variants[VariantIdx].Tracks;
		for (const FContextualAnimTrack& AnimTrack : Tracks)
		{
			if (Function(AnimTrack) == UE::ContextualAnim::EForEachResult::Break)
			{
				return;
			}
		}
	}
}

TArray<FName> UContextualAnimSceneAsset::GetRoles() const
{
 	TArray<FName> Result;

	if(RolesAsset)
	{
		for (const FContextualAnimRoleDefinition& RoleDef : RolesAsset->Roles)
		{
			Result.Add(RoleDef.Name);
		}
	}

 	return Result;
}

const FContextualAnimTrack* UContextualAnimSceneAsset::FindFirstAnimTrackForRoleThatPassesSelectionCriteria(const FName& Role, const FContextualAnimSceneBindingContext& Primary, const FContextualAnimSceneBindingContext& Querier) const
{
	for (const FContextualAnimTracksContainer& Variant : Variants)
	{
		for (const FContextualAnimTrack& AnimTrack : Variant.Tracks)
		{
			if (AnimTrack.Role == Role)
			{
				bool bSuccess = true;
				for (const UContextualAnimSelectionCriterion* Criterion : AnimTrack.SelectionCriteria)
				{
					if (Criterion && !Criterion->DoesQuerierPassCondition(Primary, Querier))
					{
						bSuccess = false;
						break;
					}
				}

				if (bSuccess)
				{
					return &AnimTrack;
				}
			}
		}
	}

	return nullptr;
}

const FContextualAnimTrack* UContextualAnimSceneAsset::FindAnimTrackForRoleWithClosestEntryLocation(const FName& Role, const FContextualAnimSceneBindingContext& Primary, const FVector& TestLocation) const
{
	const FContextualAnimTrack* Result = nullptr;

	float BestDistanceSq = MAX_FLT;
	for (const FContextualAnimTracksContainer& Variant : Variants)
	{
		for (const FContextualAnimTrack& AnimTrack : Variant.Tracks)
		{
			if (AnimTrack.Role == Role)
			{
				const FTransform EntryTransform = AnimTrack.GetAlignmentTransformAtEntryTime() * Primary.GetTransform();
				const float DistSq = FVector::DistSquared(EntryTransform.GetLocation(), TestLocation);
				if (DistSq < BestDistanceSq)
				{
					BestDistanceSq = DistSq;
					Result = &AnimTrack;
				}
			}
		}
	}

	return Result;
}

bool UContextualAnimSceneAsset::Query(FName Role, FContextualAnimQueryResult& OutResult, const FContextualAnimQueryParams& QueryParams, const FTransform& ToWorldTransform) const
{
	//@TODO: Kept around only to do not break existing content. It will go away in the future.

	FContextualAnimSceneBindingContext Primary(ToWorldTransform);
	FContextualAnimSceneBindingContext Querier(QueryParams.Querier.IsValid() ? QueryParams.Querier->GetActorTransform() : QueryParams.QueryTransform);

	if (const FContextualAnimTrack* AnimTrack = FindFirstAnimTrackForRoleThatPassesSelectionCriteria(Role, Primary, Querier))
	{
		OutResult.VariantIdx = AnimTrack->VariantIdx;
		OutResult.Animation = Cast<UAnimMontage>(AnimTrack->Animation);
		OutResult.EntryTransform = AnimTrack->GetAlignmentTransformAtEntryTime() * ToWorldTransform;
		OutResult.SyncTransform = AnimTrack->GetAlignmentTransformAtSyncTime() * ToWorldTransform;

		if (QueryParams.bFindAnimStartTime)
		{
			const FVector LocalLocation = (Querier.GetTransform().GetRelativeTransform(ToWorldTransform)).GetLocation();
			OutResult.AnimStartTime = AnimTrack->FindBestAnimStartTime(LocalLocation);
		}

		return true;
	}

	return false;
}

FTransform UContextualAnimSceneAsset::GetAlignmentTransformForRoleRelativeToScenePivot(FName Role, int32 VariantIdx, float Time) const
{
	if (const FContextualAnimTrack* AnimTrack = GetAnimTrack(Role, VariantIdx))
	{
		return AnimTrack->GetAlignmentTransformAtTime(Time);
	}

	return FTransform::Identity;
}

FTransform UContextualAnimSceneAsset::GetAlignmentTransformForRoleRelativeToOtherRole(FName FromRole, FName ToRole, int32 VariantIdx, float Time) const
{
	const FTransform FromRoleRelativeToScenePivot = GetAlignmentTransformForRoleRelativeToScenePivot(FromRole, VariantIdx, Time);
	const FTransform ToRoleRelativeToScenePivot = GetAlignmentTransformForRoleRelativeToScenePivot(ToRole, VariantIdx, Time);
	return FromRoleRelativeToScenePivot.GetRelativeTransform(ToRoleRelativeToScenePivot);
}

FTransform UContextualAnimSceneAsset::GetIKTargetTransformForRoleAtTime(FName Role, int32 VariantIdx, FName TrackName, float Time) const
{
	if(const FContextualAnimTrack* AnimTrack = GetAnimTrack(Role, VariantIdx))
	{
		return AnimTrack->IKTargetData.ExtractTransformAtTime(TrackName, Time);		
	}

	return FTransform::Identity;
}

int32 UContextualAnimSceneAsset::FindVariantIdx(FName Role, UAnimSequenceBase* Animation) const
{
	int32 Result = INDEX_NONE;

	ForEachAnimTrack([&Result, Role, Animation](const FContextualAnimTrack& AnimTrack)
		{
			if (AnimTrack.Role == Role && AnimTrack.Animation == Animation)
			{
				Result = AnimTrack.VariantIdx;
				return UE::ContextualAnim::EForEachResult::Break;
			}

			return UE::ContextualAnim::EForEachResult::Continue;
		});

	return Result;
}

int32 UContextualAnimSceneAsset::GetTotalVariants() const
{
	return Variants.Num();
}