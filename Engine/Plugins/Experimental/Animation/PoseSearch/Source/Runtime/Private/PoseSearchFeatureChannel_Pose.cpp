// Copyright Epic Games, Inc. All Rights Reserved.

#include "PoseSearchFeatureChannel_Pose.h"
#include "DrawDebugHelpers.h"
#include "PoseSearch/PoseSearch.h"
#include "PoseSearch/PoseSearchDerivedDataKey.h"
#include "PoseSearch/PoseSearchSchema.h"

#define LOCTEXT_NAMESPACE "PoseSearchFeatureChannels"

namespace UE::PoseSearch
{

constexpr bool UseCharacterSpaceVelocities = true;

struct LocalMinMax
{
	enum
	{
		Min,
		Max
	} Type = Min;
	int32 Index = 0;
	float SignalValue = 0.f;
};

template <typename T>
T GetValueAtIndex(int32 Sample, const TArray<T>& Values)
{
	const int32 Num = Values.Num();
	check(Num > 1);

	if (Sample < 0)
	{
		return (Values[1] - Values[0]) * Sample + Values[0];
	}

	if (Sample < Num)
	{
		return Values[Sample];
	}

	return (Values[Num - 1] - Values[Num - 2]) * (Sample - (Num - 1)) + Values[Num - 1];
}

static void CollectBonePositions(TArray<FVector>& BonePositions, IAssetIndexer& Indexer, int8 SchemaBoneIdx)
{
	const FAssetIndexingContext& IndexingContext = Indexer.GetIndexingContext();
	const float FiniteDelta = IndexingContext.Schema->GetSamplingInterval();
	const float SampleTimeStart = FMath::Min(IndexingContext.BeginSampleIdx * FiniteDelta, IndexingContext.AssetSampler->GetPlayLength());
	const int32 NumSamples = IndexingContext.EndSampleIdx - IndexingContext.BeginSampleIdx;

	// collecting all the bone transforms
	BonePositions.Reset();
	BonePositions.AddDefaulted(NumSamples);
	for (int32 SampleIdx = 0; SampleIdx != NumSamples; ++SampleIdx)
	{
		const float SampleTime = SampleTimeStart + SampleIdx * FiniteDelta;
		bool bUnused;
		BonePositions[SampleIdx] = Indexer.GetTransformAndCacheResults(SampleTime, SampleTimeStart, SchemaBoneIdx, bUnused).GetTranslation();
	}
}

static void CalculateSignal(const TArray<FVector>& BonePositions, TArray<float>& Signal, int32 offset = 1)
{
	Signal.Reset();
	Signal.AddDefaulted(BonePositions.Num());

	for (int32 SampleIdx = 0; SampleIdx != BonePositions.Num(); ++SampleIdx)
	{
		Signal[SampleIdx] = (GetValueAtIndex(SampleIdx + offset, BonePositions) - GetValueAtIndex(SampleIdx - offset, BonePositions)).Length();
	}
}

static void SmoothSignal(const TArray<float>& Signal, TArray<float>& SmoothedSignal, int32 offset = 1)
{
	SmoothedSignal.Reset();
	SmoothedSignal.AddDefaulted(Signal.Num());

	for (int32 SampleIdx = -offset; SampleIdx != offset; ++SampleIdx)
	{
		SmoothedSignal[0] += GetValueAtIndex(SampleIdx, Signal);
	}

	for (int32 SampleIdx = 1; SampleIdx != Signal.Num(); ++SampleIdx)
	{
		SmoothedSignal[SampleIdx] = SmoothedSignal[SampleIdx - 1] - GetValueAtIndex(SampleIdx - offset - 1, Signal) + GetValueAtIndex(SampleIdx + offset, Signal);
	}

	for (int32 SampleIdx = 0; SampleIdx != Signal.Num(); ++SampleIdx)
	{
		SmoothedSignal[SampleIdx] /= 2 * offset + 1;
	}
}

static void FindLocalMinMax(const TArray<float>& Signal, TArray<LocalMinMax>& MinMax)
{
	enum SignalState
	{
		Flat,
		Ascending,
		Descending
	};

	MinMax.Reset();
	if (Signal.Num() > 1)
	{
		SignalState State = SignalState::Flat;
		for (int32 SignalIndex = 1; SignalIndex < Signal.Num(); ++SignalIndex)
		{
			const int32 PrevSignalIndex = SignalIndex - 1;
			const float PrevSignalValue = Signal[PrevSignalIndex];
			const float SignalValue = Signal[SignalIndex];

			if (State == SignalState::Flat)
			{
				if (SignalValue > PrevSignalValue)
				{
					State = SignalState::Ascending;
				}
				else if(SignalValue < PrevSignalValue)
				{
					State = SignalState::Descending;
				}
			}
			else if (State == SignalState::Ascending)
			{
				if (SignalValue < PrevSignalValue)
				{
					State = SignalState::Descending;
					
					LocalMinMax LocalMinMax;
					LocalMinMax.Type = LocalMinMax::Max;
					LocalMinMax.Index = PrevSignalIndex;
					LocalMinMax.SignalValue = Signal[LocalMinMax.Index];

					check(MinMax.IsEmpty() || MinMax.Last().Type != LocalMinMax.Type);
					MinMax.Add(LocalMinMax);
				}
			}
			else // if (State == SignalState::Descending)
			{
				if (SignalValue > PrevSignalValue)
				{
					State = SignalState::Ascending;

					LocalMinMax LocalMinMax;
					LocalMinMax.Type = LocalMinMax::Min;
					LocalMinMax.Index = PrevSignalIndex;
					LocalMinMax.SignalValue = Signal[LocalMinMax.Index];

					check(MinMax.IsEmpty() || MinMax.Last().Type != LocalMinMax.Type);
					MinMax.Add(LocalMinMax);
				}
			}
		}
	}
}

static void ExtrapolateLocalMinMaxBoundaries(TArray<LocalMinMax>& MinMax, const TArray<float>& Signal)
{
	const int32 Num = MinMax.Num();

	check(Signal.Num() > 0);

	LocalMinMax InitialMinMax;
	LocalMinMax FinalMinMax;

	if (Num == 0)
	{
		const bool IsInitialMax = Signal[0] > Signal[Signal.Num() - 1];

		InitialMinMax.Index = 0;
		InitialMinMax.SignalValue = Signal[0];
		InitialMinMax.Type = IsInitialMax ? LocalMinMax::Max : LocalMinMax::Min;

		FinalMinMax.Index = Signal.Num() - 1;
		FinalMinMax.SignalValue = Signal[Signal.Num() - 1];
		FinalMinMax.Type = IsInitialMax ? LocalMinMax::Min : LocalMinMax::Max;

		MinMax.Add(InitialMinMax);
		MinMax.Add(FinalMinMax);
	}
	else
	{
		int32 InitialDelta = 0;
		int32 FinalDelta = 0;
		if (Num > 2)
		{
			InitialDelta = MinMax[2].Index - MinMax[1].Index;
			FinalDelta = MinMax[Num - 2].Index - MinMax[Num - 3].Index;
		}
		else if (Num > 1)
		{
			InitialDelta = MinMax[1].Index - MinMax[0].Index;
			FinalDelta = MinMax[Num - 1].Index - MinMax[Num - 2].Index;
		}
		else
		{
			InitialDelta = MinMax[0].Index;
			FinalDelta = (Signal.Num() - 1) - MinMax[0].Index;
		}

		InitialMinMax.SignalValue = Num > 1 ? MinMax[1].SignalValue : Signal[0];
		InitialMinMax.Type = MinMax[0].Type == LocalMinMax::Min ? LocalMinMax::Max : LocalMinMax::Min;
		InitialMinMax.Index = FMath::Min(MinMax[0].Index - InitialDelta, 0);

		FinalMinMax.SignalValue = Num > 1 ? MinMax[Num - 2].SignalValue : Signal[Signal.Num() - 1];
		FinalMinMax.Type = MinMax[Num - 1].Type == LocalMinMax::Min ? LocalMinMax::Max : LocalMinMax::Min;
		FinalMinMax.Index = FMath::Max(MinMax[Num - 1].Index + FinalDelta, Signal.Num() - 1);

		// there's no point in adding an InitialMinMax if the first MinMax is at the first frame of the signal
		if (MinMax[0].Index > 0)
		{
			MinMax.Insert(InitialMinMax, 0);
		}

		// there's no point in adding a FinalMinMax if the last MinMax is at the last frame of the signal
		if (MinMax[Num - 1].Index < Signal.Num() - 1)
		{
			MinMax.Add(FinalMinMax);
		}
	}
}

static void ValidateLocalMinMax(const TArray<LocalMinMax>& MinMax)
{
	for (int32 i = 1; i < MinMax.Num(); ++i)
	{
		check(MinMax[i].Type != MinMax[i - 1].Type);
		check(MinMax[i].Index > MinMax[i - 1].Index);
		if (MinMax[i].Type == LocalMinMax::Min)
		{
			check(MinMax[i].SignalValue <= MinMax[i - 1].SignalValue);
		}
		else
		{
			check(MinMax[i].SignalValue >= MinMax[i - 1].SignalValue);
		}
	}
}

static void CalculatePhaseAndCertainty(int32 Index, const TArray<LocalMinMax>& MinMax, int32 SignalSize, float& Phase, float& Certainty)
{
	// @todo: expose them via UI
	static float CertaintyMin = 1.f;
	static float CertaintyMult = 0.1f;

	const int32 LastIndex = MinMax.Num() - 1;
	for (int32 i = 1; i < MinMax.Num(); ++i)
	{
		const int32 MinMaxIndex = MinMax[i].Index;
		if (Index < MinMaxIndex)
		{
			const int32 PrevMinMaxIndex = MinMax[i - 1].Index;
			check(MinMaxIndex > PrevMinMaxIndex);
			const float Ratio = static_cast<float>((Index - PrevMinMaxIndex)) / static_cast<float>((MinMaxIndex - PrevMinMaxIndex));
			const float PhaseOffset = MinMax[i - 1].Type == LocalMinMax::Min ? 0.f : 0.5f;
			Phase = PhaseOffset + Ratio * 0.5f;

			const float DeltaSignalValue = FMath::Abs(MinMax[i - 1].SignalValue - MinMax[i].SignalValue);
			const float NextDeltaSignalValue = i < LastIndex ? FMath::Abs(MinMax[i].SignalValue - MinMax[i + 1].SignalValue) : DeltaSignalValue;
			Certainty = CertaintyMin + (DeltaSignalValue * (1.f - Ratio) + NextDeltaSignalValue * Ratio) * CertaintyMult;
			return;
		}
	}

	Phase = MinMax[LastIndex].Type == LocalMinMax::Min ? 0.f : 0.5f;
	Certainty = CertaintyMin + (LastIndex > 0 ? FMath::Abs(MinMax[LastIndex].SignalValue - MinMax[LastIndex - 1].SignalValue) : 0.f) * CertaintyMult;
}

static void CalculatePhasesFromLocalMinMax(const TArray<LocalMinMax>& MinMax, TArray<FVector2D>& Phases, int32 SignalSize)
{
	Phases.Reset();
	Phases.AddDefaulted(SignalSize);

	float Certainty = 1.f;
	float Phase = 0.f;
	for (int32 i = 0; i < SignalSize; ++i)
	{
		CalculatePhaseAndCertainty(i, MinMax, SignalSize, Phase, Certainty);
		FMath::SinCos(&Phases[i].X, &Phases[i].Y, Phase * TWO_PI);
		Phases[i] *= Certainty;
	}
}

} // namespace UE::PoseSearch

