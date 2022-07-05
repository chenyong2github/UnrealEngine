// Copyright Epic Games, Inc. All Rights Reserved.

#include "PoseSearch/PoseSearchFeatureChannels.h"
#include "AnimationRuntime.h"
#include "Animation/AnimationPoseData.h"
#include "Animation/AttributesRuntime.h"
#include "Animation/MirrorDataTable.h"
#include "DrawDebugHelpers.h"
#include "PoseSearchEigenHelper.h"
#include "UObject/ObjectSaveContext.h"

namespace UE::PoseSearch {

//////////////////////////////////////////////////////////////////////////
// Constants

constexpr float DrawDebugLineThickness = 1.0f;
constexpr float DrawDebugPointSize = 2.0f;
constexpr float DrawDebugVelocityScale = 0.08f;
constexpr float DrawDebugSphereSize = 2.0f;
constexpr int32 DrawDebugSphereSegments = 8;
constexpr float DrawDebugGradientStrength = 0.8f;
constexpr float DrawDebugSampleLabelFontScale = 1.5f;
static const FVector DrawDebugSampleLabelOffset = FVector(0.0f, 0.0f, 5.0f);

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
	const float FiniteDelta = IndexingContext.Schema->SamplingInterval;
	const float SampleTimeStart = FMath::Min(IndexingContext.BeginSampleIdx * FiniteDelta, IndexingContext.MainSampler->GetPlayLength());
	const int32 NumSamples = IndexingContext.EndSampleIdx - IndexingContext.BeginSampleIdx;

