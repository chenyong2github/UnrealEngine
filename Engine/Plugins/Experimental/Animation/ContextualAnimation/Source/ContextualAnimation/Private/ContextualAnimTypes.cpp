// Copyright Epic Games, Inc. All Rights Reserved.

#include "ContextualAnimTypes.h"
#include "AnimationUtils.h"
#include "Animation/AnimMontage.h"
#include "Containers/ArrayView.h"
#include "AnimNotifyState_MotionWarping.h"
#include "ContextualAnimUtilities.h"
#include "RootMotionModifier.h"

DEFINE_LOG_CATEGORY(LogContextualAnim);

const FContextualAnimData FContextualAnimData::EmptyAnimData;
const FContextualAnimIKTarget FContextualAnimIKTarget::InvalidIKTarget;

// FContextualAnimAlignmentTrackContainer
///////////////////////////////////////////////////////////////////////

FTransform FContextualAnimAlignmentTrackContainer::ExtractTransformAtTime(const FName& TrackName, float Time) const
{
	const int32 TrackIndex = Tracks.TrackNames.IndexOfByKey(TrackName);
	return ExtractTransformAtTime(TrackIndex, Time);
}

FTransform FContextualAnimAlignmentTrackContainer::ExtractTransformAtTime(int32 TrackIndex, float Time) const
{
	FTransform AlignmentTransform = FTransform::Identity;

	if (Tracks.AnimationTracks.IsValidIndex(TrackIndex))
	{
		const FRawAnimSequenceTrack& Track = Tracks.AnimationTracks[TrackIndex];
		const int32 TotalFrames = Track.PosKeys.Num();
		const float TrackLength = (TotalFrames - 1) * SampleInterval;
		FAnimationUtils::ExtractTransformFromTrack(Time, TotalFrames, TrackLength, Track, EAnimInterpolationType::Linear, AlignmentTransform);
	}

	return AlignmentTransform;
}

float FContextualAnimData::GetSyncTimeForWarpSection(int32 WarpSectionIndex) const
{
	//@TODO: We need a better way to identify warping sections withing the animation. This is just a temp solution
	//@TODO: We should cache this data

	float Result = 0.f;

	if(Animation && WarpSectionIndex >= 0)
	{
		FName LastWarpTargetName = NAME_None;
		int32 LastWarpSectionIndex = INDEX_NONE;

		for (int32 Idx = 0; Idx < Animation->Notifies.Num(); Idx++)
		{
			const FAnimNotifyEvent& NotifyEvent = Animation->Notifies[Idx];
			if(const UAnimNotifyState_MotionWarping* Notify = Cast<const UAnimNotifyState_MotionWarping>(NotifyEvent.NotifyStateClass))
			{
				if(const URootMotionModifier_Warp* Modifier = Cast<const URootMotionModifier_Warp>(Notify->RootMotionModifier))
				{
					const FName WarpTargetName = Modifier->WarpTargetName;
					if(WarpTargetName != NAME_None)
					{
						// First valid warping window. Initialize everything
						if (LastWarpSectionIndex == INDEX_NONE)
						{
							LastWarpTargetName = WarpTargetName;
							Result = NotifyEvent.GetEndTriggerTime();
							LastWarpSectionIndex = 0;
						}
						// If we hit another warping window but the sync point is the same as the previous, update SyncTime.
						// This is to deal with cases where a first short window is used to face the alignment point and a second one to perform the rest of the warp
						else if(WarpTargetName == LastWarpTargetName)
						{
							Result = NotifyEvent.GetEndTriggerTime();
						}
						// If we hit another warping window but with a different sync point name means that we have hit the first window of another warping section
						else
						{
							// If we haven't reached the desired WarpSection yet. Update control vars and keep moving
							if(WarpSectionIndex > LastWarpSectionIndex)
							{
								LastWarpTargetName = WarpTargetName;
								Result = NotifyEvent.GetEndTriggerTime();
								LastWarpSectionIndex++;
							}
							// Otherwise, stop here and return the value of the last window we found
							else 
							{
								break;
							}
						}
					}
				}
			}
		}
	}

	return Result;
}

float FContextualAnimData::GetSyncTimeForWarpSection(const FName& WarpSectionName) const
{
	//@TODO: We need a better way to identify warping sections within the animation. This is just a temp solution
	//@TODO: We should cache this data

	float Result = 0.f;

	if (Animation && WarpSectionName != NAME_None)
	{
		for (int32 Idx = 0; Idx < Animation->Notifies.Num(); Idx++)
		{
			const FAnimNotifyEvent& NotifyEvent = Animation->Notifies[Idx];
			if (const UAnimNotifyState_MotionWarping* Notify = Cast<const UAnimNotifyState_MotionWarping>(NotifyEvent.NotifyStateClass))
			{
				if (const URootMotionModifier_Warp* Config = Cast<const URootMotionModifier_Warp>(Notify->RootMotionModifier))
				{
					const FName WarpTargetName = Config->WarpTargetName;
					if (WarpSectionName == WarpTargetName)
					{
						const float NotifyEndTriggerTime = NotifyEvent.GetEndTriggerTime();
						if(NotifyEndTriggerTime > Result)
						{
							Result = NotifyEndTriggerTime;
						}
					}
				}
			}
		}
	}

	return Result;
}

float FContextualAnimData::FindBestAnimStartTime(const FVector& LocalLocation) const
{
	float BestTime = 0.f;

	if (AnimMaxStartTime < 0.f)
	{
		return BestTime;
	}

	const FVector SyncPointLocation = GetAlignmentTransformAtSyncTime().GetLocation();
	const float PerfectDistToSyncPointSq = GetAlignmentTransformAtEntryTime().GetTranslation().SizeSquared2D();
	const float ActualDistToSyncPointSq = FVector::DistSquared2D(LocalLocation, SyncPointLocation);

	if (ActualDistToSyncPointSq < PerfectDistToSyncPointSq)
	{
		float BestDistance = MAX_FLT;
		TArrayView<const FVector3f> PosKeys(AlignmentData.Tracks.AnimationTracks[0].PosKeys.GetData(), AlignmentData.Tracks.AnimationTracks[0].PosKeys.Num());

		//@TODO: Very simple search for now. Replace with Distance Matching + Pose Matching
		for (int32 Idx = 0; Idx < PosKeys.Num(); Idx++)
		{
			const float Time = Idx * AlignmentData.SampleInterval;
			if (AnimMaxStartTime > 0.f && Time >= AnimMaxStartTime)
			{
				break;
			}

			const float DistFromCurrentFrameToSyncPointSq = FVector::DistSquared2D(SyncPointLocation, (FVector)PosKeys[Idx]);
			if (DistFromCurrentFrameToSyncPointSq < ActualDistToSyncPointSq)
			{
				BestTime = Time;
				break;
			}
		}
	}

	return BestTime;
}

FTransform FContextualAnimCompositeTrack::GetRootTransformForAnimDataAtIndex(int32 Index) const
{
	if(AnimDataContainer.IsValidIndex(Index))
	{
		const FContextualAnimData& AnimData = AnimDataContainer[Index];
		if (AnimData.Animation)
		{
			return (Settings.MeshToComponent.Inverse() * (UContextualAnimUtilities::ExtractRootTransformFromAnimation(AnimData.Animation, 0.f) * AnimData.MeshToScene));
		}
		else
		{
			return Settings.MeshToComponent.Inverse() * AnimData.MeshToScene;
		}
	}

	return FTransform::Identity;
}