void UPoseSearchFeatureChannel_Pose::InitializeSchema(UPoseSearchSchema* Schema)
{
	using namespace UE::PoseSearch;

	ChannelDataOffset = Schema->SchemaCardinality;
	for (int32 ChannelBoneIdx = 0; ChannelBoneIdx != SampledBones.Num(); ++ChannelBoneIdx)
	{
		const FPoseSearchBone& SampledBone = SampledBones[ChannelBoneIdx];
		if (EnumHasAnyFlags(SampledBone.Flags, EPoseSearchBoneFlags::Position))
		{
			Schema->SchemaCardinality += FFeatureVectorHelper::EncodeVectorCardinality;
		}
		if (EnumHasAnyFlags(SampledBone.Flags, EPoseSearchBoneFlags::Rotation))
		{
			Schema->SchemaCardinality += FFeatureVectorHelper::EncodeQuatCardinality;
		}
		if (EnumHasAnyFlags(SampledBone.Flags, EPoseSearchBoneFlags::Velocity))
		{
			Schema->SchemaCardinality += FFeatureVectorHelper::EncodeVectorCardinality;
		}
		if (EnumHasAnyFlags(SampledBone.Flags, EPoseSearchBoneFlags::Phase))
		{
			Schema->SchemaCardinality += FFeatureVectorHelper::EncodeVector2DCardinality;
		}
	}

	ChannelCardinality = Schema->SchemaCardinality - ChannelDataOffset;

	SchemaBoneIdx.Reset();
	for (const FPoseSearchBone& Bone : SampledBones)
	{
		SchemaBoneIdx.Add(Schema->AddBoneReference(Bone.Reference));
	}
}

