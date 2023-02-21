// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_EDITOR

#include "PoseSearch/PoseSearchAssetIndexer.h"
#include "AnimationRuntime.h"
#include "Animation/MirrorDataTable.h"
#include "PoseSearch/PoseSearchAnimNotifies.h"
#include "PoseSearch/PoseSearchAssetSampler.h"
#include "PoseSearch/PoseSearchDefines.h"
#include "PoseSearch/PoseSearchFeatureChannel.h"
#include "PoseSearch/PoseSearchIndex.h"
#include "PoseSearch/PoseSearchSchema.h"

namespace UE::PoseSearch
{
	
//////////////////////////////////////////////////////////////////////////
// FSamplingParam helpers
struct FSamplingParam
{
	float WrappedParam = 0.0f;
	int32 NumCycles = 0;

	// If the animation can't loop, WrappedParam contains the clamped value and whatever is left is stored here
	float Extrapolation = 0.0f;
};

static FSamplingParam WrapOrClampSamplingParam(bool bCanWrap, float SamplingParamExtent, float SamplingParam)
{
	// This is a helper function used by both time and distance sampling. A schema may specify time or distance
	// offsets that are multiple cycles of a clip away from the current pose being sampled.
	// And that time or distance offset may before the beginning of the clip (SamplingParam < 0.0f)
	// or after the end of the clip (SamplingParam > SamplingParamExtent). So this function
	// helps determine how many cycles need to be applied and what the wrapped value should be, clamping
	// if necessary.

	FSamplingParam Result;

	Result.WrappedParam = SamplingParam;

	const bool bIsSamplingParamExtentKindaSmall = SamplingParamExtent <= UE_KINDA_SMALL_NUMBER;
	if (!bIsSamplingParamExtentKindaSmall && bCanWrap)
	{
		if (SamplingParam < 0.0f)
		{
			while (Result.WrappedParam < 0.0f)
			{
				Result.WrappedParam += SamplingParamExtent;
				++Result.NumCycles;
			}
		}

		else
		{
			while (Result.WrappedParam > SamplingParamExtent)
			{
				Result.WrappedParam -= SamplingParamExtent;
				++Result.NumCycles;
			}
		}
	}

	const float ParamClamped = FMath::Clamp(Result.WrappedParam, 0.0f, SamplingParamExtent);
	if (ParamClamped != Result.WrappedParam)
	{
		check(bIsSamplingParamExtentKindaSmall || !bCanWrap);
		Result.Extrapolation = Result.WrappedParam - ParamClamped;
		Result.WrappedParam = ParamClamped;
	}

	return Result;
}

//////////////////////////////////////////////////////////////////////////
// FAssetIndexer
void FAssetIndexer::Reset()
{
	Output = FOutput();
	Stats = FStats();
}

void FAssetIndexer::Init(const FAssetIndexingContext& InIndexingContext, const FBoneContainer& InBoneContainer)
{
	check(InIndexingContext.Schema);
	check(InIndexingContext.Schema->IsValid());
	check(InIndexingContext.AssetSampler);

	BoneContainer = InBoneContainer;
	IndexingContext = InIndexingContext;

	Reset();

	Output.FirstIndexedSample = FMath::FloorToInt(IndexingContext.RequestedSamplingRange.Min * IndexingContext.Schema->SampleRate);
	Output.LastIndexedSample = FMath::Max(0, FMath::CeilToInt(IndexingContext.RequestedSamplingRange.Max * IndexingContext.Schema->SampleRate));
	Output.NumIndexedPoses = Output.LastIndexedSample - Output.FirstIndexedSample + 1;

	Output.FeatureVectorTable.SetNumZeroed(IndexingContext.Schema->SchemaCardinality * Output.NumIndexedPoses);
	Output.PoseMetadata.SetNum(Output.NumIndexedPoses);
}

void FAssetIndexer::Process()
{
	check(GetSchema()->IsValid());
	check(IndexingContext.AssetSampler);

	FMemMark Mark(FMemStack::Get());

	if (IndexingContext.Schema->SchemaCardinality > 0)
	{
		// Index each channel
		for (const TObjectPtr<UPoseSearchFeatureChannel>& ChannelPtr : IndexingContext.Schema->Channels)
		{
			if (ChannelPtr)
			{
				ChannelPtr->IndexAsset(*this, Output.FeatureVectorTable);
			}
		}
	}

	// Generate pose metadata
	for (int32 SampleIdx = GetBeginSampleIdx(); SampleIdx != GetEndSampleIdx(); ++SampleIdx)
	{
		Output.PoseMetadata[GetVectorIdx(SampleIdx)] = GetMetadata(SampleIdx);
	}

	// Computing stats
	ComputeStats();
}

void FAssetIndexer::ComputeStats()
{
	Stats = FStats();

	const FAssetSamplingContext* SamplingContext = IndexingContext.SamplingContext;
	check(SamplingContext->FiniteDelta > UE_KINDA_SMALL_NUMBER);

	for (int32 SampleIdx = GetBeginSampleIdx(); SampleIdx != GetEndSampleIdx(); ++SampleIdx)
	{
		const float SampleTime = FMath::Min(SampleIdx * IndexingContext.Schema->GetSamplingInterval(), IndexingContext.AssetSampler->GetPlayLength());

		bool AnyClamped = false;
		const FTransform TrajTransformsPast = GetTransform(SampleTime - SamplingContext->FiniteDelta, AnyClamped);
		if (!AnyClamped)
		{
			const FTransform TrajTransformsPresent = GetTransform(SampleTime, AnyClamped);
			if (!AnyClamped)
			{
				const FTransform TrajTransformsFuture = GetTransform(SampleTime + SamplingContext->FiniteDelta, AnyClamped);
				if (!AnyClamped)
				{
					// if any transform is clamped we just skip the sample entirely
					const FVector LinearVelocityPresent = (TrajTransformsPresent.GetTranslation() - TrajTransformsPast.GetTranslation()) / SamplingContext->FiniteDelta;
					const FVector LinearVelocityFuture = (TrajTransformsFuture.GetTranslation() - TrajTransformsPresent.GetTranslation()) / SamplingContext->FiniteDelta;
					const FVector LinearAcceleration = (LinearVelocityFuture - LinearVelocityPresent) / SamplingContext->FiniteDelta;

					const float Speed = LinearVelocityPresent.Length();
					const float Acceleration = LinearAcceleration.Length();

					Stats.AccumulatedSpeed += Speed;
					Stats.MaxSpeed = FMath::Max(Stats.MaxSpeed, Speed);

					Stats.AccumulatedAcceleration += Acceleration;
					Stats.MaxAcceleration = FMath::Max(Stats.MaxAcceleration, Acceleration);

					++Stats.NumAccumulatedSamples;
				}
			}
		}
	}
}

FAssetIndexer::FSampleInfo FAssetIndexer::GetSampleInfo(float SampleTime) const
{
	FSampleInfo Sample;

	check(IndexingContext.AssetSampler);

	const float PlayLength = IndexingContext.AssetSampler->GetPlayLength();
	const bool bCanWrap = IndexingContext.AssetSampler->IsLoopable();

	float MainRelativeTime = SampleTime;
	if (SampleTime < 0.0f && bCanWrap)
	{
		// In this case we're sampling a loop backwards, so MainRelativeTime must adjust so the number of cycles is
		// counted correctly.
		MainRelativeTime += PlayLength;
	}

	const FSamplingParam SamplingParam = WrapOrClampSamplingParam(bCanWrap, PlayLength, MainRelativeTime);

	Sample.Clip = IndexingContext.AssetSampler;

	if (FMath::Abs(SamplingParam.Extrapolation) > SMALL_NUMBER)
	{
		Sample.bClamped = true;
		Sample.ClipTime = SamplingParam.WrappedParam + SamplingParam.Extrapolation;
		Sample.RootTransform = Sample.Clip->ExtractRootTransform(Sample.ClipTime);
	}
	else
	{
		Sample.ClipTime = SamplingParam.WrappedParam;
		Sample.RootTransform = FTransform::Identity;

		// Find the remaining motion deltas after wrapping
		FTransform RootMotionRemainder = Sample.Clip->ExtractRootTransform(Sample.ClipTime);

		const bool bNegativeSampleTime = SampleTime < 0.f;
		if (SamplingParam.NumCycles > 0 || bNegativeSampleTime)
		{
			const FTransform RootMotionLast = IndexingContext.AssetSampler->GetTotalRootTransform();

			// Determine how to accumulate motion for every cycle of the anim. If the sample
			// had to be clamped, this motion will end up not getting applied below.
			// Also invert the accumulation direction if the requested sample was wrapped backwards.
			FTransform RootMotionPerCycle = RootMotionLast;

			if (bNegativeSampleTime)
			{
				RootMotionPerCycle = RootMotionPerCycle.Inverse();
			}
			
			// Invert motion deltas if we wrapped backwards
			if (bNegativeSampleTime)
			{
				RootMotionRemainder.SetToRelativeTransform(RootMotionLast);
			}

			// Note if the sample was clamped, no motion will be applied here because NumCycles will be zero
			int32 CyclesRemaining = SamplingParam.NumCycles;
			while (CyclesRemaining--)
			{
				Sample.RootTransform = RootMotionPerCycle * Sample.RootTransform;
			}
		}

		Sample.RootTransform = RootMotionRemainder * Sample.RootTransform;
	}

	return Sample;
}

FTransform FAssetIndexer::MirrorTransform(const FTransform& Transform) const
{
	return IndexingContext.bMirrored ? IndexingContext.SamplingContext->MirrorTransform(Transform) : Transform;
}

FPoseSearchPoseMetadata FAssetIndexer::GetMetadata(int32 SampleIdx) const
{
	const float SequenceLength = IndexingContext.AssetSampler->GetPlayLength();
	const float SampleTime = FMath::Min(SampleIdx * IndexingContext.Schema->GetSamplingInterval(), SequenceLength);

	FPoseSearchPoseMetadata Metadata;
	Metadata.CostAddend = IndexingContext.Schema->BaseCostBias;
	Metadata.ContinuingPoseCostAddend = IndexingContext.Schema->ContinuingPoseCostBias;

	TArray<UAnimNotifyState_PoseSearchBase*> NotifyStates;
	IndexingContext.AssetSampler->ExtractPoseSearchNotifyStates(SampleTime, NotifyStates);
	for (const UAnimNotifyState_PoseSearchBase* PoseSearchNotify : NotifyStates)
	{
		if (PoseSearchNotify->GetClass()->IsChildOf<UAnimNotifyState_PoseSearchBlockTransition>())
		{
			EnumAddFlags(Metadata.Flags, EPoseSearchPoseFlags::BlockTransition);
		}
		else if (PoseSearchNotify->GetClass()->IsChildOf<UAnimNotifyState_PoseSearchModifyCost>())
		{
			const UAnimNotifyState_PoseSearchModifyCost* ModifyCostNotify =
				Cast<const UAnimNotifyState_PoseSearchModifyCost>(PoseSearchNotify);
			Metadata.CostAddend = ModifyCostNotify->CostAddend;
		}
		else if (PoseSearchNotify->GetClass()->IsChildOf<UAnimNotifyState_PoseSearchOverrideContinuingPoseCostBias>())
		{
			const UAnimNotifyState_PoseSearchOverrideContinuingPoseCostBias* ContinuingPoseCostBias =
				Cast<const UAnimNotifyState_PoseSearchOverrideContinuingPoseCostBias>(PoseSearchNotify);
			Metadata.ContinuingPoseCostAddend = ContinuingPoseCostBias->CostAddend;
		}
	}

	if (IndexingContext.AssetSampler->IsLoopable())
	{
		Metadata.CostAddend += IndexingContext.Schema->LoopingCostBias;
	}

	return Metadata;
}

FAssetIndexer::CachedEntry& FAssetIndexer::GetEntry(float SampleTime)
{
	using namespace UE::Anim;

	CachedEntry* Entry = CachedEntries.Find(SampleTime);
	const FAssetSamplingContext* SamplingContext = IndexingContext.SamplingContext;

	if (!Entry)
	{
		Entry = &CachedEntries.Add(SampleTime);
		Entry->SampleTime = SampleTime;

		if (!BoneContainer.IsValid())
		{
			UE_LOG(LogPoseSearch,
				Warning,
				TEXT("Invalid BoneContainer encountered in FAssetIndexer::GetEntry. Asset: %s. Schema: %s. BoneContainerAsset: %s. NumBoneIndices: %d"),
				*GetNameSafe(IndexingContext.AssetSampler->GetAsset()),
				*GetNameSafe(IndexingContext.Schema),
				*GetNameSafe(BoneContainer.GetAsset()),
				BoneContainer.GetCompactPoseNumBones());
		}

		const FAssetIndexer::FSampleInfo Sample = GetSampleInfo(SampleTime);
		float CurrentTime = Sample.ClipTime;
		float PreviousTime = CurrentTime - SamplingContext->FiniteDelta;

		const bool bLoopable = Sample.Clip->IsLoopable();
		const float PlayLength = Sample.Clip->GetPlayLength();
		if (!bLoopable)
		{
			// if not loopable we clamp the pose at time zero or PlayLength
			if (PreviousTime < 0.f)
			{
				PreviousTime = 0.f;
				CurrentTime = FMath::Min(SamplingContext->FiniteDelta, PlayLength);
			}
			else if (CurrentTime > PlayLength)
			{
				CurrentTime = PlayLength;
				PreviousTime = FMath::Max(PlayLength - SamplingContext->FiniteDelta, 0.f);
			}
		}

		FDeltaTimeRecord DeltaTimeRecord;
		DeltaTimeRecord.Set(PreviousTime, CurrentTime - PreviousTime);
		// no need to extract root motion here, since we use the precalculated Sample.RootTransform as root transform for the Entry
		FAnimExtractContext ExtractionCtx(static_cast<double>(CurrentTime), false, DeltaTimeRecord, bLoopable);

		FCompactPose Pose;
		FBlendedCurve UnusedCurve;
		FStackAttributeContainer UnusedAtrribute;
		FAnimationPoseData AnimPoseData = { Pose, UnusedCurve, UnusedAtrribute };

		UnusedCurve.InitFrom(BoneContainer);
		Pose.SetBoneContainer(&BoneContainer);

		Sample.Clip->ExtractPose(ExtractionCtx, AnimPoseData);

		if (IndexingContext.bMirrored)
		{
			FAnimationRuntime::MirrorPose(
				AnimPoseData.GetPose(),
				IndexingContext.Schema->MirrorDataTable->MirrorAxis,
				SamplingContext->CompactPoseMirrorBones,
				SamplingContext->ComponentSpaceRefRotations
			);
			// Note curves and attributes are not used during the indexing process and therefore don't need to be mirrored
		}

		Entry->ComponentSpacePose.InitPose(MoveTemp(Pose));
		Entry->RootTransform = Sample.RootTransform;
		Entry->bClamped = Sample.bClamped;
	}

	return *Entry;
}

// returns the transform in component space for the bone indexed by Schema->BoneReferences[SchemaBoneIdx] at SampleTime seconds
FTransform FAssetIndexer::GetComponentSpaceTransform(float SampleTime, bool& bClamped, int8 SchemaBoneIdx)
{
	CachedEntry& Entry = GetEntry(SampleTime);
	bClamped = Entry.bClamped;

	if (!IndexingContext.Schema->IsRootBone(SchemaBoneIdx))
	{
		return CalculateComponentSpaceTransform(Entry, SchemaBoneIdx);
	}

	return FTransform::Identity;
}

// returns the transform in animation space for the bone indexed by Schema->BoneReferences[SchemaBoneIdx] at SampleTime seconds
FTransform FAssetIndexer::GetTransform(float SampleTime, bool& bClamped, int8 SchemaBoneIdx)
{
	CachedEntry& Entry = GetEntry(SampleTime);
	bClamped = Entry.bClamped;

	const FTransform MirroredRootTransform = MirrorTransform(Entry.RootTransform);
	if (!IndexingContext.Schema->IsRootBone(SchemaBoneIdx))
	{
		return CalculateComponentSpaceTransform(Entry, SchemaBoneIdx) * MirroredRootTransform;
	}

	return MirroredRootTransform;
}

FTransform FAssetIndexer::CalculateComponentSpaceTransform(FAssetIndexer::CachedEntry& Entry, int8 SchemaBoneIdx)
{
	const FBoneReference& BoneReference = IndexingContext.Schema->BoneReferences[SchemaBoneIdx];
	const FCompactPoseBoneIndex CompactBoneIndex = BoneContainer.MakeCompactPoseIndex(FMeshPoseBoneIndex(BoneReference.BoneIndex));
	return Entry.ComponentSpacePose.GetComponentSpaceTransform(CompactBoneIndex);
}

FQuat FAssetIndexer::GetSampleRotation(float SampleTimeOffset, int32 SampleIdx, int8 SchemaSampleBoneIdx, int8 SchemaOriginBoneIdx)
{
	const float OriginTime = FMath::Min(SampleIdx * IndexingContext.Schema->GetSamplingInterval(), IndexingContext.AssetSampler->GetPlayLength());
	const float SampleTime = OriginTime + SampleTimeOffset;

	// @todo: add support for SchemaSampleBoneIdx
	if (!IndexingContext.Schema->IsRootBone(SchemaOriginBoneIdx))
	{
		UE_LOG(LogPoseSearch,
			Error,
			TEXT("FAssetIndexer::GetSampleRotation: support for non root origin bones not implemented (bone: '%s', schema: '%s'"), 
			*IndexingContext.Schema->BoneReferences[SchemaOriginBoneIdx].BoneName.ToString(),
			*GetNameSafe(IndexingContext.Schema));
	}

	bool bUnused;
	if (SampleTime == OriginTime)
	{
		return GetComponentSpaceTransform(SampleTime, bUnused, SchemaSampleBoneIdx).GetRotation();
	}

	const FTransform RootBoneTransform = GetTransform(OriginTime, bUnused);
	FTransform BoneTransform = GetTransform(SampleTime, bUnused, SchemaSampleBoneIdx);
	BoneTransform.SetToRelativeTransform(RootBoneTransform);
	return BoneTransform.GetRotation();
}

FVector FAssetIndexer::GetSamplePosition(float SampleTimeOffset, int32 SampleIdx, int8 SchemaSampleBoneIdx, int8 SchemaOriginBoneIdx)
{
	const float OriginTime = FMath::Min(SampleIdx * IndexingContext.Schema->GetSamplingInterval(), IndexingContext.AssetSampler->GetPlayLength());
	const float SampleTime = OriginTime + SampleTimeOffset;

	bool bUnused;
	return GetSamplePositionInternal(SampleTime, OriginTime, bUnused, SchemaSampleBoneIdx, SchemaOriginBoneIdx);
}

FVector FAssetIndexer::GetSamplePositionInternal(float SampleTime, float OriginTime, bool& bClamped, int8 SchemaSampleBoneIdx, int8 SchemaOriginBoneIdx)
{
	if (SampleTime == OriginTime)
	{
		if (IndexingContext.Schema->IsRootBone(SchemaOriginBoneIdx))
		{
			return GetComponentSpaceTransform(SampleTime, bClamped, SchemaSampleBoneIdx).GetTranslation();
		}

		bool bOriginClamped;
		const FVector SampleBonePosition = GetComponentSpaceTransform(SampleTime, bClamped, SchemaSampleBoneIdx).GetTranslation();
		const FVector OriginBonePosition = GetComponentSpaceTransform(OriginTime, bOriginClamped, SchemaOriginBoneIdx).GetTranslation();
		bClamped |= bOriginClamped;
		return SampleBonePosition - OriginBonePosition;
	}

	bool Unused;
	const FTransform RootBoneTransform = GetTransform(OriginTime, Unused);
	const FTransform SampleBoneTransform = GetTransform(SampleTime, bClamped, SchemaSampleBoneIdx);
	if (IndexingContext.Schema->IsRootBone(SchemaOriginBoneIdx))
	{
		return RootBoneTransform.InverseTransformPosition(SampleBoneTransform.GetTranslation());
	}

	bool bOriginClamped;
	const FTransform OriginBoneTransform = GetTransform(OriginTime, bOriginClamped, SchemaOriginBoneIdx);
	bClamped |= bOriginClamped;
	const FVector DeltaBoneTranslation = SampleBoneTransform.GetTranslation() - OriginBoneTransform.GetTranslation();
	return RootBoneTransform.InverseTransformVector(DeltaBoneTranslation);
}

FVector FAssetIndexer::GetSampleVelocity(float SampleTimeOffset, int32 SampleIdx, int8 SchemaSampleBoneIdx, int8 SchemaOriginBoneIdx, bool bUseCharacterSpaceVelocities)
{
	const float OriginSampleTime = FMath::Min(SampleIdx * IndexingContext.Schema->GetSamplingInterval(), IndexingContext.AssetSampler->GetPlayLength());
	const float SubsampleTime = OriginSampleTime + SampleTimeOffset;
	const float FiniteDelta = IndexingContext.SamplingContext->FiniteDelta;

	bool bClampedPast, bClampedPresent;
	const FVector BonePositionPast = GetSamplePositionInternal(SubsampleTime - FiniteDelta, bUseCharacterSpaceVelocities ? OriginSampleTime - FiniteDelta : OriginSampleTime, bClampedPast, SchemaSampleBoneIdx, SchemaOriginBoneIdx);
	const FVector BonePositionPresent = GetSamplePositionInternal(SubsampleTime, OriginSampleTime, bClampedPresent, SchemaSampleBoneIdx, SchemaOriginBoneIdx);

	FVector LinearVelocity;
	if (!bClampedPast)
	{
		LinearVelocity = (BonePositionPresent - BonePositionPast) / FiniteDelta;
	}
	else
	{
		bool Unused;
		const FVector BonePositionFuture = GetSamplePositionInternal(SubsampleTime + FiniteDelta, bUseCharacterSpaceVelocities ? OriginSampleTime + FiniteDelta : OriginSampleTime, Unused, SchemaSampleBoneIdx, SchemaOriginBoneIdx);
		LinearVelocity = (BonePositionFuture - BonePositionPresent) / FiniteDelta;
	}
	return LinearVelocity;
}

int32 FAssetIndexer::GetVectorIdx(int32 SampleIdx) const
{
	return SampleIdx - GetBeginSampleIdx();
}

TArrayView<float> FAssetIndexer::GetPoseVector(int32 SampleIdx, TArrayView<float> FeatureVectorTable) const
{
	check(IndexingContext.Schema);
	return MakeArrayView(&FeatureVectorTable[GetVectorIdx(SampleIdx) * IndexingContext.Schema->SchemaCardinality], IndexingContext.Schema->SchemaCardinality);
}

const UPoseSearchSchema* FAssetIndexer::GetSchema() const
{
	check(IndexingContext.Schema);
	return IndexingContext.Schema;
}

} // namespace UE::PoseSearch
#endif // WITH_EDITOR
