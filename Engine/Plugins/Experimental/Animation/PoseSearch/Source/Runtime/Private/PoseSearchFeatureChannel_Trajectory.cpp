// Copyright Epic Games, Inc. All Rights Reserved.

#include "PoseSearchFeatureChannel_Trajectory.h"
#include "DrawDebugHelpers.h"
#include "PoseSearch/PoseSearch.h"
#include "PoseSearch/PoseSearchDerivedDataKey.h"
#include "PoseSearch/PoseSearchSchema.h"

#define LOCTEXT_NAMESPACE "PoseSearchFeatureChannels"

void UPoseSearchFeatureChannel_Trajectory::PreSave(FObjectPreSaveContext ObjectSaveContext)
{
	Samples.Sort([](const FPoseSearchTrajectorySample& a, const FPoseSearchTrajectorySample& b)
	{
		return a.Offset < b.Offset;
	});

	Super::PreSave(ObjectSaveContext);
}

void UPoseSearchFeatureChannel_Trajectory::InitializeSchema(UPoseSearchSchema* Schema)
{
	using namespace UE::PoseSearch;

	ChannelDataOffset = Schema->SchemaCardinality;

	for (const FPoseSearchTrajectorySample& Sample : Samples)
	{
		if (EnumHasAnyFlags(Sample.Flags, EPoseSearchTrajectoryFlags::Position))
		{
			Schema->SchemaCardinality += FFeatureVectorHelper::EncodeVectorCardinality;
		}
		if (EnumHasAnyFlags(Sample.Flags, EPoseSearchTrajectoryFlags::PositionXY))
		{
			Schema->SchemaCardinality += FFeatureVectorHelper::EncodeVector2DCardinality;
		}

		if (EnumHasAnyFlags(Sample.Flags, EPoseSearchTrajectoryFlags::Velocity))
		{
			Schema->SchemaCardinality += FFeatureVectorHelper::EncodeVectorCardinality;
		}
		if (EnumHasAnyFlags(Sample.Flags, EPoseSearchTrajectoryFlags::VelocityXY))
		{
			Schema->SchemaCardinality += FFeatureVectorHelper::EncodeVector2DCardinality;
		}

		if (EnumHasAnyFlags(Sample.Flags, EPoseSearchTrajectoryFlags::VelocityDirection))
		{
			Schema->SchemaCardinality += FFeatureVectorHelper::EncodeVectorCardinality;
		}
		if (EnumHasAnyFlags(Sample.Flags, EPoseSearchTrajectoryFlags::VelocityDirectionXY))
		{
			Schema->SchemaCardinality += FFeatureVectorHelper::EncodeVector2DCardinality;
		}

		if (EnumHasAnyFlags(Sample.Flags, EPoseSearchTrajectoryFlags::FacingDirection))
		{
			Schema->SchemaCardinality += FFeatureVectorHelper::EncodeVectorCardinality;
		}
		if (EnumHasAnyFlags(Sample.Flags, EPoseSearchTrajectoryFlags::FacingDirectionXY))
		{
			Schema->SchemaCardinality += FFeatureVectorHelper::EncodeVector2DCardinality;
		}
	}

	ChannelCardinality = Schema->SchemaCardinality - ChannelDataOffset;
}

void UPoseSearchFeatureChannel_Trajectory::FillWeights(TArray<float>& Weights) const
{
	using namespace UE::PoseSearch;

	int32 DataOffset = ChannelDataOffset;
	for (const FPoseSearchTrajectorySample& Sample : Samples)
	{
		if (EnumHasAnyFlags(Sample.Flags, EPoseSearchTrajectoryFlags::Position))
		{
			for (int32 i = 0; i != FFeatureVectorHelper::EncodeVectorCardinality; ++i)
			{
				Weights[DataOffset + i] = Weight * Sample.Weight;
			}
			DataOffset += FFeatureVectorHelper::EncodeVectorCardinality;
		}
		if (EnumHasAnyFlags(Sample.Flags, EPoseSearchTrajectoryFlags::PositionXY))
		{
			for (int32 i = 0; i != FFeatureVectorHelper::EncodeVector2DCardinality; ++i)
			{
				Weights[DataOffset + i] = Weight * Sample.Weight;
			}
			DataOffset += FFeatureVectorHelper::EncodeVector2DCardinality;
		}

		if (EnumHasAnyFlags(Sample.Flags, EPoseSearchTrajectoryFlags::Velocity))
		{
			for (int32 i = 0; i != FFeatureVectorHelper::EncodeVectorCardinality; ++i)
			{
				Weights[DataOffset + i] = Weight * Sample.Weight;
			}
			DataOffset += FFeatureVectorHelper::EncodeVectorCardinality;
		}
		if (EnumHasAnyFlags(Sample.Flags, EPoseSearchTrajectoryFlags::VelocityXY))
		{
			for (int32 i = 0; i != FFeatureVectorHelper::EncodeVector2DCardinality; ++i)
			{
				Weights[DataOffset + i] = Weight * Sample.Weight;
			}
			DataOffset += FFeatureVectorHelper::EncodeVector2DCardinality;
		}

		if (EnumHasAnyFlags(Sample.Flags, EPoseSearchTrajectoryFlags::VelocityDirection))
		{
			for (int32 i = 0; i != FFeatureVectorHelper::EncodeVectorCardinality; ++i)
			{
				Weights[DataOffset + i] = Weight * Sample.Weight;
			}
			DataOffset += FFeatureVectorHelper::EncodeVectorCardinality;
		}
		if (EnumHasAnyFlags(Sample.Flags, EPoseSearchTrajectoryFlags::VelocityDirectionXY))
		{
			for (int32 i = 0; i != FFeatureVectorHelper::EncodeVector2DCardinality; ++i)
			{
				Weights[DataOffset + i] = Weight * Sample.Weight;
			}
			DataOffset += FFeatureVectorHelper::EncodeVector2DCardinality;
		}

		if (EnumHasAnyFlags(Sample.Flags, EPoseSearchTrajectoryFlags::FacingDirection))
		{
			for (int32 i = 0; i != FFeatureVectorHelper::EncodeVectorCardinality; ++i)
			{
				Weights[DataOffset + i] = Weight * Sample.Weight;
			}
			DataOffset += FFeatureVectorHelper::EncodeVectorCardinality;
		}
		if (EnumHasAnyFlags(Sample.Flags, EPoseSearchTrajectoryFlags::FacingDirectionXY))
		{
			for (int32 i = 0; i != FFeatureVectorHelper::EncodeVector2DCardinality; ++i)
			{
				Weights[DataOffset + i] = Weight * Sample.Weight;
			}
			DataOffset += FFeatureVectorHelper::EncodeVector2DCardinality;
		}
	}

	check(DataOffset == ChannelDataOffset + ChannelCardinality);
}