void UPoseSearchFeatureChannel_Pose::FillWeights(TArray<float>& Weights) const
{
	using namespace UE::PoseSearch;

	int32 DataOffset = ChannelDataOffset;

	const int32 NumBones = SampledBones.Num();
	for (int32 ChannelBoneIdx = 0; ChannelBoneIdx != NumBones; ++ChannelBoneIdx)
	{
		const FPoseSearchBone& SampledBone = SampledBones[ChannelBoneIdx];
		if (EnumHasAnyFlags(SampledBone.Flags, EPoseSearchBoneFlags::Position))
		{
			for (int32 i = 0; i != FFeatureVectorHelper::EncodeVectorCardinality; ++i)
			{
				Weights[DataOffset + i] = Weight * SampledBone.Weight;
			}
			DataOffset += FFeatureVectorHelper::EncodeVectorCardinality;
		}
		if (EnumHasAnyFlags(SampledBone.Flags, EPoseSearchBoneFlags::Rotation))
		{
			for (int32 i = 0; i != FFeatureVectorHelper::EncodeQuatCardinality; ++i)
			{
				Weights[DataOffset + i] = Weight * SampledBone.Weight;
			}
			DataOffset += FFeatureVectorHelper::EncodeQuatCardinality;
		}
		if (EnumHasAnyFlags(SampledBone.Flags, EPoseSearchBoneFlags::Velocity))
		{
			for (int32 i = 0; i != FFeatureVectorHelper::EncodeVectorCardinality; ++i)
			{
				Weights[DataOffset + i] = Weight * SampledBone.Weight;
			}
			DataOffset += FFeatureVectorHelper::EncodeVectorCardinality;
		}
		if (EnumHasAnyFlags(SampledBone.Flags, EPoseSearchBoneFlags::Phase))
		{
			for (int32 i = 0; i != FFeatureVectorHelper::EncodeVector2DCardinality; ++i)
			{
				Weights[DataOffset + i] = Weight * SampledBone.Weight;
			}
			DataOffset += FFeatureVectorHelper::EncodeVector2DCardinality;
		}
	}

	check(DataOffset == ChannelDataOffset + ChannelCardinality);
}

