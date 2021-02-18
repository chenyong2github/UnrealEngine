// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	BlendSpace.cpp: 2D BlendSpace functionality
=============================================================================*/ 

#include "Animation/BlendSpace.h"
#include "Animation/AnimNotifyQueue.h"
#include "AnimationUtils.h"
#include "Animation/BlendSpaceUtilities.h"
#include "UObject/FrameworkObjectVersion.h"
#include "UObject/UObjectIterator.h"
#include "Logging/TokenizedMessage.h"
#include "Logging/MessageLog.h"
#include "Animation/AnimationPoseData.h"
#include "Animation/CustomAttributesRuntime.h"

#define LOCTEXT_NAMESPACE "BlendSpace"

DECLARE_CYCLE_STAT(TEXT("BlendSpace GetAnimPose"), STAT_BlendSpace_GetAnimPose, STATGROUP_Anim);

/** Scratch buffers for multi-threaded usage */
struct FBlendSpaceScratchData : public TThreadSingleton<FBlendSpaceScratchData>
{
	TArray<FBlendSampleData> OldSampleDataList;
	TArray<FBlendSampleData> NewSampleDataList;
	TArray<FGridBlendSample, TInlineAllocator<4> > RawGridSamples;
};

UBlendSpace::UBlendSpace(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SampleIndexWithMarkers = INDEX_NONE;

	/** Use highest weighted animation as default */
	NotifyTriggerMode = ENotifyTriggerMode::HighestWeightedAnimation;

	if (DimensionIndices.Num() == 0)
	{
		DimensionIndices = { 0, 1 };
	}
}

void UBlendSpace::PostLoad()
{
	Super::PostLoad();

#if WITH_EDITOR	
	// Only do this during editor time (could alter the blendspace data during runtime otherwise) 
	ValidateSampleData();
#endif // WITH_EDITOR

	InitializePerBoneBlend();
}

void UBlendSpace::Serialize(FArchive& Ar)
{
	Ar.UsingCustomVersion(FFrameworkObjectVersion::GUID);
	Super::Serialize(Ar);

#if WITH_EDITOR
	if (Ar.IsLoading() && (Ar.CustomVer(FFrameworkObjectVersion::GUID) < FFrameworkObjectVersion::BlendSpacePostLoadSnapToGrid))
	{
		// This will ensure that all grid points are in valid position and the bIsSnapped flag is set
		SnapSamplesToClosestGridPoint();
	}

	if (Ar.IsLoading() && (Ar.CustomVer(FFrameworkObjectVersion::GUID) < FFrameworkObjectVersion::SupportBlendSpaceRateScale))
	{
		for (FBlendSample& Sample : SampleData)
		{
			Sample.RateScale = 1.0f;
		}
	}
#endif // WITH_EDITOR
}

#if WITH_EDITOR
void UBlendSpace::PreEditChange(FProperty* PropertyAboutToChange)
{
	Super::PreEditChange(PropertyAboutToChange);

	// Cache the axis ranges if it is going to change, this so the samples can be remapped correctly
	const FName PropertyName = PropertyAboutToChange ? PropertyAboutToChange->GetFName() : NAME_None;
	if ((PropertyName == GET_MEMBER_NAME_CHECKED(FBlendParameter, Min) || PropertyName == GET_MEMBER_NAME_CHECKED(FBlendParameter, Max)))
	{
		for (int32 AxisIndex = 0; AxisIndex < 3; ++AxisIndex)
		{
			PreviousAxisMinMaxValues[AxisIndex].X = BlendParameters[AxisIndex].Min;
			PreviousAxisMinMaxValues[AxisIndex].Y = BlendParameters[AxisIndex].Max;
		}
	}
}

