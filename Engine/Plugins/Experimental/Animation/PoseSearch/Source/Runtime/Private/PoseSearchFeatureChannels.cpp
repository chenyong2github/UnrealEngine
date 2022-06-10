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


//////////////////////////////////////////////////////////////////////////
// Drawing helpers

static FLinearColor GetColorForFeature(FPoseSearchFeatureDesc Feature, const FPoseSearchFeatureVectorLayout* Layout)
{
	const float FeatureIdx = Layout->Features.IndexOfByKey(Feature);
	const float FeatureCountIdx = Layout->Features.Num() - 1;
	const float FeatureCountIdxHalf = FeatureCountIdx / 2.f;
	check(FeatureIdx != INDEX_NONE);

	const float Hue = FeatureIdx < FeatureCountIdxHalf
		? FMath::GetMappedRangeValueUnclamped({ 0.f, FeatureCountIdxHalf }, FVector2f(60.f, 0.f), FeatureIdx)
		: FMath::GetMappedRangeValueUnclamped({ FeatureCountIdxHalf, FeatureCountIdx }, FVector2f(280.f, 220.f), FeatureIdx);

	const FLinearColor ColorHSV(Hue, 1.f, 1.f);
	return ColorHSV.HSVToLinearRGB();
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
	auto AddFeatures = [this, &Initializer](EPoseSearchFeatureType Type)
	{
		FPoseSearchFeatureDesc Feature;
		Feature.Type = Type;
		Feature.ChannelIdx = GetChannelIndex();

		const int32 NumBones = SampledBones.Num();
		for (int32 SubsampleIdx = 0; SubsampleIdx != SampleTimes.Num(); ++SubsampleIdx)
		{
			Feature.SubsampleIdx = SubsampleIdx;

			for (int32 ChannelBoneIdx = 0; ChannelBoneIdx != NumBones; ++ChannelBoneIdx)
			{
				Feature.ChannelFeatureId = ChannelBoneIdx;

				const FPoseSearchBone& SampledBone = SampledBones[ChannelBoneIdx];
				bool bAddFeature = (SampledBone.GetTypeMask() & (1 << static_cast<int>(Type))) != 0;
				if (bAddFeature)
				{
					Initializer.AddFeatureDesc(Feature);
				}
			}
		}
	};

	for (int32 FeatureType = 0; FeatureType != (int32)EPoseSearchFeatureType::Num; ++FeatureType)
	{
		AddFeatures(EPoseSearchFeatureType(FeatureType));
	}

	FeatureParams.Reset();
	for (const FPoseSearchBone& Bone: SampledBones)
	{
		FPoseSearchPoseFeatureInfo FeatureInfo;
		FeatureInfo.SchemaBoneIdx = Initializer.AddBoneReference(Bone.Reference);
		FeatureParams.Add(FeatureInfo);
	}
}

void UPoseSearchFeatureChannel_Pose::IndexAsset(const UE::PoseSearch::IAssetIndexer& Indexer,  UE::PoseSearch::FAssetIndexingOutput& IndexingOutput) const
{
	using namespace UE::PoseSearch;

	const FAssetIndexingContext& IndexingContext = Indexer.GetIndexingContext();
	for (int32 SampleIdx = IndexingContext.BeginSampleIdx; SampleIdx != IndexingContext.EndSampleIdx; ++SampleIdx)
	{
		int32 VectorIdx = SampleIdx - IndexingContext.BeginSampleIdx;
		FPoseSearchFeatureVectorBuilder& FeatureVector = IndexingOutput.PoseVectors[VectorIdx];
		AddPoseFeatures(Indexer, SampleIdx, FeatureVector);
	}
}