// @todo: do we really need to use double(s) in all this math?
void UPoseSearchFeatureChannel_Pose::CalculatePhases(UE::PoseSearch::IAssetIndexer& Indexer, UE::PoseSearch::FAssetIndexingOutput& IndexingOutput, TArray<TArray<FVector2D>>& OutPhases) const
{
	// @todo: expose them via UI
	static float BoneSamplingCentralDifferencesTime = 0.2f; // seconds
	static float SmoothingWindowTime = 0.3f; // seconds

	using namespace UE::PoseSearch;
	
	OutPhases.Reset();
	OutPhases.AddDefaulted(SampledBones.Num());
	
	const float FiniteDelta = Indexer.GetIndexingContext().Schema->GetSamplingInterval();

	TArray<float> Signal;
	TArray<float> SmoothedSignal;
	TArray<LocalMinMax> LocalMinMax;
	TArray<FVector> BonePositions;
	for (int32 ChannelBoneIdx = 0; ChannelBoneIdx != SampledBones.Num(); ++ChannelBoneIdx)
	{
		const FPoseSearchBone& SampledBone = SampledBones[ChannelBoneIdx];
		if (EnumHasAnyFlags(SampledBone.Flags, EPoseSearchBoneFlags::Phase))
		{
			CollectBonePositions(BonePositions, Indexer, SchemaBoneIdx[ChannelBoneIdx]);

			// @todo: have different way of calculating signals, for example: height of the bone transform, acceleration, etc?
			const int32 BoneSamplingCentralDifferencesOffset = FMath::Max(FMath::CeilToInt(BoneSamplingCentralDifferencesTime / FiniteDelta), 1);
			CalculateSignal(BonePositions, Signal, BoneSamplingCentralDifferencesOffset);

			const int32 SmoothingWindowOffset = FMath::Max(FMath::CeilToInt(SmoothingWindowTime / FiniteDelta), 1);
			SmoothSignal(Signal, SmoothedSignal, SmoothingWindowOffset);

			FindLocalMinMax(SmoothedSignal, LocalMinMax);
			ValidateLocalMinMax(LocalMinMax);

			ExtrapolateLocalMinMaxBoundaries(LocalMinMax, SmoothedSignal);
			ValidateLocalMinMax(LocalMinMax);
			CalculatePhasesFromLocalMinMax(LocalMinMax, OutPhases[ChannelBoneIdx], SmoothedSignal.Num());
		}
	}
}

void UPoseSearchFeatureChannel_Pose::IndexAsset(UE::PoseSearch::IAssetIndexer& Indexer,  UE::PoseSearch::FAssetIndexingOutput& IndexingOutput) const
{
	using namespace UE::PoseSearch;
	 
	// Phases is an array of array with cardinality SampledBones.Num() times NumSamples (IndexingContext.EndSampleIdx - IndexingContext.BeginSampleIdx)
	// of 2 dimensional vectors (FVector2D) representing phases in an Eucledean space with phase angle sin/cos as direction and certainty of the signal as magnitude,
	// where certainty is a function of the amplitude of the signal used as input
	TArray<TArray<FVector2D>> Phases;
	CalculatePhases(Indexer, IndexingOutput, Phases);

	const FAssetIndexingContext& IndexingContext = Indexer.GetIndexingContext();
	for (int32 SampleIdx = IndexingContext.BeginSampleIdx; SampleIdx != IndexingContext.EndSampleIdx; ++SampleIdx)
	{
		const int32 VectorIdx = SampleIdx - IndexingContext.BeginSampleIdx;
		AddPoseFeatures(Indexer, SampleIdx, IndexingOutput.GetPoseVector(VectorIdx), Phases);
	}
}