void UBlendSpace::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	const FName MemberPropertyName = PropertyChangedEvent.MemberProperty ? PropertyChangedEvent.MemberProperty->GetFName() : NAME_None;
	const FName PropertyName = PropertyChangedEvent.Property ? PropertyChangedEvent.Property->GetFName() : NAME_None;

	if ((MemberPropertyName == GET_MEMBER_NAME_CHECKED(UBlendSpace, PerBoneBlend) && PropertyName == GET_MEMBER_NAME_CHECKED(FBoneReference, BoneName)) || PropertyName == GET_MEMBER_NAME_CHECKED(UBlendSpace, PerBoneBlend))
	{
		InitializePerBoneBlend();
	}

	if (MemberPropertyName == GET_MEMBER_NAME_CHECKED(UBlendSpace, BlendParameters))
	{
		if (PropertyName == GET_MEMBER_NAME_CHECKED(FBlendParameter, GridNum))
		{
			// Tried and snap samples to points on the grid, those who do not fit or cannot be snapped are marked as invalid
			SnapSamplesToClosestGridPoint();
		}
		else if (PropertyName == GET_MEMBER_NAME_CHECKED(FBlendParameter, Min) || PropertyName == GET_MEMBER_NAME_CHECKED(FBlendParameter, Max))
		{
			// Remap the samples to the new values by normalizing the axis and applying the new value range		
			RemapSamplesToNewAxisRange();
		}
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif // WITH_EDITOR


bool UBlendSpace::UpdateBlendSamples_Internal(const FVector& InBlendSpacePosition, float InDeltaTime, TArray<FBlendSampleData>& InOutOldSampleDataList, TArray<FBlendSampleData>& InOutSampleDataCache) const
{
	// For Target weight interpolation, we'll need to save old data, and interpolate to new data
	TArray<FBlendSampleData>& NewSampleDataList = FBlendSpaceScratchData::Get().NewSampleDataList;
	check(!NewSampleDataList.Num()); // this must be called non-recursively

	InOutOldSampleDataList.Append(InOutSampleDataCache);

	// @fixme: temporary code to clear the invalid sample data 
	// related jira: UE-71107
	for (int32 Index = 0; Index < InOutOldSampleDataList.Num(); ++Index)
	{
		if (!SampleData.IsValidIndex(InOutOldSampleDataList[Index].SampleDataIndex))
		{
			InOutOldSampleDataList.RemoveAt(Index);
			--Index;
		}
	}

	// get sample data based on new input
	// consolidate all samples and sort them, so that we can handle from biggest weight to smallest
	InOutSampleDataCache.Reset();

	// get sample data from blendspace
	bool bSuccessfullySampled = false;
	if (GetSamplesFromBlendInput(InBlendSpacePosition, NewSampleDataList))
	{
		// if target weight interpolation is set
		if (TargetWeightInterpolationSpeedPerSec > 0.f || PerBoneBlend.Num() > 0)
		{
			// target weight interpolation
			if (InterpolateWeightOfSampleData(InDeltaTime, InOutOldSampleDataList, NewSampleDataList, InOutSampleDataCache))
			{
				// now I need to normalize
				FBlendSampleData::NormalizeDataWeight(InOutSampleDataCache);
			}
			else
			{
				// if interpolation failed, just copy new sample data to sample data
				InOutSampleDataCache = NewSampleDataList;
			}
		}
		else
		{
			// when there is no target weight interpolation, just copy new to target
			InOutSampleDataCache.Append(NewSampleDataList);
		}

		bSuccessfullySampled = true;
	}

	NewSampleDataList.Reset();

	return bSuccessfullySampled;
}

bool UBlendSpace::UpdateBlendSamples(const FVector& InBlendSpacePosition, float InDeltaTime, TArray<FBlendSampleData>& InOutSampleDataCache) const
{
	TArray<FBlendSampleData>& OldSampleDataList = FBlendSpaceScratchData::Get().OldSampleDataList;
	check(!OldSampleDataList.Num()); // this must be called non-recursively
	const bool bResult = UpdateBlendSamples_Internal(InBlendSpacePosition, InDeltaTime, OldSampleDataList, InOutSampleDataCache);
	OldSampleDataList.Reset();
	return bResult;
}

void UBlendSpace::TickAssetPlayer(FAnimTickRecord& Instance, struct FAnimNotifyQueue& NotifyQueue, FAnimAssetTickContext& Context) const
{
	check(Instance.BlendSpace.BlendSampleDataCache);

	// Scratch area for old samples
	TArray<FBlendSampleData>& OldSampleDataList = FBlendSpaceScratchData::Get().OldSampleDataList;
	check(!OldSampleDataList.Num()); // this must be called non-recursively
	// new sample data that will be used for evaluation
	TArray<FBlendSampleData>& SampleDataList = *Instance.BlendSpace.BlendSampleDataCache;

	const float DeltaTime = Context.GetDeltaTime();
	float MoveDelta = Instance.PlayRateMultiplier * DeltaTime;

	// this happens even if MoveDelta == 0.f. This still should happen if it is being interpolated
	// since we allow setting position of blendspace, we can't ignore MoveDelta == 0.f
	// also now we don't have to worry about not following if DeltaTime = 0.f
	{
		// first filter input using blend filter
		const FVector BlendSpacePosition(Instance.BlendSpace.BlendSpacePositionX, Instance.BlendSpace.BlendSpacePositionY, 0.f);
		const FVector FilteredBlendInput = FilterInput(Instance.BlendSpace.BlendFilter, BlendSpacePosition, DeltaTime);

		if (UpdateBlendSamples_Internal(FilteredBlendInput, DeltaTime, OldSampleDataList, SampleDataList))
		{
			float NewAnimLength = 0.f;
			float PreInterpAnimLength = 0.f;

			if (TargetWeightInterpolationSpeedPerSec > 0.f)
			{
				// recalculate AnimLength based on weight of target animations - this is used for scaling animation later (change speed)
				PreInterpAnimLength = GetAnimationLengthFromSampleData(*Instance.BlendSpace.BlendSampleDataCache);
				UE_LOG(LogAnimation, Verbose, TEXT("BlendSpace(%s) - FilteredBlendInput(%s) : PreAnimLength(%0.5f) "), *GetName(), *FilteredBlendInput.ToString(), PreInterpAnimLength);
			}

			EBlendSpaceAxis AxisToScale = GetAxisToScale();
			if (AxisToScale != BSA_None)
			{
				float FilterMultiplier = 1.f;
				// first use multiplier using new blendinput
				// new filtered input is going to be used for sampling animation
				// so we'll need to change playrate if you'd like to not slide foot
				if (!BlendSpacePosition.Equals(FilteredBlendInput))
				{
					// apply speed change if you want, 
					if (AxisToScale == BSA_X)
					{
						if (FilteredBlendInput.X != 0.f)
						{
							FilterMultiplier = BlendSpacePosition.X / FilteredBlendInput.X;
						}
					}
					else if (AxisToScale == BSA_Y)
					{
						if (FilteredBlendInput.Y != 0.f)
						{
							FilterMultiplier = BlendSpacePosition.Y / FilteredBlendInput.Y;
						}
					}
				}

				// Now find if clamped input is different. If different, then apply scale to fit in. This allows
				// "extrapolation" of the blend space outside of the range by time scaling the animation, which is
				// appropriate when the specified axis is speed (for example).
				FVector ClampedInput = GetClampedBlendInput(FilteredBlendInput);
				if (!ClampedInput.Equals(FilteredBlendInput))
				{
					// apply speed change if you want, 
					if (AxisToScale == BSA_X && !BlendParameters[0].bWrapInput)
					{
						if (ClampedInput.X != 0.f)
						{
							FilterMultiplier *= FilteredBlendInput.X / ClampedInput.X;
						}
					}
					else if (AxisToScale == BSA_Y)
					{
						if (ClampedInput.Y != 0.f && !BlendParameters[1].bWrapInput)
						{
							FilterMultiplier *= FilteredBlendInput.Y / ClampedInput.Y;
						}
					}
				}

				MoveDelta *= FilterMultiplier;
				UE_LOG(LogAnimation, Log, TEXT("BlendSpace(%s) - FilteredBlendInput(%s) : FilteredBlendInput(%s), FilterMultiplier(%0.2f)"), *GetName(), *BlendSpacePosition.ToString(), *FilteredBlendInput.ToString(), FilterMultiplier);
			}

			bool bCanDoMarkerSync = (SampleIndexWithMarkers != INDEX_NONE) && (Context.IsSingleAnimationContext() || (Instance.bCanUseMarkerSync && Context.CanUseMarkerPosition()));

			if (bCanDoMarkerSync)
			{
				//Copy previous frame marker data to current frame
				for (const FBlendSampleData& PrevBlendSampleItem : OldSampleDataList)
				{
					for (FBlendSampleData& CurrentBlendSampleItem : SampleDataList)
					{
						// it only can have one animation in the sample, make sure to copy Time
						if (PrevBlendSampleItem.Animation && PrevBlendSampleItem.Animation == CurrentBlendSampleItem.Animation)
						{
							CurrentBlendSampleItem.Time = PrevBlendSampleItem.Time;
							CurrentBlendSampleItem.PreviousTime = PrevBlendSampleItem.PreviousTime;
							CurrentBlendSampleItem.MarkerTickRecord = PrevBlendSampleItem.MarkerTickRecord;
						}
					}
				}
			}

			NewAnimLength = GetAnimationLengthFromSampleData(SampleDataList);

			if (PreInterpAnimLength > 0.f && NewAnimLength > 0.f)
			{
				MoveDelta *= PreInterpAnimLength / NewAnimLength;
			}

			float& NormalizedCurrentTime = *(Instance.TimeAccumulator);
			float NormalizedPreviousTime = NormalizedCurrentTime;

			// @note for sync group vs non sync group
			// in blendspace, it will still sync even if only one node in sync group
			// so you're never non-sync group unless you have situation where some markers are relevant to one sync group but not all the time
			// here we save NormalizedCurrentTime as Highest weighted samples' position in sync group
			// if you're not in sync group, NormalizedCurrentTime is based on normalized length by sample weights
			// if you move between sync to non sync within blendspace, you're going to see pop because we'll have to jump
			// for now, our rule is to keep normalized time as highest weighted sample position within its own length
			// also MoveDelta doesn't work if you're in sync group. It will move according to sync group position
			// @todo consider using MoveDelta when  this is leader, but that can be scary because it's not matching with DeltaTime any more. 
			// if you have interpolation delay, that value can be applied, but the output might be unpredictable. 
			// 
			// to fix this better in the future, we should use marker sync position from last tick
			// but that still doesn't fix if you just join sync group, you're going to see pop since your animation doesn't fix

			if (Context.IsLeader())
			{
				// advance current time - blend spaces hold normalized time as when dealing with changing anim length it would be possible to go backwards
				UE_LOG(LogAnimation, Verbose, TEXT("BlendSpace(%s) - FilteredBlendInput(%s) : AnimLength(%0.5f) "), *GetName(), *FilteredBlendInput.ToString(), NewAnimLength);

				Context.SetPreviousAnimationPositionRatio(NormalizedCurrentTime);

				const int32 HighestMarkerSyncWeightIndex = bCanDoMarkerSync ? FBlendSpaceUtilities::GetHighestWeightMarkerSyncSample(SampleDataList, SampleData) : -1;

				if (HighestMarkerSyncWeightIndex == -1)
				{
					bCanDoMarkerSync = false;
				}

				if (bCanDoMarkerSync)
				{
					FBlendSampleData& SampleDataItem = SampleDataList[HighestMarkerSyncWeightIndex];
					const FBlendSample& Sample = SampleData[SampleDataItem.SampleDataIndex];

					bool bResetMarkerDataOnFollowers = false;
					if (!Instance.MarkerTickRecord->IsValid(Instance.bLooping))
					{
						SampleDataItem.MarkerTickRecord.Reset();
						bResetMarkerDataOnFollowers = true;
						SampleDataItem.Time = NormalizedCurrentTime * Sample.Animation->GetPlayLength();
					}
					else if (!SampleDataItem.MarkerTickRecord.IsValid(Instance.bLooping) && Context.MarkerTickContext.GetMarkerSyncStartPosition().IsValid())
					{
						Sample.Animation->GetMarkerIndicesForPosition(Context.MarkerTickContext.GetMarkerSyncStartPosition(), true, SampleDataItem.MarkerTickRecord.PreviousMarker, SampleDataItem.MarkerTickRecord.NextMarker, SampleDataItem.Time);
					}

					const float NewDeltaTime = Context.GetDeltaTime() * Instance.PlayRateMultiplier * Sample.RateScale * Sample.Animation->RateScale;
					if (!FMath::IsNearlyZero(NewDeltaTime))
					{
						Context.SetLeaderDelta(NewDeltaTime);
						Sample.Animation->TickByMarkerAsLeader(SampleDataItem.MarkerTickRecord, Context.MarkerTickContext, SampleDataItem.Time, SampleDataItem.PreviousTime, NewDeltaTime, Instance.bLooping);
						check(!Instance.bLooping || Context.MarkerTickContext.IsMarkerSyncStartValid());
						TickFollowerSamples(SampleDataList, HighestMarkerSyncWeightIndex, Context, bResetMarkerDataOnFollowers);
					}
					NormalizedCurrentTime = SampleDataItem.Time / Sample.Animation->GetPlayLength();
					*Instance.MarkerTickRecord = SampleDataItem.MarkerTickRecord;
				}
				else
				{
					// Advance time using current/new anim length
					float CurrentTime = NormalizedCurrentTime * NewAnimLength;
					FAnimationRuntime::AdvanceTime(Instance.bLooping, MoveDelta, /*inout*/ CurrentTime, NewAnimLength);
					NormalizedCurrentTime = NewAnimLength ? (CurrentTime / NewAnimLength) : 0.0f;
					UE_LOG(LogAnimMarkerSync, Log, TEXT("Leader (%s) (bCanDoMarkerSync == false)  - PreviousTime (%0.2f), CurrentTime (%0.2f), MoveDelta (%0.2f) "), *GetName(), NormalizedPreviousTime, NormalizedCurrentTime, MoveDelta);
				}

				Context.SetAnimationPositionRatio(NormalizedCurrentTime);
			}
			else
			{
				if (!Context.MarkerTickContext.IsMarkerSyncStartValid())
				{
					bCanDoMarkerSync = false;
				}

				if (bCanDoMarkerSync)
				{
					const int32 HighestWeightIndex = FBlendSpaceUtilities::GetHighestWeightSample(SampleDataList);
					FBlendSampleData& SampleDataItem = SampleDataList[HighestWeightIndex];
					const FBlendSample& Sample = SampleData[SampleDataItem.SampleDataIndex];

					if (Context.GetDeltaTime() != 0.f)
					{
						if (!Instance.MarkerTickRecord->IsValid(Instance.bLooping))
						{
							SampleDataItem.Time = NormalizedCurrentTime * Sample.Animation->GetPlayLength();
						}

						TickFollowerSamples(SampleDataList, -1, Context, false);
					}
					*Instance.MarkerTickRecord = SampleDataItem.MarkerTickRecord;
					NormalizedCurrentTime = SampleDataItem.Time / Sample.Animation->GetPlayLength();
				}
				else
				{
					NormalizedPreviousTime = Context.GetPreviousAnimationPositionRatio();
					NormalizedCurrentTime = Context.GetAnimationPositionRatio();
					UE_LOG(LogAnimMarkerSync, Log, TEXT("Follower (%s) (bCanDoMarkerSync == false) - PreviousTime (%0.2f), CurrentTime (%0.2f), MoveDelta (%0.2f) "), *GetName(), NormalizedPreviousTime, NormalizedCurrentTime, MoveDelta);
				}
			}

			// generate notifies and sets time
			{
				TArray<FAnimNotifyEventReference> Notifies;

				const float ClampedNormalizedPreviousTime = FMath::Clamp<float>(NormalizedPreviousTime, 0.f, 1.f);
				const float ClampedNormalizedCurrentTime = FMath::Clamp<float>(NormalizedCurrentTime, 0.f, 1.f);
				const bool bGenerateNotifies = (NormalizedCurrentTime != NormalizedPreviousTime) && NotifyTriggerMode != ENotifyTriggerMode::None;

				// Get the index of the highest weight, assuming that the first is the highest until we find otherwise
				const bool bTriggerNotifyHighestWeightedAnim = NotifyTriggerMode == ENotifyTriggerMode::HighestWeightedAnimation && SampleDataList.Num() > 0;
				const int32 HighestWeightIndex = (bGenerateNotifies && bTriggerNotifyHighestWeightedAnim) ? FBlendSpaceUtilities::GetHighestWeightSample(SampleDataList) : -1;

				for (int32 I = 0; I < SampleDataList.Num(); ++I)
				{
					FBlendSampleData& SampleEntry = SampleDataList[I];
					const int32 SampleDataIndex = SampleEntry.SampleDataIndex;

					// Skip SamplesPoints that has no relevant weight
					if (SampleData.IsValidIndex(SampleDataIndex) && (SampleEntry.TotalWeight > ZERO_ANIMWEIGHT_THRESH))
					{
						const FBlendSample& Sample = SampleData[SampleDataIndex];
						if (Sample.Animation)
						{
							float PrevSampleDataTime;
							float& CurrentSampleDataTime = SampleEntry.Time;

							const float MultipliedSampleRateScale = Sample.Animation->RateScale * Sample.RateScale;

							if (!bCanDoMarkerSync || Sample.Animation->AuthoredSyncMarkers.Num() == 0) //Have already updated time if we are doing marker sync
							{
								const float SampleNormalizedPreviousTime = MultipliedSampleRateScale >= 0.f ? ClampedNormalizedPreviousTime : 1.f - ClampedNormalizedPreviousTime;
								const float SampleNormalizedCurrentTime = MultipliedSampleRateScale >= 0.f ? ClampedNormalizedCurrentTime : 1.f - ClampedNormalizedCurrentTime;
								PrevSampleDataTime = SampleNormalizedPreviousTime * Sample.Animation->GetPlayLength();
								CurrentSampleDataTime = SampleNormalizedCurrentTime * Sample.Animation->GetPlayLength();
							}
							else
							{
								PrevSampleDataTime = SampleEntry.PreviousTime;
							}

							// Figure out delta time 
							float DeltaTimePosition = CurrentSampleDataTime - PrevSampleDataTime;
							const float SampleMoveDelta = MoveDelta * MultipliedSampleRateScale;

							// if we went against play rate, then loop around.
							if ((SampleMoveDelta * DeltaTimePosition) < 0.f)
							{
								DeltaTimePosition += FMath::Sign<float>(SampleMoveDelta) * Sample.Animation->GetPlayLength();
							}

							if (bGenerateNotifies && (!bTriggerNotifyHighestWeightedAnim || (I == HighestWeightIndex)))
							{
								// Harvest and record notifies
								Sample.Animation->GetAnimNotifies(PrevSampleDataTime, DeltaTimePosition, Instance.bLooping, Notifies);
							}

							if (Context.RootMotionMode == ERootMotionMode::RootMotionFromEverything && Sample.Animation->bEnableRootMotion)
							{
								Context.RootMotionMovementParams.AccumulateWithBlend(Sample.Animation->ExtractRootMotion(PrevSampleDataTime, DeltaTimePosition, Instance.bLooping), SampleEntry.GetWeight());
							}


							UE_LOG(LogAnimation, Verbose, TEXT("%d. Blending animation(%s) with %f weight at time %0.2f"), I + 1, *Sample.Animation->GetName(), SampleEntry.GetWeight(), CurrentSampleDataTime);
						}
					}
				}

				if (bGenerateNotifies && Notifies.Num() > 0)
				{
					NotifyQueue.AddAnimNotifies(Context.ShouldGenerateNotifies(), Notifies, Instance.EffectiveBlendWeight);
				}
			}
		}

		OldSampleDataList.Reset();
	}
}

bool UBlendSpace::IsValidAdditive() const
{
	return ContainsMatchingSamples(AAT_LocalSpaceBase) || ContainsMatchingSamples(AAT_RotationOffsetMeshSpace);
}

#if WITH_EDITOR
bool UBlendSpace::GetAllAnimationSequencesReferred(TArray<UAnimationAsset*>& AnimationAssets, bool bRecursive /*= true*/)
{
	Super::GetAllAnimationSequencesReferred(AnimationAssets, bRecursive);

	for (auto Iter = SampleData.CreateConstIterator(); Iter; ++Iter)
	{
		// saves all samples in the AnimSequences
		UAnimSequence* Sequence = (*Iter).Animation;
		if (Sequence)
		{
			Sequence->HandleAnimReferenceCollection(AnimationAssets, bRecursive);
		}
	}

	if (PreviewBasePose)
	{
		PreviewBasePose->HandleAnimReferenceCollection(AnimationAssets, bRecursive);
	}

	return (AnimationAssets.Num() > 0);
}

void UBlendSpace::ReplaceReferredAnimations(const TMap<UAnimationAsset*, UAnimationAsset*>& ReplacementMap)
{
	Super::ReplaceReferredAnimations(ReplacementMap);

	TArray<FBlendSample> NewSamples;
	for (auto Iter = SampleData.CreateIterator(); Iter; ++Iter)
	{
		FBlendSample& Sample = (*Iter);
		UAnimSequence* Anim = Sample.Animation;

		if (Anim)
		{
			UAnimSequence* const* ReplacementAsset = (UAnimSequence* const*)ReplacementMap.Find(Anim);
			if (ReplacementAsset)
			{
				Sample.Animation = *ReplacementAsset;
				Sample.Animation->ReplaceReferredAnimations(ReplacementMap);
				NewSamples.Add(Sample);
			}
		}
	}

	if (PreviewBasePose)
	{
		UAnimSequence* const* ReplacementAsset = (UAnimSequence* const*)ReplacementMap.Find(PreviewBasePose);
		if (ReplacementAsset)
		{
			PreviewBasePose = *ReplacementAsset;
			PreviewBasePose->ReplaceReferredAnimations(ReplacementMap);
		}
	}

	SampleData = NewSamples;
}

int32 UBlendSpace::GetMarkerUpdateCounter() const
{
	return MarkerDataUpdateCounter;
}

void UBlendSpace::RuntimeValidateMarkerData()
{
	check(IsInGameThread());

	for (FBlendSample& Sample : SampleData)
	{
		if (Sample.Animation && Sample.CachedMarkerDataUpdateCounter != Sample.Animation->GetMarkerUpdateCounter())
		{
			// Revalidate data
			ValidateSampleData();
			return;
		}
	}
}

#endif // WITH_EDITOR

// @todo fixme: slow approach. If the perbone gets popular, we should change this to array of weight 
// we should think about changing this to 
int32 UBlendSpace::GetPerBoneInterpolationIndex(int32 BoneIndex, const FBoneContainer& RequiredBones) const
{
	for (int32 Iter = 0; Iter < PerBoneBlend.Num(); ++Iter)
	{
		// we would like to make sure if 
		if (PerBoneBlend[Iter].BoneReference.IsValidToEvaluate(RequiredBones) && RequiredBones.BoneIsChildOf(BoneIndex, RequiredBones.GetCompactPoseIndexFromSkeletonIndex(PerBoneBlend[Iter].BoneReference.BoneIndex).GetInt()))
		{
			return Iter;
		}
	}

	return INDEX_NONE;
}

bool UBlendSpace::IsValidAdditiveType(EAdditiveAnimationType AdditiveType) const
{
	return (AdditiveType == AAT_LocalSpaceBase || AdditiveType == AAT_RotationOffsetMeshSpace || AdditiveType == AAT_None);
}

void UBlendSpace::ResetToRefPose(FCompactPose& OutPose) const
{
	if (IsValidAdditive())
	{
		OutPose.ResetToAdditiveIdentity();
	}
	else
	{
		OutPose.ResetToRefPose();
	}
}

void UBlendSpace::GetAnimationPose(TArray<FBlendSampleData>& BlendSampleDataCache, /*out*/ FCompactPose& OutPose, /*out*/ FBlendedCurve& OutCurve) const
{
	FStackCustomAttributes TempAttributes;
	FAnimationPoseData AnimationPoseData = { OutPose, OutCurve, TempAttributes };
	GetAnimationPose(BlendSampleDataCache, AnimationPoseData);
}

void UBlendSpace::GetAnimationPose(TArray<FBlendSampleData>& BlendSampleDataCache, /*out*/ FAnimationPoseData& OutAnimationPoseData) const
{
	GetAnimationPose_Internal(BlendSampleDataCache, TArrayView<FPoseLink>(), nullptr, false, OutAnimationPoseData);
}

void UBlendSpace::GetAnimationPose(TArray<FBlendSampleData>& BlendSampleDataCache, TArrayView<FPoseLink> InPoseLinks, /*out*/ FPoseContext& Output) const
{
	FAnimationPoseData AnimationPoseData(Output);
	GetAnimationPose_Internal(BlendSampleDataCache, InPoseLinks, Output.AnimInstanceProxy, Output.ExpectsAdditivePose(), AnimationPoseData);
}

void UBlendSpace::GetAnimationPose_Internal(TArray<FBlendSampleData>& BlendSampleDataCache, TArrayView<FPoseLink> InPoseLinks, FAnimInstanceProxy* InProxy, bool bInExpectsAdditivePose, /*out*/ FAnimationPoseData& OutAnimationPoseData) const
{
	SCOPE_CYCLE_COUNTER(STAT_BlendSpace_GetAnimPose);
	FScopeCycleCounterUObject BlendSpaceScope(this);

	FCompactPose& OutPose = OutAnimationPoseData.GetPose();
	FBlendedCurve& OutCurve = OutAnimationPoseData.GetCurve();

	if (BlendSampleDataCache.Num() == 0)
	{
		ResetToRefPose(OutPose);
		return;
	}

	const bool bNested = InPoseLinks.Num() > 0;
	const int32 NumPoses = BlendSampleDataCache.Num();

	TArray<FCompactPose, TInlineAllocator<8>> ChildrenPoses;
	ChildrenPoses.AddZeroed(NumPoses);

	TArray<FBlendedCurve, TInlineAllocator<8>> ChildrenCurves;
	ChildrenCurves.AddZeroed(NumPoses);

	TArray<FStackCustomAttributes, TInlineAllocator<8>> ChildrenAttributes;
	ChildrenAttributes.AddZeroed(NumPoses);

	TArray<float, TInlineAllocator<8>> ChildrenWeights;
	ChildrenWeights.AddZeroed(NumPoses);

	for (int32 ChildrenIdx = 0; ChildrenIdx < ChildrenPoses.Num(); ++ChildrenIdx)
	{
		ChildrenPoses[ChildrenIdx].SetBoneContainer(&OutPose.GetBoneContainer());
		ChildrenCurves[ChildrenIdx].InitFrom(OutCurve);
	}

	// get all child atoms we interested in
	for (int32 I = 0; I < NumPoses; ++I)
	{
		FCompactPose& Pose = ChildrenPoses[I];

		if (SampleData.IsValidIndex(BlendSampleDataCache[I].SampleDataIndex))
		{
			const FBlendSample& Sample = SampleData[BlendSampleDataCache[I].SampleDataIndex];
			ChildrenWeights[I] = BlendSampleDataCache[I].GetWeight();

			if (bNested)
			{
				check(InPoseLinks.IsValidIndex(BlendSampleDataCache[I].SampleDataIndex));

				// Evaluate the linked graphs
				FPoseContext ChildPoseContext(InProxy, bInExpectsAdditivePose);
				InPoseLinks[BlendSampleDataCache[I].SampleDataIndex].Evaluate(ChildPoseContext);

				// Move out poses etc. for blending
				ChildrenPoses[I] = MoveTemp(ChildPoseContext.Pose);
				ChildrenCurves[I] = MoveTemp(ChildPoseContext.Curve);
				ChildrenAttributes[I] = MoveTemp(ChildPoseContext.CustomAttributes);
			}
			else
			{
				if (Sample.Animation
#if WITH_EDITOR
					// verify if Sample.Animation->GetSkeleton matches
					&& ensure(GetSkeleton()->IsCompatible(Sample.Animation->GetSkeleton()))
#endif // WITH_EDITOR
					)
				{
					const float Time = FMath::Clamp<float>(BlendSampleDataCache[I].Time, 0.f, Sample.Animation->GetPlayLength());

					FAnimationPoseData ChildAnimationPoseData = { Pose, ChildrenCurves[I], ChildrenAttributes[I] };
					// first one always fills up the source one
					Sample.Animation->GetAnimationPose(ChildAnimationPoseData, FAnimExtractContext(Time, true));
				}
				else
				{
					ResetToRefPose(Pose);
				}
			}
		}
		else
		{
			ResetToRefPose(Pose);
		}
	}

	TArrayView<FCompactPose> ChildrenPosesView(ChildrenPoses);

	if (PerBoneBlend.Num() > 0)
	{
		if (IsValidAdditive())
		{
			if (bRotationBlendInMeshSpace)
			{
				FAnimationRuntime::BlendPosesTogetherPerBoneInMeshSpace(ChildrenPosesView, ChildrenCurves, ChildrenAttributes, this, BlendSampleDataCache, OutAnimationPoseData);
			}
			else
			{
				FAnimationRuntime::BlendPosesTogetherPerBone(ChildrenPosesView, ChildrenCurves, ChildrenAttributes, this, BlendSampleDataCache, OutAnimationPoseData);
			}
		}
		else
		{
			FAnimationRuntime::BlendPosesTogetherPerBone(ChildrenPosesView, ChildrenCurves, ChildrenAttributes, this, BlendSampleDataCache, OutAnimationPoseData);
		}
	}
	else
	{
		FAnimationRuntime::BlendPosesTogether(ChildrenPosesView, ChildrenCurves, ChildrenAttributes, ChildrenWeights, OutAnimationPoseData);
	}

	// Once all the accumulation and blending has been done, normalize rotations.
	OutPose.NormalizeRotations();
}

const FBlendParameter& UBlendSpace::GetBlendParameter(const int32 Index) const
{
	checkf(Index >= 0 && Index < 3, TEXT("Invalid Blend Parameter Index"));
	return BlendParameters[Index];
}

const struct FBlendSample& UBlendSpace::GetBlendSample(const int32 SampleIndex) const
{
#if WITH_EDITOR
	checkf(IsValidBlendSampleIndex(SampleIndex), TEXT("Invalid blend sample index"));
#endif // WITH_EDITOR
	return SampleData[SampleIndex];
}

bool UBlendSpace::GetSamplesFromBlendInput(const FVector& BlendInput, TArray<FBlendSampleData>& OutSampleDataList) const
{
	TArray<FGridBlendSample, TInlineAllocator<4> >& RawGridSamples = FBlendSpaceScratchData::Get().RawGridSamples;
	check(!RawGridSamples.Num()); // this must be called non-recursively
	GetRawSamplesFromBlendInput(BlendInput, RawGridSamples);

	OutSampleDataList.Reset();
	OutSampleDataList.Reserve(RawGridSamples.Num() * FEditorElement::MAX_VERTICES);

	// Consolidate all samples
	for (int32 SampleNum = 0; SampleNum < RawGridSamples.Num(); ++SampleNum)
	{
		FGridBlendSample& GridSample = RawGridSamples[SampleNum];
		float GridWeight = GridSample.BlendWeight;
		FEditorElement& GridElement = GridSample.GridElement;

		for (int32 Ind = 0; Ind < GridElement.MAX_VERTICES; ++Ind)
		{
			const int32 SampleDataIndex = GridElement.Indices[Ind];
			if (SampleData.IsValidIndex(SampleDataIndex))
			{
				int32 Index = OutSampleDataList.AddUnique(SampleDataIndex);
				FBlendSampleData& NewSampleData = OutSampleDataList[Index];

				NewSampleData.AddWeight(GridElement.Weights[Ind] * GridWeight);
				NewSampleData.Animation = SampleData[SampleDataIndex].Animation;
				NewSampleData.SamplePlayRate = SampleData[SampleDataIndex].RateScale;
			}
		}
	}

	// At this point we'll only have one of each sample, but different samples can point to the same
	// animation. We can combine those, making sure to interpolate the parameters like play rate too.
	for (int32 Index1 = 0; Index1 < OutSampleDataList.Num(); ++Index1)
	{
		FBlendSampleData& FirstSample = OutSampleDataList[Index1];
		for (int32 Index2 = Index1 + 1; Index2 < OutSampleDataList.Num(); ++Index2)
		{
			FBlendSampleData& SecondSample = OutSampleDataList[Index2];
			// if they have same sample, remove the Index2, and get out
			if (FirstSample.SampleDataIndex == SecondSample.SampleDataIndex || // Shouldn't happen
				(FirstSample.Animation != nullptr && FirstSample.Animation == SecondSample.Animation))
			{
				//Calc New Sample Playrate
				const float TotalWeight = FirstSample.GetWeight() + SecondSample.GetWeight();

				// Only combine playrates if total weight > 0
				if (!FMath::IsNearlyZero(TotalWeight))
				{
					const float OriginalWeightedPlayRate = FirstSample.SamplePlayRate * (FirstSample.GetWeight() / TotalWeight);
					const float SecondSampleWeightedPlayRate = SecondSample.SamplePlayRate * (SecondSample.GetWeight() / TotalWeight);
					FirstSample.SamplePlayRate = OriginalWeightedPlayRate + SecondSampleWeightedPlayRate;

					// add weight
					FirstSample.AddWeight(SecondSample.GetWeight());
				}

				// as for time or previous time will be the master one(Index1)
				OutSampleDataList.RemoveAtSwap(Index2, 1, false);
				--Index2;
			}
		}
	}

	OutSampleDataList.Sort([](const FBlendSampleData& A, const FBlendSampleData& B) { return B.TotalWeight < A.TotalWeight; });

	// remove noisy ones
	int32 TotalSample = OutSampleDataList.Num();
	float TotalWeight = 0.f;
	for (int32 I = 0; I < TotalSample; ++I)
	{
		if (OutSampleDataList[I].TotalWeight < ZERO_ANIMWEIGHT_THRESH)
		{
			// cut anything in front of this 
			OutSampleDataList.RemoveAt(I, TotalSample - I, false); // we won't shrink here, that might screw up alloc optimization at a higher level, if not this is temp anyway
			break;
		}

		TotalWeight += OutSampleDataList[I].TotalWeight;
	}

	for (int32 I = 0; I < OutSampleDataList.Num(); ++I)
	{
		// normalize to all weights
		OutSampleDataList[I].TotalWeight /= TotalWeight;
	}
	RawGridSamples.Reset();
	return (OutSampleDataList.Num() != 0);
}

void UBlendSpace::InitializeFilter(FBlendFilter* Filter) const
{
	if (Filter)
	{
		Filter->FilterPerAxis[0].Initialize(InterpolationParam[0].InterpolationTime, InterpolationParam[0].InterpolationType);
		Filter->FilterPerAxis[1].Initialize(InterpolationParam[1].InterpolationTime, InterpolationParam[1].InterpolationType);
		Filter->FilterPerAxis[2].Initialize(InterpolationParam[2].InterpolationTime, InterpolationParam[2].InterpolationType);
	}
}

#if WITH_EDITOR
void UBlendSpace::ValidateSampleData()
{
	// (done here since it won't be triggered in the BlendSpace::PostEditChangeProperty, due to empty property during Undo)
	SnapSamplesToClosestGridPoint();

	bool bSampleDataChanged = false;
	AnimLength = 0.f;

	bool bAllMarkerPatternsMatch = true;
	FSyncPattern BlendSpacePattern;

	int32 SampleWithMarkers = INDEX_NONE;

	for (int32 SampleIndex = 0; SampleIndex < SampleData.Num(); ++SampleIndex)
	{
		FBlendSample& Sample = SampleData[SampleIndex];

		// see if same data exists, by same, same values
		for (int32 ComparisonSampleIndex = SampleIndex + 1; ComparisonSampleIndex < SampleData.Num(); ++ComparisonSampleIndex)
		{
			if (IsSameSamplePoint(Sample.SampleValue, SampleData[ComparisonSampleIndex].SampleValue))
			{
				SampleData.RemoveAt(ComparisonSampleIndex);
				--ComparisonSampleIndex;

				bSampleDataChanged = true;
			}
		}

		if (IsAsset())
		{
			Sample.bIsValid = ValidateSampleValue(Sample.SampleValue, SampleIndex) && (Sample.Animation != nullptr);

			if (Sample.bIsValid)
			{
				if (Sample.Animation->GetPlayLength() > AnimLength)
				{
					// @todo : should apply scale? If so, we'll need to apply also when blend
					AnimLength = Sample.Animation->GetPlayLength();
				}

				Sample.CachedMarkerDataUpdateCounter = Sample.Animation->GetMarkerUpdateCounter();

				if (Sample.Animation->AuthoredSyncMarkers.Num() > 0)
				{
					auto PopulateMarkerNameArray = [](TArray<FName>& Pattern, TArray<struct FAnimSyncMarker>& AuthoredSyncMarkers)
					{
						Pattern.Reserve(AuthoredSyncMarkers.Num());
						for (FAnimSyncMarker& Marker : AuthoredSyncMarkers)
						{
							Pattern.Add(Marker.MarkerName);
						}
					};

					if (SampleWithMarkers == INDEX_NONE)
					{
						SampleWithMarkers = SampleIndex;
					}

					if (BlendSpacePattern.MarkerNames.Num() == 0)
					{
						PopulateMarkerNameArray(BlendSpacePattern.MarkerNames, Sample.Animation->AuthoredSyncMarkers);
					}
					else
					{
						TArray<FName> ThisPattern;
						PopulateMarkerNameArray(ThisPattern, Sample.Animation->AuthoredSyncMarkers);
						if (!BlendSpacePattern.DoesPatternMatch(ThisPattern))
						{
							bAllMarkerPatternsMatch = false;
						}
					}
				}
			}
			else
			{
				if (IsRunningGame())
				{
					UE_LOG(LogAnimation, Error, TEXT("[%s : %d] - Missing Sample Animation"), *GetFullName(), SampleIndex + 1);
				}
				else
				{
					static FName NAME_LoadErrors("LoadErrors");
					FMessageLog LoadErrors(NAME_LoadErrors);

					TSharedRef<FTokenizedMessage> Message = LoadErrors.Error();
					Message->AddToken(FTextToken::Create(LOCTEXT("EmptyAnimationData1", "The BlendSpace ")));
					Message->AddToken(FAssetNameToken::Create(GetPathName(), FText::FromString(GetName())));
					Message->AddToken(FTextToken::Create(LOCTEXT("EmptyAnimationData2", " has sample with no animation. Recommend to remove sample point or set new animation.")));
					LoadErrors.Notify();
				}
			}
		}
		else
		{
			Sample.bIsValid = ValidateSampleValue(Sample.SampleValue, SampleIndex);
		}
	}

	// set rotation blend in mesh space
	bRotationBlendInMeshSpace = ContainsMatchingSamples(AAT_RotationOffsetMeshSpace);

	SampleIndexWithMarkers = bAllMarkerPatternsMatch ? SampleWithMarkers : INDEX_NONE;

	if (bSampleDataChanged)
	{
		GridSamples.Empty();
		MarkPackageDirty();
	}
}

bool UBlendSpace::AddSample(const FVector& SampleValue)
{
	// We should only be adding samples without a source animation if we are not a standalone asset
	check(!IsAsset());

	const bool bValidSampleData = ValidateSampleValue(SampleValue);

	if (bValidSampleData)
	{
		SampleData.Add(FBlendSample(nullptr, SampleValue, true, bValidSampleData));
		UpdatePreviewBasePose();
	}

	return bValidSampleData;
}

bool UBlendSpace::AddSample(UAnimSequence* AnimationSequence, const FVector& SampleValue)
{
	const bool bValidSampleData = ValidateSampleValue(SampleValue) && ValidateAnimationSequence(AnimationSequence);

	if (bValidSampleData)
	{
		SampleData.Add(FBlendSample(AnimationSequence, SampleValue, true, bValidSampleData));
		UpdatePreviewBasePose();
	}

	return bValidSampleData;
}

bool UBlendSpace::EditSampleValue(const int32 BlendSampleIndex, const FVector& NewValue, bool bSnap)
{
	const bool bValidValue = SampleData.IsValidIndex(BlendSampleIndex) && ValidateSampleValue(NewValue, BlendSampleIndex);

	if (bValidValue)
	{
		// Set new value if it passes the tests
		SampleData[BlendSampleIndex].SampleValue = NewValue;
		SampleData[BlendSampleIndex].bIsValid = bValidValue;
		SampleData[BlendSampleIndex].bSnapToGrid = bSnap;
	}

	return bValidValue;
}

bool UBlendSpace::UpdateSampleAnimation(UAnimSequence* AnimationSequence, const FVector& SampleValue)
{
	int32 UpdateSampleIndex = INDEX_NONE;
	for (int32 SampleIndex = 0; SampleIndex < SampleData.Num(); ++SampleIndex)
	{
		if (IsSameSamplePoint(SampleValue, SampleData[SampleIndex].SampleValue))
		{
			UpdateSampleIndex = SampleIndex;
			break;
		}
	}

	if (UpdateSampleIndex != INDEX_NONE)
	{
		SampleData[UpdateSampleIndex].Animation = AnimationSequence;
	}

	return UpdateSampleIndex != INDEX_NONE;
}

bool UBlendSpace::ReplaceSampleAnimation(const int32 BlendSampleIndex, UAnimSequence* AnimationSequence)
{
	const bool bValidValue = SampleData.IsValidIndex(BlendSampleIndex);
	if (bValidValue)
	{
		SampleData[BlendSampleIndex].Animation = AnimationSequence;
	}

	return bValidValue;
}

bool UBlendSpace::DeleteSample(const int32 BlendSampleIndex)
{
	const bool bValidRemoval = SampleData.IsValidIndex(BlendSampleIndex);

	if (bValidRemoval)
	{
		SampleData.RemoveAtSwap(BlendSampleIndex);
		UpdatePreviewBasePose();
	}

	return bValidRemoval;
}

bool UBlendSpace::IsValidBlendSampleIndex(const int32 SampleIndex) const
{
	return SampleData.IsValidIndex(SampleIndex);
}

const TArray<FEditorElement>& UBlendSpace::GetGridSamples() const
{
	return GridSamples;
}

void UBlendSpace::FillupGridElements(const TArray<int32>& PointListToSampleIndices, const TArray<FEditorElement>& GridElements, const TArray<int32>& InDimensionIndices)
{
	DimensionIndices = InDimensionIndices;

	GridSamples.Empty(GridElements.Num());
	GridSamples.AddUninitialized(GridElements.Num());

	for (int32 ElementIndex = 0; ElementIndex < GridElements.Num(); ++ElementIndex)
	{
		const FEditorElement& ViewGrid = GridElements[ElementIndex];
		FEditorElement NewGrid;
		float TotalWeight = 0.f;
		for (int32 VertexIndex = 0; VertexIndex < FEditorElement::MAX_VERTICES; ++VertexIndex)
		{
			const int32 SampleIndex = ViewGrid.Indices[VertexIndex];
			if (SampleIndex != INDEX_NONE && PointListToSampleIndices.IsValidIndex(SampleIndex))
			{
				NewGrid.Indices[VertexIndex] = PointListToSampleIndices[SampleIndex];
			}
			else
			{
				NewGrid.Indices[VertexIndex] = INDEX_NONE;
			}

			if (NewGrid.Indices[VertexIndex] == INDEX_NONE)
			{
				NewGrid.Weights[VertexIndex] = 0.f;
			}
			else
			{
				NewGrid.Weights[VertexIndex] = ViewGrid.Weights[VertexIndex];
				TotalWeight += ViewGrid.Weights[VertexIndex];
			}
		}

		// Need to normalize the weights
		if (TotalWeight > 0.f)
		{
			for (int32 J = 0; J < FEditorElement::MAX_VERTICES; ++J)
			{
				NewGrid.Weights[J] /= TotalWeight;
			}
		}

		GridSamples[ElementIndex] = NewGrid;
	}
}

void UBlendSpace::EmptyGridElements()
{
	GridSamples.Empty();
}

bool UBlendSpace::ValidateAnimationSequence(const UAnimSequence* AnimationSequence) const
{
	const bool bValidAnimationSequence = IsAnimationCompatible(AnimationSequence)
		&& IsAnimationCompatibleWithSkeleton(AnimationSequence)
		&& (GetNumberOfBlendSamples() == 0 || DoesAnimationMatchExistingSamples(AnimationSequence));

	return bValidAnimationSequence;
}

bool UBlendSpace::DoesAnimationMatchExistingSamples(const UAnimSequence* AnimationSequence) const
{
	const bool bMatchesExistingAnimations = ContainsMatchingSamples(AnimationSequence->AdditiveAnimType);
	return bMatchesExistingAnimations;
}

bool UBlendSpace::ShouldAnimationBeAdditive() const
{
	const bool bShouldBeAdditive = !ContainsNonAdditiveSamples();
	return bShouldBeAdditive;
}

bool UBlendSpace::IsAnimationCompatibleWithSkeleton(const UAnimSequence* AnimationSequence) const
{
	// Check if the animation sequences skeleton is compatible with the blendspace one
	const USkeleton* MySkeleton = GetSkeleton();
	const bool bIsAnimationCompatible = AnimationSequence && MySkeleton && AnimationSequence->GetSkeleton() && MySkeleton->IsCompatible(AnimationSequence->GetSkeleton());
	return bIsAnimationCompatible;
}

bool UBlendSpace::IsAnimationCompatible(const UAnimSequence* AnimationSequence) const
{
	// If the supplied animation is of a different additive animation type or this blendspace support non-additive animations
	const bool bIsCompatible = IsValidAdditiveType(AnimationSequence->AdditiveAnimType);
	return bIsCompatible;
}

bool UBlendSpace::ValidateSampleValue(const FVector& SampleValue, int32 OriginalIndex) const
{
	bool bValid = true;
	bValid &= IsSampleWithinBounds(SampleValue);
	bValid &= !IsTooCloseToExistingSamplePoint(SampleValue, OriginalIndex);
	return bValid;
}

bool UBlendSpace::IsSampleWithinBounds(const FVector& SampleValue) const
{
	return !((SampleValue.X < BlendParameters[0].Min) ||
		(SampleValue.X > BlendParameters[0].Max) ||
		(SampleValue.Y < BlendParameters[1].Min) ||
		(SampleValue.Y > BlendParameters[1].Max));
}

bool UBlendSpace::IsTooCloseToExistingSamplePoint(const FVector& SampleValue, int32 OriginalIndex) const
{
	bool bMatchesSamplePoint = false;
	for (int32 SampleIndex = 0; SampleIndex < SampleData.Num(); ++SampleIndex)
	{
		if (SampleIndex != OriginalIndex)
		{
			if (IsSameSamplePoint(SampleValue, SampleData[SampleIndex].SampleValue))
			{
				bMatchesSamplePoint = true;
				break;
			}
		}
	}

	return bMatchesSamplePoint;
}

#endif // WITH_EDITOR

void UBlendSpace::InitializePerBoneBlend()
{
	const USkeleton* MySkeleton = GetSkeleton();
	for (FPerBoneInterpolation& BoneInterpolationData : PerBoneBlend)
	{
		BoneInterpolationData.Initialize(MySkeleton);
	}
	// Sort this by bigger to smaller, then we don't have to worry about checking the best parent
	PerBoneBlend.Sort([](const FPerBoneInterpolation& A, const FPerBoneInterpolation& B) { return A.BoneReference.BoneIndex > B.BoneReference.BoneIndex; });
}

void UBlendSpace::TickFollowerSamples(TArray<FBlendSampleData>& SampleDataList, const int32 HighestWeightIndex, FAnimAssetTickContext& Context, bool bResetMarkerDataOnFollowers) const
{
	for (int32 SampleIndex = 0; SampleIndex < SampleDataList.Num(); ++SampleIndex)
	{
		FBlendSampleData& SampleDataItem = SampleDataList[SampleIndex];
		const FBlendSample& Sample = SampleData[SampleDataItem.SampleDataIndex];
		if (HighestWeightIndex != SampleIndex)
		{
			if (bResetMarkerDataOnFollowers)
			{
				SampleDataItem.MarkerTickRecord.Reset();
			}

			if (Sample.Animation->AuthoredSyncMarkers.Num() > 0) // Update followers who can do marker sync, others will be handled later in TickAssetPlayer
			{
				Sample.Animation->TickByMarkerAsFollower(SampleDataItem.MarkerTickRecord, Context.MarkerTickContext, SampleDataItem.Time, SampleDataItem.PreviousTime, Context.GetLeaderDelta(), true);
			}
		}
	}
}

float UBlendSpace::GetAnimationLengthFromSampleData(const TArray<FBlendSampleData>& SampleDataList) const
{
	float BlendAnimLength = 0.f;
	for (int32 I = 0; I < SampleDataList.Num(); ++I)
	{
		const int32 SampleDataIndex = SampleDataList[I].SampleDataIndex;
		if (SampleData.IsValidIndex(SampleDataIndex))
		{
			const FBlendSample& Sample = SampleData[SampleDataIndex];
			if (Sample.Animation)
			{
				//Use the SamplePlayRate from the SampleDataList, not the RateScale from SampleData as SamplePlayRate might contain
				//Multiple samples contribution which we would otherwise lose
				const float MultipliedSampleRateScale = Sample.Animation->RateScale * SampleDataList[I].SamplePlayRate;
				// apply rate scale to get actual playback time
				BlendAnimLength += (Sample.Animation->GetPlayLength() / ((MultipliedSampleRateScale) != 0.0f ? FMath::Abs(MultipliedSampleRateScale) : 1.0f)) * SampleDataList[I].GetWeight();
				UE_LOG(LogAnimation, Verbose, TEXT("[%d] - Sample Animation(%s) : Weight(%0.5f) "), I + 1, *Sample.Animation->GetName(), SampleDataList[I].GetWeight());
			}
		}
	}

	return BlendAnimLength;
}

FVector UBlendSpace::GetClampedBlendInput(const FVector& BlendInput)  const
{
	FVector AdjustedInput = BlendInput;
	for (int iAxis = 0; iAxis != 3; ++iAxis)
	{
		if (!BlendParameters[iAxis].bWrapInput)
		{
			AdjustedInput[iAxis] = FMath::Clamp(AdjustedInput[iAxis], BlendParameters[iAxis].Min, BlendParameters[iAxis].Max);
		}
	}
	return AdjustedInput;
}

FVector UBlendSpace::GetClampedAndWrappedBlendInput(const FVector& BlendInput) const
{
	FVector AdjustedInput = BlendInput;
	for (int iAxis = 0; iAxis != 3; ++iAxis)
	{
		if (BlendParameters[iAxis].bWrapInput)
		{
			AdjustedInput[iAxis] = FMath::Wrap(AdjustedInput[iAxis], BlendParameters[iAxis].Min, BlendParameters[iAxis].Max);
		}
		else
		{
			AdjustedInput[iAxis] = FMath::Clamp(AdjustedInput[iAxis], BlendParameters[iAxis].Min, BlendParameters[iAxis].Max);
		}
	}
	return AdjustedInput;
}

FVector UBlendSpace::GetNormalizedBlendInput(const FVector& BlendInput) const
{
	FVector AdjustedInput = GetClampedAndWrappedBlendInput(BlendInput);

	const FVector MinBlendInput = FVector(BlendParameters[0].Min, BlendParameters[1].Min, BlendParameters[2].Min);
	const FVector GridSize = FVector(BlendParameters[0].GetGridSize(), BlendParameters[1].GetGridSize(), BlendParameters[2].GetGridSize());

	FVector NormalizedBlendInput = (AdjustedInput - MinBlendInput) / GridSize;
	return NormalizedBlendInput;
}

const FEditorElement* UBlendSpace::GetGridSampleInternal(int32 Index) const
{
	return GridSamples.IsValidIndex(Index) ? &GridSamples[Index] : NULL;
}

// When using CriticallyDampedSmoothing, how to go from the interpolation speed to the smooth
// time? What would the critically damped velocity be as it goes from a starting value of 0 to a
// target of 1 (see eq in CriticallyDampedSmoothing), starting with v = 0?
//
// v = W^2 t exp(-W t)
//
// Differentiate and set equal to zero to find maximum v is at t = 1 / W
//
// vMax = W / e = 2 / (SmoothingTime * e)
//
// Set this equal to TargetWeightInterpolationSpeedPerSec, we get
//
// SmoothingTime = 2 / (e * TargetWeightInterpolationSpeedPerSec)
//
// However - this looks significantly slower than when using a constant speed, because we're
// easing in/out, so aim for twice this speed (i.e. smooth over half the time)
static float SmoothingTimeFromSpeed(float Speed)
{
	return 1.0f / (EULERS_NUMBER * Speed);
}

static void SmoothWeight(float& Output, float& OutputRate, float Input, float InputRate, float Target, float DeltaTime, float Speed, bool bUseEaseInOut)
{
	if (Speed <= 0.0f)
	{
		Output = Target;
		return;
	}

	if (bUseEaseInOut)
	{
		Output = Input;
		OutputRate = InputRate;
		FMath::CriticallyDampedSmoothing(Output, OutputRate, Target, DeltaTime, SmoothingTimeFromSpeed(Speed));
	}
	else
	{
		Output = FMath::FInterpConstantTo(Input, Target, DeltaTime, Speed);
	}
}

bool UBlendSpace::InterpolateWeightOfSampleData(float DeltaTime, const TArray<FBlendSampleData>& OldSampleDataList, const TArray<FBlendSampleData>& NewSampleDataList, TArray<FBlendSampleData>& FinalSampleDataList) const
{
	float TotalFinalWeight = 0.f;
	float TotalFinalPerBoneWeight = 0.0f;

	// now interpolate from old to new target, this is brute-force
	for (auto OldIt = OldSampleDataList.CreateConstIterator(); OldIt; ++OldIt)
	{
		// Now need to modify old sample, so copy it
		FBlendSampleData OldSample = *OldIt;
		bool bTargetSampleExists = false;

		if (OldSample.PerBoneBlendData.Num() != PerBoneBlend.Num())
		{
			OldSample.PerBoneBlendData.Init(OldSample.TotalWeight, PerBoneBlend.Num());
			OldSample.PerBoneWeightRate.Init(OldSample.WeightRate, PerBoneBlend.Num());
		}

		for (auto NewIt = NewSampleDataList.CreateConstIterator(); NewIt; ++NewIt)
		{
			const FBlendSampleData& NewSample = *NewIt;
			// if same sample is found, interpolate
			if (NewSample.SampleDataIndex == OldSample.SampleDataIndex)
			{
				FBlendSampleData InterpData = NewSample;
				SmoothWeight(InterpData.TotalWeight, InterpData.WeightRate, OldSample.TotalWeight, OldSample.WeightRate, NewSample.TotalWeight, DeltaTime, TargetWeightInterpolationSpeedPerSec, bTargetWeightInterpolationEaseInOut);
				InterpData.PerBoneBlendData = OldSample.PerBoneBlendData;
				InterpData.PerBoneWeightRate = OldSample.PerBoneWeightRate;

				// now interpolate the per bone weights
				float TotalPerBoneWeight = 0.0f;
				for (int32 Iter = 0; Iter < InterpData.PerBoneBlendData.Num(); ++Iter)
				{
					SmoothWeight(
						InterpData.PerBoneBlendData[Iter], InterpData.PerBoneWeightRate[Iter],
						OldSample.PerBoneBlendData[Iter], OldSample.PerBoneWeightRate[Iter], NewSample.TotalWeight,
						DeltaTime, PerBoneBlend[Iter].InterpolationSpeedPerSec, bTargetWeightInterpolationEaseInOut);
					TotalPerBoneWeight += InterpData.PerBoneBlendData[Iter];
				}

				if (InterpData.TotalWeight > ZERO_ANIMWEIGHT_THRESH || TotalPerBoneWeight > ZERO_ANIMWEIGHT_THRESH)
				{
					FinalSampleDataList.Add(InterpData);
					TotalFinalWeight += InterpData.GetWeight();
					TotalFinalPerBoneWeight += TotalPerBoneWeight;
					bTargetSampleExists = true;
					break;
				}
			}
		}

		// if new target isn't found, interpolate to 0.f, this is gone
		if (bTargetSampleExists == false)
		{
			FBlendSampleData InterpData = OldSample;
			SmoothWeight(InterpData.TotalWeight, InterpData.WeightRate, OldSample.TotalWeight, OldSample.WeightRate, 0.0f, DeltaTime, TargetWeightInterpolationSpeedPerSec, bTargetWeightInterpolationEaseInOut);
			// now interpolate the per bone weights
			float TotalPerBoneWeight = 0.0f;
			for (int32 Iter = 0; Iter < InterpData.PerBoneBlendData.Num(); ++Iter)
			{
				SmoothWeight(
					InterpData.PerBoneBlendData[Iter], InterpData.PerBoneWeightRate[Iter],
					OldSample.PerBoneBlendData[Iter], OldSample.PerBoneWeightRate[Iter], 0.0f,
					DeltaTime, PerBoneBlend[Iter].InterpolationSpeedPerSec, bTargetWeightInterpolationEaseInOut);
				TotalPerBoneWeight += InterpData.PerBoneBlendData[Iter];
			}

			// add it if it's not zero
			if (InterpData.TotalWeight > ZERO_ANIMWEIGHT_THRESH || TotalPerBoneWeight > ZERO_ANIMWEIGHT_THRESH)
			{
				FinalSampleDataList.Add(InterpData);
				TotalFinalWeight += InterpData.GetWeight();
				TotalFinalPerBoneWeight += TotalPerBoneWeight;
			}
		}
	}

	// now find new samples that are not found from old samples
	for (auto OldIt = NewSampleDataList.CreateConstIterator(); OldIt; ++OldIt)
	{
		// Now need to modify old sample, so copy it
		FBlendSampleData OldSample = *OldIt;
		bool bOldSampleExists = false;

		if (OldSample.PerBoneBlendData.Num() != PerBoneBlend.Num())
		{
			OldSample.PerBoneBlendData.Init(OldSample.TotalWeight, PerBoneBlend.Num());
			OldSample.PerBoneWeightRate.Init(OldSample.WeightRate, PerBoneBlend.Num());
		}

		for (auto NewIt = FinalSampleDataList.CreateConstIterator(); NewIt; ++NewIt)
		{
			const FBlendSampleData& NewSample = *NewIt;
			if (NewSample.SampleDataIndex == OldSample.SampleDataIndex)
			{
				bOldSampleExists = true;
				break;
			}
		}

		// add those new samples
		if (bOldSampleExists == false)
		{
			FBlendSampleData InterpData = OldSample;
			float TargetWeight = InterpData.TotalWeight;
			OldSample.TotalWeight = 0.0f;
			OldSample.WeightRate = 0.0f;
			SmoothWeight(InterpData.TotalWeight, InterpData.WeightRate, OldSample.TotalWeight, OldSample.WeightRate, TargetWeight, DeltaTime, TargetWeightInterpolationSpeedPerSec, bTargetWeightInterpolationEaseInOut);
			// now interpolate the per bone weights
			float TotalPerBoneWeight = 0.0f;
			for (int32 Iter = 0; Iter < InterpData.PerBoneBlendData.Num(); ++Iter)
			{
				float Target = OldSample.PerBoneBlendData[Iter];
				OldSample.PerBoneBlendData[Iter] = 0.0f;
				OldSample.PerBoneWeightRate[Iter] = 0.0f;
				SmoothWeight(
					InterpData.PerBoneBlendData[Iter], InterpData.PerBoneWeightRate[Iter],
					OldSample.PerBoneBlendData[Iter], OldSample.PerBoneWeightRate[Iter], Target,
					DeltaTime, PerBoneBlend[Iter].InterpolationSpeedPerSec, bTargetWeightInterpolationEaseInOut);
				TotalPerBoneWeight += InterpData.PerBoneBlendData[Iter];
			}
			if (InterpData.TotalWeight > ZERO_ANIMWEIGHT_THRESH || TotalPerBoneWeight > ZERO_ANIMWEIGHT_THRESH)
			{
				FinalSampleDataList.Add(InterpData);
				TotalFinalWeight += InterpData.GetWeight();
				TotalFinalPerBoneWeight += TotalPerBoneWeight;
			}
		}
	}

	return TotalFinalWeight > ZERO_ANIMWEIGHT_THRESH || TotalFinalPerBoneWeight > ZERO_ANIMWEIGHT_THRESH;
}

FVector UBlendSpace::FilterInput(FBlendFilter* Filter, const FVector& BlendInput, float DeltaTime) const
{
#if WITH_EDITOR
	// Check 
	for (int32 AxisIndex = 0; AxisIndex < 3; ++AxisIndex)
	{
		if (Filter->FilterPerAxis[AxisIndex].NeedsUpdate(InterpolationParam[AxisIndex].InterpolationType, InterpolationParam[AxisIndex].InterpolationTime))
		{
			InitializeFilter(Filter);
			break;
		}
	}
#endif
	FVector FilteredBlendInput;
	FilteredBlendInput.X = Filter->FilterPerAxis[0].GetFilteredData(BlendInput.X, DeltaTime);
	FilteredBlendInput.Y = Filter->FilterPerAxis[1].GetFilteredData(BlendInput.Y, DeltaTime);
	FilteredBlendInput.Z = Filter->FilterPerAxis[2].GetFilteredData(BlendInput.Z, DeltaTime);
	return FilteredBlendInput;
}

bool UBlendSpace::ContainsMatchingSamples(EAdditiveAnimationType AdditiveType) const
{
	bool bMatching = true;
	for (const FBlendSample& Sample : SampleData)
	{
		const UAnimSequence* Animation = Sample.Animation;
		bMatching &= (SampleData.Num() > 1 && Animation == nullptr) || (Animation && ((AdditiveType == AAT_None) ? true : Animation->IsValidAdditive()) && Animation->AdditiveAnimType == AdditiveType);

		if (bMatching == false)
		{
			break;
		}
	}

	return bMatching && SampleData.Num() > 0;
}

bool UBlendSpace::IsSameSamplePoint(const FVector& SamplePointA, const FVector& SamplePointB) const
{
#if 1
	return FMath::IsNearlyEqual(SamplePointA.X, SamplePointB.X)
		&& FMath::IsNearlyEqual(SamplePointA.Y, SamplePointB.Y)
		&& FMath::IsNearlyEqual(SamplePointA.Z, SamplePointB.Z);
#else
	if (DimensionIndices.Num() == 0 || DimensionIndices.Num() > 3)
	{
		UE_LOG(LogAnimation, Warning, TEXT("Unhandled dimensionality in samples: %d"), DimensionIndices.Num());
		return false;
	}
	for (int32 iDim = 0; iDim != DimensionIndices.Num(); ++iDim)
	{
		if (!FMath::IsNearlyEqual(SamplePointA[iDim], SamplePointB[iDim]))
			return false;
	}
	return true;
#endif
}


#if WITH_EDITOR
bool UBlendSpace::ContainsNonAdditiveSamples() const
{
	return ContainsMatchingSamples(AAT_None);
}

void UBlendSpace::UpdatePreviewBasePose()
{
#if WITH_EDITORONLY_DATA
	PreviewBasePose = nullptr;
	// Check if blendspace is additive and try to find a ref pose 
	if (IsValidAdditive())
	{
		for (const FBlendSample& BlendSample : SampleData)
		{
			if (BlendSample.Animation && BlendSample.Animation->RefPoseSeq)
			{
				PreviewBasePose = BlendSample.Animation->RefPoseSeq;
				break;
			}
		}
	}
#endif // WITH_EDITORONLY_DATA
}

void UBlendSpace::UpdateBlendSpacesUsingAnimSequence(UAnimSequenceBase* Sequence)
{
	for (TObjectIterator<UBlendSpace> BlendSpaceIt; BlendSpaceIt; ++BlendSpaceIt)
	{
		TArray<UAnimationAsset*> ReferredAssets;
		BlendSpaceIt->GetAllAnimationSequencesReferred(ReferredAssets, false);

		if (ReferredAssets.Contains(Sequence))
		{
			BlendSpaceIt->Modify();
			BlendSpaceIt->ValidateSampleData();
		}
	}
}

#endif // WITH_EDITOR

TArray<FName>* UBlendSpace::GetUniqueMarkerNames()
{
	return (SampleIndexWithMarkers != INDEX_NONE && SampleData.Num() > 0) ? SampleData[SampleIndexWithMarkers].Animation->GetUniqueMarkerNames() : nullptr;
}

void UBlendSpace::GetRawSamplesFromBlendInput(const FVector& BlendInput, TArray<FGridBlendSample, TInlineAllocator<4> >& OutBlendSamples) const
{
	switch (DimensionIndices.Num())
	{
	case 1:
		GetRawSamplesFromBlendInput1D(BlendInput, OutBlendSamples);
		break;
	case 2:
		GetRawSamplesFromBlendInput2D(BlendInput, OutBlendSamples);
		break;
	default:
		UE_LOG(LogAnimation, Warning, TEXT("Unhandled dimensionality in samples: %d"), DimensionIndices.Num());
		break;
	}
}

#if WITH_EDITOR
void UBlendSpace::SnapSamplesToClosestGridPoint()
{
	switch (DimensionIndices.Num())
	{
	case 1:
		SnapSamplesToClosestGridPoint1D();
		break;
	case 2:
		SnapSamplesToClosestGridPoint2D();
		break;
	default:
		UE_LOG(LogAnimation, Warning, TEXT("Unhandled dimensionality in samples: %d"), DimensionIndices.Num());
		break;
	}
}

void UBlendSpace::RemapSamplesToNewAxisRange()
{
	switch (DimensionIndices.Num())
	{
	case 1:
		RemapSamplesToNewAxisRange1D();
		break;
	case 2:
		RemapSamplesToNewAxisRange2D();
		break;
	default:
		UE_LOG(LogAnimation, Warning, TEXT("Unhandled dimensionality in samples: %d"), DimensionIndices.Num());
		break;
	}
}

#endif // WITH_EDITOR



/*-----------------------------------------------------------------------------
	1D functionality.
-----------------------------------------------------------------------------*/

void UBlendSpace::GetRawSamplesFromBlendInput1D(const FVector& BlendInput, TArray<FGridBlendSample, TInlineAllocator<4> >& OutBlendSamples) const
{
	check(DimensionIndices.Num() == 1);
	int32 Index0 = DimensionIndices[0];

	FVector NormalizedBlendInput = GetNormalizedBlendInput(BlendInput);

	float GridIndex = FMath::TruncToFloat(NormalizedBlendInput[Index0]);
	float Remainder = NormalizedBlendInput[Index0] - GridIndex;

	const FEditorElement* BeforeElement = GetGridSampleInternal(GridIndex);

	if (BeforeElement)
	{
		FGridBlendSample NewSample;
		NewSample.GridElement = *BeforeElement;
		// now calculate weight - GridElement has weights to nearest samples, here we weight the grid element
		NewSample.BlendWeight = (1.f - Remainder);
		OutBlendSamples.Add(NewSample);
	}
	else
	{
		FGridBlendSample NewSample;
		NewSample.GridElement = FEditorElement();
		NewSample.BlendWeight = 0.f;
		OutBlendSamples.Add(NewSample);
	}

	const FEditorElement* AfterElement = GetGridSampleInternal(GridIndex + 1);

	if (AfterElement)
	{
		FGridBlendSample NewSample;
		NewSample.GridElement = *AfterElement;
		// now calculate weight - GridElement has weights to nearest samples, here we weight the grid element
		NewSample.BlendWeight = (Remainder);
		OutBlendSamples.Add(NewSample);
	}
	else
	{
		FGridBlendSample NewSample;
		NewSample.GridElement = FEditorElement();
		NewSample.BlendWeight = 0.f;
		OutBlendSamples.Add(NewSample);
	}
}

#if WITH_EDITOR

void UBlendSpace::SnapSamplesToClosestGridPoint1D()
{
	check(DimensionIndices.Num() == 1);
	int32 Index0 = DimensionIndices[0];

	TArray<float> GridPoints;
	TArray<int32> ClosestSampleToGridPoint;
	TArray<bool> SampleDataOnPoints;
	TArray<bool> ShouldSnap;

	const float GridMin = BlendParameters[Index0].Min;
	const float GridMax = BlendParameters[Index0].Max;
	const float GridRange = GridMax - GridMin;
	const int32 NumGridPoints = BlendParameters[Index0].GridNum + 1;
	const float GridStep = GridRange / BlendParameters[Index0].GridNum;

	// First mark all samples as invalid and s unsnapped, recording whether they were snapped before
	ShouldSnap.SetNumZeroed(SampleData.Num());
	for (int32 BlendSampleIndex = 0; BlendSampleIndex < SampleData.Num(); ++BlendSampleIndex)
	{
		FBlendSample& BlendSample = SampleData[BlendSampleIndex];
		ShouldSnap[BlendSampleIndex] = BlendSample.bSnapToGrid;
		BlendSample.bSnapToGrid = false;
	}

	for (int32 GridPointIndex = 0; GridPointIndex < NumGridPoints; ++GridPointIndex)
	{
		const float GridPointValue = (GridPointIndex * GridStep) + GridMin;
		GridPoints.Add(GridPointValue);
	}

	ClosestSampleToGridPoint.Init(INDEX_NONE, GridPoints.Num());

	// Find closest sample to grid point
	for (int32 PointIndex = 0; PointIndex < GridPoints.Num(); ++PointIndex)
	{
		const float GridPoint = GridPoints[PointIndex];
		float SmallestDistance = FLT_MAX;
		int32 Index = INDEX_NONE;

		for (int32 SampleIndex = 0; SampleIndex < SampleData.Num(); ++SampleIndex)
		{
			FBlendSample& BlendSample = SampleData[SampleIndex];
			const float Distance = FMath::Abs(GridPoint - BlendSample.SampleValue[Index0]);
			if (Distance < SmallestDistance)
			{
				Index = SampleIndex;
				SmallestDistance = Distance;
			}
		}

		ClosestSampleToGridPoint[PointIndex] = Index;
	}

	// Find closest grid point to sample
	for (int32 SampleIndex = 0; SampleIndex < SampleData.Num(); ++SampleIndex)
	{
		if (ShouldSnap[SampleIndex])
		{
			FBlendSample& BlendSample = SampleData[SampleIndex];

			// Find closest grid point
			float SmallestDistance = FLT_MAX;
			int32 Index = INDEX_NONE;
			for (int32 PointIndex = 0; PointIndex < GridPoints.Num(); ++PointIndex)
			{
				const float Distance = FMath::Abs(GridPoints[PointIndex] - BlendSample.SampleValue[Index0]);
				if (Distance < SmallestDistance)
				{
					Index = PointIndex;
					SmallestDistance = Distance;
				}
			}

			// Only move the sample if it is also closest to the grid point
			if (Index != INDEX_NONE && ClosestSampleToGridPoint[Index] == SampleIndex)
			{
				BlendSample.SampleValue[Index0] = GridPoints[Index];
				BlendSample.bSnapToGrid = true;
			}
		}
	}
}

void UBlendSpace::RemapSamplesToNewAxisRange1D()
{
	check(DimensionIndices.Num() == 1);
	int32 Index0 = DimensionIndices[0];

	const float OldGridMin = PreviousAxisMinMaxValues[Index0].X;
	const float OldGridMax = PreviousAxisMinMaxValues[Index0].Y;
	const float OldGridRange = OldGridMax - OldGridMin;

	const float NewGridMin = BlendParameters[Index0].Min;
	const float NewGridMax = BlendParameters[Index0].Max;
	const float NewGridRange = NewGridMax - NewGridMin;

	for (FBlendSample& BlendSample : SampleData)
	{
		const float NormalizedValue = (BlendSample.SampleValue[Index0] - OldGridMin) / OldGridRange;
		BlendSample.SampleValue[Index0] = NewGridMin + (NormalizedValue * NewGridRange);
	}
}


#endif // WITH_EDITOR



/*-----------------------------------------------------------------------------
	2D functionality.
-----------------------------------------------------------------------------*/

const FEditorElement* UBlendSpace::GetEditorElement(int32 XIndex, int32 YIndex) const
{
	int32 Index = XIndex * (BlendParameters[1].GridNum + 1) + YIndex;
	return GetGridSampleInternal(Index);
}

void UBlendSpace::GetRawSamplesFromBlendInput2D(const FVector& BlendInput, TArray<FGridBlendSample, TInlineAllocator<4> >& OutBlendSamples) const
{
	check(DimensionIndices.Num() == 2);

	OutBlendSamples.Reset();
	OutBlendSamples.AddUninitialized(4);

	FGridBlendSample& LeftBottom = OutBlendSamples[0];
	FGridBlendSample& RightBottom = OutBlendSamples[1];
	FGridBlendSample& LeftTop = OutBlendSamples[2];
	FGridBlendSample& RightTop = OutBlendSamples[3];

	const FVector NormalizedBlendInput = GetNormalizedBlendInput(BlendInput);
	const FVector GridIndex(FMath::TruncToFloat(NormalizedBlendInput.X), FMath::TruncToFloat(NormalizedBlendInput.Y), 0.f);
	const FVector Remainder = NormalizedBlendInput - GridIndex;
	// bi-linear very simple interpolation
	const FEditorElement* EleLT = GetEditorElement(GridIndex.X, GridIndex.Y + 1);
	if (EleLT)
	{
		LeftTop.GridElement = *EleLT;
		// now calculate weight - distance to each corner since input is already normalized within grid, we can just calculate distance 
		LeftTop.BlendWeight = (1.f - Remainder.X) * Remainder.Y;
	}
	else
	{
		LeftTop.GridElement = FEditorElement();
		LeftTop.BlendWeight = 0.f;
	}

	const FEditorElement* EleRT = GetEditorElement(GridIndex.X + 1, GridIndex.Y + 1);
	if (EleRT)
	{
		RightTop.GridElement = *EleRT;
		RightTop.BlendWeight = Remainder.X * Remainder.Y;
	}
	else
	{
		RightTop.GridElement = FEditorElement();
		RightTop.BlendWeight = 0.f;
	}

	const FEditorElement* EleLB = GetEditorElement(GridIndex.X, GridIndex.Y);
	if (EleLB)
	{
		LeftBottom.GridElement = *EleLB;
		LeftBottom.BlendWeight = (1.f - Remainder.X) * (1.f - Remainder.Y);
	}
	else
	{
		LeftBottom.GridElement = FEditorElement();
		LeftBottom.BlendWeight = 0.f;
	}

	const FEditorElement* EleRB = GetEditorElement(GridIndex.X + 1, GridIndex.Y);
	if (EleRB)
	{
		RightBottom.GridElement = *EleRB;
		RightBottom.BlendWeight = Remainder.X * (1.f - Remainder.Y);
	}
	else
	{
		RightBottom.GridElement = FEditorElement();
		RightBottom.BlendWeight = 0.f;
	}
}


#if WITH_EDITOR

void UBlendSpace::SnapSamplesToClosestGridPoint2D()
{
	check(DimensionIndices.Num() == 2);

	TArray<FVector> GridPoints;
	TArray<int32> ClosestSampleToGridPoint;
	TArray<bool> SampleDataOnPoints;
	TArray<bool> ShouldSnap;

	const FVector GridMin(BlendParameters[0].Min, BlendParameters[1].Min, 0.0f);
	const FVector GridMax(BlendParameters[0].Max, BlendParameters[1].Max, 0.0f);
	const FVector GridRange(GridMax.X - GridMin.X, GridMax.Y - GridMin.Y, 0.0f);
	const FIntPoint NumGridPoints(BlendParameters[0].GridNum + 1, BlendParameters[1].GridNum + 1);
	const FVector GridStep(GridRange.X / BlendParameters[0].GridNum, GridRange.Y / BlendParameters[1].GridNum, 0.0f);
	
	// First mark all samples as unsnapped, recording whether they were snapped before
	ShouldSnap.SetNumZeroed(SampleData.Num());
	for (int32 BlendSampleIndex = 0; BlendSampleIndex < SampleData.Num(); ++BlendSampleIndex)
	{
		FBlendSample& BlendSample = SampleData[BlendSampleIndex];
		ShouldSnap[BlendSampleIndex] = BlendSample.bSnapToGrid;
		BlendSample.bSnapToGrid = false;
	}

	for (int32 GridY = 0; GridY < NumGridPoints.Y; ++GridY)
	{
		for (int32 GridX = 0; GridX < NumGridPoints.X; ++GridX)
		{
			const FVector GridPoint((GridX * GridStep.X) + GridMin.X, (GridY * GridStep.Y) + GridMin.Y, 0.0f);
			GridPoints.Add(GridPoint);
		}
	}

	ClosestSampleToGridPoint.Init(INDEX_NONE, GridPoints.Num());

	// Find closest sample to grid point
	for (int32 PointIndex = 0; PointIndex < GridPoints.Num(); ++PointIndex)
	{
		const FVector& GridPoint = GridPoints[PointIndex];
		float SmallestDistance = FLT_MAX;
		int32 Index = INDEX_NONE;

		for (int32 SampleIndex = 0; SampleIndex < SampleData.Num(); ++SampleIndex)
		{
			FBlendSample& BlendSample = SampleData[SampleIndex];
			const float Distance = (GridPoint - BlendSample.SampleValue).SizeSquared2D();
			if (Distance < SmallestDistance)
			{
				Index = SampleIndex;
				SmallestDistance = Distance;
			}
		}

		ClosestSampleToGridPoint[PointIndex] = Index;
	}

	// Find closest grid point to sample
	for (int32 SampleIndex = 0; SampleIndex < SampleData.Num(); ++SampleIndex)
	{
		if(ShouldSnap[SampleIndex])
		{
			FBlendSample& BlendSample = SampleData[SampleIndex];

			// Find closest grid point
			float SmallestDistance = FLT_MAX;
			int32 Index = INDEX_NONE;
			for (int32 PointIndex = 0; PointIndex < GridPoints.Num(); ++PointIndex)
			{
				const float Distance = (GridPoints[PointIndex] - BlendSample.SampleValue).SizeSquared2D();
				if (Distance < SmallestDistance)
				{
					Index = PointIndex;
					SmallestDistance = Distance;
				}
			}

			// Only move the sample if it is also closest to the grid point
			if (Index != INDEX_NONE && ClosestSampleToGridPoint[Index] == SampleIndex)
			{
				BlendSample.SampleValue = GridPoints[Index];
				BlendSample.bSnapToGrid = true;
			}
		}
	}
}


void UBlendSpace::RemapSamplesToNewAxisRange2D()
{
	check(DimensionIndices.Num() == 2);

	const FVector OldGridMin(PreviousAxisMinMaxValues[0].X, PreviousAxisMinMaxValues[1].X, 0.0f);
	const FVector OldGridMax(PreviousAxisMinMaxValues[0].Y, PreviousAxisMinMaxValues[1].Y, 1.0f);
	const FVector OldGridRange = OldGridMax - OldGridMin;

	const FVector NewGridMin(BlendParameters[0].Min, BlendParameters[1].Min, 0.0f);
	const FVector NewGridMax(BlendParameters[0].Max, BlendParameters[1].Max, 1.0f);
	const FVector NewGridRange = NewGridMax - NewGridMin;

	for (FBlendSample& BlendSample : SampleData)
	{
		const FVector NormalizedValue = (BlendSample.SampleValue - OldGridMin) / OldGridRange;
		BlendSample.SampleValue = NewGridMin + (NormalizedValue * NewGridRange);
	}
}
#endif // WITH_EDITOR

#undef LOCTEXT_NAMESPACE 