void UPoseSearchFeatureChannel_Pose::AddPoseFeatures(const  UE::PoseSearch::IAssetIndexer& Indexer, int32 SampleIdx, FPoseSearchFeatureVectorBuilder& FeatureVector) const
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
		Samples[0] = Indexer.GetSampleInfoRelative(SubsampleTime - SamplingContext->FiniteDelta, Origin);
		Samples[1] = Indexer.GetSampleInfoRelative(SubsampleTime, Origin);
		Samples[2] = Indexer.GetSampleInfoRelative(SubsampleTime + SamplingContext->FiniteDelta, Origin);

		// Get pose samples
		for (int32 Term = 0; Term != NumFiniteDiffTerms; ++Term)
		{
			float CurrentTime = Samples[Term].ClipTime;
			float PreviousTime = CurrentTime - SamplingContext->FiniteDelta;

			FDeltaTimeRecord DeltaTimeRecord;
			DeltaTimeRecord.Set(PreviousTime, CurrentTime - PreviousTime);
			FAnimExtractContext ExtractionCtx(static_cast<double>(CurrentTime), true, DeltaTimeRecord, Samples[Term].Clip->IsLoopable());

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

bool UPoseSearchFeatureChannel_Pose::BuildQuery(UE::PoseSearch::FQueryBuildingContext& Context) const
{
	check(Context.Schema);
	
	if (!Context.History)
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
		if (!Context.History->TrySamplePose(SecondsAgo, Context.Schema->Skeleton->GetReferenceSkeleton(), Context.Schema->BoneIndicesWithParents))
		{
			return false;
		}

		TArrayView<const FTransform> ComponentPose = Context.History->GetComponentPoseSample();
		TArrayView<const FTransform> ComponentPrevPose = Context.History->GetPrevComponentPoseSample();
		FTransform RootTransform = Context.History->GetRootTransformSample();
		FTransform RootTransformPrev = Context.History->GetPrevRootTransformSample();
		for (int32 SampledBoneIdx = 0; SampledBoneIdx != SampledBones.Num(); ++SampledBoneIdx)
		{
			Feature.ChannelFeatureId = SampledBoneIdx;

			int32 SchemaBoneIdx = FeatureParams[SampledBoneIdx].SchemaBoneIdx;

			int32 SkeletonBoneIndex = Context.Schema->BoneIndices[SchemaBoneIdx];

			const FTransform& Transform = ComponentPose[SkeletonBoneIndex];
			const FTransform& PrevTransform = ComponentPrevPose[SkeletonBoneIndex] * (RootTransformPrev * RootTransform.Inverse());
			Context.Query.SetTransform(Feature, Transform);
			Context.Query.SetTransformVelocity(Feature, Transform, PrevTransform, Context.History->GetSampleTimeInterval());
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
			const bool bHaveBonePos = Reader.GetPosition(Feature, &BonePos);
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
	auto AddFeatures = [this, &Initializer](int32 NumSampleOffsets, EPoseSearchFeatureType Type)
	{
		FPoseSearchFeatureDesc Feature;
		Feature.ChannelIdx = GetChannelIndex();
		Feature.Type = Type;
		Feature.ChannelFeatureId = 0; // Unused

		for (Feature.SubsampleIdx = 0; Feature.SubsampleIdx != NumSampleOffsets; ++Feature.SubsampleIdx)
		{
			Initializer.AddFeatureDesc(Feature);
		}
	};

	if (bUsePositions)
	{
		AddFeatures(SampleOffsets.Num(), EPoseSearchFeatureType::Position);
	}

	if (bUseLinearVelocities)
	{
		AddFeatures(SampleOffsets.Num(), EPoseSearchFeatureType::LinearVelocity);
	}

	if (bUseFacingDirections)
	{
		AddFeatures(SampleOffsets.Num(), EPoseSearchFeatureType::ForwardVector);
	}
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

bool UPoseSearchFeatureChannel_Trajectory::BuildQuery(UE::PoseSearch::FQueryBuildingContext& Context) const
{
	if (!Context.Trajectory)
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
		const FTrajectorySample Sample = FTrajectorySampleRange::IterSampleTrajectory(Context.Trajectory->Samples, SampleDomain, SampleOffset, NextIterStartIdx);

		Feature.SubsampleIdx = Idx;

		Feature.Type = EPoseSearchFeatureType::LinearVelocity;
		Context.Query.SetVector(Feature, Sample.LinearVelocity);

		Context.Query.SetTransform(Feature, Sample.Transform);
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