void UPoseSearchFeatureChannel_Pose::AddPoseFeatures(UE::PoseSearch::IAssetIndexer& Indexer, int32 SampleIdx, TArrayView<float> FeatureVector, const TArray<TArray<FVector2D>>& Phases) const
{
	// This function samples the instantaneous pose at time t as well as the pose's velocity and acceleration at time t.
	// Symmetric finite differences are used to approximate derivatives:
	//	First symmetric derivative:   f'(t) ~ (f(t+h) - f(t-h)) / 2h
	//	Second symmetric derivative: f''(t) ~ (f(t+h) - 2f(t) + f(t-h)) / h^2
	// Where h is a constant time delta
	// So this means three pose extractions are taken at time t-h, t, and t+h
	
	using namespace UE::PoseSearch;
	using FSampleInfo = IAssetIndexer::FSampleInfo;

	if (SampledBones.IsEmpty())
	{
		return;
	}

	const FAssetIndexingContext& IndexingContext = Indexer.GetIndexingContext();
	const FAssetSamplingContext* SamplingContext = IndexingContext.SamplingContext;

	const float SampleTime = FMath::Min(SampleIdx * IndexingContext.Schema->GetSamplingInterval(), IndexingContext.AssetSampler->GetPlayLength());

	int32 DataOffset = ChannelDataOffset;
	for (int32 ChannelBoneIdx = 0; ChannelBoneIdx != SampledBones.Num(); ++ChannelBoneIdx)
	{
		const FPoseSearchBone& SampledBone = SampledBones[ChannelBoneIdx];

		// Get each bone's component transform, velocity, and acceleration and add accumulated root motion at this time offset
		// Think of this process as freezing the character in place (at SampleTime) and then tracing the paths of their joints
		// as they move through space from past to present to future (at times indicated by PoseSampleTimes).

		if (EnumHasAnyFlags(SampledBone.Flags, EPoseSearchBoneFlags::Position))
		{
			bool ClampedPresent;
			const FTransform BoneTransformsPresent = Indexer.GetTransformAndCacheResults(SampleTime, SampleTime, SchemaBoneIdx[ChannelBoneIdx], ClampedPresent);
			FFeatureVectorHelper::EncodeVector(FeatureVector, DataOffset, BoneTransformsPresent.GetTranslation());
		}

		if (EnumHasAnyFlags(SampledBone.Flags, EPoseSearchBoneFlags::Rotation))
		{
			bool ClampedPresent;
			const FTransform BoneTransformsPresent = Indexer.GetTransformAndCacheResults(SampleTime, SampleTime, SchemaBoneIdx[ChannelBoneIdx], ClampedPresent);
			FFeatureVectorHelper::EncodeQuat(FeatureVector, DataOffset, BoneTransformsPresent.GetRotation());
		}

		if (EnumHasAnyFlags(SampledBone.Flags, EPoseSearchBoneFlags::Velocity))
		{
			bool ClampedPast, ClampedPresent, ClampedFuture;
			const FTransform BoneTransformsPast = Indexer.GetTransformAndCacheResults(SampleTime - SamplingContext->FiniteDelta, UseCharacterSpaceVelocities ? SampleTime - SamplingContext->FiniteDelta : SampleTime, SchemaBoneIdx[ChannelBoneIdx], ClampedPast);
			const FTransform BoneTransformsPresent = Indexer.GetTransformAndCacheResults(SampleTime, SampleTime, SchemaBoneIdx[ChannelBoneIdx], ClampedPresent);
			const FTransform BoneTransformsFuture = Indexer.GetTransformAndCacheResults(SampleTime + SamplingContext->FiniteDelta, UseCharacterSpaceVelocities ? SampleTime + SamplingContext->FiniteDelta : SampleTime, SchemaBoneIdx[ChannelBoneIdx], ClampedFuture);

			// We can get a better finite difference if we ignore samples that have
			// been clamped at either side of the clip. However, if the central sample 
			// itself is clamped, or there are no samples that are clamped, we can just 
			// use the central difference as normal.
			FVector LinearVelocity;
			if (ClampedPast && !ClampedPresent && !ClampedFuture)
			{
				LinearVelocity = (BoneTransformsFuture.GetTranslation() - BoneTransformsPresent.GetTranslation()) / SamplingContext->FiniteDelta;
			}
			else if (ClampedFuture && !ClampedPresent && !ClampedPast)
			{
				LinearVelocity = (BoneTransformsPresent.GetTranslation() - BoneTransformsPast.GetTranslation()) / SamplingContext->FiniteDelta;
			}
			else
			{
				LinearVelocity = (BoneTransformsFuture.GetTranslation() - BoneTransformsPast.GetTranslation()) / (SamplingContext->FiniteDelta * 2.0f);
			}

			FFeatureVectorHelper::EncodeVector(FeatureVector, DataOffset, LinearVelocity);
		}

		if (EnumHasAnyFlags(SampledBone.Flags, EPoseSearchBoneFlags::Phase))
		{
			const int32 VectorIdx = SampleIdx - IndexingContext.BeginSampleIdx;
			FFeatureVectorHelper::EncodeVector2D(FeatureVector, DataOffset, Phases[ChannelBoneIdx][VectorIdx]);
		}
	}

	check(DataOffset == ChannelDataOffset + ChannelCardinality);
}