void UPoseSearchFeatureChannel_Trajectory::IndexAsset(UE::PoseSearch::IAssetIndexer& Indexer,  UE::PoseSearch::FAssetIndexingOutput& IndexingOutput) const
{
	const UE::PoseSearch::FAssetIndexingContext& IndexingContext = Indexer.GetIndexingContext();
	for (int32 SampleIdx = IndexingContext.BeginSampleIdx; SampleIdx != IndexingContext.EndSampleIdx; ++SampleIdx)
	{
		const int32 VectorIdx = SampleIdx - IndexingContext.BeginSampleIdx;
		IndexAssetPrivate(Indexer, SampleIdx, IndexingOutput.GetPoseVector(VectorIdx));
	}
}

void UPoseSearchFeatureChannel_Trajectory::IndexAssetPrivate(const UE::PoseSearch::IAssetIndexer& Indexer, int32 SampleIdx, TArrayView<float> FeatureVector) const
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
	float SampleTime = FMath::Min(SampleIdx * IndexingContext.Schema->GetSamplingInterval(), IndexingContext.AssetSampler->GetPlayLength());
	FSampleInfo Origin = Indexer.GetSampleInfo(SampleTime);

	int32 DataOffset = ChannelDataOffset;
	for (const FPoseSearchTrajectorySample& Sample : Samples)
	{
		const float SubsampleTime = Sample.Offset + SampleTime;

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

		const FVector LinearVelocityDirection = LinearVelocity.GetClampedToMaxSize(1.0f);
		const FVector FacingDirection = MirroredRootPresent.GetRotation().GetForwardVector();

		if (EnumHasAnyFlags(Sample.Flags, EPoseSearchTrajectoryFlags::Position))
		{
			FFeatureVectorHelper::EncodeVector(FeatureVector, DataOffset, MirroredRootPresent.GetTranslation());
		}
		if (EnumHasAnyFlags(Sample.Flags, EPoseSearchTrajectoryFlags::PositionXY))
		{
			FFeatureVectorHelper::EncodeVector2D(FeatureVector, DataOffset, FVector2D(MirroredRootPresent.GetTranslation().X, MirroredRootPresent.GetTranslation().Y));
		}

		if (EnumHasAnyFlags(Sample.Flags, EPoseSearchTrajectoryFlags::Velocity))
		{
			FFeatureVectorHelper::EncodeVector(FeatureVector, DataOffset, LinearVelocity);
		}
		if (EnumHasAnyFlags(Sample.Flags, EPoseSearchTrajectoryFlags::VelocityXY))
		{
			FFeatureVectorHelper::EncodeVector2D(FeatureVector, DataOffset, FVector2D(LinearVelocity.X, LinearVelocity.Y));
		}

		if (EnumHasAnyFlags(Sample.Flags, EPoseSearchTrajectoryFlags::VelocityDirection))
		{
			FFeatureVectorHelper::EncodeVector(FeatureVector, DataOffset, LinearVelocityDirection);
		}
		if (EnumHasAnyFlags(Sample.Flags, EPoseSearchTrajectoryFlags::VelocityDirectionXY))
		{
			FFeatureVectorHelper::EncodeVector2D(FeatureVector, DataOffset, FVector2D(LinearVelocityDirection.X, LinearVelocityDirection.Y));
		}

		if (EnumHasAnyFlags(Sample.Flags, EPoseSearchTrajectoryFlags::FacingDirection))
		{
			FFeatureVectorHelper::EncodeVector(FeatureVector, DataOffset, FacingDirection);
		}
		if (EnumHasAnyFlags(Sample.Flags, EPoseSearchTrajectoryFlags::FacingDirectionXY))
		{
			FFeatureVectorHelper::EncodeVector2D(FeatureVector, DataOffset, FVector2D(FacingDirection.X, FacingDirection.Y).GetSafeNormal());
		}
	}
	check(DataOffset == ChannelDataOffset + ChannelCardinality);
}

