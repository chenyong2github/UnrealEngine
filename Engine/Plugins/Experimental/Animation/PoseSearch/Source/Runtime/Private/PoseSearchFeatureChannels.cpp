// Copyright Epic Games, Inc. All Rights Reserved.

#include "PoseSearch/PoseSearchFeatureChannels.h"
#include "PoseSearch/PoseSearch.h"
#include "AnimationRuntime.h"
#include "Animation/AnimationPoseData.h"
#include "Animation/AttributesRuntime.h"
#include "Animation/MirrorDataTable.h"
#include "DrawDebugHelpers.h"
#include "UObject/ObjectSaveContext.h"

namespace UE::PoseSearch {

//////////////////////////////////////////////////////////////////////////
// Constants

constexpr float DrawDebugLineThickness = 2.0f;
constexpr float DrawDebugPointSize = 3.0f;
constexpr float DrawDebugVelocityScale = 0.08f;
constexpr float DrawDebugArrowSize = 30.0f;
constexpr float DrawDebugSphereSize = 3.0f;
constexpr int32 DrawDebugSphereSegments = 10;
constexpr float DrawDebugGradientStrength = 0.8f;
constexpr float DrawDebugSampleLabelFontScale = 1.0f;
static const FVector DrawDebugSampleLabelOffset = FVector(0.0f, 0.0f, -10.0f);

constexpr bool UseCharacterSpaceVelocities = true;

//////////////////////////////////////////////////////////////////////////
// Drawing helpers

static FLinearColor GetColorForFeature(FPoseSearchFeatureDesc Feature, const FPoseSearchFeatureVectorLayout* Layout)
{
	const float FeatureIdx = Layout->Features.IndexOfByKey(Feature);
	const float FeatureCountIdx = Layout->Features.Num() - 1;
	const float FeatureCountIdxHalf = FeatureCountIdx / 2.f;
	check(FeatureIdx != (float)INDEX_NONE);

	const float Hue = FeatureIdx < FeatureCountIdxHalf
		? FMath::GetMappedRangeValueUnclamped({ 0.f, FeatureCountIdxHalf }, FVector2f(60.f, 0.f), FeatureIdx)
		: FMath::GetMappedRangeValueUnclamped({ FeatureCountIdxHalf, FeatureCountIdx }, FVector2f(280.f, 220.f), FeatureIdx);

	const FLinearColor ColorHSV(Hue, 1.f, 1.f);
	return ColorHSV.HSVToLinearRGB();
}


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

static void FindLocalMinMax(const TArray<float>& Signal, TArray<LocalMinMax>& MinMax, int32 offset = 1)
{
	check(offset > 0);
	MinMax.Reset();
	for (int32 i = 0; i < Signal.Num(); ++i)
	{
		const float Previous = GetValueAtIndex(i - offset, Signal);
		const float Current = GetValueAtIndex(i, Signal);
		const float Next = GetValueAtIndex(i + offset, Signal);

		const float DeltaSignalValueBackward = Previous - Current;
		const float DeltaSignalValueForward = Next - Current;

		const float Sign = DeltaSignalValueBackward * DeltaSignalValueForward;
		if (Sign >= 0.f && DeltaSignalValueBackward != 0.f)
		{
			LocalMinMax LocalMinMax;
			LocalMinMax.Type = DeltaSignalValueForward < 0.f ? LocalMinMax::Max : LocalMinMax::Min;
			LocalMinMax.Index = i;
			LocalMinMax.SignalValue = Signal[i];

			check(MinMax.IsEmpty() || MinMax.Last().Type != LocalMinMax.Type);
			MinMax.Add(LocalMinMax);
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
			check(MinMax[i].SignalValue < MinMax[i - 1].SignalValue);
		}
		else
		{
			check(MinMax[i].SignalValue > MinMax[i - 1].SignalValue);
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

//////////////////////////////////////////////////////////////////////////
// UPoseSearchFeatureChannel
class USkeleton* UPoseSearchFeatureChannel::GetSkeleton(bool& bInvalidSkeletonIsError, const IPropertyHandle* PropertyHandle)
{
	bInvalidSkeletonIsError = false;

	const UPoseSearchSchema* Schema = Cast<UPoseSearchSchema>(GetOuter());
	return Schema ? Schema->Skeleton : nullptr;
}



//////////////////////////////////////////////////////////////////////////
// UPoseSearchFeatureChannel_Pose

void UPoseSearchFeatureChannel_Pose::PreSave(FObjectPreSaveContext ObjectSaveContext)
{
	SampleTimes.Sort(TLess<>());
	Super::PreSave(ObjectSaveContext);
}

void UPoseSearchFeatureChannel_Pose::InitializeSchema(UE::PoseSearch::FSchemaInitializer& Initializer)
{
	Super::InitializeSchema(Initializer);

	const int32 NumBones = SampledBones.Num();
	for (int32 ChannelBoneIdx = 0; ChannelBoneIdx != NumBones; ++ChannelBoneIdx)
	{
		const FPoseSearchBone& SampledBone = SampledBones[ChannelBoneIdx];
		if (SampledBone.bUsePosition)
		{
			Initializer.AddFeatures(GetChannelIndex(), EPoseSearchFeatureType::Position, ChannelBoneIdx, SampleTimes.Num());
		}
		if (SampledBone.bUseRotation)
		{
			Initializer.AddFeatures(GetChannelIndex(), EPoseSearchFeatureType::Rotation, ChannelBoneIdx, SampleTimes.Num());
		}
		if (SampledBone.bUseVelocity)
		{
			Initializer.AddFeatures(GetChannelIndex(), EPoseSearchFeatureType::LinearVelocity, ChannelBoneIdx, SampleTimes.Num());
		}
		if (SampledBone.bUsePhase)
		{
			Initializer.AddFeatures(GetChannelIndex(), EPoseSearchFeatureType::Phase, ChannelBoneIdx, SampleTimes.Num());
		}
	}

	ChannelCardinality = Initializer.GetCurrentCardinalityFrom(ChannelDataOffset);

	FeatureParams.Reset();
	for (const FPoseSearchBone& Bone : SampledBones)
	{
		FPoseSearchPoseFeatureInfo FeatureInfo;
		FeatureInfo.SchemaBoneIdx = Initializer.AddBoneReference(Bone.Reference);
		FeatureParams.Add(FeatureInfo);
	}
}

// @todo: do we really need to use double(s) in all this math?
void UPoseSearchFeatureChannel_Pose::CalculatePhases(const UE::PoseSearch::IAssetIndexer& Indexer, UE::PoseSearch::FAssetIndexingOutput& IndexingOutput, TArray<TArray<FVector2D>>& OutPhases) const
{
	// @todo: expose them via UI
	static float BoneSamplingCentralDifferencesTime = 0.2f; // seconds
	static float SmoothingWindowTime = 0.3f; // seconds

	using namespace UE::PoseSearch;
	using FSampleInfo = IAssetIndexer::FSampleInfo;

	const FAssetIndexingContext& IndexingContext = Indexer.GetIndexingContext();

	const float SampleTimeStart = FMath::Min(IndexingContext.BeginSampleIdx * IndexingContext.Schema->SamplingInterval, IndexingContext.MainSampler->GetPlayLength());
	const FAssetSamplingContext* SamplingContext = IndexingContext.SamplingContext;
	const float FiniteDelta = IndexingContext.Schema->SamplingInterval;

	const int32 NumSamples = IndexingContext.EndSampleIdx - IndexingContext.BeginSampleIdx;

	TArray<TArray<FVector>> BonePositions;
	BonePositions.AddDefaulted(SampledBones.Num());
	for (int32 ChannelBoneIdx = 0; ChannelBoneIdx != SampledBones.Num(); ++ChannelBoneIdx)
	{
		BonePositions[ChannelBoneIdx].AddDefaulted(NumSamples);
	}

	FDeltaTimeRecord DeltaTimeRecord;
	FCompactPose Pose;
	FCSPose<FCompactPose> ComponentSpacePose;
	FBlendedCurve UnusedCurve;
	UE::Anim::FStackAttributeContainer UnusedAtrribute;
	Pose.SetBoneContainer(&SamplingContext->BoneContainer);

	// collecting all the bone transforms
	FSampleInfo Origin = Indexer.GetSampleInfo(SampleTimeStart);
	for (int32 SampleIdx = 0; SampleIdx != NumSamples; ++SampleIdx)
	{
		const float SampleTime = SampleTimeStart + SampleIdx * FiniteDelta;
		const float PreviousTime = SampleTime - FiniteDelta; // @todo: clamp to zero?

		FSampleInfo Sample = Indexer.GetSampleInfoRelative(SampleTime, Origin);

		DeltaTimeRecord.Set(PreviousTime, SampleTime - PreviousTime);
		FAnimExtractContext ExtractionCtx(static_cast<double>(SampleTime), true, DeltaTimeRecord, Sample.Clip->IsLoopable());
		
		UnusedCurve.InitFrom(SamplingContext->BoneContainer);
		FAnimationPoseData AnimPoseData = { Pose, UnusedCurve, UnusedAtrribute };
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

		ComponentSpacePose.InitPose(Pose);
		for (int32 ChannelBoneIdx = 0; ChannelBoneIdx != SampledBones.Num(); ++ChannelBoneIdx)
		{
			const FPoseSearchPoseFeatureInfo& PoseFeatureInfo = FeatureParams[ChannelBoneIdx];
			const FBoneReference& BoneReference = IndexingContext.Schema->BoneReferences[PoseFeatureInfo.SchemaBoneIdx];
			const FCompactPoseBoneIndex CompactBoneIndex = SamplingContext->BoneContainer.MakeCompactPoseIndex(FMeshPoseBoneIndex(BoneReference.BoneIndex));
			const FTransform BoneTransform = ComponentSpacePose.GetComponentSpaceTransform(CompactBoneIndex) * Indexer.MirrorTransform(Sample.RootTransform);
			BonePositions[ChannelBoneIdx][SampleIdx] = BoneTransform.GetTranslation();
		}
	}

	OutPhases.Reset();
	OutPhases.AddDefaulted(SampledBones.Num());

	const int32 BoneSamplingCentralDifferencesOffset = FMath::Max(FMath::CeilToInt(BoneSamplingCentralDifferencesTime / FiniteDelta), 1);
	const int32 SmoothingWindowOffset = FMath::Max(FMath::CeilToInt(SmoothingWindowTime / FiniteDelta), 1);
	
	TArray<float> Signal;
	TArray<float> SmoothedSignal;
	TArray<LocalMinMax> LocalMinMax;
	for (int32 ChannelBoneIdx = 0; ChannelBoneIdx != SampledBones.Num(); ++ChannelBoneIdx)
	{
		// @todo: have different way of calculating signals, for example: height of the bone transform, acceleration, etc?
		CalculateSignal(BonePositions[ChannelBoneIdx], Signal, BoneSamplingCentralDifferencesOffset);
		
		SmoothSignal(Signal, SmoothedSignal, SmoothingWindowOffset);

		FindLocalMinMax(SmoothedSignal, LocalMinMax);
		ValidateLocalMinMax(LocalMinMax);

		ExtrapolateLocalMinMaxBoundaries(LocalMinMax, SmoothedSignal);
		ValidateLocalMinMax(LocalMinMax);
		CalculatePhasesFromLocalMinMax(LocalMinMax, OutPhases[ChannelBoneIdx], SmoothedSignal.Num());
	}
}

void UPoseSearchFeatureChannel_Pose::IndexAsset(const UE::PoseSearch::IAssetIndexer& Indexer,  UE::PoseSearch::FAssetIndexingOutput& IndexingOutput) const
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
		int32 VectorIdx = SampleIdx - IndexingContext.BeginSampleIdx;
		FPoseSearchFeatureVectorBuilder& FeatureVector = IndexingOutput.PoseVectors[VectorIdx];
		AddPoseFeatures(Indexer, SampleIdx, FeatureVector, Phases);
	}
}

void UPoseSearchFeatureChannel_Pose::AddPoseFeatures(const  UE::PoseSearch::IAssetIndexer& Indexer, int32 SampleIdx, FPoseSearchFeatureVectorBuilder& FeatureVector, const TArray<TArray<FVector2D>>& Phases) const
{
	// This function samples the instantaneous pose at time t as well as the pose's velocity and acceleration at time t.
	// Symmetric finite differences are used to approximate derivatives:
	//	First symmetric derivative:   f'(t) ~ (f(t+h) - f(t-h)) / 2h
	//	Second symmetric derivative: f''(t) ~ (f(t+h) - 2f(t) + f(t-h)) / h^2
	// Where h is a constant time delta
	// So this means three pose extractions are taken at time t-h, t, and t+h
	constexpr int32 NumFiniteDiffTerms = 3;

	using namespace UE::PoseSearch;
	using FSampleInfo = IAssetIndexer::FSampleInfo;

	if (SampledBones.IsEmpty() || SampleTimes.IsEmpty())
	{
		return;
	}

	const FAssetIndexingContext& IndexingContext = Indexer.GetIndexingContext();
	const FAssetSamplingContext* SamplingContext = IndexingContext.SamplingContext;

	FCompactPose Poses[NumFiniteDiffTerms];
	FCSPose<FCompactPose> ComponentSpacePoses[NumFiniteDiffTerms];
	FBlendedCurve UnusedCurves[NumFiniteDiffTerms];
	UE::Anim::FStackAttributeContainer UnusedAtrributes[NumFiniteDiffTerms];

	for (FCompactPose& Pose : Poses)
	{
		Pose.SetBoneContainer(&SamplingContext->BoneContainer);
	}

	for (FBlendedCurve& Curve : UnusedCurves)
	{
		Curve.InitFrom(SamplingContext->BoneContainer);
	}

	FAnimationPoseData AnimPoseData[NumFiniteDiffTerms] =
	{
		{Poses[0], UnusedCurves[0], UnusedAtrributes[0]},
		{Poses[1], UnusedCurves[1], UnusedAtrributes[1]},
		{Poses[2], UnusedCurves[2], UnusedAtrributes[2]},
	};

	FPoseSearchFeatureDesc Feature;
	Feature.ChannelIdx = GetChannelIndex();

	float SampleTime = FMath::Min(SampleIdx * IndexingContext.Schema->SamplingInterval, IndexingContext.MainSampler->GetPlayLength());
	FSampleInfo Origin = Indexer.GetSampleInfo(SampleTime);

	for (int32 SubsampleIdx = 0; SubsampleIdx != SampleTimes.Num(); ++SubsampleIdx)
	{
		Feature.SubsampleIdx = SubsampleIdx;

		float SubsampleTime = SampleTime + SampleTimes[SubsampleIdx];

		// For each pose subsample term, get the corresponding clip, accumulated root motion,
		// and wrap the time parameter based on the clip's length.
		FSampleInfo Samples[NumFiniteDiffTerms];

		if (UseCharacterSpaceVelocities)
		{
			// character space velocity
			Samples[0] = Indexer.GetSampleInfoRelative(SubsampleTime - SamplingContext->FiniteDelta, Indexer.GetSampleInfo(SubsampleTime - SamplingContext->FiniteDelta));
			Samples[1] = Indexer.GetSampleInfoRelative(SubsampleTime, Indexer.GetSampleInfo(SampleTime));
			Samples[2] = Indexer.GetSampleInfoRelative(SubsampleTime + SamplingContext->FiniteDelta, Indexer.GetSampleInfo(SubsampleTime + SamplingContext->FiniteDelta));
		}
		else
		{
			// animation space velocity
			Samples[0] = Indexer.GetSampleInfoRelative(SubsampleTime - SamplingContext->FiniteDelta, Origin);
			Samples[1] = Indexer.GetSampleInfoRelative(SubsampleTime, Origin);
			Samples[2] = Indexer.GetSampleInfoRelative(SubsampleTime + SamplingContext->FiniteDelta, Origin);
		}
		
		// Get pose samples
		for (int32 Term = 0; Term != NumFiniteDiffTerms; ++Term)
		{
			float CurrentTime = Samples[Term].ClipTime;
			float PreviousTime = CurrentTime - SamplingContext->FiniteDelta;

			FDeltaTimeRecord DeltaTimeRecord;
			DeltaTimeRecord.Set(PreviousTime, CurrentTime - PreviousTime);
			FAnimExtractContext ExtractionCtx(CurrentTime, true, DeltaTimeRecord, Samples[Term].Clip->IsLoopable());

			Samples[Term].Clip->ExtractPose(ExtractionCtx, AnimPoseData[Term]);

			if (IndexingContext.bMirrored)
			{
				FAnimationRuntime::MirrorPose(
					AnimPoseData[Term].GetPose(),
					IndexingContext.Schema->MirrorDataTable->MirrorAxis,
					SamplingContext->CompactPoseMirrorBones,
					SamplingContext->ComponentSpaceRefRotations
				);
				// Note curves and attributes are not used during the indexing process and therefore don't need to be mirrored
			}

			ComponentSpacePoses[Term].InitPose(Poses[Term]);
		}

		// Get each bone's component transform, velocity, and acceleration and add accumulated root motion at this time offset
		// Think of this process as freezing the character in place (at SampleTime) and then tracing the paths of their joints
		// as they move through space from past to present to future (at times indicated by PoseSampleTimes).
		for (int32 ChannelBoneIdx = 0; ChannelBoneIdx != SampledBones.Num(); ++ChannelBoneIdx)
		{
			const FPoseSearchPoseFeatureInfo& PoseFeatureInfo = FeatureParams[ChannelBoneIdx];
			const FBoneReference& BoneReference = IndexingContext.Schema->BoneReferences[PoseFeatureInfo.SchemaBoneIdx];

			Feature.ChannelFeatureId = ChannelBoneIdx;

			FCompactPoseBoneIndex CompactBoneIndex = SamplingContext->BoneContainer.MakeCompactPoseIndex(FMeshPoseBoneIndex(BoneReference.BoneIndex));

			FTransform BoneTransforms[NumFiniteDiffTerms];
			for (int32 Term = 0; Term != NumFiniteDiffTerms; ++Term)
			{
				BoneTransforms[Term] = ComponentSpacePoses[Term].GetComponentSpaceTransform(CompactBoneIndex);
				BoneTransforms[Term] = BoneTransforms[Term] * Indexer.MirrorTransform(Samples[Term].RootTransform);
			}

			// Add properties to the feature vector for the pose at SampleIdx
			FeatureVector.SetTransform(Feature, BoneTransforms[1]);

			// We can get a better finite difference if we ignore samples that have
			// been clamped at either side of the clip. However, if the central sample 
			// itself is clamped, or there are no samples that are clamped, we can just 
			// use the central difference as normal.
			if (Samples[0].bClamped && !Samples[1].bClamped && !Samples[2].bClamped)
			{
				FeatureVector.SetTransformVelocity(Feature, BoneTransforms[2], BoneTransforms[1], SamplingContext->FiniteDelta);
			}
			else if (Samples[2].bClamped && !Samples[1].bClamped && !Samples[0].bClamped)
			{
				FeatureVector.SetTransformVelocity(Feature, BoneTransforms[1], BoneTransforms[0], SamplingContext->FiniteDelta);
			}
			else
			{
				FeatureVector.SetTransformVelocity(Feature, BoneTransforms[2], BoneTransforms[1], BoneTransforms[0], SamplingContext->FiniteDelta);
			}

			// @todo: support for SubsampleIdx
			FeatureVector.SetPhase(Feature, Phases[ChannelBoneIdx][SampleIdx]);
		}
	}
}

FFloatRange UPoseSearchFeatureChannel_Pose::GetHorizonRange(EPoseSearchFeatureDomain Domain) const
{
	FFloatRange Extent = FFloatRange::Empty();
	if (Domain == EPoseSearchFeatureDomain::Time)
	{
		if (SampleTimes.Num())
		{
			Extent = FFloatRange::Inclusive(SampleTimes[0], SampleTimes.Last());
		}
	}

	return Extent;
}

void UPoseSearchFeatureChannel_Pose::GenerateDDCKey(FBlake3& InOutKeyHasher) const
{
	InOutKeyHasher.Update(MakeMemoryView(SampledBones));
	InOutKeyHasher.Update(MakeMemoryView(SampleTimes));
}

bool UPoseSearchFeatureChannel_Pose::BuildQuery(
	FPoseSearchContext& SearchContext,
	FPoseSearchFeatureVectorBuilder& InOutQuery) const
{
	const bool bSkip =
		SearchContext.CurrentResult.IsValid() &&
		SearchContext.CurrentResult.Database->Schema == InOutQuery.GetSchema();

	if (bSkip)
	{
		// todo: instead of skipping because the pose should have been copied already (that's currently happening in
		// UPoseSearchSchema::BuildIndex()), consider making the copy here, but only copy the right values, not the
		// whole query
		return true;
	}

	if (!SearchContext.History)
	{
		return false;
	}

	FPoseSearchFeatureDesc Feature;
	Feature.ChannelIdx = GetChannelIndex();

	for (int32 SubsampleIdx = 0; SubsampleIdx != SampleTimes.Num(); ++SubsampleIdx)
	{
		Feature.SubsampleIdx = SubsampleIdx;

		// Stop when we've reached future samples
		float SampleTime = SampleTimes[SubsampleIdx];
		if (SampleTime > 0.0f)
		{
			break;
		}

		float SecondsAgo = -SampleTime;
		if (!SearchContext.History->TrySamplePose(SecondsAgo, InOutQuery.GetSchema()->Skeleton->GetReferenceSkeleton(), InOutQuery.GetSchema()->BoneIndicesWithParents))
		{
			return false;
		}

		TArrayView<const FTransform> ComponentPose = SearchContext.History->GetComponentPoseSample();
		TArrayView<const FTransform> ComponentPrevPose = SearchContext.History->GetPrevComponentPoseSample();
		FTransform RootTransform = SearchContext.History->GetRootTransformSample();
		FTransform RootTransformPrev = SearchContext.History->GetPrevRootTransformSample();
		for (int32 SampledBoneIdx = 0; SampledBoneIdx != SampledBones.Num(); ++SampledBoneIdx)
		{
			Feature.ChannelFeatureId = SampledBoneIdx;

			int32 SchemaBoneIdx = FeatureParams[SampledBoneIdx].SchemaBoneIdx;

			int32 SkeletonBoneIndex = InOutQuery.GetSchema()->BoneIndices[SchemaBoneIdx];

			const FTransform& Transform = ComponentPose[SkeletonBoneIndex];

			FTransform PrevTransform;
			if (UE::PoseSearch::UseCharacterSpaceVelocities)
			{
				// character space velocity
				PrevTransform = ComponentPrevPose[SkeletonBoneIndex];
			}
			else
			{
				// animation space velocity
				PrevTransform = ComponentPrevPose[SkeletonBoneIndex] * (RootTransformPrev * RootTransform.Inverse());
			}
			
			InOutQuery.SetTransform(Feature, Transform);
			InOutQuery.SetTransformVelocity(Feature, Transform, PrevTransform, SearchContext.History->GetSampleTimeInterval());
		}
	}

	return true;
}

void UPoseSearchFeatureChannel_Pose::DebugDraw(const UE::PoseSearch::FDebugDrawParams& DrawParams, const  UE::PoseSearch::FFeatureVectorReader& Reader) const
{
	using namespace UE::PoseSearch;

	const UPoseSearchSchema* Schema = DrawParams.GetSchema();
	check(Schema && Schema->IsValid());

	const float LifeTime = DrawParams.DefaultLifeTime;
	const uint8 DepthPriority = ESceneDepthPriorityGroup::SDPG_Foreground + 2;
	const bool bPersistent = EnumHasAnyFlags(DrawParams.Flags, EDebugDrawFlags::Persistent);

	FPoseSearchFeatureDesc Feature;
	Feature.ChannelIdx = GetChannelIndex();

	const int32 NumSubsamples = SampleTimes.Num();
	const int32 NumBones = SampledBones.Num();

	if ((NumSubsamples * NumBones) == 0)
	{
		return;
	}

	for (int32 SubsampleIdx = 0; SubsampleIdx != NumSubsamples; ++SubsampleIdx)
	{
		Feature.SubsampleIdx = SubsampleIdx;

		for (int32 ChannelBoneIdx = 0; ChannelBoneIdx != NumBones; ++ChannelBoneIdx)
		{
			Feature.ChannelFeatureId = ChannelBoneIdx;

			FVector BonePos;
			bool bHaveBonePos = Reader.GetPosition(Feature, &BonePos);
			if (bHaveBonePos)
			{
				Feature.Type = EPoseSearchFeatureType::Position;

				FLinearColor LinearColor = DrawParams.Color ? *DrawParams.Color : GetColorForFeature(Feature, Reader.GetLayout());
				FColor Color = LinearColor.ToFColor(true);

				BonePos = DrawParams.RootTransform.TransformPosition(BonePos);
				if (EnumHasAnyFlags(DrawParams.Flags, EDebugDrawFlags::DrawFast | EDebugDrawFlags::DrawSearchIndex))
				{
					DrawDebugPoint(DrawParams.World, BonePos, DrawParams.PointSize, Color, bPersistent, DrawParams.DefaultLifeTime, DepthPriority);
				}
				else
				{
					DrawDebugSphere(DrawParams.World, BonePos, DrawDebugSphereSize, DrawDebugSphereSegments, Color, bPersistent, LifeTime, DepthPriority);
				}

				if (EnumHasAnyFlags(DrawParams.Flags, EDebugDrawFlags::DrawBoneNames))
				{
					int32 SchemaBoneIdx = this->FeatureParams[ChannelBoneIdx].SchemaBoneIdx;
					DrawDebugString(
						DrawParams.World, BonePos + FVector(0.0, 0.0, 10.0),
						Schema->BoneReferences[SchemaBoneIdx].BoneName.ToString(),
						nullptr, Color, LifeTime, false, 1.0f);
				}
			}
			else if (DrawParams.Mesh != nullptr)
			{
				BonePos = DrawParams.Mesh->GetSocketTransform(SampledBones[ChannelBoneIdx].Reference.BoneName).GetLocation();
				bHaveBonePos = true;
			}

			FVector BoneVel;
			if (bHaveBonePos && Reader.GetLinearVelocity(Feature, &BoneVel))
			{
				Feature.Type = EPoseSearchFeatureType::LinearVelocity;

				FLinearColor LinearColor = DrawParams.Color ? *DrawParams.Color : GetColorForFeature(Feature, Reader.GetLayout());
				FColor Color = LinearColor.ToFColor(true);

				BoneVel *= DrawDebugVelocityScale;
				BoneVel = DrawParams.RootTransform.TransformVector(BoneVel);
				FVector BoneVelDirection = BoneVel.GetSafeNormal();

				if (EnumHasAnyFlags(DrawParams.Flags, EDebugDrawFlags::DrawSearchIndex))
				{
					DrawDebugPoint(DrawParams.World, BoneVel, DrawParams.PointSize, Color, bPersistent, DrawParams.DefaultLifeTime, DepthPriority);
				}
				else
				{
					const float AdjustedThickness =
						EnumHasAnyFlags(DrawParams.Flags, EDebugDrawFlags::DrawFast) ?
						0.0f : DrawDebugLineThickness;

					DrawDebugDirectionalArrow(
						DrawParams.World,
						BonePos + BoneVelDirection * DrawDebugSphereSize,
						BonePos + BoneVel,
						DrawDebugArrowSize,
						Color,
						bPersistent,
						LifeTime,
						DepthPriority,
						AdjustedThickness);
				}
			}

			FVector2D Phase;
			if (bHaveBonePos && Reader.GetPhase(Feature, &Phase))
			{
				Feature.Type = EPoseSearchFeatureType::Phase;

				FLinearColor LinearColor = DrawParams.Color ? *DrawParams.Color : GetColorForFeature(Feature, Reader.GetLayout());
				FColor Color = LinearColor.ToFColor(true);

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
	}
}



//////////////////////////////////////////////////////////////////////////
// UPoseSearchFeatureChannel_Trajectory
void UPoseSearchFeatureChannel_Trajectory::PreSave(FObjectPreSaveContext ObjectSaveContext)
{
	SampleOffsets.Sort(TLess<>());

	Super::PreSave(ObjectSaveContext);
}

void UPoseSearchFeatureChannel_Trajectory::InitializeSchema( UE::PoseSearch::FSchemaInitializer& Initializer)
{
	Super::InitializeSchema(Initializer);

	if (bUsePositions)
	{
		Initializer.AddFeatures(GetChannelIndex(), EPoseSearchFeatureType::Position, 0, SampleOffsets.Num());
	}

	if (bUseLinearVelocities)
	{
		Initializer.AddFeatures(GetChannelIndex(), EPoseSearchFeatureType::LinearVelocity, 0, SampleOffsets.Num());
	}

	if (bUseFacingDirections)
	{
		Initializer.AddFeatures(GetChannelIndex(), EPoseSearchFeatureType::ForwardVector, 0, SampleOffsets.Num());
	}

	ChannelCardinality = Initializer.GetCurrentCardinalityFrom(ChannelDataOffset);
}

void UPoseSearchFeatureChannel_Trajectory::IndexAsset(const UE::PoseSearch::IAssetIndexer& Indexer,  UE::PoseSearch::FAssetIndexingOutput& IndexingOutput) const
{
	using namespace UE::PoseSearch;

	auto IndexFeatures = [&Indexer, &IndexingOutput](auto FeatureIndexingFunc)
	{
		const FAssetIndexingContext& IndexingContext = Indexer.GetIndexingContext();
		for (int32 SampleIdx = IndexingContext.BeginSampleIdx; SampleIdx != IndexingContext.EndSampleIdx; ++SampleIdx)
		{
			int32 VectorIdx = SampleIdx - IndexingContext.BeginSampleIdx;
			FPoseSearchFeatureVectorBuilder& FeatureVector = IndexingOutput.PoseVectors[VectorIdx];
			FeatureIndexingFunc(Indexer, SampleIdx, FeatureVector);
		}
	};

	switch (Domain)
	{
		case EPoseSearchFeatureDomain::Time:
			IndexFeatures([this](auto&&... Args){ return IndexTimeFeatures(Args...); });
			break;

		case EPoseSearchFeatureDomain::Distance:
			IndexFeatures([this](auto&&... Args){ return IndexDistanceFeatures(Args...); });
			break;

		default:
			checkNoEntry();
	}
}

void UPoseSearchFeatureChannel_Trajectory::IndexTimeFeatures(const UE::PoseSearch::IAssetIndexer& Indexer, int32 SampleIdx,  FPoseSearchFeatureVectorBuilder& FeatureVector) const
{
	// This function samples the instantaneous trajectory at time t as well as the trajectory's velocity and acceleration at time t.
	// Symmetric finite differences are used to approximate derivatives:
	//	First symmetric derivative:   f'(t) ~ (f(t+h) - f(t-h)) / 2h
	//	Second symmetric derivative: f''(t) ~ (f(t+h) - 2f(t) + f(t-h)) / h^2
	// Where h is a constant time delta
	// So this means three root motion extractions are taken at time t-h, t, and t+h

	using namespace UE::PoseSearch;
	using FSampleInfo = IAssetIndexer::FSampleInfo;

	const FAssetIndexingContext& IndexingContext = Indexer.GetIndexingContext();


	FPoseSearchFeatureDesc Feature;
	Feature.ChannelIdx = GetChannelIndex();
	Feature.ChannelFeatureId = 0; // Unused

	float SampleTime = FMath::Min(SampleIdx * IndexingContext.Schema->SamplingInterval, IndexingContext.MainSampler->GetPlayLength());
	FSampleInfo Origin = Indexer.GetSampleInfo(SampleTime);

	for (int32 SubsampleIdx = 0; SubsampleIdx != SampleOffsets.Num(); ++SubsampleIdx)
	{
		Feature.SubsampleIdx = SubsampleIdx;

		float SubsampleTime = SampleTime + SampleOffsets[SubsampleIdx];

		// For each pose subsample term, get the corresponding clip, accumulated root motion,
		// and wrap the time parameter based on the clip's length.
		FSampleInfo Samples[3];
		Samples[0] = Indexer.GetSampleInfoRelative(SubsampleTime - IndexingContext.SamplingContext->FiniteDelta, Origin);
		Samples[1] = Indexer.GetSampleInfoRelative(SubsampleTime, Origin);
		Samples[2] = Indexer.GetSampleInfoRelative(SubsampleTime + IndexingContext.SamplingContext->FiniteDelta, Origin);

		// Mirror transforms if requested
		FTransform MirroredRoots[3];
		MirroredRoots[0] = Indexer.MirrorTransform(Samples[0].RootTransform);
		MirroredRoots[1] = Indexer.MirrorTransform(Samples[1].RootTransform);
		MirroredRoots[2] = Indexer.MirrorTransform(Samples[2].RootTransform);

		// Add properties to the feature vector for the pose at SampleIdx
		FeatureVector.SetTransform(Feature, MirroredRoots[1]);

		// We can get a better finite difference if we ignore samples that have
		// been clamped at either side of the clip. However, if the central sample 
		// itself is clamped, or there are no samples that are clamped, we can just 
		// use the central difference as normal.
		if (Samples[0].bClamped && !Samples[1].bClamped && !Samples[2].bClamped)
		{
			FeatureVector.SetTransformVelocity(Feature, MirroredRoots[2], MirroredRoots[1], IndexingContext.SamplingContext->FiniteDelta);
		}
		else if (Samples[2].bClamped && !Samples[1].bClamped && !Samples[0].bClamped)
		{
			FeatureVector.SetTransformVelocity(Feature, MirroredRoots[1], MirroredRoots[0], IndexingContext.SamplingContext->FiniteDelta);
		}
		else
		{
			FeatureVector.SetTransformVelocity(Feature, MirroredRoots[2], MirroredRoots[1], MirroredRoots[0], IndexingContext.SamplingContext->FiniteDelta);
		}
	}
}

void UPoseSearchFeatureChannel_Trajectory::IndexDistanceFeatures(const  UE::PoseSearch::IAssetIndexer& Indexer, int32 SampleIdx, FPoseSearchFeatureVectorBuilder& FeatureVector) const
{
	// This function is very similar to AddTrajectoryTimeFeatures, but samples are taken in the distance domain
	// instead of time domain.

	using namespace UE::PoseSearch;
	using FSampleInfo = IAssetIndexer::FSampleInfo;

	const FAssetIndexingContext& IndexingContext = Indexer.GetIndexingContext();


	FPoseSearchFeatureDesc Feature;
	Feature.ChannelIdx = GetChannelIndex();
	Feature.ChannelFeatureId = 0; // Unused

	float SampleTime = FMath::Min(SampleIdx * IndexingContext.Schema->SamplingInterval, IndexingContext.MainSampler->GetPlayLength());
	FSampleInfo Origin = Indexer.GetSampleInfo(SampleTime);

	FQuat RootReferenceRot = IndexingContext.SamplingContext->BoneContainer.GetRefPoseTransform(FCompactPoseBoneIndex(0)).GetRotation();
	for (int32 SubsampleIdx = 0; SubsampleIdx != SampleOffsets.Num(); ++SubsampleIdx)
	{
		Feature.SubsampleIdx = SubsampleIdx;

		// For distance based sampling of the trajectory, we first have to look up the time value
		// we're sampling given the desired travel distance of the root for this distance offset.
		// Once we know the time, we can then carry on just like time-based sampling.
		const float SubsampleDistance = Origin.RootDistance + SampleOffsets[SubsampleIdx];
		float SubsampleTime = Indexer.GetSampleTimeFromDistance(SubsampleDistance);

		// For each pose subsample term, get the corresponding clip, accumulated root motion,
		// and wrap the time parameter based on the clip's length.
		FSampleInfo Samples[3];
		Samples[0] = Indexer.GetSampleInfoRelative(SubsampleTime - IndexingContext.SamplingContext->FiniteDelta, Origin);
		Samples[1] = Indexer.GetSampleInfoRelative(SubsampleTime, Origin);
		Samples[2] = Indexer.GetSampleInfoRelative(SubsampleTime + IndexingContext.SamplingContext->FiniteDelta, Origin);

		// Mirror transforms if requested
		FTransform MirroredRoots[3];
		MirroredRoots[0] = Indexer.MirrorTransform(Samples[0].RootTransform);
		MirroredRoots[1] = Indexer.MirrorTransform(Samples[1].RootTransform);
		MirroredRoots[2] = Indexer.MirrorTransform(Samples[2].RootTransform);

		// Add properties to the feature vector for the pose at SampleIdx
		FeatureVector.SetTransform(Feature, MirroredRoots[1]);

		// We can get a better finite difference if we ignore samples that have
		// been clamped at either side of the clip. However, if the central sample 
		// itself is clamped, or there are no samples that are clamped, we can just 
		// use the central difference as normal.
		if (Samples[0].bClamped && !Samples[1].bClamped && !Samples[2].bClamped)
		{
			FeatureVector.SetTransformVelocity(Feature, MirroredRoots[2], MirroredRoots[1], IndexingContext.SamplingContext->FiniteDelta);
		}
		else if (Samples[2].bClamped && !Samples[1].bClamped && !Samples[0].bClamped)
		{
			FeatureVector.SetTransformVelocity(Feature, MirroredRoots[1], MirroredRoots[0], IndexingContext.SamplingContext->FiniteDelta);
		}
		else
		{
			FeatureVector.SetTransformVelocity(Feature, MirroredRoots[2], MirroredRoots[1], MirroredRoots[0], IndexingContext.SamplingContext->FiniteDelta);
		}
	}
}

FFloatRange UPoseSearchFeatureChannel_Trajectory::GetHorizonRange(EPoseSearchFeatureDomain InDomain) const
{
	FFloatRange Extent = FFloatRange::Empty();
	if (InDomain == Domain)
	{
		if (SampleOffsets.Num())
		{
			Extent = FFloatRange::Inclusive(SampleOffsets[0], SampleOffsets.Last());
		}
	}

	return Extent;
}

void UPoseSearchFeatureChannel_Trajectory::GenerateDDCKey(FBlake3& InOutKeyHasher) const
{
	InOutKeyHasher.Update(&bUseLinearVelocities, sizeof(bUseLinearVelocities));
	InOutKeyHasher.Update(&bUsePositions, sizeof(bUsePositions));
	InOutKeyHasher.Update(&bUseFacingDirections, sizeof(bUseFacingDirections));
	InOutKeyHasher.Update(&Domain, sizeof(Domain));
	InOutKeyHasher.Update(MakeMemoryView(SampleOffsets));
}

bool UPoseSearchFeatureChannel_Trajectory::BuildQuery(
	FPoseSearchContext& SearchContext,
	FPoseSearchFeatureVectorBuilder& InOutQuery) const
{
	if (!SearchContext.Trajectory)
	{
		return false;
	}

	ETrajectorySampleDomain SampleDomain;
	switch (Domain)
	{
		case EPoseSearchFeatureDomain::Time:
			SampleDomain = ETrajectorySampleDomain::Time;
			break;

		case EPoseSearchFeatureDomain::Distance:
			SampleDomain = ETrajectorySampleDomain::Distance;
			break;

		default:
			checkNoEntry();
			return false;
	}

	FPoseSearchFeatureDesc Feature;
	Feature.ChannelIdx = GetChannelIndex();

	for (int32 Idx = 0, NextIterStartIdx = 0, Num = SampleOffsets.Num(); Idx < Num; ++Idx)
	{
		const float SampleOffset = SampleOffsets[Idx];
		const FTrajectorySample Sample = FTrajectorySampleRange::IterSampleTrajectory(
			SearchContext.Trajectory->Samples,
			SampleDomain, 
			SampleOffset, 
			NextIterStartIdx);

		Feature.SubsampleIdx = Idx;

		Feature.Type = EPoseSearchFeatureType::LinearVelocity;
		InOutQuery.SetVector(Feature, Sample.LinearVelocity);

		InOutQuery.SetTransform(Feature, Sample.Transform);
	}

	return true;
}

void UPoseSearchFeatureChannel_Trajectory::DebugDraw(const UE::PoseSearch::FDebugDrawParams& DrawParams, const UE::PoseSearch::FFeatureVectorReader& Reader) const
{
	using namespace UE::PoseSearch;

	const float LifeTime = DrawParams.DefaultLifeTime;
	const uint8 DepthPriority = ESceneDepthPriorityGroup::SDPG_Foreground + 2;
	const bool bPersistent = EnumHasAnyFlags(DrawParams.Flags, EDebugDrawFlags::Persistent);

	FPoseSearchFeatureDesc Feature;
	Feature.ChannelIdx = GetChannelIndex();

	const int32 NumSubsamples = SampleOffsets.Num();
	if (NumSubsamples == 0)
	{
		return;
	}

	auto GetGradientColor = [](const FLinearColor& OriginalColor, int SampleIdx, int NumSamples, EDebugDrawFlags Flags) -> FLinearColor
	{
		int Denominator = NumSamples - 1;
		if (Denominator <= 0 || !EnumHasAnyFlags(Flags, EDebugDrawFlags::DrawSamplesWithColorGradient))
		{
			return OriginalColor;
		}

		return OriginalColor * (1.0f - DrawDebugGradientStrength * (SampleIdx / (float)Denominator));
	};

	for (int32 SubsampleIdx = 0; SubsampleIdx != NumSubsamples; ++SubsampleIdx)
	{
		Feature.SubsampleIdx = SubsampleIdx;

		FVector TrajectoryPos;
		if (Reader.GetPosition(Feature, &TrajectoryPos))
		{
			Feature.Type = EPoseSearchFeatureType::Position;

			FLinearColor LinearColor = DrawParams.Color ? *DrawParams.Color : GetColorForFeature(Feature, Reader.GetLayout());
			FLinearColor GradientColor = GetGradientColor(LinearColor, SubsampleIdx, NumSubsamples, DrawParams.Flags);
			FColor Color = GradientColor.ToFColor(true);

			TrajectoryPos = DrawParams.RootTransform.TransformPosition(TrajectoryPos);
			if (EnumHasAnyFlags(DrawParams.Flags, EDebugDrawFlags::DrawFast | EDebugDrawFlags::DrawSearchIndex))
			{
				DrawDebugPoint(DrawParams.World, TrajectoryPos, DrawParams.PointSize, Color, bPersistent, DrawParams.DefaultLifeTime, DepthPriority);
			}
			else
			{
				DrawDebugSphere(DrawParams.World, TrajectoryPos, DrawDebugSphereSize, DrawDebugSphereSegments, Color, bPersistent, LifeTime, DepthPriority);
			}
		}
		else
		{
			TrajectoryPos = DrawParams.RootTransform.GetTranslation();
		}

		FVector TrajectoryVel;
		if (Reader.GetLinearVelocity(Feature, &TrajectoryVel))
		{
			Feature.Type = EPoseSearchFeatureType::LinearVelocity;

			FLinearColor LinearColor = DrawParams.Color ? *DrawParams.Color : GetColorForFeature(Feature, Reader.GetLayout());
			FLinearColor GradientColor = GetGradientColor(LinearColor, SubsampleIdx, NumSubsamples, DrawParams.Flags);
			FColor Color = GradientColor.ToFColor(true);

			TrajectoryVel *= DrawDebugVelocityScale;
			TrajectoryVel = DrawParams.RootTransform.TransformVector(TrajectoryVel);
			FVector TrajectoryVelDirection = TrajectoryVel.GetSafeNormal();


			if (EnumHasAnyFlags(DrawParams.Flags, EDebugDrawFlags::DrawSearchIndex))
			{
				DrawDebugPoint(DrawParams.World, TrajectoryVel, DrawParams.PointSize, Color, bPersistent, DrawParams.DefaultLifeTime, DepthPriority);
			}
			else
			{
				const float AdjustedThickness =
					EnumHasAnyFlags(DrawParams.Flags, EDebugDrawFlags::DrawFast) ?
					0.0f : DrawDebugLineThickness;

				DrawDebugDirectionalArrow(
					DrawParams.World,
					TrajectoryPos + TrajectoryVelDirection * DrawDebugSphereSize,
					TrajectoryPos + TrajectoryVel,
					DrawDebugArrowSize,
					Color,
					bPersistent,
					LifeTime,
					DepthPriority,
					AdjustedThickness
				);
			}
		}

		FVector TrajectoryForward;
		if (Reader.GetForwardVector(Feature, &TrajectoryForward))
		{
			Feature.Type = EPoseSearchFeatureType::ForwardVector;

			FLinearColor LinearColor = DrawParams.Color ? *DrawParams.Color : GetColorForFeature(Feature, Reader.GetLayout());
			FLinearColor GradientColor = GetGradientColor(LinearColor, SubsampleIdx, NumSubsamples, DrawParams.Flags);
			FColor Color = GradientColor.ToFColor(true);

			TrajectoryForward = DrawParams.RootTransform.TransformVector(TrajectoryForward);

			if (EnumHasAnyFlags(DrawParams.Flags, EDebugDrawFlags::DrawSearchIndex))
			{
				DrawDebugPoint(DrawParams.World, TrajectoryForward, DrawParams.PointSize, Color, bPersistent, DrawParams.DefaultLifeTime, DepthPriority);
			}
			else
			{
				const float AdjustedThickness =
					EnumHasAnyFlags(DrawParams.Flags, EDebugDrawFlags::DrawFast) ?
					0.0f : DrawDebugLineThickness;

				DrawDebugDirectionalArrow(
					DrawParams.World,
					TrajectoryPos + TrajectoryForward * DrawDebugSphereSize,
					TrajectoryPos + TrajectoryForward * DrawDebugSphereSize * 2.0f,
					DrawDebugArrowSize,
					Color,
					bPersistent,
					LifeTime,
					DepthPriority,
					AdjustedThickness
				);
			}
		}

		if (EnumHasAnyFlags(DrawParams.Flags, EDebugDrawFlags::DrawSampleLabels))
		{
			FLinearColor LinearColor = DrawParams.Color ? *DrawParams.Color : GetColorForFeature(Feature, Reader.GetLayout());
			FLinearColor GradientColor = GetGradientColor(LinearColor, SubsampleIdx, NumSubsamples, DrawParams.Flags);
			FColor Color = GradientColor.ToFColor(true);

			FString SampleLabel;
			if (DrawParams.LabelPrefix.IsEmpty())
			{
				SampleLabel = FString::Format(TEXT("{0}"), { SubsampleIdx });
			}
			else
			{
				SampleLabel = FString::Format(TEXT("{1}[{0}]"), { SubsampleIdx, DrawParams.LabelPrefix.GetData() });
			}
			DrawDebugString(
				DrawParams.World,
				TrajectoryPos + DrawDebugSampleLabelOffset,
				SampleLabel,
				nullptr,
				Color,
				LifeTime,
				false,
				DrawDebugSampleLabelFontScale);
		}
	}
}