void UPoseSearchFeatureChannel_Pose::BuildQuery(UE::PoseSearch::FSearchContext& SearchContext, FPoseSearchFeatureVectorBuilder& InOutQuery) const
{
	using namespace UE::PoseSearch;

	const bool bIsCurrentResultValid = SearchContext.CurrentResult.IsValid();
	const bool bSkip = InputQueryPose != EInputQueryPose::UseCharacterPose && bIsCurrentResultValid && SearchContext.CurrentResult.Database->Schema == InOutQuery.GetSchema();
	if (bSkip || !SearchContext.History)
	{
		if (bIsCurrentResultValid)
		{
			const float LerpValue = InputQueryPose == EInputQueryPose::UseInterpolatedContinuingPose ? SearchContext.CurrentResult.LerpValue : 0.f;
			int32 DataOffset = ChannelDataOffset;
			for (int32 SampledBoneIdx = 0; SampledBoneIdx != SampledBones.Num(); ++SampledBoneIdx)
			{
				const FPoseSearchBone& SampledBone = SampledBones[SampledBoneIdx];
				if (EnumHasAnyFlags(SampledBone.Flags, EPoseSearchBoneFlags::Position))
				{
					FFeatureVectorHelper::EncodeVector(InOutQuery.EditValues(), DataOffset, SearchContext.GetCurrentResultPrevPoseVector(), SearchContext.GetCurrentResultPoseVector(), SearchContext.GetCurrentResultNextPoseVector(), LerpValue);
				}

				if (EnumHasAnyFlags(SampledBone.Flags, EPoseSearchBoneFlags::Rotation))
				{
					FFeatureVectorHelper::EncodeQuat(InOutQuery.EditValues(), DataOffset, SearchContext.GetCurrentResultPrevPoseVector(), SearchContext.GetCurrentResultPoseVector(), SearchContext.GetCurrentResultNextPoseVector(), LerpValue);
				}

				if (EnumHasAnyFlags(SampledBone.Flags, EPoseSearchBoneFlags::Velocity))
				{
					FFeatureVectorHelper::EncodeVector(InOutQuery.EditValues(), DataOffset, SearchContext.GetCurrentResultPrevPoseVector(), SearchContext.GetCurrentResultPoseVector(), SearchContext.GetCurrentResultNextPoseVector(), LerpValue);
				}

				if (EnumHasAnyFlags(SampledBone.Flags, EPoseSearchBoneFlags::Phase))
				{
					FFeatureVectorHelper::EncodeVector2D(InOutQuery.EditValues(), DataOffset, SearchContext.GetCurrentResultPrevPoseVector(), SearchContext.GetCurrentResultPoseVector(), SearchContext.GetCurrentResultNextPoseVector(), LerpValue);
				}
			}
		}
		// else leave the InOutQuery set to zero since the SearchContext.History is invalid and it'll fail if we continue
	}
	else
	{
		struct CachedTransforms
		{
			FTransform Current;
			FTransform Previous;
			bool Valid = false; // @todo: remove this
		};
		TArray<CachedTransforms, TInlineAllocator<32>> CachedTransforms;
		CachedTransforms.AddUninitialized(SampledBones.Num());

		float SampleTime = 0.f;

		for (int32 SampledBoneIdx = 0; SampledBoneIdx != SampledBones.Num(); ++SampledBoneIdx)
		{
			CachedTransforms[SampledBoneIdx].Current = SearchContext.TryGetTransformAndCacheResults(SampleTime, InOutQuery.GetSchema(), SchemaBoneIdx[SampledBoneIdx]);
			const FPoseSearchBone& SampledBone = SampledBones[SampledBoneIdx];

			if (EnumHasAnyFlags(SampledBone.Flags, EPoseSearchBoneFlags::Velocity))
			{
				check(SearchContext.History);
				const float HistorySameplInterval = SearchContext.History->GetSampleTimeInterval();

				CachedTransforms[SampledBoneIdx].Previous = SearchContext.TryGetTransformAndCacheResults(SampleTime - HistorySameplInterval, InOutQuery.GetSchema(), SchemaBoneIdx[SampledBoneIdx]);

				if (!UE::PoseSearch::UseCharacterSpaceVelocities)
				{
					const FTransform RootTransform = SearchContext.TryGetTransformAndCacheResults(SampleTime, InOutQuery.GetSchema(), FSearchContext::SchemaRootBoneIdx);
					const FTransform RootTransformPrev = SearchContext.TryGetTransformAndCacheResults(SampleTime - HistorySameplInterval, InOutQuery.GetSchema(), FSearchContext::SchemaRootBoneIdx);

					// animation space velocity
					CachedTransforms[SampledBoneIdx].Previous = CachedTransforms[SampledBoneIdx].Previous * (RootTransformPrev * RootTransform.Inverse());
				}
			}
			CachedTransforms[SampledBoneIdx].Valid = true;
		}

		int32 DataOffset = ChannelDataOffset;
		for (int32 SampledBoneIdx = 0; SampledBoneIdx != SampledBones.Num(); ++SampledBoneIdx)
		{
			const FPoseSearchBone& SampledBone = SampledBones[SampledBoneIdx];
			if (EnumHasAnyFlags(SampledBone.Flags, EPoseSearchBoneFlags::Position))
			{
				if (CachedTransforms[SampledBoneIdx].Valid)
				{
					FFeatureVectorHelper::EncodeVector(InOutQuery.EditValues(), DataOffset, CachedTransforms[SampledBoneIdx].Current.GetTranslation());
				}
				else
				{
					// preserve the InOutQuery.EditValues() and increase the DataOffset
					DataOffset += FFeatureVectorHelper::EncodeVectorCardinality;
				}
			}

			if (EnumHasAnyFlags(SampledBone.Flags, EPoseSearchBoneFlags::Rotation))
			{
				if (CachedTransforms[SampledBoneIdx].Valid)
				{
					FFeatureVectorHelper::EncodeQuat(InOutQuery.EditValues(), DataOffset, CachedTransforms[SampledBoneIdx].Current.GetRotation());
				}
				else
				{
					// preserve the InOutQuery.EditValues() and increase the DataOffset
					DataOffset += FFeatureVectorHelper::EncodeQuatCardinality;
				}
			}

			if (EnumHasAnyFlags(SampledBone.Flags, EPoseSearchBoneFlags::Velocity))
			{
				if (CachedTransforms[SampledBoneIdx].Valid)
				{
					const FVector LinearVelocity = (CachedTransforms[SampledBoneIdx].Current.GetTranslation() - CachedTransforms[SampledBoneIdx].Previous.GetTranslation()) / SearchContext.History->GetSampleTimeInterval();
					FFeatureVectorHelper::EncodeVector(InOutQuery.EditValues(), DataOffset, LinearVelocity);
				}
				else
				{
					// preserve the InOutQuery.EditValues() and increase the DataOffset
					DataOffset += FFeatureVectorHelper::EncodeVectorCardinality;
				}
			}

			if (EnumHasAnyFlags(SampledBone.Flags, EPoseSearchBoneFlags::Phase))
			{
				DataOffset += FFeatureVectorHelper::EncodeVector2DCardinality;

				// @todo: Support phase in BuildQuery
				// FFeatureVectorHelper::EncodeVector2D(InOutQuery.EditValues(), DataOffset, ???);
			}
		}

		check(DataOffset == ChannelDataOffset + ChannelCardinality);
	}
}