void UPoseSearchFeatureChannel_Trajectory::BuildQuery(UE::PoseSearch::FSearchContext& SearchContext, FPoseSearchFeatureVectorBuilder& InOutQuery) const
{
	using namespace UE::PoseSearch;

	if (!SearchContext.Trajectory)
	{
		// @todo: do we want to reuse the SearchContext.CurrentResult data if valid?
		return;
	}

	int32 NextIterStartIdx = 0;
	int32 DataOffset = ChannelDataOffset;
	float PreviousOffset = -FLT_MAX;
	for (const FPoseSearchTrajectorySample& Sample : Samples)
	{
		// making sure Samples are sorted
		check(Sample.Offset >= PreviousOffset);
		const FTrajectorySample TrajectorySample = FTrajectorySampleRange::IterSampleTrajectory(SearchContext.Trajectory->Samples, Sample.Offset, NextIterStartIdx);

		const FVector LinearVelocityDirection = TrajectorySample.LinearVelocity.GetClampedToMaxSize(1.0f);
		const FVector FacingDirection = TrajectorySample.Transform.GetRotation().GetForwardVector();

		if (EnumHasAnyFlags(Sample.Flags, EPoseSearchTrajectoryFlags::Position))
		{
			FFeatureVectorHelper::EncodeVector(InOutQuery.EditValues(), DataOffset, TrajectorySample.Transform.GetTranslation());
		}
		if (EnumHasAnyFlags(Sample.Flags, EPoseSearchTrajectoryFlags::PositionXY))
		{
			FFeatureVectorHelper::EncodeVector2D(InOutQuery.EditValues(), DataOffset, FVector2D(TrajectorySample.Transform.GetTranslation().X, TrajectorySample.Transform.GetTranslation().Y));
		}

		if (EnumHasAnyFlags(Sample.Flags, EPoseSearchTrajectoryFlags::Velocity))
		{
			FFeatureVectorHelper::EncodeVector(InOutQuery.EditValues(), DataOffset, TrajectorySample.LinearVelocity);
		}
		if (EnumHasAnyFlags(Sample.Flags, EPoseSearchTrajectoryFlags::VelocityXY))
		{
			FFeatureVectorHelper::EncodeVector2D(InOutQuery.EditValues(), DataOffset, FVector2D(TrajectorySample.LinearVelocity.X, TrajectorySample.LinearVelocity.Y));
		}

		if (EnumHasAnyFlags(Sample.Flags, EPoseSearchTrajectoryFlags::VelocityDirection))
		{
			FFeatureVectorHelper::EncodeVector(InOutQuery.EditValues(), DataOffset, LinearVelocityDirection);
		}
		if (EnumHasAnyFlags(Sample.Flags, EPoseSearchTrajectoryFlags::VelocityDirectionXY))
		{
			FFeatureVectorHelper::EncodeVector2D(InOutQuery.EditValues(), DataOffset, FVector2D(LinearVelocityDirection.X, LinearVelocityDirection.Y));
		}

		if (EnumHasAnyFlags(Sample.Flags, EPoseSearchTrajectoryFlags::FacingDirection))
		{
			FFeatureVectorHelper::EncodeVector(InOutQuery.EditValues(), DataOffset, FacingDirection);
		}
		if (EnumHasAnyFlags(Sample.Flags, EPoseSearchTrajectoryFlags::FacingDirectionXY))
		{
			FFeatureVectorHelper::EncodeVector2D(InOutQuery.EditValues(), DataOffset, FVector2D(FacingDirection.X, FacingDirection.Y).GetSafeNormal());
		}
	}
	check(DataOffset == ChannelDataOffset + ChannelCardinality);
}

// lazy initialized helper to interpolate or extrapolate (linearly) UPoseSearchFeatureChannel_Trajectory trajectory positions from FPoseSearchTrajectorySample containing EPoseSearchTrajectoryFlags::Position for samples without it
struct TrajectoryPositionReconstructor
{
	struct PositionAndOffsetSample
	{
		FVector Position;
		float Offset;
	};

	TArray<PositionAndOffsetSample, TInlineAllocator<32>> PositionAndOffsetSamples;
	bool bInitialized = false;

	void Init(const UPoseSearchFeatureChannel_Trajectory& TrajectoryChannel, TConstArrayView<float> PoseVector, const FTransform& RootTransform)
	{
		PositionAndOffsetSamples.Reserve(TrajectoryChannel.Samples.Num() + 1);
		bool bAddZeroOffsetSample = true;
		int32 DataOffset = TrajectoryChannel.GetChannelDataOffset();
		for (const FPoseSearchTrajectorySample& Sample : TrajectoryChannel.Samples)
		{
			if (EnumHasAnyFlags(Sample.Flags, EPoseSearchTrajectoryFlags::Position))
			{
				PositionAndOffsetSample PositionAndOffsetSample;
				PositionAndOffsetSample.Position = UE::PoseSearch::FFeatureVectorHelper::DecodeVector(PoseVector, DataOffset);
				PositionAndOffsetSample.Position = RootTransform.TransformPosition(PositionAndOffsetSample.Position);
				PositionAndOffsetSample.Offset = Sample.Offset;
				PositionAndOffsetSamples.Add(PositionAndOffsetSample);

				if (FMath::IsNearlyZero(Sample.Offset))
				{
					bAddZeroOffsetSample = false;
				}
			}
			if (EnumHasAnyFlags(Sample.Flags, EPoseSearchTrajectoryFlags::PositionXY))
			{
				const FVector2D Position2D = UE::PoseSearch::FFeatureVectorHelper::DecodeVector2D(PoseVector, DataOffset);

				if (!EnumHasAnyFlags(Sample.Flags, EPoseSearchTrajectoryFlags::Position))
				{
					PositionAndOffsetSample PositionAndOffsetSample;
					PositionAndOffsetSample.Position = FVector(Position2D.X, Position2D.Y, 0);
					PositionAndOffsetSample.Position = RootTransform.TransformPosition(PositionAndOffsetSample.Position);
					PositionAndOffsetSample.Offset = Sample.Offset;
					PositionAndOffsetSamples.Add(PositionAndOffsetSample);

					if (FMath::IsNearlyZero(Sample.Offset))
					{
						bAddZeroOffsetSample = false;
					}
				}
			}

			if (EnumHasAnyFlags(Sample.Flags, EPoseSearchTrajectoryFlags::Velocity))
			{
				DataOffset += UE::PoseSearch::FFeatureVectorHelper::EncodeVectorCardinality;
			}
			if (EnumHasAnyFlags(Sample.Flags, EPoseSearchTrajectoryFlags::VelocityXY))
			{
				DataOffset += UE::PoseSearch::FFeatureVectorHelper::EncodeVector2DCardinality;
			}

			if (EnumHasAnyFlags(Sample.Flags, EPoseSearchTrajectoryFlags::VelocityDirection))
			{
				DataOffset += UE::PoseSearch::FFeatureVectorHelper::EncodeVectorCardinality;
			}
			if (EnumHasAnyFlags(Sample.Flags, EPoseSearchTrajectoryFlags::VelocityDirectionXY))
			{
				DataOffset += UE::PoseSearch::FFeatureVectorHelper::EncodeVector2DCardinality;
			}

			if (EnumHasAnyFlags(Sample.Flags, EPoseSearchTrajectoryFlags::FacingDirection))
			{
				DataOffset += UE::PoseSearch::FFeatureVectorHelper::EncodeVectorCardinality;
			}
			if (EnumHasAnyFlags(Sample.Flags, EPoseSearchTrajectoryFlags::FacingDirectionXY))
			{
				DataOffset += UE::PoseSearch::FFeatureVectorHelper::EncodeVector2DCardinality;
			}
		}

		if (bAddZeroOffsetSample)
		{
			PositionAndOffsetSample PositionAndOffsetSample;
			PositionAndOffsetSample.Position = RootTransform.GetTranslation();
			PositionAndOffsetSample.Offset = 0.f;
			PositionAndOffsetSamples.Add(PositionAndOffsetSample);
		}

		PositionAndOffsetSamples.Sort([](const PositionAndOffsetSample& a, const PositionAndOffsetSample& b)
		{
			return a.Offset < b.Offset;
		});

		bInitialized = true;
		check(DataOffset == TrajectoryChannel.GetChannelDataOffset() + TrajectoryChannel.GetChannelCardinality());
	}

