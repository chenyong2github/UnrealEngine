// Copyright Epic Games, Inc. All Rights Reserved.

#include "ContextualAnimAsset.h"
#include "Components/SkeletalMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "AnimationUtils.h"
#include "AnimationRuntime.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimationPoseData.h"
#include "Animation/AnimTypes.h"
#include "Containers/ArrayView.h"

DEFINE_LOG_CATEGORY(LogContextualAnim);

// UContextualAnimUtilities
///////////////////////////////////////////////////////////////////////

void UContextualAnimUtilities::ExtractLocalSpacePose(const UAnimSequenceBase* Animation, const FBoneContainer& BoneContainer, float Time, bool bExtractRootMotion, FCompactPose& OutPose)
{
	OutPose.SetBoneContainer(&BoneContainer);

	FBlendedCurve Curve;
	Curve.InitFrom(BoneContainer);

	FAnimExtractContext Context(Time, bExtractRootMotion);

	FStackCustomAttributes Attributes;
	FAnimationPoseData AnimationPoseData(OutPose, Curve, Attributes);
	if (const UAnimSequence* AnimSequence = Cast<UAnimSequence>(Animation))
	{
		AnimSequence->GetBonePose(AnimationPoseData, Context);
	}
	else if (const UAnimMontage* AnimMontage = Cast<UAnimMontage>(Animation))
	{
		const FAnimTrack& AnimTrack = AnimMontage->SlotAnimTracks[0].AnimTrack;
		AnimTrack.GetAnimationPose(AnimationPoseData, Context);
	}
}

void UContextualAnimUtilities::ExtractComponentSpacePose(const UAnimSequenceBase* Animation, const FBoneContainer& BoneContainer, float Time, bool bExtractRootMotion, FCSPose<FCompactPose>& OutPose)
{
	FCompactPose Pose;
	ExtractLocalSpacePose(Animation, BoneContainer, Time, bExtractRootMotion, Pose);
	OutPose.InitPose(MoveTemp(Pose));
}

FTransform UContextualAnimUtilities::ExtractRootMotionFromAnimation(const UAnimSequenceBase* Animation, float StartTime, float EndTime)
{
	if (const UAnimMontage* Anim = Cast<UAnimMontage>(Animation))
	{
		return Anim->ExtractRootMotionFromTrackRange(StartTime, EndTime);
	}

	if (const UAnimSequence* Anim = Cast<UAnimSequence>(Animation))
	{
		return Anim->ExtractRootMotionFromRange(StartTime, EndTime);
	}

	return FTransform::Identity;
}

// FAlignmentTrackContainer
///////////////////////////////////////////////////////////////////////

FTransform FAlignmentTrackContainer::ExtractTransformAtTime(float Time) const
{
	FTransform AlignmentTransform = FTransform::Identity;
	const int32 TotalFrames = Track.PosKeys.Num();
	const float TrackLength = (TotalFrames - 1) * SampleInterval;
	FAnimationUtils::ExtractTransformFromTrack(Time, TotalFrames, TrackLength, Track, EAnimInterpolationType::Linear, AlignmentTransform);
	return AlignmentTransform;
}

// FContextualAnimData
///////////////////////////////////////////////////////////////////////

float FContextualAnimData::FindBestAnimStartTime(const FVector& LocalLocation) const
{
	float BestTime = 0.f;

	if(AnimMinStartTime < 0.f)
	{
		return BestTime;
	}

	const FVector SyncPointLocation = GetAlignmentTransformAtSyncTime().GetLocation();
	const float PerfectDistToSyncPointSq = GetAlignmentTransformAtEntryTime().GetTranslation().SizeSquared2D();
	const float ActualDistToSyncPointSq = FVector::DistSquared2D(LocalLocation, SyncPointLocation);

	if(ActualDistToSyncPointSq < PerfectDistToSyncPointSq)
	{
		float BestDistance = MAX_FLT;
		TArrayView<const FVector> PosKeys(AlignmentData.Track.PosKeys.GetData(), AlignmentData.Track.PosKeys.Num());

		//@TODO: Very simple search for now. Replace with Distance Matching + Pose Matching
		for (int32 Idx = 0; Idx < PosKeys.Num(); Idx++)
		{
			const float Time = Idx * AlignmentData.SampleInterval;
			if (AnimMinStartTime > 0.f && Time >= AnimMinStartTime)
			{
				break;
			}

			const float DistFromCurrentFrameToSyncPointSq = FVector::DistSquared2D(SyncPointLocation, PosKeys[Idx]);
			if (DistFromCurrentFrameToSyncPointSq < ActualDistToSyncPointSq)
			{
				BestTime = Time;
				break;
			}
		}
	}

	return BestTime;
}

