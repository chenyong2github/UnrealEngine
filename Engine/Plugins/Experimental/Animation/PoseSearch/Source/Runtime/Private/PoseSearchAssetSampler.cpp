// Copyright Epic Games, Inc. All Rights Reserved.

#include "PoseSearch/PoseSearchAssetSampler.h"
#include "AnimationRuntime.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimRootMotionProvider.h"
#include "Animation/BlendSpace.h"
#include "Animation/MirrorDataTable.h"
#include "PoseSearch/PoseSearchAnimNotifies.h"
#include "PoseSearch/PoseSearchDefines.h"

namespace UE::PoseSearch
{

//////////////////////////////////////////////////////////////////////////
// Root motion extrapolation

static FTransform ExtrapolateRootMotion(
	FTransform SampleToExtrapolate,
	float SampleStart, 
	float SampleEnd, 
	float ExtrapolationTime)
{
	const float SampleDelta = SampleEnd - SampleStart;
	check(!FMath::IsNearlyZero(SampleDelta));

	// converting ExtrapolationTime to a positive number to avoid dealing with the negative extrapolation and inverting
	// transforms later on.
	const float AbsExtrapolationTime = FMath::Abs(ExtrapolationTime);
	const float AbsSampleDelta = FMath::Abs(SampleDelta);
	const FTransform AbsTimeSampleToExtrapolate = ExtrapolationTime >= 0.0f ? SampleToExtrapolate : SampleToExtrapolate.Inverse();

	// because we're extrapolating rotation, the extrapolation must be integrated over time
	const float SampleMultiplier = AbsExtrapolationTime / AbsSampleDelta;
	float IntegralNumSamples;
	float RemainingSampleFraction = FMath::Modf(SampleMultiplier, &IntegralNumSamples);
	int32 NumSamples = (int32)IntegralNumSamples;

	// adding full samples to the extrapolated root motion
	FTransform ExtrapolatedRootMotion = FTransform::Identity;
	for (int i = 0; i < NumSamples; ++i)
	{
		ExtrapolatedRootMotion = AbsTimeSampleToExtrapolate * ExtrapolatedRootMotion;
	}

	// and a blend with identity for whatever is left
	FTransform RemainingExtrapolatedRootMotion;
	RemainingExtrapolatedRootMotion.Blend(
		FTransform::Identity,
		AbsTimeSampleToExtrapolate,
		RemainingSampleFraction);

	ExtrapolatedRootMotion = RemainingExtrapolatedRootMotion * ExtrapolatedRootMotion;
	return ExtrapolatedRootMotion;
}

//////////////////////////////////////////////////////////////////////////
// FSequenceBaseSampler
void FSequenceBaseSampler::Init(const FInput& InInput)
{
	check(InInput.SequenceBase.Get());
	Input = InInput;
}

void FSequenceBaseSampler::Process()
{
}

float FSequenceBaseSampler::GetPlayLength() const
{
	return Input.SequenceBase->GetPlayLength();
}

float FSequenceBaseSampler::GetScaledTime(float Time) const
{
	return Time;
}

bool FSequenceBaseSampler::IsLoopable() const
{
	return Input.SequenceBase->bLoop;
}

void FSequenceBaseSampler::ExtractPose(const FAnimExtractContext& ExtractionCtx, FAnimationPoseData& OutAnimPoseData) const
{
	Input.SequenceBase->GetAnimationPose(OutAnimPoseData, ExtractionCtx);
}

FTransform FSequenceBaseSampler::GetTotalRootTransform() const
{
	const FTransform InitialRootTransform = Input.SequenceBase->ExtractRootTrackTransform(0.f, nullptr);
	const FTransform LastRootTransform = Input.SequenceBase->ExtractRootTrackTransform(Input.SequenceBase->GetPlayLength(), nullptr);
	const FTransform TotalRootTransform = LastRootTransform.GetRelativeTransform(InitialRootTransform);
	return TotalRootTransform;
}

FTransform FSequenceBaseSampler::ExtractRootTransform(float Time) const
{
	FTransform RootTransform = FTransform::Identity;
	const UAnimSequenceBase* SequenceBase = Input.SequenceBase.Get();

	if (IsLoopable())
	{
		RootTransform = SequenceBase->ExtractRootMotion(0.0f, Time, true);
	}
	else
	{
		const float PlayLength = SequenceBase->GetPlayLength();
		const float ClampedTime = FMath::Clamp(Time, 0.0f, PlayLength);
		const float ExtrapolationTime = Time - ClampedTime;

		// If Time is less than zero, ExtrapolationTime will be negative. In this case, we extrapolate the beginning of the 
		// animation to estimate where the root would be at Time
		if (ExtrapolationTime < -SMALL_NUMBER)
		{
			FTransform SampleToExtrapolate = SequenceBase->ExtractRootMotionFromRange(0.0f, ExtrapolationSampleTime);

			const FTransform ExtrapolatedRootMotion = UE::PoseSearch::ExtrapolateRootMotion(
				SampleToExtrapolate,
				0.0f, ExtrapolationSampleTime,
				ExtrapolationTime);
			RootTransform = ExtrapolatedRootMotion;
		}
		else
		{
			RootTransform = SequenceBase->ExtractRootMotionFromRange(0.0f, ClampedTime);

			// If Time is greater than PlayLength, ExtrapolationTime will be a positive number. In this case, we extrapolate
			// the end of the animation to estimate where the root would be at Time
			if (ExtrapolationTime > SMALL_NUMBER)
			{
				FTransform SampleToExtrapolate = SequenceBase->ExtractRootMotionFromRange(PlayLength - ExtrapolationSampleTime, PlayLength);

				const FTransform ExtrapolatedRootMotion = UE::PoseSearch::ExtrapolateRootMotion(
					SampleToExtrapolate,
					PlayLength - ExtrapolationSampleTime, PlayLength,
					ExtrapolationTime);
				RootTransform = ExtrapolatedRootMotion * RootTransform;
			}
		}
	}

	return RootTransform;
}

void FSequenceBaseSampler::ExtractPoseSearchNotifyStates(
	float Time, 
	TArray<UAnimNotifyState_PoseSearchBase*>& NotifyStates) const
{
	// getting pose search notifies in an interval of size ExtractionInterval, centered on Time
	FAnimNotifyContext NotifyContext;
	Input.SequenceBase->GetAnimNotifies(Time - (ExtractionInterval * 0.5f), ExtractionInterval, NotifyContext);

	// check which notifies actually overlap Time and are of the right base type
	for (const FAnimNotifyEventReference& EventReference : NotifyContext.ActiveNotifies)
	{
		const FAnimNotifyEvent* NotifyEvent = EventReference.GetNotify();
		if (!NotifyEvent)
		{
			continue;
		}

		if (NotifyEvent->GetTriggerTime() > Time ||
			NotifyEvent->GetEndTriggerTime() < Time)
		{
			continue;
		}

		UAnimNotifyState_PoseSearchBase* PoseSearchAnimNotify = 
			Cast<UAnimNotifyState_PoseSearchBase>(NotifyEvent->NotifyStateClass);
		if (PoseSearchAnimNotify)
		{
			NotifyStates.Add(PoseSearchAnimNotify);
		}
	}
}

const UAnimationAsset* FSequenceBaseSampler::GetAsset() const
{
	return Input.SequenceBase.Get();
}

//////////////////////////////////////////////////////////////////////////
// FBlendSpaceSampler
void FBlendSpaceSampler::Init(const FInput& InInput)
{
	check(InInput.BlendSpace.Get());
	Input = InInput;
}

void FBlendSpaceSampler::Process()
{
	FMemMark Mark(FMemStack::Get());

	ProcessPlayLength();
	ProcessRootTransform();
}

float FBlendSpaceSampler::GetScaledTime(float Time) const
{
	const float ScaledTime = GetPlayLength() > UE_KINDA_SMALL_NUMBER ? Time / GetPlayLength() : 0.f;
	return ScaledTime;
}

bool FBlendSpaceSampler::IsLoopable() const
{
	return Input.BlendSpace->bLoop;
}

void FBlendSpaceSampler::ExtractPose(const FAnimExtractContext& ExtractionCtx, FAnimationPoseData& OutAnimPoseData) const
{
	TArray<FBlendSampleData> BlendSamples;
	int32 TriangulationIndex = 0;
	Input.BlendSpace->GetSamplesFromBlendInput(Input.BlendParameters, BlendSamples, TriangulationIndex, true);

	for (int32 BlendSampleIdex = 0; BlendSampleIdex < BlendSamples.Num(); BlendSampleIdex++)
	{
		float Scale = BlendSamples[BlendSampleIdex].Animation->GetPlayLength() / PlayLength;

		FDeltaTimeRecord BlendSampleDeltaTimeRecord;
		BlendSampleDeltaTimeRecord.Set(ExtractionCtx.DeltaTimeRecord.GetPrevious() * Scale, ExtractionCtx.DeltaTimeRecord.Delta * Scale);

		BlendSamples[BlendSampleIdex].DeltaTimeRecord = BlendSampleDeltaTimeRecord;
		BlendSamples[BlendSampleIdex].PreviousTime = ExtractionCtx.DeltaTimeRecord.GetPrevious() * Scale;
		BlendSamples[BlendSampleIdex].Time = ExtractionCtx.CurrentTime * Scale;
	}

	Input.BlendSpace->GetAnimationPose(BlendSamples, ExtractionCtx, OutAnimPoseData);
}

FTransform FBlendSpaceSampler::GetTotalRootTransform() const
{
	const FTransform InitialRootTransform = ExtractBlendSpaceRootTrackTransform(0.f);
	const FTransform LastRootTransform = ExtractBlendSpaceRootTrackTransform(PlayLength);
	const FTransform TotalRootTransform = LastRootTransform.GetRelativeTransform(InitialRootTransform);
	return TotalRootTransform;
}

FTransform FBlendSpaceSampler::ExtractRootTransform(float Time) const
{
	FTransform RootTransform = FTransform::Identity;
	if (IsLoopable())
	{
		RootTransform = ExtractBlendSpaceRootMotion(0.0f, Time, true);
	}
	else
	{
		const float ClampedTime = FMath::Clamp(Time, 0.0f, PlayLength);
		const float ExtrapolationTime = Time - ClampedTime;


		// If Time is less than zero, ExtrapolationTime will be negative. In this case, we extrapolate the beginning of the 
		// animation to estimate where the root would be at Time
		if (ExtrapolationTime < -SMALL_NUMBER)
		{
			FTransform SampleToExtrapolate = ExtractBlendSpaceRootMotionFromRange(0.0f, ExtrapolationSampleTime);

			const FTransform ExtrapolatedRootMotion = UE::PoseSearch::ExtrapolateRootMotion(
				SampleToExtrapolate,
				0.0f, ExtrapolationSampleTime,
				ExtrapolationTime);
			RootTransform = ExtrapolatedRootMotion;
		}
		else
		{
			RootTransform = ExtractBlendSpaceRootMotionFromRange(0.0f, ClampedTime);

			// If Time is greater than PlayLength, ExtrapolationTime will be a positive number. In this case, we extrapolate
			// the end of the animation to estimate where the root would be at Time
			if (ExtrapolationTime > SMALL_NUMBER)
			{
				FTransform SampleToExtrapolate = ExtractBlendSpaceRootMotionFromRange(PlayLength - ExtrapolationSampleTime, PlayLength);

				const FTransform ExtrapolatedRootMotion = UE::PoseSearch::ExtrapolateRootMotion(
					SampleToExtrapolate,
					PlayLength - ExtrapolationSampleTime, PlayLength,
					ExtrapolationTime);
				RootTransform = ExtrapolatedRootMotion * RootTransform;
			}
		}
	}

	return RootTransform;
}

static int32 GetHighestWeightSample(const TArray<struct FBlendSampleData>& SampleDataList)
{
	int32 HighestWeightIndex = 0;
	float HighestWeight = SampleDataList[HighestWeightIndex].GetClampedWeight();
	for (int32 I = 1; I < SampleDataList.Num(); I++)
	{
		if (SampleDataList[I].GetClampedWeight() > HighestWeight)
		{
			HighestWeightIndex = I;
			HighestWeight = SampleDataList[I].GetClampedWeight();
		}
	}
	return HighestWeightIndex;
}

void FBlendSpaceSampler::ExtractPoseSearchNotifyStates(
	float Time,
	TArray<UAnimNotifyState_PoseSearchBase*>& NotifyStates) const
{
	if (Input.BlendSpace->NotifyTriggerMode == ENotifyTriggerMode::HighestWeightedAnimation)
	{
		// Set up blend samples
		TArray<FBlendSampleData> BlendSamples;
		int32 TriangulationIndex = 0;
		Input.BlendSpace->GetSamplesFromBlendInput(Input.BlendParameters, BlendSamples, TriangulationIndex, true);

		// Find highest weighted
		const int32 HighestWeightIndex = GetHighestWeightSample(BlendSamples);

		check(HighestWeightIndex != -1);

		// getting pose search notifies in an interval of size ExtractionInterval, centered on Time
		float SampleTime = Time * (BlendSamples[HighestWeightIndex].Animation->GetPlayLength() / PlayLength);

		// Get notifies for highest weighted
		FAnimNotifyContext NotifyContext;
		BlendSamples[HighestWeightIndex].Animation->GetAnimNotifies(
			(SampleTime - (ExtractionInterval * 0.5f)),
			ExtractionInterval, 
			NotifyContext);

		// check which notifies actually overlap Time and are of the right base type
		for (const FAnimNotifyEventReference& EventReference : NotifyContext.ActiveNotifies)
		{
			const FAnimNotifyEvent* NotifyEvent = EventReference.GetNotify();
			if (!NotifyEvent)
			{
				continue;
			}

			if (NotifyEvent->GetTriggerTime() > SampleTime ||
				NotifyEvent->GetEndTriggerTime() < SampleTime)
			{
				continue;
			}

			UAnimNotifyState_PoseSearchBase* PoseSearchAnimNotify =
				Cast<UAnimNotifyState_PoseSearchBase>(NotifyEvent->NotifyStateClass);
			if (PoseSearchAnimNotify)
			{
				NotifyStates.Add(PoseSearchAnimNotify);
			}
		}
	}
}

FTransform FBlendSpaceSampler::ExtractBlendSpaceRootTrackTransform(float Time) const
{
	checkf(AccumulatedRootTransform.Num() > 0, TEXT("ProcessRootTransform must be run first"));

	const int32 Index = Time * Input.RootTransformSamplingRate;
	const int32 FirstIndexClamped = FMath::Clamp(Index + 0, 0, AccumulatedRootTransform.Num() - 1);
	const int32 SecondIndexClamped = FMath::Clamp(Index + 1, 0, AccumulatedRootTransform.Num() - 1);
	const float Alpha = FMath::Fmod(Time * Input.RootTransformSamplingRate, 1.0f);
	FTransform OutputTransform;
	OutputTransform.Blend(AccumulatedRootTransform[FirstIndexClamped], AccumulatedRootTransform[SecondIndexClamped], Alpha);
	return OutputTransform;
}

FTransform FBlendSpaceSampler::ExtractBlendSpaceRootMotionFromRange(float StartTrackPosition, float EndTrackPosition) const
{
	checkf(AccumulatedRootTransform.Num() > 0, TEXT("ProcessRootTransform must be run first"));

	FTransform RootTransformRefPose = ExtractBlendSpaceRootTrackTransform(0.0f);

	FTransform StartTransform = ExtractBlendSpaceRootTrackTransform(StartTrackPosition);
	FTransform EndTransform = ExtractBlendSpaceRootTrackTransform(EndTrackPosition);

	// Transform to Component Space
	const FTransform RootToComponent = RootTransformRefPose.Inverse();
	StartTransform = RootToComponent * StartTransform;
	EndTransform = RootToComponent * EndTransform;

	return EndTransform.GetRelativeTransform(StartTransform);
}

FTransform FBlendSpaceSampler::ExtractBlendSpaceRootMotion(float StartTime, float DeltaTime, bool bAllowLooping) const
{
	FRootMotionMovementParams RootMotionParams;

	if (DeltaTime != 0.f)
	{
		bool const bPlayingBackwards = (DeltaTime < 0.f);

		float PreviousPosition = StartTime;
		float CurrentPosition = StartTime;
		float DesiredDeltaMove = DeltaTime;

		do
		{
			// Disable looping here. Advance to desired position, or beginning / end of animation 
			const ETypeAdvanceAnim AdvanceType = FAnimationRuntime::AdvanceTime(false, DesiredDeltaMove, CurrentPosition, PlayLength);

			// Verify position assumptions
			//ensureMsgf(bPlayingBackwards ? (CurrentPosition <= PreviousPosition) : (CurrentPosition >= PreviousPosition), TEXT("in Animation %s(Skeleton %s) : bPlayingBackwards(%d), PreviousPosition(%0.2f), Current Position(%0.2f)"),
			//	*GetName(), *GetNameSafe(GetSkeleton()), bPlayingBackwards, PreviousPosition, CurrentPosition);

			RootMotionParams.Accumulate(ExtractBlendSpaceRootMotionFromRange(PreviousPosition, CurrentPosition));

			// If we've hit the end of the animation, and we're allowed to loop, keep going.
			if ((AdvanceType == ETAA_Finished) && bAllowLooping)
			{
				const float ActualDeltaMove = (CurrentPosition - PreviousPosition);
				DesiredDeltaMove -= ActualDeltaMove;

				PreviousPosition = bPlayingBackwards ? PlayLength : 0.f;
				CurrentPosition = PreviousPosition;
			}
			else
			{
				break;
			}
		} while (true);
	}

	return RootMotionParams.GetRootMotionTransform();
}

void FBlendSpaceSampler::ProcessPlayLength()
{
	TArray<FBlendSampleData> BlendSamples;
	int32 TriangulationIndex = 0;
	Input.BlendSpace->GetSamplesFromBlendInput(Input.BlendParameters, BlendSamples, TriangulationIndex, true);

	PlayLength = Input.BlendSpace->GetAnimationLengthFromSampleData(BlendSamples);
}

void FBlendSpaceSampler::ProcessRootTransform()
{
	// Pre-compute root motion

	int32 NumRootSamples = FMath::Max(PlayLength * Input.RootTransformSamplingRate + 1, 1);
	AccumulatedRootTransform.SetNumUninitialized(NumRootSamples);

	TArray<FBlendSampleData> BlendSamples;
	int32 TriangulationIndex = 0;
	Input.BlendSpace->GetSamplesFromBlendInput(Input.BlendParameters, BlendSamples, TriangulationIndex, true);

	FTransform RootMotionAccumulation = FTransform::Identity;

	AccumulatedRootTransform[0] = RootMotionAccumulation;

	for (int32 SampleIdx = 1; SampleIdx < NumRootSamples; ++SampleIdx)
	{
		float PreviousTime = float(SampleIdx - 1) / Input.RootTransformSamplingRate;
		float CurrentTime = float(SampleIdx - 0) / Input.RootTransformSamplingRate;

		FDeltaTimeRecord DeltaTimeRecord;
		DeltaTimeRecord.Set(PreviousTime, CurrentTime - PreviousTime);
		FAnimExtractContext ExtractionCtx(static_cast<double>(CurrentTime), true, DeltaTimeRecord, IsLoopable());

		for (int32 BlendSampleIdex = 0; BlendSampleIdex < BlendSamples.Num(); BlendSampleIdex++)
		{
			float Scale = BlendSamples[BlendSampleIdex].Animation->GetPlayLength() / PlayLength;

			FDeltaTimeRecord BlendSampleDeltaTimeRecord;
			BlendSampleDeltaTimeRecord.Set(DeltaTimeRecord.GetPrevious() * Scale, DeltaTimeRecord.Delta * Scale);

			BlendSamples[BlendSampleIdex].DeltaTimeRecord = BlendSampleDeltaTimeRecord;
			BlendSamples[BlendSampleIdex].PreviousTime = PreviousTime * Scale;
			BlendSamples[BlendSampleIdex].Time = CurrentTime * Scale;
		}

		FCompactPose Pose;
		FBlendedCurve BlendedCurve;
		UE::Anim::FStackAttributeContainer StackAttributeContainer;
		FAnimationPoseData AnimPoseData(Pose, BlendedCurve, StackAttributeContainer);

		Pose.SetBoneContainer(&Input.BoneContainer);
		BlendedCurve.InitFrom(Input.BoneContainer);

		Input.BlendSpace->GetAnimationPose(BlendSamples, ExtractionCtx, AnimPoseData);

		const UE::Anim::IAnimRootMotionProvider* RootMotionProvider = UE::Anim::IAnimRootMotionProvider::Get();

		if (ensureMsgf(RootMotionProvider, TEXT("Could not get Root Motion Provider.")))
		{
			if (ensureMsgf(RootMotionProvider->HasRootMotion(StackAttributeContainer), TEXT("Blend Space had no Root Motion Attribute.")))
			{
				FTransform RootMotionDelta;
				RootMotionProvider->ExtractRootMotion(StackAttributeContainer, RootMotionDelta);

				RootMotionAccumulation = RootMotionDelta * RootMotionAccumulation;
			}
		}

		AccumulatedRootTransform[SampleIdx] = RootMotionAccumulation;
	}
}

const UAnimationAsset* FBlendSpaceSampler::GetAsset() const
{
	return Input.BlendSpace.Get();
}

//////////////////////////////////////////////////////////////////////////
// FAnimMontageSampler
void FAnimMontageSampler::Init(const FInput& InInput)
{
	check(InInput.AnimMontage.Get());
	Input = InInput;
}

void FAnimMontageSampler::Process()
{
}

float FAnimMontageSampler::GetPlayLength() const
{
	return Input.AnimMontage->GetPlayLength();
}

float FAnimMontageSampler::GetScaledTime(float Time) const
{
	return Time;
}

bool FAnimMontageSampler::IsLoopable() const
{
	return Input.AnimMontage->bLoop;
}

void FAnimMontageSampler::ExtractPose(const FAnimExtractContext& ExtractionCtx, FAnimationPoseData& OutAnimPoseData) const
{
	// @todo: add support for SlotName / multiple SlotAnimTracks
	if (Input.AnimMontage->SlotAnimTracks.Num() != 1)
	{
		UE_LOG(LogPoseSearch, Error, TEXT("FAnimMontageSampler::ExtractPose: so far we support only montages with one SlotAnimTracks. %s has %d"), *Input.AnimMontage->GetName(), Input.AnimMontage->SlotAnimTracks.Num());
		OutAnimPoseData.GetPose().ResetToRefPose();
		return;
	}

	Input.AnimMontage->SlotAnimTracks[0].AnimTrack.GetAnimationPose(OutAnimPoseData, ExtractionCtx);
}

FTransform FAnimMontageSampler::GetTotalRootTransform() const
{
	// @todo: add support for SlotName / multiple SlotAnimTracks
	if (Input.AnimMontage->SlotAnimTracks.Num() != 1)
	{
		UE_LOG(LogPoseSearch, Error, TEXT("FAnimMontageSampler::GetTotalRootTransform: so far we support only montages with one SlotAnimTracks. %s has %d"), *Input.AnimMontage->GetName(), Input.AnimMontage->SlotAnimTracks.Num());
		return FTransform::Identity;
	}

	// @todo: optimize me
	const FTransform InitialRootTransform = ExtractRootTransform(0.f);
	const FTransform LastRootTransform = ExtractRootTransform(GetPlayLength());
	const FTransform TotalRootTransform = LastRootTransform.GetRelativeTransform(InitialRootTransform);
	return TotalRootTransform;
}

FTransform FAnimMontageSampler::ExtractRootTransformInternal(float StartTime, float EndTime) const
{
	// @todo: add support for SlotName / multiple SlotAnimTracks
	if (Input.AnimMontage->SlotAnimTracks.Num() != 1)
	{
		UE_LOG(LogPoseSearch, Error, TEXT("FAnimMontageSampler::ExtractRootTransform: so far we support only montages with one SlotAnimTracks. %s has %d"), *Input.AnimMontage->GetName(), Input.AnimMontage->SlotAnimTracks.Num());
		return FTransform::Identity;
	}

	const FAnimTrack& RootMotionAnimTrack = Input.AnimMontage->SlotAnimTracks[0].AnimTrack;
	TArray<FRootMotionExtractionStep> RootMotionExtractionSteps;
	RootMotionAnimTrack.GetRootMotionExtractionStepsForTrackRange(RootMotionExtractionSteps, StartTime, EndTime);
	FRootMotionMovementParams AccumulatedRootMotionParams;
	for (const FRootMotionExtractionStep& CurStep : RootMotionExtractionSteps)
	{
		if (CurStep.AnimSequence)
		{
			AccumulatedRootMotionParams.Accumulate(CurStep.AnimSequence->ExtractRootMotionFromRange(CurStep.StartPosition, CurStep.EndPosition));
		}
	}
	return AccumulatedRootMotionParams.GetRootMotionTransform();
}

FTransform FAnimMontageSampler::ExtractRootTransform(float Time) const
{
	FTransform RootTransform = FTransform::Identity;
	if (IsLoopable())
	{
		RootTransform = ExtractRootTransformInternal(0.f, Time);
	}
	else
	{
		const float PlayLength = GetPlayLength();
		const float ClampedTime = FMath::Clamp(Time, 0.f, PlayLength);
		const float ExtrapolationTime = Time - ClampedTime;

		// If Time is less than zero, ExtrapolationTime will be negative. In this case, we extrapolate the beginning of the 
		// animation to estimate where the root would be at Time
		if (ExtrapolationTime < -SMALL_NUMBER)
		{
			FTransform SampleToExtrapolate = ExtractRootTransformInternal(0.f, ExtrapolationSampleTime);

			const FTransform ExtrapolatedRootMotion = UE::PoseSearch::ExtrapolateRootMotion(
				SampleToExtrapolate,
				0.0f, ExtrapolationSampleTime,
				ExtrapolationTime);
			RootTransform = ExtrapolatedRootMotion;
		}
		else
		{
			RootTransform = ExtractRootTransformInternal(0.f, ClampedTime);

			// If Time is greater than PlayLength, ExtrapolationTime will be a positive number. In this case, we extrapolate
			// the end of the animation to estimate where the root would be at Time
			if (ExtrapolationTime > SMALL_NUMBER)
			{
				FTransform SampleToExtrapolate = ExtractRootTransformInternal(PlayLength - ExtrapolationSampleTime, PlayLength);

				const FTransform ExtrapolatedRootMotion = UE::PoseSearch::ExtrapolateRootMotion(
					SampleToExtrapolate,
					PlayLength - ExtrapolationSampleTime, PlayLength,
					ExtrapolationTime);
				RootTransform = ExtrapolatedRootMotion * RootTransform;
			}
		}
	}

	return RootTransform;
}

void FAnimMontageSampler::ExtractPoseSearchNotifyStates(float Time, TArray<UAnimNotifyState_PoseSearchBase*>& NotifyStates) const
{
	// getting pose search notifies in an interval of size ExtractionInterval, centered on Time
	FAnimNotifyContext NotifyContext;
	Input.AnimMontage->GetAnimNotifies(Time - (ExtractionInterval * 0.5f), ExtractionInterval, NotifyContext);

	// check which notifies actually overlap Time and are of the right base type
	for (const FAnimNotifyEventReference& EventReference : NotifyContext.ActiveNotifies)
	{
		const FAnimNotifyEvent* NotifyEvent = EventReference.GetNotify();
		if (!NotifyEvent)
		{
			continue;
		}

		if (NotifyEvent->GetTriggerTime() > Time ||
			NotifyEvent->GetEndTriggerTime() < Time)
		{
			continue;
		}

		UAnimNotifyState_PoseSearchBase* PoseSearchAnimNotify =
			Cast<UAnimNotifyState_PoseSearchBase>(NotifyEvent->NotifyStateClass);
		if (PoseSearchAnimNotify)
		{
			NotifyStates.Add(PoseSearchAnimNotify);
		}
	}
}

const UAnimationAsset* FAnimMontageSampler::GetAsset() const
{
	return Input.AnimMontage.Get();
}

} // namespace UE::PoseSearch