void UPoseSearchFeatureChannel_Pose::DebugDraw(const UE::PoseSearch::FDebugDrawParams& DrawParams, TConstArrayView<float> PoseVector) const
{
#if ENABLE_DRAW_DEBUG
	using namespace UE::PoseSearch;

	const UPoseSearchSchema* Schema = DrawParams.GetSchema();
	check(Schema && Schema->IsValid());

	const float LifeTime = DrawParams.DefaultLifeTime;
	const uint8 DepthPriority = ESceneDepthPriorityGroup::SDPG_Foreground + 2;
	const bool bPersistent = EnumHasAnyFlags(DrawParams.Flags, EDebugDrawFlags::Persistent);

	const int32 NumBones = SampledBones.Num();

	if (NumBones == 0)
	{
		return;
	}

	int32 DataOffset = ChannelDataOffset;
	for (int32 ChannelBoneIdx = 0; ChannelBoneIdx != NumBones; ++ChannelBoneIdx)
	{
		const FPoseSearchBone& SampledBone = SampledBones[ChannelBoneIdx];

		FVector BonePos;
		if (EnumHasAnyFlags(SampledBone.Flags, EPoseSearchBoneFlags::Position))
		{
			BonePos = FFeatureVectorHelper::DecodeVector(PoseVector, DataOffset);

			const FColor Color = DrawParams.GetColor(SampledBone.ColorPresetIndex);

			BonePos = DrawParams.RootTransform.TransformPosition(BonePos);
			if (EnumHasAnyFlags(DrawParams.Flags, EDebugDrawFlags::DrawFast | EDebugDrawFlags::DrawSearchIndex))
			{
				DrawDebugPoint(DrawParams.World, BonePos, DrawParams.PointSize, Color, bPersistent, LifeTime, DepthPriority);
			}
			else
			{
				DrawDebugSphere(DrawParams.World, BonePos, 2.f, 8, Color, bPersistent, LifeTime, DepthPriority);
			}

			if (EnumHasAnyFlags(DrawParams.Flags, EDebugDrawFlags::DrawBoneNames))
			{
				DrawDebugString(
					DrawParams.World, BonePos + FVector(0.0, 0.0, 10.0),
					Schema->BoneReferences[SchemaBoneIdx[ChannelBoneIdx]].BoneName.ToString(),
					nullptr, Color, LifeTime, false, 1.0f);
			}
		}
		else
		{
			BonePos = DrawParams.Mesh != nullptr ? DrawParams.Mesh->GetSocketTransform(SampledBones[ChannelBoneIdx].Reference.BoneName).GetLocation() : DrawParams.RootTransform.GetTranslation();
		}

		if (EnumHasAnyFlags(SampledBone.Flags, EPoseSearchBoneFlags::Rotation))
		{
			const FQuat BoneRot = FFeatureVectorHelper::DecodeQuat(PoseVector, DataOffset);
			// @todo: debug draw rotation
		}

		if (EnumHasAnyFlags(SampledBone.Flags, EPoseSearchBoneFlags::Velocity))
		{
			FVector BoneVel = FFeatureVectorHelper::DecodeVector(PoseVector, DataOffset);

			const FColor Color = DrawParams.GetColor(SampledBone.ColorPresetIndex);

			BoneVel *= 0.08f;
			BoneVel = DrawParams.RootTransform.TransformVector(BoneVel);
			FVector BoneVelDirection = BoneVel.GetSafeNormal();

			if (EnumHasAnyFlags(DrawParams.Flags, EDebugDrawFlags::DrawSearchIndex))
			{
				DrawDebugLine(DrawParams.World, BonePos, BonePos + BoneVel, Color, bPersistent, LifeTime, DepthPriority);
			}
			else
			{
				const float AdjustedThickness = EnumHasAnyFlags(DrawParams.Flags, EDebugDrawFlags::DrawFast) ? 0.0f : 1.f;

				DrawDebugLine(
					DrawParams.World,
					BonePos + BoneVelDirection * 2.f,
					BonePos + BoneVel,
					Color,
					bPersistent,
					LifeTime,
					DepthPriority,
					AdjustedThickness);
			}
		}

		if (EnumHasAnyFlags(SampledBone.Flags, EPoseSearchBoneFlags::Phase))
		{
			const FVector2D Phase = FFeatureVectorHelper::DecodeVector2D(PoseVector, DataOffset);

			const FColor Color = DrawParams.GetColor(SampledBone.ColorPresetIndex);

			static float ScaleFactor = 1.f;

			const FVector TransformXAxisVector = DrawParams.RootTransform.TransformVector(FVector::XAxisVector);
			const FVector TransformYAxisVector = DrawParams.RootTransform.TransformVector(FVector::YAxisVector);
			const FVector TransformZAxisVector = DrawParams.RootTransform.TransformVector(FVector::ZAxisVector);

			const FVector PhaseVector = (TransformZAxisVector * Phase.X + TransformYAxisVector * Phase.Y) * ScaleFactor;
			DrawDebugLine(DrawParams.World, BonePos, BonePos + PhaseVector, Color, bPersistent, LifeTime, DepthPriority, 0.f);

			static int32 Segments = 64;
			FMatrix CircleTransform;
			CircleTransform.SetAxes(&TransformXAxisVector, &TransformYAxisVector, &TransformZAxisVector, &BonePos);
			DrawDebugCircle(DrawParams.World, CircleTransform, PhaseVector.Length(), Segments, Color, bPersistent, LifeTime, DepthPriority, 0.f, false);
		}
	}

	check(DataOffset == ChannelDataOffset + ChannelCardinality);
#endif // ENABLE_DRAW_DEBUG
}