// UContextualAnimAsset
///////////////////////////////////////////////////////////////////////

UContextualAnimAsset::UContextualAnimAsset(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer) 
{
	SampleRate = 15;
	MeshToComponent = FTransform(FRotator(0.f, -90.f, 0.f));
	MotionWarpSyncPointName = FName(TEXT("Interact"));
}

void UContextualAnimAsset::PreSave(const class ITargetPlatform* TargetPlatform)
{
	const FTransform MeshToComponentInverse = MeshToComponent.Inverse();

	TArray<FBoneIndexType> RequiredBoneIndexArray;
	RequiredBoneIndexArray.Add(0);
	FBoneContainer BoneContainer;

	const float SampleInterval = 1.f / SampleRate;

	for (FContextualAnimData& Data : DataContainer)
	{
		const UAnimMontage* Animation = Data.Animation.LoadSynchronous();

		if(Animation == nullptr || !Animation->IsValidSectionIndex(0))
		{
			continue;
		}
		
		// For now we just assume the entry animation for the interaction is the first section of the montage
		float SectionStartTime, SectionEndTime;
		Animation->GetSectionStartAndEndTime(0, SectionStartTime, SectionEndTime);

		float Time = 0.f;
		float EndTime = SectionEndTime; 
		int32 SampleIndex = 0;

		Data.AlignmentData.Initialize(SampleInterval);
		FRawAnimSequenceTrack& NewTrack = Data.AlignmentData.Track;

		if (AlignmentJoint.IsNone())
		{
			if(!BoneContainer.IsValid())
			{
				BoneContainer = FBoneContainer(RequiredBoneIndexArray, FCurveEvaluationOption(false), *Animation->GetSkeleton());
			}

			while (Time < EndTime)
			{
				Time = FMath::Clamp(SampleIndex * SampleInterval, 0.f, EndTime);
				SampleIndex++;

				FCSPose<FCompactPose> ComponentSpacePose;
				UContextualAnimUtilities::ExtractComponentSpacePose(Animation, BoneContainer, Time, false, ComponentSpacePose);

				const FTransform RootTransform = MeshToComponentInverse * ComponentSpacePose.GetComponentSpaceTransform(FCompactPoseBoneIndex(0));
				const FTransform RootRelativeToRef = RootTransform.GetRelativeTransform(MeshToComponentInverse);

				NewTrack.PosKeys.Add(RootRelativeToRef.GetLocation());
				NewTrack.RotKeys.Add(RootRelativeToRef.GetRotation());
				NewTrack.ScaleKeys.Add(RootRelativeToRef.GetScale3D());
			};
		}
		else
		{
			if (!BoneContainer.IsValid())
			{
				const int32 AlignmentJointIdx = Animation->GetSkeleton()->GetReferenceSkeleton().FindBoneIndex(AlignmentJoint);
				if (AlignmentJointIdx == INDEX_NONE)
				{
					UE_LOG(LogContextualAnim, Warning, TEXT("%s Can't generate alignment track for %s. Reason: AlignmentJoint (%s) invalid"),
						*GetNameSafe(this), *GetNameSafe(Animation), *AlignmentJoint.ToString());

					continue;
				}

				RequiredBoneIndexArray.Add(AlignmentJointIdx);
				Animation->GetSkeleton()->GetReferenceSkeleton().EnsureParentsExistAndSort(RequiredBoneIndexArray);

				BoneContainer = FBoneContainer(RequiredBoneIndexArray, FCurveEvaluationOption(false), *Animation->GetSkeleton());
			}

			while (Time < EndTime)
			{
				Time = FMath::Clamp(SampleIndex * SampleInterval, 0.f, EndTime);
				SampleIndex++;

				FCSPose<FCompactPose> ComponentSpacePose;
				UContextualAnimUtilities::ExtractComponentSpacePose(Animation, BoneContainer, Time, false, ComponentSpacePose);

				const FTransform RootTransform = MeshToComponentInverse * ComponentSpacePose.GetComponentSpaceTransform(FCompactPoseBoneIndex(0));

				FCompactPoseBoneIndex BoneIndex = BoneContainer.MakeCompactPoseIndex(FMeshPoseBoneIndex(BoneContainer.GetReferenceSkeleton().FindBoneIndex(AlignmentJoint)));
				const FTransform RefTransform = MeshToComponentInverse * ComponentSpacePose.GetComponentSpaceTransform(FCompactPoseBoneIndex(BoneIndex));

				const FTransform RootRelativeToRef = RootTransform.GetRelativeTransform(RefTransform);

				NewTrack.PosKeys.Add(RootRelativeToRef.GetLocation());
				NewTrack.RotKeys.Add(RootRelativeToRef.GetRotation());
				NewTrack.ScaleKeys.Add(RootRelativeToRef.GetScale3D());
			};
		}

		const FTransform EntryTransform = Data.GetAlignmentTransformAtEntryTime();

		// Reference value for Distance Test
		Data.Distance.Value = EntryTransform.GetTranslation().Size2D();

		// Reference value for Angle Test
		const FVector Direction = EntryTransform.GetTranslation().GetSafeNormal2D();
		FQuat Delta = FQuat::FindBetweenNormals(Direction, FVector::ForwardVector);
		Data.Angle.Value = Delta.Rotator().Yaw;

		// Reference value for Facing Test
		Data.Facing.Value = EntryTransform.Rotator().Yaw;
	}

	Super::PreSave(TargetPlatform);
}