	FVector GetReconstructedTrajectoryPos(const UPoseSearchFeatureChannel_Trajectory& TrajectoryChannel, TConstArrayView<float> PoseVector, const FTransform& RootTransform, float SampleOffset)
	{
		if (!bInitialized)
		{
			Init(TrajectoryChannel, PoseVector, RootTransform);
		}

		return GetReconstructedTrajectoryPos(SampleOffset);
	}

	FVector GetReconstructedTrajectoryPos(float SampleOffset) const
	{
		check(bInitialized);
		check(PositionAndOffsetSamples.Num() > 0);
		if (PositionAndOffsetSamples.Num() >= 2)
		{
			const int32 LowerBoundIdx = Algo::LowerBound(PositionAndOffsetSamples, SampleOffset, [](const PositionAndOffsetSample& PositionAndOffsetSample, float Value)
				{
					return Value > PositionAndOffsetSample.Offset;
				});

			const int32 PrevIdx = FMath::Clamp(LowerBoundIdx, 0, PositionAndOffsetSamples.Num() - 2);
			const int32 NextIdx = PrevIdx + 1;

			const float Denominator = PositionAndOffsetSamples[NextIdx].Offset - PositionAndOffsetSamples[PrevIdx].Offset;
			if (FMath::IsNearlyZero(Denominator))
			{
				return PositionAndOffsetSamples[PrevIdx].Position;
			}

			const float Numerator = SampleOffset - PositionAndOffsetSamples[PrevIdx].Offset;
			const float LerpValue = Numerator / Denominator;
			return FMath::Lerp(PositionAndOffsetSamples[PrevIdx].Position, PositionAndOffsetSamples[NextIdx].Position, LerpValue);
		}

		return PositionAndOffsetSamples[0].Position;
	}
};