#if WITH_EDITOR
void UPoseSearchFeatureChannel_Pose::PopulateChannelLayoutSet(UE::PoseSearch::FFeatureChannelLayoutSet& FeatureChannelLayoutSet) const
{
	using namespace UE::PoseSearch;
	int32 DataOffset = ChannelDataOffset;

	auto Add = [&FeatureChannelLayoutSet, &DataOffset](const FPoseSearchBone& SampledBone, EPoseSearchBoneFlags BoneFlag, const char* Label, int32 Cardinality)
	{
		FString SkeletonName = FeatureChannelLayoutSet.CurrentSchema->Skeleton->GetName();
		FString BoneName = SampledBone.Reference.BoneName.ToString();

		UE::PoseSearch::FKeyBuilder KeyBuilder;
		KeyBuilder << SkeletonName << BoneName << BoneFlag;
		FeatureChannelLayoutSet.Add(FString::Format(TEXT("{0} {1}"), { BoneName, Label }), KeyBuilder.Finalize(), DataOffset, Cardinality);

		DataOffset += Cardinality;
	};

	for (int32 ChannelBoneIdx = 0; ChannelBoneIdx != SampledBones.Num(); ++ChannelBoneIdx)
	{
		const FPoseSearchBone& SampledBone = SampledBones[ChannelBoneIdx];
		if (EnumHasAnyFlags(SampledBone.Flags, EPoseSearchBoneFlags::Position))
		{
			Add(SampledBone, EPoseSearchBoneFlags::Position, "Pos", FFeatureVectorHelper::EncodeVectorCardinality);
		}
		if (EnumHasAnyFlags(SampledBone.Flags, EPoseSearchBoneFlags::Rotation))
		{
			Add(SampledBone, EPoseSearchBoneFlags::Rotation, "Rot", FFeatureVectorHelper::EncodeQuatCardinality);
		}
		if (EnumHasAnyFlags(SampledBone.Flags, EPoseSearchBoneFlags::Velocity))
		{
			Add(SampledBone, EPoseSearchBoneFlags::Velocity, "Vel", FFeatureVectorHelper::EncodeVectorCardinality);
		}
		if (EnumHasAnyFlags(SampledBone.Flags, EPoseSearchBoneFlags::Phase))
		{
			Add(SampledBone, EPoseSearchBoneFlags::Phase, "Pha", FFeatureVectorHelper::EncodeVector2DCardinality);
		}
	}

	check(DataOffset == ChannelDataOffset + ChannelCardinality);
}

void UPoseSearchFeatureChannel_Pose::ComputeCostBreakdowns(UE::PoseSearch::ICostBreakDownData& CostBreakDownData, const UPoseSearchSchema* Schema) const
{
	using namespace UE::PoseSearch;

	CostBreakDownData.AddEntireBreakDownSection(LOCTEXT("ColumnLabelPoseChannelTotal", "Pose Total"), Schema, ChannelDataOffset, ChannelCardinality);

	if (CostBreakDownData.IsVerbose())
	{
		int32 DataOffset = ChannelDataOffset;

		for (int32 ChannelBoneIdx = 0; ChannelBoneIdx != SampledBones.Num(); ++ChannelBoneIdx)
		{
			const FPoseSearchBone& SampledBone = SampledBones[ChannelBoneIdx];
			if (EnumHasAnyFlags(SampledBone.Flags, EPoseSearchBoneFlags::Position))
			{
				CostBreakDownData.AddEntireBreakDownSection(FText::Format(LOCTEXT("ColumnLabelPoseChannelPosition", "{0} Pos"), FText::FromName(SampledBone.Reference.BoneName)), Schema, DataOffset, FFeatureVectorHelper::EncodeVectorCardinality);
				DataOffset += FFeatureVectorHelper::EncodeVectorCardinality;
			}
			if (EnumHasAnyFlags(SampledBone.Flags, EPoseSearchBoneFlags::Rotation))
			{
				CostBreakDownData.AddEntireBreakDownSection(FText::Format(LOCTEXT("ColumnLabelPoseChannelRotation", "{0} Rot"), FText::FromName(SampledBone.Reference.BoneName)), Schema, DataOffset, FFeatureVectorHelper::EncodeQuatCardinality);
				DataOffset += FFeatureVectorHelper::EncodeQuatCardinality;
			}
			if (EnumHasAnyFlags(SampledBone.Flags, EPoseSearchBoneFlags::Velocity))
			{
				CostBreakDownData.AddEntireBreakDownSection(FText::Format(LOCTEXT("ColumnLabelPoseChannelVelocity", "{0} Vel"), FText::FromName(SampledBone.Reference.BoneName)), Schema, DataOffset, FFeatureVectorHelper::EncodeVectorCardinality);
				DataOffset += FFeatureVectorHelper::EncodeVectorCardinality;
			}
			if (EnumHasAnyFlags(SampledBone.Flags, EPoseSearchBoneFlags::Phase))
			{
				CostBreakDownData.AddEntireBreakDownSection(FText::Format(LOCTEXT("ColumnLabelPoseChannelPhase", "{0} Pha"), FText::FromName(SampledBone.Reference.BoneName)), Schema, DataOffset, FFeatureVectorHelper::EncodeVector2DCardinality);
				DataOffset += FFeatureVectorHelper::EncodeVector2DCardinality;
			}
		}

		check(DataOffset == ChannelDataOffset + ChannelCardinality);
	}
}

#endif // WITH_EDITOR

#undef LOCTEXT_NAMESPACE