bool UContextualAnimAsset::QueryData(FContextualAnimQueryResult& OutResult, const FContextualAnimQueryParams& QueryParams, const FTransform& ToWorldTransform) const
{
	OutResult.Reset();

	const FTransform QueryTransform = QueryParams.Querier.IsValid() ? QueryParams.Querier->GetActorTransform() : QueryParams.QueryTransform;

	int32 DataIndex = INDEX_NONE;
	if (QueryParams.bComplexQuery)
	{
		for (int32 Idx = 0; Idx < DataContainer.Num(); Idx++)
		{
			const FContextualAnimData& Data = DataContainer[Idx];
			const FTransform EntryTransform = Data.GetAlignmentTransformAtEntryTime() * ToWorldTransform;

			FVector Origin = ToWorldTransform.GetLocation();
			FVector Direction = (EntryTransform.GetLocation() - Origin).GetSafeNormal2D();

			if (Data.OffsetFromOrigin != 0.f)
			{
				Origin = Origin + Direction * Data.OffsetFromOrigin;
			}

			// Distance Test
			//--------------------------------------------------
			if (Data.Distance.MaxDistance > 0.f || Data.Distance.MinDistance > 0.f)
			{
				const float DistSq = FVector::DistSquared2D(Origin, QueryTransform.GetLocation());

				if (Data.Distance.MaxDistance > 0.f)
				{
					if (DistSq > FMath::Square(Data.Distance.MaxDistance))
					{
						continue;
					}
				}

				if (Data.Distance.MinDistance > 0.f)
				{
					if (DistSq < FMath::Square(Data.Distance.MinDistance))
					{
						continue;
					}
				}
			}

			// Angle Test
			//--------------------------------------------------
			if (Data.Angle.Tolerance > 0.f)
			{
				//@TODO: Cache this
				const float AngleCos = FMath::Cos(FMath::Clamp(FMath::DegreesToRadians(Data.Angle.Tolerance), 0.f, PI));
				const FVector ToLocation = (QueryTransform.GetLocation() - Origin).GetSafeNormal2D();
				if (FVector::DotProduct(ToLocation, Direction) < AngleCos)
				{
					continue;
				}
			}

			// Facing Test
			//--------------------------------------------------
			if (Data.Facing.Tolerance > 0.f)
			{
				const FTransform SyncTransform = Data.GetAlignmentTransformAtSyncTime() * ToWorldTransform;

				//@TODO: Cache this
				const float FacingCos = FMath::Cos(FMath::Clamp(FMath::DegreesToRadians(Data.Facing.Tolerance), 0.f, PI));
				const FVector ToSyncPoint = (SyncTransform.GetLocation() - QueryTransform.GetLocation()).GetSafeNormal2D();
				if (FVector::DotProduct(QueryTransform.GetRotation().GetForwardVector(), ToSyncPoint) < FacingCos)
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
		for (int32 Idx = 0; Idx < DataContainer.Num(); Idx++)
		{
			const FContextualAnimData& Data = DataContainer[Idx];

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
		const FContextualAnimData& ResultData = DataContainer[DataIndex];

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

	return false;
}