void UPoseSearchFeatureChannel_Trajectory::DebugDraw(const UE::PoseSearch::FDebugDrawParams& DrawParams, TConstArrayView<float> PoseVector) const
{
#if ENABLE_DRAW_DEBUG
	using namespace UE::PoseSearch;

	const float LifeTime = DrawParams.DefaultLifeTime;
	const uint8 DepthPriority = ESceneDepthPriorityGroup::SDPG_Foreground + 2;
	const bool bPersistent = EnumHasAnyFlags(DrawParams.Flags, EDebugDrawFlags::Persistent);

	const int32 NumSamples = Samples.Num();
	if (NumSamples == 0)
	{
		return;
	}

	int32 DataOffset = ChannelDataOffset;
	int32 SampleIdx = 0;
	TrajectoryPositionReconstructor TrajectoryPositionReconstructor;
	TArray<FVector, TInlineAllocator<32>> TrajSplinePos;
	TArray<FColor, TInlineAllocator<32>> TrajSplineColor;
	for (const FPoseSearchTrajectorySample& Sample : Samples)
	{
		bool bIsTrajectoryPosValid = false;
		FVector TrajectoryPos = FVector::Zero();
		
		if (EnumHasAnyFlags(Sample.Flags, EPoseSearchTrajectoryFlags::Position))
		{
			TrajectoryPos = FFeatureVectorHelper::DecodeVector(PoseVector, DataOffset);
			TrajectoryPos = DrawParams.RootTransform.TransformPosition(TrajectoryPos);
			
			bIsTrajectoryPosValid = true;

			// validating TrajectoryPositionReconstructor
			check((TrajectoryPositionReconstructor.GetReconstructedTrajectoryPos(*this, PoseVector, DrawParams.RootTransform, Sample.Offset) - TrajectoryPos).IsNearlyZero());

			const FColor Color = DrawParams.GetColor(Sample.ColorPresetIndex);

			if (EnumHasAnyFlags(DrawParams.Flags, EDebugDrawFlags::DrawFast | EDebugDrawFlags::DrawSearchIndex))
			{
				DrawDebugPoint(DrawParams.World, TrajectoryPos, DrawParams.PointSize, Color, bPersistent, LifeTime, DepthPriority);
			}
			else
			{
				DrawDebugSphere(DrawParams.World, TrajectoryPos, 2.f, 8, Color, bPersistent, LifeTime, DepthPriority);
			}

			TrajSplinePos.Add(TrajectoryPos);
			TrajSplineColor.Add(Color);
		}
		if (EnumHasAnyFlags(Sample.Flags, EPoseSearchTrajectoryFlags::PositionXY))
		{
			FVector2D TrajectoryPos2D = FFeatureVectorHelper::DecodeVector2D(PoseVector, DataOffset);
			if (!bIsTrajectoryPosValid)
			{
				TrajectoryPos = FVector(TrajectoryPos2D.X, TrajectoryPos2D.Y, 0);
				TrajectoryPos = DrawParams.RootTransform.TransformPosition(TrajectoryPos);
				bIsTrajectoryPosValid = true;

				const FColor Color = DrawParams.GetColor(Sample.ColorPresetIndex);

				if (EnumHasAnyFlags(DrawParams.Flags, EDebugDrawFlags::DrawFast | EDebugDrawFlags::DrawSearchIndex))
				{
					DrawDebugPoint(DrawParams.World, TrajectoryPos, DrawParams.PointSize, Color, bPersistent, LifeTime, DepthPriority);
				}
				else
				{
					DrawDebugSphere(DrawParams.World, TrajectoryPos, 2.f, 8, Color, bPersistent, LifeTime, DepthPriority);
				}

				TrajSplinePos.Add(TrajectoryPos);
				TrajSplineColor.Add(Color);
			}
		}
		
		if (!bIsTrajectoryPosValid)
		{
			TrajectoryPos = TrajectoryPositionReconstructor.GetReconstructedTrajectoryPos(*this, PoseVector, DrawParams.RootTransform, Sample.Offset);
			
			TrajSplinePos.Add(TrajectoryPos);
			const FColor Color = TrajSplineColor.IsEmpty() ? FColor::Black : TrajSplineColor.Last();
			TrajSplineColor.Add(Color);
		}

		if (EnumHasAnyFlags(Sample.Flags, EPoseSearchTrajectoryFlags::Velocity))
		{
			FVector TrajectoryVel = FFeatureVectorHelper::DecodeVector(PoseVector, DataOffset);

			const FColor Color = DrawParams.GetColor(Sample.ColorPresetIndex);

			TrajectoryVel *= 0.08f;
			TrajectoryVel = DrawParams.RootTransform.TransformVector(TrajectoryVel);
			const FVector TrajectoryVelDirection = TrajectoryVel.GetSafeNormal();

			if (EnumHasAnyFlags(DrawParams.Flags, EDebugDrawFlags::DrawSearchIndex))
			{
				DrawDebugLine(DrawParams.World, TrajectoryPos, TrajectoryPos + TrajectoryVel, Color, bPersistent, LifeTime, DepthPriority);
			}
			else
			{
				const float AdjustedThickness = EnumHasAnyFlags(DrawParams.Flags, EDebugDrawFlags::DrawFast) ? 0.0f : 1.f;

				DrawDebugLine(
					DrawParams.World,
					TrajectoryPos + TrajectoryVelDirection * 2.f,
					TrajectoryPos + TrajectoryVel,
					Color,
					bPersistent,
					LifeTime,
					DepthPriority,
					AdjustedThickness
				);
			}
		}
		if (EnumHasAnyFlags(Sample.Flags, EPoseSearchTrajectoryFlags::VelocityXY))
		{
			const FVector2D TrajectoryVel2D = FFeatureVectorHelper::DecodeVector2D(PoseVector, DataOffset);
			FVector TrajectoryVel(TrajectoryVel2D.X, TrajectoryVel2D.Y, 0.f);

			const FColor Color = DrawParams.GetColor(Sample.ColorPresetIndex);

			TrajectoryVel *= 0.08f;
			TrajectoryVel = DrawParams.RootTransform.TransformVector(TrajectoryVel);
			const FVector TrajectoryVelDirection = TrajectoryVel.GetSafeNormal();

			if (EnumHasAnyFlags(DrawParams.Flags, EDebugDrawFlags::DrawSearchIndex))
			{
				DrawDebugLine(DrawParams.World, TrajectoryPos, TrajectoryPos + TrajectoryVel, Color, bPersistent, LifeTime, DepthPriority);
			}
			else
			{
				const float AdjustedThickness = EnumHasAnyFlags(DrawParams.Flags, EDebugDrawFlags::DrawFast) ? 0.0f : 1.f;

				DrawDebugLine(
					DrawParams.World,
					TrajectoryPos + TrajectoryVelDirection * 2.f,
					TrajectoryPos + TrajectoryVel,
					Color,
					bPersistent,
					LifeTime,
					DepthPriority,
					AdjustedThickness
				);
			}
		}

		if (EnumHasAnyFlags(Sample.Flags, EPoseSearchTrajectoryFlags::VelocityDirection))
		{
			FVector TrajectoryVelDirection = FFeatureVectorHelper::DecodeVector(PoseVector, DataOffset);

			const FColor Color = DrawParams.GetColor(Sample.ColorPresetIndex);

			TrajectoryVelDirection = DrawParams.RootTransform.TransformVector(TrajectoryVelDirection);

			if (EnumHasAnyFlags(DrawParams.Flags, EDebugDrawFlags::DrawSearchIndex))
			{
				DrawDebugLine(DrawParams.World, TrajectoryPos, TrajectoryPos + TrajectoryVelDirection, Color, bPersistent, LifeTime, DepthPriority);
			}
			else
			{
				const float AdjustedThickness = EnumHasAnyFlags(DrawParams.Flags, EDebugDrawFlags::DrawFast) ? 0.0f : 1.f;

				DrawDebugLine(
					DrawParams.World,
					TrajectoryPos + TrajectoryVelDirection * 2.f,
					TrajectoryPos + TrajectoryVelDirection * 2.f * 10.0f,
					Color,
					bPersistent,
					LifeTime,
					DepthPriority,
					AdjustedThickness
				);
			}
		}

		if (EnumHasAnyFlags(Sample.Flags, EPoseSearchTrajectoryFlags::VelocityDirectionXY))
		{
			const FVector2D TrajectoryVelDirection2D = FFeatureVectorHelper::DecodeVector2D(PoseVector, DataOffset);
			FVector TrajectoryVelDirection(TrajectoryVelDirection2D.X, TrajectoryVelDirection2D.Y, 0.f);

			const FColor Color = DrawParams.GetColor(Sample.ColorPresetIndex);

			TrajectoryVelDirection = DrawParams.RootTransform.TransformVector(TrajectoryVelDirection);

			if (EnumHasAnyFlags(DrawParams.Flags, EDebugDrawFlags::DrawSearchIndex))
			{
				DrawDebugLine(DrawParams.World, TrajectoryPos, TrajectoryPos + TrajectoryVelDirection, Color, bPersistent, LifeTime, DepthPriority);
			}
			else
			{
				const float AdjustedThickness = EnumHasAnyFlags(DrawParams.Flags, EDebugDrawFlags::DrawFast) ? 0.0f : 1.f;

				DrawDebugLine(
					DrawParams.World,
					TrajectoryPos + TrajectoryVelDirection * 2.f,
					TrajectoryPos + TrajectoryVelDirection * 2.f * 10.0f,
					Color,
					bPersistent,
					LifeTime,
					DepthPriority,
					AdjustedThickness
				);
			}
		}

		if (EnumHasAnyFlags(Sample.Flags, EPoseSearchTrajectoryFlags::FacingDirection))
		{
			FVector TrajectoryForward = FFeatureVectorHelper::DecodeVector(PoseVector, DataOffset);

			const FColor Color = DrawParams.GetColor(Sample.ColorPresetIndex);

			TrajectoryForward = DrawParams.RootTransform.TransformVector(TrajectoryForward);

			if (EnumHasAnyFlags(DrawParams.Flags, EDebugDrawFlags::DrawSearchIndex))
			{
				DrawDebugLine(DrawParams.World, TrajectoryPos, TrajectoryPos + TrajectoryForward, Color, bPersistent, LifeTime, DepthPriority);
			}
			else
			{
				const float AdjustedThickness = EnumHasAnyFlags(DrawParams.Flags, EDebugDrawFlags::DrawFast) ? 0.0f : 1.f;

				DrawDebugLine(
					DrawParams.World,
					TrajectoryPos + TrajectoryForward * 2.f,
					TrajectoryPos + TrajectoryForward * 2.f * 10.0f,
					Color,
					bPersistent,
					LifeTime,
					DepthPriority,
					AdjustedThickness
				);
			}
		}

		if (EnumHasAnyFlags(Sample.Flags, EPoseSearchTrajectoryFlags::FacingDirectionXY))
		{
			const FVector2D TrajectoryForward2D = FFeatureVectorHelper::DecodeVector2D(PoseVector, DataOffset);
			FVector TrajectoryForward(TrajectoryForward2D.X, TrajectoryForward2D.Y, 0.f);

			const FColor Color = DrawParams.GetColor(Sample.ColorPresetIndex);

			TrajectoryForward = DrawParams.RootTransform.TransformVector(TrajectoryForward);

			if (EnumHasAnyFlags(DrawParams.Flags, EDebugDrawFlags::DrawSearchIndex))
			{
				DrawDebugLine(DrawParams.World, TrajectoryPos, TrajectoryPos + TrajectoryForward, Color, bPersistent, LifeTime, DepthPriority);
			}
			else
			{
				const float AdjustedThickness = EnumHasAnyFlags(DrawParams.Flags, EDebugDrawFlags::DrawFast) ? 0.0f : 1.f;

				DrawDebugLine(
					DrawParams.World,
					TrajectoryPos + TrajectoryForward * 2.f,
					TrajectoryPos + TrajectoryForward * 2.f * 10.0f,
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
			const FColor Color = DrawParams.GetColor(Sample.ColorPresetIndex);

			const FString SampleLabel = FString::Format(TEXT("{0}"), { SampleIdx });

			static const FVector DrawDebugSampleLabelOffset = FVector(0.0f, 0.0f, 5.0f);
			DrawDebugString(
				DrawParams.World,
				TrajectoryPos + DrawDebugSampleLabelOffset,
				SampleLabel,
				nullptr,
				Color,
				LifeTime,
				false,
				1.5f);
		}

		++SampleIdx;
	}

	DrawCentripetalCatmullRomSpline(DrawParams.World, TrajSplinePos, TrajSplineColor, 0.5f, 8.f, bPersistent, LifeTime, DepthPriority, 0.f);

	check(DataOffset == ChannelDataOffset + ChannelCardinality);
#endif // ENABLE_DRAW_DEBUG
}

#if WITH_EDITOR
void UPoseSearchFeatureChannel_Trajectory::PopulateChannelLayoutSet(UE::PoseSearch::FFeatureChannelLayoutSet& FeatureChannelLayoutSet) const
{
	using namespace UE::PoseSearch;

	int32 DataOffset = ChannelDataOffset;

	auto Add = [&FeatureChannelLayoutSet, &DataOffset](EPoseSearchTrajectoryFlags SampleFlag, float Offset, const char* Label, int32 Cardinality)
	{
		FString SkeletonName = FeatureChannelLayoutSet.CurrentSchema->Skeleton->GetName();

		UE::PoseSearch::FKeyBuilder KeyBuilder;
		KeyBuilder << SkeletonName << SampleFlag << Offset;
		FeatureChannelLayoutSet.Add(FString::Format(TEXT("Traj {0} {1}"), { Label, Offset }), KeyBuilder.Finalize(), DataOffset, Cardinality);

		DataOffset += Cardinality;
	};

	for (const FPoseSearchTrajectorySample& Sample : Samples)
	{
		if (EnumHasAnyFlags(Sample.Flags, EPoseSearchTrajectoryFlags::Position))
		{
			Add(EPoseSearchTrajectoryFlags::Position, Sample.Offset, "Pos", FFeatureVectorHelper::EncodeVectorCardinality);
		}
		if (EnumHasAnyFlags(Sample.Flags, EPoseSearchTrajectoryFlags::PositionXY))
		{
			Add(EPoseSearchTrajectoryFlags::PositionXY, Sample.Offset, "PosXY", FFeatureVectorHelper::EncodeVector2DCardinality);
		}

		if (EnumHasAnyFlags(Sample.Flags, EPoseSearchTrajectoryFlags::Velocity))
		{
			Add(EPoseSearchTrajectoryFlags::Velocity, Sample.Offset, "Vel", FFeatureVectorHelper::EncodeVectorCardinality);
		}
		if (EnumHasAnyFlags(Sample.Flags, EPoseSearchTrajectoryFlags::VelocityXY))
		{
			Add(EPoseSearchTrajectoryFlags::VelocityXY, Sample.Offset, "VelXY", FFeatureVectorHelper::EncodeVector2DCardinality);
		}

		if (EnumHasAnyFlags(Sample.Flags, EPoseSearchTrajectoryFlags::VelocityDirection))
		{
			Add(EPoseSearchTrajectoryFlags::VelocityDirection, Sample.Offset, "VelDir", FFeatureVectorHelper::EncodeVectorCardinality);
		}
		if (EnumHasAnyFlags(Sample.Flags, EPoseSearchTrajectoryFlags::VelocityDirectionXY))
		{
			Add(EPoseSearchTrajectoryFlags::VelocityDirectionXY, Sample.Offset, "VelDirXY", FFeatureVectorHelper::EncodeVector2DCardinality);
		}

		if (EnumHasAnyFlags(Sample.Flags, EPoseSearchTrajectoryFlags::FacingDirection))
		{
			Add(EPoseSearchTrajectoryFlags::FacingDirection, Sample.Offset, "Fac", FFeatureVectorHelper::EncodeVectorCardinality);
		}
		if (EnumHasAnyFlags(Sample.Flags, EPoseSearchTrajectoryFlags::FacingDirectionXY))
		{
			Add(EPoseSearchTrajectoryFlags::FacingDirectionXY, Sample.Offset, "FacXY", FFeatureVectorHelper::EncodeVector2DCardinality);
		}
	}

	check(DataOffset == ChannelDataOffset + ChannelCardinality);
}

void UPoseSearchFeatureChannel_Trajectory::ComputeCostBreakdowns(UE::PoseSearch::ICostBreakDownData& CostBreakDownData, const UPoseSearchSchema* Schema) const
{
	using namespace UE::PoseSearch;

	CostBreakDownData.AddEntireBreakDownSection(LOCTEXT("ColumnLabelTrajChannelTotal", "Traj Total"), Schema, ChannelDataOffset, ChannelCardinality);

	if (CostBreakDownData.IsVerbose())
	{
		int32 DataOffset = ChannelDataOffset;

		for (const FPoseSearchTrajectorySample& Sample : Samples)
		{
			if (EnumHasAnyFlags(Sample.Flags, EPoseSearchTrajectoryFlags::Position))
			{
				CostBreakDownData.AddEntireBreakDownSection(FText::Format(LOCTEXT("ColumnLabelTrajChannelPosition", "Traj Pos {0}"), Sample.Offset), Schema, DataOffset, FFeatureVectorHelper::EncodeVectorCardinality);
				DataOffset += FFeatureVectorHelper::EncodeVectorCardinality;
			}
			if (EnumHasAnyFlags(Sample.Flags, EPoseSearchTrajectoryFlags::PositionXY))
			{
				CostBreakDownData.AddEntireBreakDownSection(FText::Format(LOCTEXT("ColumnLabelTrajChannelPositionXY", "Traj PosXY {0}"), Sample.Offset), Schema, DataOffset, FFeatureVectorHelper::EncodeVector2DCardinality);
				DataOffset += FFeatureVectorHelper::EncodeVector2DCardinality;
			}

			if (EnumHasAnyFlags(Sample.Flags, EPoseSearchTrajectoryFlags::Velocity))
			{
				CostBreakDownData.AddEntireBreakDownSection(FText::Format(LOCTEXT("ColumnLabelTrajChannelVelocity", "Traj Vel {0}"), Sample.Offset), Schema, DataOffset, FFeatureVectorHelper::EncodeVectorCardinality);
				DataOffset += FFeatureVectorHelper::EncodeVectorCardinality;
			}
			if (EnumHasAnyFlags(Sample.Flags, EPoseSearchTrajectoryFlags::VelocityXY))
			{
				CostBreakDownData.AddEntireBreakDownSection(FText::Format(LOCTEXT("ColumnLabelTrajChannelVelocityXY", "Traj VelXY {0}"), Sample.Offset), Schema, DataOffset, FFeatureVectorHelper::EncodeVector2DCardinality);
				DataOffset += FFeatureVectorHelper::EncodeVector2DCardinality;
			}

			if (EnumHasAnyFlags(Sample.Flags, EPoseSearchTrajectoryFlags::VelocityDirection))
			{
				CostBreakDownData.AddEntireBreakDownSection(FText::Format(LOCTEXT("ColumnLabelTrajChannelVelocityDirection", "Traj VelDir {0}"), Sample.Offset), Schema, DataOffset, FFeatureVectorHelper::EncodeVectorCardinality);
				DataOffset += FFeatureVectorHelper::EncodeVectorCardinality;
			}
			if (EnumHasAnyFlags(Sample.Flags, EPoseSearchTrajectoryFlags::VelocityDirectionXY))
			{
				CostBreakDownData.AddEntireBreakDownSection(FText::Format(LOCTEXT("ColumnLabelTrajChannelVelocityDirectionXY", "Traj VelDirXY {0}"), Sample.Offset), Schema, DataOffset, FFeatureVectorHelper::EncodeVector2DCardinality);
				DataOffset += FFeatureVectorHelper::EncodeVector2DCardinality;
			}

			if (EnumHasAnyFlags(Sample.Flags, EPoseSearchTrajectoryFlags::FacingDirection))
			{
				CostBreakDownData.AddEntireBreakDownSection(FText::Format(LOCTEXT("ColumnLabelTrajChannelFacingDirection", "Traj Fac {0}"), Sample.Offset), Schema, DataOffset, FFeatureVectorHelper::EncodeVectorCardinality);
				DataOffset += FFeatureVectorHelper::EncodeVectorCardinality;
			}
			if (EnumHasAnyFlags(Sample.Flags, EPoseSearchTrajectoryFlags::FacingDirectionXY))
			{
				CostBreakDownData.AddEntireBreakDownSection(FText::Format(LOCTEXT("ColumnLabelTrajChannelFacingDirectionXY", "Traj FacXY {0}"), Sample.Offset), Schema, DataOffset, FFeatureVectorHelper::EncodeVector2DCardinality);
				DataOffset += FFeatureVectorHelper::EncodeVector2DCardinality;
			}
		}

		check(DataOffset == ChannelDataOffset + ChannelCardinality);
	}
}

#endif // WITH_EDITOR

bool UPoseSearchFeatureChannel_Trajectory::GetEstimatedSpeedRatio(TConstArrayView<float> QueryVector, TConstArrayView<float> PoseVector, float& EstimatedSpeedRatio) const
{
	float EstimatedQuerySpeed = 0.f;
	float EstimatedPoseSpeed = 0.f;

	int32 QueryDataOffset = ChannelDataOffset;
	int32 PoseDataOffset = ChannelDataOffset;

	bool bValidEstimate = false;
	for (const FPoseSearchTrajectorySample& Sample : Samples)
	{
		// @todo: decode positions and use them to estimate velocities in case UPoseSearchFeatureChannel_Trajectory doens't contain EPoseSearchTrajectoryFlags::Velocity or EPoseSearchTrajectoryFlags::VelocityXY samples
		if (EnumHasAnyFlags(Sample.Flags, EPoseSearchTrajectoryFlags::Position))
		{
			QueryDataOffset += UE::PoseSearch::FFeatureVectorHelper::EncodeVectorCardinality;
			PoseDataOffset += UE::PoseSearch::FFeatureVectorHelper::EncodeVectorCardinality;
		}
		if (EnumHasAnyFlags(Sample.Flags, EPoseSearchTrajectoryFlags::PositionXY))
		{
			QueryDataOffset += UE::PoseSearch::FFeatureVectorHelper::EncodeVector2DCardinality;
			PoseDataOffset += UE::PoseSearch::FFeatureVectorHelper::EncodeVector2DCardinality;
		}

		if (EnumHasAnyFlags(Sample.Flags, EPoseSearchTrajectoryFlags::Velocity))
		{
			const FVector QueryVelocity = UE::PoseSearch::FFeatureVectorHelper::DecodeVector(QueryVector, QueryDataOffset);
			const FVector PoseVelocity = UE::PoseSearch::FFeatureVectorHelper::DecodeVector(PoseVector, PoseDataOffset);
			EstimatedQuerySpeed += QueryVelocity.Length();
			EstimatedPoseSpeed += PoseVelocity.Length();
			bValidEstimate = true;
		}
		if (EnumHasAnyFlags(Sample.Flags, EPoseSearchTrajectoryFlags::VelocityXY))
		{
			const FVector2D QueryVelocity = UE::PoseSearch::FFeatureVectorHelper::DecodeVector2D(QueryVector, QueryDataOffset);
			const FVector2D PoseVelocity = UE::PoseSearch::FFeatureVectorHelper::DecodeVector2D(PoseVector, PoseDataOffset);
			EstimatedQuerySpeed += QueryVelocity.Length();
			EstimatedPoseSpeed += PoseVelocity.Length();
			bValidEstimate = true;
		}

		if (EnumHasAnyFlags(Sample.Flags, EPoseSearchTrajectoryFlags::VelocityDirection))
		{
			QueryDataOffset += UE::PoseSearch::FFeatureVectorHelper::EncodeVectorCardinality;
			PoseDataOffset += UE::PoseSearch::FFeatureVectorHelper::EncodeVectorCardinality;
		}
		if (EnumHasAnyFlags(Sample.Flags, EPoseSearchTrajectoryFlags::VelocityDirectionXY))
		{
			QueryDataOffset += UE::PoseSearch::FFeatureVectorHelper::EncodeVector2DCardinality;
			PoseDataOffset += UE::PoseSearch::FFeatureVectorHelper::EncodeVector2DCardinality;
		}
		if (EnumHasAnyFlags(Sample.Flags, EPoseSearchTrajectoryFlags::FacingDirection))
		{
			QueryDataOffset += UE::PoseSearch::FFeatureVectorHelper::EncodeVectorCardinality;
			PoseDataOffset += UE::PoseSearch::FFeatureVectorHelper::EncodeVectorCardinality;
		}
		if (EnumHasAnyFlags(Sample.Flags, EPoseSearchTrajectoryFlags::FacingDirectionXY))
		{
			QueryDataOffset += UE::PoseSearch::FFeatureVectorHelper::EncodeVector2DCardinality;
			PoseDataOffset += UE::PoseSearch::FFeatureVectorHelper::EncodeVector2DCardinality;
		}
	}

	check(QueryDataOffset == ChannelDataOffset + ChannelCardinality);
	check(PoseDataOffset == ChannelDataOffset + ChannelCardinality);

	if (bValidEstimate && EstimatedPoseSpeed > UE_KINDA_SMALL_NUMBER)
	{
		EstimatedSpeedRatio = EstimatedQuerySpeed / EstimatedPoseSpeed;
	}
	else
	{
		EstimatedSpeedRatio = 1.f;
	}

	return bValidEstimate;
}

#undef LOCTEXT_NAMESPACE