	// collecting all the bone transforms
	BonePositions.Reset();
	BonePositions.AddDefaulted(NumSamples);
	for (int32 SampleIdx = 0; SampleIdx != NumSamples; ++SampleIdx)
	{
		const float SampleTime = SampleTimeStart + SampleIdx * FiniteDelta;
		bool Unused;
		BonePositions[SampleIdx] = Indexer.GetTransformAndCacheResults(SampleTime, SampleTimeStart, SchemaBoneIdx, Unused).GetTranslation();
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
// UPoseSearchFeatureChannel_Position

void UPoseSearchFeatureChannel_Position::InitializeSchema(UE::PoseSearch::FSchemaInitializer& Initializer)
{
	Super::InitializeSchema(Initializer);
	ChannelCardinality = UE::PoseSearch::FFeatureVectorHelper::EncodeVectorCardinality;
	Initializer.SetCurrentChannelDataOffset(ChannelDataOffset + ChannelCardinality);
	SchemaBoneIdx = Initializer.AddBoneReference(Bone);
}

void UPoseSearchFeatureChannel_Position::FillWeights(TArray<float>& Weights) const
{
	using namespace UE::PoseSearch;

	for (int32 i = 0; i != FFeatureVectorHelper::EncodeVectorCardinality; ++i)
	{
		Weights[ChannelDataOffset + i] = Weight;
	}
}

void UPoseSearchFeatureChannel_Position::IndexAsset(UE::PoseSearch::IAssetIndexer& Indexer, UE::PoseSearch::FAssetIndexingOutput& IndexingOutput) const
{
	using namespace UE::PoseSearch;

	const FAssetIndexingContext& IndexingContext = Indexer.GetIndexingContext();
	const FAssetSamplingContext* SamplingContext = IndexingContext.SamplingContext;

	for (int32 SampleIdx = IndexingContext.BeginSampleIdx; SampleIdx != IndexingContext.EndSampleIdx; ++SampleIdx)
	{
		int32 VectorIdx = SampleIdx - IndexingContext.BeginSampleIdx;
		FPoseSearchFeatureVectorBuilder& FeatureVector = IndexingOutput.PoseVectors[VectorIdx];

		const float OriginSampleTime = FMath::Min(SampleIdx * IndexingContext.Schema->SamplingInterval, IndexingContext.MainSampler->GetPlayLength());
		const float SubsampleTime = OriginSampleTime + SampleTimeOffset;

		bool ClampedPresent;
		const FTransform BoneTransformsPresent = Indexer.GetTransformAndCacheResults(SubsampleTime, UseCharacterSpaceVelocities ? SubsampleTime : OriginSampleTime, SchemaBoneIdx, ClampedPresent);
		int32 DataOffset = ChannelDataOffset;
		FFeatureVectorHelper::EncodeVector(FeatureVector.EditValues(), DataOffset, BoneTransformsPresent.GetTranslation());
	}
}

void UPoseSearchFeatureChannel_Position::GenerateDDCKey(FBlake3& InOutKeyHasher) const
{
	InOutKeyHasher.Update(&Bone, sizeof(Bone));
	InOutKeyHasher.Update(&Weight, sizeof(Weight));
	InOutKeyHasher.Update(&SampleTimeOffset, sizeof(SampleTimeOffset));
}

bool UPoseSearchFeatureChannel_Position::BuildQuery(FPoseSearchContext& SearchContext, FPoseSearchFeatureVectorBuilder& InOutQuery) const
{
	using namespace UE::PoseSearch;

	const bool bSkip = SearchContext.CurrentResult.IsValid() && SearchContext.CurrentResult.Database->Schema == InOutQuery.GetSchema();

	if (bSkip)
	{
		// todo: instead of skipping because the pose should have been copied already (that's currently happening in
		// UPoseSearchSchema::BuildIndex()), consider making the copy here, but only copy the right values, not the
		// whole query
		return true;
	}

	// @todo: add a layer of caching to TrySamplePose to improve performance
	if (!SearchContext.History || !SearchContext.History->TrySamplePose(-SampleTimeOffset, InOutQuery.GetSchema()->Skeleton->GetReferenceSkeleton(), InOutQuery.GetSchema()->BoneIndicesWithParents))
	{
		return false;
	}

	const TArrayView<const FTransform> ComponentPose = SearchContext.History->GetComponentPoseSample();
	const FTransform RootTransform = SearchContext.History->GetRootTransformSample();
	const FTransform RootTransformPrev = SearchContext.History->GetPrevRootTransformSample();
	const int32 SkeletonBoneIndex = InOutQuery.GetSchema()->BoneIndices[SchemaBoneIdx];
	
	int32 DataOffset = ChannelDataOffset;
	FFeatureVectorHelper::EncodeVector(InOutQuery.EditValues(), DataOffset, ComponentPose[SkeletonBoneIndex].GetTranslation());
	
	return true;
}

void UPoseSearchFeatureChannel_Position::DebugDraw(const UE::PoseSearch::FDebugDrawParams& DrawParams, TArrayView<const float> PoseVector) const
{
	using namespace UE::PoseSearch;

	const UPoseSearchSchema* Schema = DrawParams.GetSchema();
	check(Schema && Schema->IsValid());

	const float LifeTime = DrawParams.DefaultLifeTime;
	const uint8 DepthPriority = ESceneDepthPriorityGroup::SDPG_Foreground + 2;
	const bool bPersistent = EnumHasAnyFlags(DrawParams.Flags, EDebugDrawFlags::Persistent);

	int32 DataOffset = ChannelDataOffset;
	const FVector BonePos = DrawParams.RootTransform.TransformPosition(FFeatureVectorHelper::DecodeVector(PoseVector, DataOffset));

	const FLinearColor LinearColor = DrawParams.GetColor(this);
	const FColor Color = LinearColor.ToFColor(true);

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
		DrawDebugString(DrawParams.World, BonePos + FVector(0.0, 0.0, 10.0), Schema->BoneReferences[SchemaBoneIdx].BoneName.ToString(), nullptr, Color, LifeTime, false, 1.0f);
	}
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
	using namespace UE::PoseSearch;

	Super::InitializeSchema(Initializer);

	int32 DataOffset = ChannelDataOffset;
	for (int32 ChannelBoneIdx = 0; ChannelBoneIdx != SampledBones.Num(); ++ChannelBoneIdx)
	{
		const FPoseSearchBone& SampledBone = SampledBones[ChannelBoneIdx];
		if (SampledBone.bUsePosition)
		{
			for (int32 SubsampleIdx = 0; SubsampleIdx != SampleTimes.Num(); ++SubsampleIdx)
			{
				DataOffset += FFeatureVectorHelper::EncodeVectorCardinality;
			}
		}
		if (SampledBone.bUseRotation)
		{
			for (int32 SubsampleIdx = 0; SubsampleIdx != SampleTimes.Num(); ++SubsampleIdx)
			{
				DataOffset += FFeatureVectorHelper::EncodeQuatCardinality;
			}
		}
		if (SampledBone.bUseVelocity)
		{
			for (int32 SubsampleIdx = 0; SubsampleIdx != SampleTimes.Num(); ++SubsampleIdx)
			{
				DataOffset += FFeatureVectorHelper::EncodeVectorCardinality;
			}
		}
		if (SampledBone.bUsePhase)
		{
			for (int32 SubsampleIdx = 0; SubsampleIdx != SampleTimes.Num(); ++SubsampleIdx)
			{
				DataOffset += FFeatureVectorHelper::EncodeVector2DCardinality;
			}
		}
	}

	ChannelCardinality = DataOffset - ChannelDataOffset;

	Initializer.SetCurrentChannelDataOffset(DataOffset);

	SchemaBoneIdx.Reset();
	for (const FPoseSearchBone& Bone : SampledBones)
	{
		SchemaBoneIdx.Add(Initializer.AddBoneReference(Bone.Reference));
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
		if (SampledBone.bUsePosition)
		{
			for (int32 SubsampleIdx = 0; SubsampleIdx != SampleTimes.Num(); ++SubsampleIdx)
			{
				for (int32 i = 0; i != FFeatureVectorHelper::EncodeVectorCardinality; ++i)
				{
					Weights[DataOffset + i] = SampledBone.Weight;
				}
				DataOffset += FFeatureVectorHelper::EncodeVectorCardinality;
			}
		}
		if (SampledBone.bUseRotation)
		{
			for (int32 SubsampleIdx = 0; SubsampleIdx != SampleTimes.Num(); ++SubsampleIdx)
			{
				for (int32 i = 0; i != FFeatureVectorHelper::EncodeQuatCardinality; ++i)
				{
					Weights[DataOffset + i] = SampledBone.Weight;
				}
				DataOffset += FFeatureVectorHelper::EncodeQuatCardinality;
			}
		}
		if (SampledBone.bUseVelocity)
		{
			for (int32 SubsampleIdx = 0; SubsampleIdx != SampleTimes.Num(); ++SubsampleIdx)
			{
				for (int32 i = 0; i != FFeatureVectorHelper::EncodeVectorCardinality; ++i)
				{
					Weights[DataOffset + i] = SampledBone.Weight;
				}
				DataOffset += FFeatureVectorHelper::EncodeVectorCardinality;
			}
		}
		if (SampledBone.bUsePhase)
		{
			for (int32 SubsampleIdx = 0; SubsampleIdx != SampleTimes.Num(); ++SubsampleIdx)
			{
				for (int32 i = 0; i != FFeatureVectorHelper::EncodeVector2DCardinality; ++i)
				{
					Weights[DataOffset + i] = SampledBone.Weight;
				}
				DataOffset += FFeatureVectorHelper::EncodeVector2DCardinality;
			}
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
	
	const float FiniteDelta = Indexer.GetIndexingContext().Schema->SamplingInterval;

	TArray<float> Signal;
	TArray<float> SmoothedSignal;
	TArray<LocalMinMax> LocalMinMax;
	TArray<FVector> BonePositions;
	for (int32 ChannelBoneIdx = 0; ChannelBoneIdx != SampledBones.Num(); ++ChannelBoneIdx)
	{
		const FPoseSearchBone& SampledBone = SampledBones[ChannelBoneIdx];
		if (SampledBone.bUsePhase)
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
		int32 VectorIdx = SampleIdx - IndexingContext.BeginSampleIdx;
		FPoseSearchFeatureVectorBuilder& FeatureVector = IndexingOutput.PoseVectors[VectorIdx];
		AddPoseFeatures(Indexer, SampleIdx, FeatureVector, Phases);
	}
}

void UPoseSearchFeatureChannel_Pose::ComputeMeanDeviations(const Eigen::MatrixXd& CenteredPoseMatrix, Eigen::VectorXd& MeanDeviations) const
{
	using namespace UE::PoseSearch;

	int32 DataOffset = ChannelDataOffset;

	const int32 NumBones = SampledBones.Num();
	for (int32 ChannelBoneIdx = 0; ChannelBoneIdx != NumBones; ++ChannelBoneIdx)
	{
		const FPoseSearchBone& SampledBone = SampledBones[ChannelBoneIdx];
		if (SampledBone.bUsePosition)
		{
			for (int32 SubsampleIdx = 0; SubsampleIdx != SampleTimes.Num(); ++SubsampleIdx)
			{
				FFeatureVectorHelper::ComputeMeanDeviations(CenteredPoseMatrix, MeanDeviations, DataOffset, FFeatureVectorHelper::EncodeVectorCardinality);
			}
		}
		if (SampledBone.bUseRotation)
		{
			for (int32 SubsampleIdx = 0; SubsampleIdx != SampleTimes.Num(); ++SubsampleIdx)
			{
				FFeatureVectorHelper::ComputeMeanDeviations(CenteredPoseMatrix, MeanDeviations, DataOffset, FFeatureVectorHelper::EncodeQuatCardinality);
			}
		}
		if (SampledBone.bUseVelocity)
		{
			for (int32 SubsampleIdx = 0; SubsampleIdx != SampleTimes.Num(); ++SubsampleIdx)
			{
				FFeatureVectorHelper::ComputeMeanDeviations(CenteredPoseMatrix, MeanDeviations, DataOffset, FFeatureVectorHelper::EncodeVectorCardinality);
			}
		}
		if (SampledBone.bUsePhase)
		{
			FFeatureVectorHelper::ComputeMeanDeviations(CenteredPoseMatrix, MeanDeviations, DataOffset, FFeatureVectorHelper::EncodeVector2DCardinality);
		}
	}

	check(DataOffset == ChannelDataOffset + ChannelCardinality);
}

void UPoseSearchFeatureChannel_Pose::AddPoseFeatures(UE::PoseSearch::IAssetIndexer& Indexer, int32 SampleIdx, FPoseSearchFeatureVectorBuilder& FeatureVector, const TArray<TArray<FVector2D>>& Phases) const
{
	// This function samples the instantaneous pose at time t as well as the pose's velocity and acceleration at time t.
	// Symmetric finite differences are used to approximate derivatives:
	//	First symmetric derivative:   f'(t) ~ (f(t+h) - f(t-h)) / 2h
	//	Second symmetric derivative: f''(t) ~ (f(t+h) - 2f(t) + f(t-h)) / h^2
	// Where h is a constant time delta
	// So this means three pose extractions are taken at time t-h, t, and t+h
	
	using namespace UE::PoseSearch;
	using FSampleInfo = IAssetIndexer::FSampleInfo;

	if (SampledBones.IsEmpty() || SampleTimes.IsEmpty())
	{
		return;
	}

	const FAssetIndexingContext& IndexingContext = Indexer.GetIndexingContext();
	const FAssetSamplingContext* SamplingContext = IndexingContext.SamplingContext;

	const float SampleTime = FMath::Min(SampleIdx * IndexingContext.Schema->SamplingInterval, IndexingContext.MainSampler->GetPlayLength());

	int32 DataOffset = ChannelDataOffset;
	for (int32 ChannelBoneIdx = 0; ChannelBoneIdx != SampledBones.Num(); ++ChannelBoneIdx)
	{
		const FPoseSearchBone& SampledBone = SampledBones[ChannelBoneIdx];

		// Get each bone's component transform, velocity, and acceleration and add accumulated root motion at this time offset
		// Think of this process as freezing the character in place (at SampleTime) and then tracing the paths of their joints
		// as they move through space from past to present to future (at times indicated by PoseSampleTimes).

		if (SampledBone.bUsePosition)
		{
			for (int32 SubsampleIdx = 0; SubsampleIdx != SampleTimes.Num(); ++SubsampleIdx)
			{
				const float SubsampleTime = SampleTime + SampleTimes[SubsampleIdx];

				bool ClampedPresent;
				const FTransform BoneTransformsPresent = Indexer.GetTransformAndCacheResults(SubsampleTime, UseCharacterSpaceVelocities ? SubsampleTime : SampleTime, SchemaBoneIdx[ChannelBoneIdx], ClampedPresent);
				FFeatureVectorHelper::EncodeVector(FeatureVector.EditValues(), DataOffset, BoneTransformsPresent.GetTranslation());
			}
		}

		if (SampledBone.bUseRotation)
		{
			for (int32 SubsampleIdx = 0; SubsampleIdx != SampleTimes.Num(); ++SubsampleIdx)
			{
				const float SubsampleTime = SampleTime + SampleTimes[SubsampleIdx];

				bool ClampedPresent;
				const FTransform BoneTransformsPresent = Indexer.GetTransformAndCacheResults(SubsampleTime, UseCharacterSpaceVelocities ? SubsampleTime : SampleTime, SchemaBoneIdx[ChannelBoneIdx], ClampedPresent);
				FFeatureVectorHelper::EncodeQuat(FeatureVector.EditValues(), DataOffset, BoneTransformsPresent.GetRotation());
			}
		}

		if (SampledBone.bUseVelocity)
		{
			for (int32 SubsampleIdx = 0; SubsampleIdx != SampleTimes.Num(); ++SubsampleIdx)
			{
				const float SubsampleTime = SampleTime + SampleTimes[SubsampleIdx];

				bool ClampedPast, ClampedPresent, ClampedFuture;
				const FTransform BoneTransformsPast = Indexer.GetTransformAndCacheResults(SubsampleTime - SamplingContext->FiniteDelta, UseCharacterSpaceVelocities ? SubsampleTime - SamplingContext->FiniteDelta : SampleTime, SchemaBoneIdx[ChannelBoneIdx], ClampedPast);
				const FTransform BoneTransformsPresent = Indexer.GetTransformAndCacheResults(SubsampleTime, UseCharacterSpaceVelocities ? SubsampleTime : SampleTime, SchemaBoneIdx[ChannelBoneIdx], ClampedPresent);
				const FTransform BoneTransformsFuture = Indexer.GetTransformAndCacheResults(SubsampleTime + SamplingContext->FiniteDelta, UseCharacterSpaceVelocities ? SubsampleTime + SamplingContext->FiniteDelta : SampleTime, SchemaBoneIdx[ChannelBoneIdx], ClampedFuture);

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

				FFeatureVectorHelper::EncodeVector(FeatureVector.EditValues(), DataOffset, LinearVelocity);
			}
		}

		if (SampledBone.bUsePhase)
		{
			for (int32 SubsampleIdx = 0; SubsampleIdx != SampleTimes.Num(); ++SubsampleIdx)
			{
				// @todo: support for SubsampleIdx
				FFeatureVectorHelper::EncodeVector2D(FeatureVector.EditValues(), DataOffset, Phases[ChannelBoneIdx][SampleIdx]);
			}
		}
	}
}

void UPoseSearchFeatureChannel_Pose::GenerateDDCKey(FBlake3& InOutKeyHasher) const
{
	InOutKeyHasher.Update(MakeMemoryView(SampledBones));
	InOutKeyHasher.Update(MakeMemoryView(SampleTimes));
}

bool UPoseSearchFeatureChannel_Pose::BuildQuery(FPoseSearchContext& SearchContext, FPoseSearchFeatureVectorBuilder& InOutQuery) const
{
	using namespace UE::PoseSearch;

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

	struct CachedTransforms
	{
		FTransform Current;
		FTransform Previous;
		bool Valid = false;
	};
	TArray<CachedTransforms> CachedTransforms;
	CachedTransforms.AddUninitialized(SampleTimes.Num() * SampledBones.Num());

	for (int32 SubsampleIdx = 0; SubsampleIdx != SampleTimes.Num(); ++SubsampleIdx)
	{
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

		const TArrayView<const FTransform> ComponentPose = SearchContext.History->GetComponentPoseSample();
		const TArrayView<const FTransform> ComponentPrevPose = SearchContext.History->GetPrevComponentPoseSample();
		const FTransform RootTransform = SearchContext.History->GetRootTransformSample();
		const FTransform RootTransformPrev = SearchContext.History->GetPrevRootTransformSample();
		for (int32 SampledBoneIdx = 0; SampledBoneIdx != SampledBones.Num(); ++SampledBoneIdx)
		{
			const int32 SkeletonBoneIndex = InOutQuery.GetSchema()->BoneIndices[SchemaBoneIdx[SampledBoneIdx]];
			const int32 CachedTransformsIndex = SubsampleIdx * SampledBones.Num() + SampledBoneIdx;
			CachedTransforms[CachedTransformsIndex].Current = ComponentPose[SkeletonBoneIndex];

			if (UE::PoseSearch::UseCharacterSpaceVelocities)
			{
				// character space velocity
				CachedTransforms[CachedTransformsIndex].Previous = ComponentPrevPose[SkeletonBoneIndex];
			}
			else
			{
				// animation space velocity
				CachedTransforms[CachedTransformsIndex].Previous = ComponentPrevPose[SkeletonBoneIndex] * (RootTransformPrev * RootTransform.Inverse());
			}

			CachedTransforms[CachedTransformsIndex].Valid = true;
		}
	}

	int32 DataOffset = ChannelDataOffset;
	for (int32 SampledBoneIdx = 0; SampledBoneIdx != SampledBones.Num(); ++SampledBoneIdx)
	{
		const FPoseSearchBone& SampledBone = SampledBones[SampledBoneIdx];
		if (SampledBone.bUsePosition)
		{
			for (int32 SubsampleIdx = 0; SubsampleIdx != SampleTimes.Num(); ++SubsampleIdx)
			{
				const int32 CachedTransformsIndex = SubsampleIdx * SampledBones.Num() + SampledBoneIdx;
				if (CachedTransforms[CachedTransformsIndex].Valid)
				{
					FFeatureVectorHelper::EncodeVector(InOutQuery.EditValues(), DataOffset, CachedTransforms[CachedTransformsIndex].Current.GetTranslation());
				}
				else
				{
					// preserve the InOutQuery.EditValues() and increase the DataOffset
					DataOffset += FFeatureVectorHelper::EncodeVectorCardinality;
				}
			}
		}

		if (SampledBone.bUseRotation)
		{
			for (int32 SubsampleIdx = 0; SubsampleIdx != SampleTimes.Num(); ++SubsampleIdx)
			{
				const int32 CachedTransformsIndex = SubsampleIdx * SampledBones.Num() + SampledBoneIdx;
				if (CachedTransforms[CachedTransformsIndex].Valid)
				{
					FFeatureVectorHelper::EncodeQuat(InOutQuery.EditValues(), DataOffset, CachedTransforms[CachedTransformsIndex].Current.GetRotation());
				}
				else
				{
					// preserve the InOutQuery.EditValues() and increase the DataOffset
					DataOffset += FFeatureVectorHelper::EncodeQuatCardinality;
				}
			}
		}

		if (SampledBone.bUseVelocity)
		{
			for (int32 SubsampleIdx = 0; SubsampleIdx != SampleTimes.Num(); ++SubsampleIdx)
			{
				const int32 CachedTransformsIndex = SubsampleIdx * SampledBones.Num() + SampledBoneIdx;
				if (CachedTransforms[CachedTransformsIndex].Valid)
				{
					const FVector LinearVelocity = (CachedTransforms[CachedTransformsIndex].Current.GetTranslation() - CachedTransforms[CachedTransformsIndex].Previous.GetTranslation()) / SearchContext.History->GetSampleTimeInterval();
					FFeatureVectorHelper::EncodeVector(InOutQuery.EditValues(), DataOffset, LinearVelocity);
				}
				else
				{
					// preserve the InOutQuery.EditValues() and increase the DataOffset
					DataOffset += FFeatureVectorHelper::EncodeVectorCardinality;
				}
			}
		}

		if (SampledBone.bUsePhase)
		{
			for (int32 SubsampleIdx = 0; SubsampleIdx != SampleTimes.Num(); ++SubsampleIdx)
			{
				const int32 CachedTransformsIndex = SubsampleIdx * SampledBones.Num() + SampledBoneIdx;
				DataOffset += FFeatureVectorHelper::EncodeVector2DCardinality;

				// @todo: Support phase in BuildQuery
				// FFeatureVectorHelper::EncodeVector2D(InOutQuery.EditValues(), DataOffset, ???);
			}
		}
	}

	return true;
}

void UPoseSearchFeatureChannel_Pose::DebugDraw(const UE::PoseSearch::FDebugDrawParams& DrawParams, TArrayView<const float> PoseVector) const
{
	using namespace UE::PoseSearch;

	const UPoseSearchSchema* Schema = DrawParams.GetSchema();
	check(Schema && Schema->IsValid());

	const float LifeTime = DrawParams.DefaultLifeTime;
	const uint8 DepthPriority = ESceneDepthPriorityGroup::SDPG_Foreground + 2;
	const bool bPersistent = EnumHasAnyFlags(DrawParams.Flags, EDebugDrawFlags::Persistent);

	const int32 NumSubsamples = SampleTimes.Num();
	const int32 NumBones = SampledBones.Num();

	if ((NumSubsamples * NumBones) == 0)
	{
		return;
	}

	int32 DataOffset = ChannelDataOffset;
	for (int32 ChannelBoneIdx = 0; ChannelBoneIdx != NumBones; ++ChannelBoneIdx)
	{
		const FPoseSearchBone& SampledBone = SampledBones[ChannelBoneIdx];

		TArray<FVector> BonePos;
		BonePos.AddUninitialized(NumSubsamples);
		if (SampledBone.bUsePosition)
		{
			for (int32 SubsampleIdx = 0; SubsampleIdx != NumSubsamples; ++SubsampleIdx)
			{
				BonePos[SubsampleIdx] = FFeatureVectorHelper::DecodeVector(PoseVector, DataOffset);

				const FLinearColor LinearColor = DrawParams.GetColor(this);
				const FColor Color = LinearColor.ToFColor(true);

				BonePos[SubsampleIdx] = DrawParams.RootTransform.TransformPosition(BonePos[SubsampleIdx]);
				if (EnumHasAnyFlags(DrawParams.Flags, EDebugDrawFlags::DrawFast | EDebugDrawFlags::DrawSearchIndex))
				{
					DrawDebugPoint(DrawParams.World, BonePos[SubsampleIdx], DrawParams.PointSize, Color, bPersistent, DrawParams.DefaultLifeTime, DepthPriority);
				}
				else
				{
					DrawDebugSphere(DrawParams.World, BonePos[SubsampleIdx], DrawDebugSphereSize, DrawDebugSphereSegments, Color, bPersistent, LifeTime, DepthPriority);
				}

				if (EnumHasAnyFlags(DrawParams.Flags, EDebugDrawFlags::DrawBoneNames))
				{
					DrawDebugString(
						DrawParams.World, BonePos[SubsampleIdx] + FVector(0.0, 0.0, 10.0),
						Schema->BoneReferences[SchemaBoneIdx[ChannelBoneIdx]].BoneName.ToString(),
						nullptr, Color, LifeTime, false, 1.0f);
				}
			}
		}
		else
		{
			for (int32 SubsampleIdx = 0; SubsampleIdx != NumSubsamples; ++SubsampleIdx)
			{
				// @todo: initialize with the character position instead of FVector::Zero?
				BonePos[SubsampleIdx] = DrawParams.Mesh != nullptr ? DrawParams.Mesh->GetSocketTransform(SampledBones[ChannelBoneIdx].Reference.BoneName).GetLocation() : FVector::Zero();
			}
		}

		if (SampledBone.bUseRotation)
		{
			for (int32 SubsampleIdx = 0; SubsampleIdx != NumSubsamples; ++SubsampleIdx)
			{
				const FQuat BoneRot = FFeatureVectorHelper::DecodeQuat(PoseVector, DataOffset);

				// @todo: debug draw rotation
			}
		}

		if (SampledBone.bUseVelocity)
		{
			for (int32 SubsampleIdx = 0; SubsampleIdx != NumSubsamples; ++SubsampleIdx)
			{
				FVector BoneVel = FFeatureVectorHelper::DecodeVector(PoseVector, DataOffset);

				const FLinearColor LinearColor = DrawParams.GetColor(this);
				const FColor Color = LinearColor.ToFColor(true);

				BoneVel *= DrawDebugVelocityScale;
				BoneVel = DrawParams.RootTransform.TransformVector(BoneVel);
				FVector BoneVelDirection = BoneVel.GetSafeNormal();

				if (EnumHasAnyFlags(DrawParams.Flags, EDebugDrawFlags::DrawSearchIndex))
				{
					DrawDebugLine(DrawParams.World, BonePos[SubsampleIdx], BonePos[SubsampleIdx] + BoneVel, Color, bPersistent, DrawParams.DefaultLifeTime, DepthPriority);
				}
				else
				{
					const float AdjustedThickness =
						EnumHasAnyFlags(DrawParams.Flags, EDebugDrawFlags::DrawFast) ?
						0.0f : DrawDebugLineThickness;

					DrawDebugLine(
						DrawParams.World,
						BonePos[SubsampleIdx] + BoneVelDirection * DrawDebugSphereSize,
						BonePos[SubsampleIdx] + BoneVel,
						Color,
						bPersistent,
						LifeTime,
						DepthPriority,
						AdjustedThickness);
				}
			}
		}

		if (SampledBone.bUsePhase)
		{
			for (int32 SubsampleIdx = 0; SubsampleIdx != NumSubsamples; ++SubsampleIdx)
			{
				const FVector2D Phase = FFeatureVectorHelper::DecodeVector2D(PoseVector, DataOffset);

				const FLinearColor LinearColor = DrawParams.GetColor(this);
				const FColor Color = LinearColor.ToFColor(true);

				static float ScaleFactor = 1.f;

				const FVector TransformXAxisVector = DrawParams.RootTransform.TransformVector(FVector::XAxisVector);
				const FVector TransformYAxisVector = DrawParams.RootTransform.TransformVector(FVector::YAxisVector);
				const FVector TransformZAxisVector = DrawParams.RootTransform.TransformVector(FVector::ZAxisVector);

				const FVector PhaseVector = (TransformZAxisVector * Phase.X + TransformYAxisVector * Phase.Y) * ScaleFactor;
				DrawDebugLine(DrawParams.World, BonePos[SubsampleIdx], BonePos[SubsampleIdx] + PhaseVector, Color, bPersistent, LifeTime, DepthPriority, 0.f);

				static int32 Segments = 64;
				FMatrix CircleTransform;
				CircleTransform.SetAxes(&TransformXAxisVector, &TransformYAxisVector, &TransformZAxisVector, &BonePos[SubsampleIdx]);
				DrawDebugCircle(DrawParams.World, CircleTransform, PhaseVector.Length(), Segments, Color, bPersistent, LifeTime, DepthPriority, 0.f, false);
			}
		}
	}

	check(DataOffset == ChannelDataOffset + ChannelCardinality);
}

//////////////////////////////////////////////////////////////////////////
// UPoseSearchFeatureChannel_Trajectory
void UPoseSearchFeatureChannel_Trajectory::PreSave(FObjectPreSaveContext ObjectSaveContext)
{
	SampleOffsets.Sort(TLess<>());

	Super::PreSave(ObjectSaveContext);
}

void UPoseSearchFeatureChannel_Trajectory::InitializeSchema(UE::PoseSearch::FSchemaInitializer& Initializer)
{
	using namespace UE::PoseSearch;

	Super::InitializeSchema(Initializer);

	int32 DataOffset = ChannelDataOffset;
	if (bUsePositions)
	{
		for (int32 SubsampleIdx = 0; SubsampleIdx != SampleOffsets.Num(); ++SubsampleIdx)
		{
			DataOffset += FFeatureVectorHelper::EncodeVectorCardinality;
		}
	}

	if (bUseLinearVelocities)
	{
		for (int32 SubsampleIdx = 0; SubsampleIdx != SampleOffsets.Num(); ++SubsampleIdx)
		{
			DataOffset += FFeatureVectorHelper::EncodeVectorCardinality;
		}
	}

	if (bUseFacingDirections)
	{
		for (int32 SubsampleIdx = 0; SubsampleIdx != SampleOffsets.Num(); ++SubsampleIdx)
		{
			DataOffset += FFeatureVectorHelper::EncodeVectorCardinality;
		}
	}

	ChannelCardinality = DataOffset - ChannelDataOffset;

	Initializer.SetCurrentChannelDataOffset(DataOffset);
}

void UPoseSearchFeatureChannel_Trajectory::FillWeights(TArray<float>& Weights) const
{
	const int32 Begin = ChannelDataOffset;
	const int32 End = ChannelDataOffset + ChannelCardinality;
	for (int i = Begin; i < End; ++i)
	{
		Weights[i] = Weight;
	}
}

void UPoseSearchFeatureChannel_Trajectory::IndexAsset(UE::PoseSearch::IAssetIndexer& Indexer,  UE::PoseSearch::FAssetIndexingOutput& IndexingOutput) const
{
	const UE::PoseSearch::FAssetIndexingContext& IndexingContext = Indexer.GetIndexingContext();
	for (int32 SampleIdx = IndexingContext.BeginSampleIdx; SampleIdx != IndexingContext.EndSampleIdx; ++SampleIdx)
	{
		const int32 VectorIdx = SampleIdx - IndexingContext.BeginSampleIdx;
		IndexAssetPrivate(Indexer, SampleIdx, IndexingOutput.PoseVectors[VectorIdx]);
	}
}

void UPoseSearchFeatureChannel_Trajectory::ComputeMeanDeviations(const Eigen::MatrixXd& CenteredPoseMatrix, Eigen::VectorXd& MeanDeviations) const
{
	using namespace UE::PoseSearch;

	int32 DataOffset = ChannelDataOffset;
	if (bUsePositions)
	{
		for (int32 SubsampleIdx = 0; SubsampleIdx != SampleOffsets.Num(); ++SubsampleIdx)
		{
			FFeatureVectorHelper::ComputeMeanDeviations(CenteredPoseMatrix, MeanDeviations, DataOffset, FFeatureVectorHelper::EncodeVectorCardinality);
		}
	}

	if (bUseLinearVelocities)
	{
		for (int32 SubsampleIdx = 0; SubsampleIdx != SampleOffsets.Num(); ++SubsampleIdx)
		{
			FFeatureVectorHelper::ComputeMeanDeviations(CenteredPoseMatrix, MeanDeviations, DataOffset, FFeatureVectorHelper::EncodeVectorCardinality);
		}
	}

	if (bUseFacingDirections)
	{
		for (int32 SubsampleIdx = 0; SubsampleIdx != SampleOffsets.Num(); ++SubsampleIdx)
		{
			FFeatureVectorHelper::ComputeMeanDeviations(CenteredPoseMatrix, MeanDeviations, DataOffset, FFeatureVectorHelper::EncodeVectorCardinality);
		}
	}

	check(DataOffset == ChannelDataOffset + ChannelCardinality);
}

float UPoseSearchFeatureChannel_Trajectory::GetSampleTime(const UE::PoseSearch::IAssetIndexer& Indexer, int32 SubsampleIdx, float SampleTime, float RootDistance) const
{
	switch (Domain)
	{
	case EPoseSearchFeatureDomain::Time:
		return SampleTime + SampleOffsets[SubsampleIdx];

	case EPoseSearchFeatureDomain::Distance:
		return Indexer.GetSampleTimeFromDistance(RootDistance + SampleOffsets[SubsampleIdx]);

	default:
		checkNoEntry();
	}
	
	return 0.0f;
}

void UPoseSearchFeatureChannel_Trajectory::IndexAssetPrivate(const UE::PoseSearch::IAssetIndexer& Indexer, int32 SampleIdx, FPoseSearchFeatureVectorBuilder& FeatureVector) const
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
	float SampleTime = FMath::Min(SampleIdx * IndexingContext.Schema->SamplingInterval, IndexingContext.MainSampler->GetPlayLength());
	FSampleInfo Origin = Indexer.GetSampleInfo(SampleTime);

	int32 DataOffset = ChannelDataOffset;
	if (bUsePositions)
	{
		for (int32 SubsampleIdx = 0; SubsampleIdx != SampleOffsets.Num(); ++SubsampleIdx)
		{
			const float SubsampleTime = GetSampleTime(Indexer, SubsampleIdx, SampleTime, Origin.RootDistance);
			const FSampleInfo SamplePresent = Indexer.GetSampleInfoRelative(SubsampleTime, Origin);
			FFeatureVectorHelper::EncodeVector(FeatureVector.EditValues(), DataOffset, Indexer.MirrorTransform(SamplePresent.RootTransform).GetTranslation());
		}
	}

	if (bUseLinearVelocities)
	{
		for (int32 SubsampleIdx = 0; SubsampleIdx != SampleOffsets.Num(); ++SubsampleIdx)
		{
			const float SubsampleTime = GetSampleTime(Indexer, SubsampleIdx, SampleTime, Origin.RootDistance);

			// For each pose subsample term, get the corresponding clip, accumulated root motion,
			// and wrap the time parameter based on the clip's length.
			const FSampleInfo SamplePast = Indexer.GetSampleInfoRelative(SubsampleTime - IndexingContext.SamplingContext->FiniteDelta, Origin);
			const FSampleInfo SamplePresent = Indexer.GetSampleInfoRelative(SubsampleTime, Origin);
			const FSampleInfo SampleFuture = Indexer.GetSampleInfoRelative(SubsampleTime + IndexingContext.SamplingContext->FiniteDelta, Origin);

			// Mirror transforms if requested
			const FTransform MirroredRootPast = Indexer.MirrorTransform(SamplePast.RootTransform);
			const FTransform MirroredRootPresent = Indexer.MirrorTransform(SamplePresent.RootTransform);
			const FTransform MirroredRootFuture = Indexer.MirrorTransform(SampleFuture.RootTransform);

			// We can get a better finite difference if we ignore samples that have
			// been clamped at either side of the clip. However, if the central sample 
			// itself is clamped, or there are no samples that are clamped, we can just 
			// use the central difference as normal.
			FVector LinearVelocity;
			if (SamplePast.bClamped && !SamplePresent.bClamped && !SampleFuture.bClamped)
			{
				LinearVelocity = (MirroredRootFuture.GetTranslation() - MirroredRootPresent.GetTranslation()) / IndexingContext.SamplingContext->FiniteDelta;
			}
			else if (SampleFuture.bClamped && !SamplePresent.bClamped && !SamplePast.bClamped)
			{
				LinearVelocity = (MirroredRootPresent.GetTranslation() - MirroredRootPast.GetTranslation()) / IndexingContext.SamplingContext->FiniteDelta;
			}
			else
			{
				LinearVelocity = (MirroredRootFuture.GetTranslation() - MirroredRootPast.GetTranslation()) / (IndexingContext.SamplingContext->FiniteDelta * 2.0f);
			}

			FFeatureVectorHelper::EncodeVector(FeatureVector.EditValues(), DataOffset, LinearVelocity);
		}
	}

	if (bUseFacingDirections)
	{
		for (int32 SubsampleIdx = 0; SubsampleIdx != SampleOffsets.Num(); ++SubsampleIdx)
		{
			const float SubsampleTime = GetSampleTime(Indexer, SubsampleIdx, SampleTime, Origin.RootDistance);
			const FSampleInfo SamplePresent = Indexer.GetSampleInfoRelative(SubsampleTime, Origin);
			FFeatureVectorHelper::EncodeVector(FeatureVector.EditValues(), DataOffset, Indexer.MirrorTransform(SamplePresent.RootTransform).GetRotation().GetAxisY());
		}
	}
}

void UPoseSearchFeatureChannel_Trajectory::GenerateDDCKey(FBlake3& InOutKeyHasher) const
{
	InOutKeyHasher.Update(&bUseLinearVelocities, sizeof(bUseLinearVelocities));
	InOutKeyHasher.Update(&bUsePositions, sizeof(bUsePositions));
	InOutKeyHasher.Update(&bUseFacingDirections, sizeof(bUseFacingDirections));
	InOutKeyHasher.Update(&Domain, sizeof(Domain));
	InOutKeyHasher.Update(MakeMemoryView(SampleOffsets));
	InOutKeyHasher.Update(&Weight, sizeof(Weight));
}

bool UPoseSearchFeatureChannel_Trajectory::BuildQuery(
	FPoseSearchContext& SearchContext,
	FPoseSearchFeatureVectorBuilder& InOutQuery) const
{
	using namespace UE::PoseSearch;

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

	// @todo: optimize dynamic allocation
	TArray<FTrajectorySample> Samples;
	Samples.Reserve(SampleOffsets.Num());
	for (int32 Idx = 0, NextIterStartIdx = 0, Num = SampleOffsets.Num(); Idx < Num; ++Idx)
	{
		Samples.Add(FTrajectorySampleRange::IterSampleTrajectory(SearchContext.Trajectory->Samples, SampleDomain, SampleOffsets[Idx], NextIterStartIdx));
	}

	int32 DataOffset = ChannelDataOffset;
	if (bUsePositions)
	{
		for (int32 Idx = 0, Num = SampleOffsets.Num(); Idx < Num; ++Idx)
		{
			FFeatureVectorHelper::EncodeVector(InOutQuery.EditValues(), DataOffset, Samples[Idx].Transform.GetTranslation());
		}
	}

	if (bUseLinearVelocities)
	{
		for (int32 Idx = 0, Num = SampleOffsets.Num(); Idx < Num; ++Idx)
		{
			FFeatureVectorHelper::EncodeVector(InOutQuery.EditValues(), DataOffset, Samples[Idx].LinearVelocity);
		}
	}

	if (bUseFacingDirections)
	{ 
		for (int32 Idx = 0, Num = SampleOffsets.Num(); Idx < Num; ++Idx)
		{
			FFeatureVectorHelper::EncodeVector(InOutQuery.EditValues(), DataOffset, Samples[Idx].Transform.GetRotation().GetAxisY());
		}
	}

	return true;
}

void UPoseSearchFeatureChannel_Trajectory::DebugDraw(const UE::PoseSearch::FDebugDrawParams& DrawParams, TArrayView<const float> PoseVector) const
{
	using namespace UE::PoseSearch;

	const float LifeTime = DrawParams.DefaultLifeTime;
	const uint8 DepthPriority = ESceneDepthPriorityGroup::SDPG_Foreground + 2;
	const bool bPersistent = EnumHasAnyFlags(DrawParams.Flags, EDebugDrawFlags::Persistent);

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

	TArray<FVector> TrajectoryPos;
	TrajectoryPos.AddUninitialized(NumSubsamples);
	int32 DataOffset = ChannelDataOffset;
	for (int32 SubsampleIdx = 0; SubsampleIdx != NumSubsamples; ++SubsampleIdx)
	{
		if (bUsePositions)
		{
			TrajectoryPos[SubsampleIdx] = FFeatureVectorHelper::DecodeVector(PoseVector, DataOffset);

			FLinearColor LinearColor = DrawParams.GetColor(this);
			FLinearColor GradientColor = GetGradientColor(LinearColor, SubsampleIdx, NumSubsamples, DrawParams.Flags);
			FColor Color = GradientColor.ToFColor(true);

			TrajectoryPos[SubsampleIdx] = DrawParams.RootTransform.TransformPosition(TrajectoryPos[SubsampleIdx]);
			if (EnumHasAnyFlags(DrawParams.Flags, EDebugDrawFlags::DrawFast | EDebugDrawFlags::DrawSearchIndex))
			{
				DrawDebugPoint(DrawParams.World, TrajectoryPos[SubsampleIdx], DrawParams.PointSize, Color, bPersistent, DrawParams.DefaultLifeTime, DepthPriority);
			}
			else
			{
				DrawDebugSphere(DrawParams.World, TrajectoryPos[SubsampleIdx], DrawDebugSphereSize, DrawDebugSphereSegments, Color, bPersistent, LifeTime, DepthPriority);
			}
		}
		else
		{
			TrajectoryPos[SubsampleIdx] = DrawParams.RootTransform.GetTranslation();
		}
	}

	if (bUseLinearVelocities)
	{
		for (int32 SubsampleIdx = 0; SubsampleIdx != NumSubsamples; ++SubsampleIdx)
		{
			FVector TrajectoryVel = FFeatureVectorHelper::DecodeVector(PoseVector, DataOffset);

			const FLinearColor LinearColor = DrawParams.GetColor(this);
			const FLinearColor GradientColor = GetGradientColor(LinearColor, SubsampleIdx, NumSubsamples, DrawParams.Flags);
			const FColor Color = GradientColor.ToFColor(true);

			TrajectoryVel *= DrawDebugVelocityScale;
			TrajectoryVel = DrawParams.RootTransform.TransformVector(TrajectoryVel);
			const FVector TrajectoryVelDirection = TrajectoryVel.GetSafeNormal();

			if (EnumHasAnyFlags(DrawParams.Flags, EDebugDrawFlags::DrawSearchIndex))
			{
				DrawDebugLine(DrawParams.World, TrajectoryPos[SubsampleIdx], TrajectoryPos[SubsampleIdx] + TrajectoryVel, Color, bPersistent, DrawParams.DefaultLifeTime, DepthPriority);
			}
			else
			{
				const float AdjustedThickness =
					EnumHasAnyFlags(DrawParams.Flags, EDebugDrawFlags::DrawFast) ?
					0.0f : DrawDebugLineThickness;

				DrawDebugLine(
					DrawParams.World,
					TrajectoryPos[SubsampleIdx] + TrajectoryVelDirection * DrawDebugSphereSize,
					TrajectoryPos[SubsampleIdx] + TrajectoryVel,
					Color,
					bPersistent,
					LifeTime,
					DepthPriority,
					AdjustedThickness
				);
			}
		}
	}

	if (bUseFacingDirections)
	{
		for (int32 SubsampleIdx = 0; SubsampleIdx != NumSubsamples; ++SubsampleIdx)
		{
			FVector TrajectoryForward = FFeatureVectorHelper::DecodeVector(PoseVector, DataOffset);

			const FLinearColor LinearColor = DrawParams.GetColor(this);
			const FLinearColor GradientColor = GetGradientColor(LinearColor, SubsampleIdx, NumSubsamples, DrawParams.Flags);
			const FColor Color = GradientColor.ToFColor(true);

			TrajectoryForward = DrawParams.RootTransform.TransformVector(TrajectoryForward);

			if (EnumHasAnyFlags(DrawParams.Flags, EDebugDrawFlags::DrawSearchIndex))
			{
				DrawDebugLine(DrawParams.World, TrajectoryPos[SubsampleIdx], TrajectoryPos[SubsampleIdx] + TrajectoryForward, Color, bPersistent, DrawParams.DefaultLifeTime, DepthPriority);
			}
			else
			{
				const float AdjustedThickness =
					EnumHasAnyFlags(DrawParams.Flags, EDebugDrawFlags::DrawFast) ?
					0.0f : DrawDebugLineThickness;

				DrawDebugLine(
					DrawParams.World,
					TrajectoryPos[SubsampleIdx] + TrajectoryForward * DrawDebugSphereSize,
					TrajectoryPos[SubsampleIdx] + TrajectoryForward * DrawDebugSphereSize * 2.0f,
					Color,
					bPersistent,
					LifeTime,
					DepthPriority,
					AdjustedThickness
				);
			}
		}
	}

	if (EnumHasAnyFlags(DrawParams.Flags, EDebugDrawFlags::DrawSampleLabels))
	{
		for (int32 SubsampleIdx = 0; SubsampleIdx != NumSubsamples; ++SubsampleIdx)
		{
			const FLinearColor LinearColor = DrawParams.GetColor(this);
			const FLinearColor GradientColor = GetGradientColor(LinearColor, SubsampleIdx, NumSubsamples, DrawParams.Flags);
			const FColor Color = GradientColor.ToFColor(true);

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
				TrajectoryPos[SubsampleIdx] + DrawDebugSampleLabelOffset,
				SampleLabel,
				nullptr,
				Color,
				LifeTime,
				false,
				DrawDebugSampleLabelFontScale);
		}
	}

	check(DataOffset == ChannelDataOffset + ChannelCardinality);
}