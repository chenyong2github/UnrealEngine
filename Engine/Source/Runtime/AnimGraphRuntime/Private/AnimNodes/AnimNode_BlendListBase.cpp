// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimNodes/AnimNode_BlendListBase.h"
#include "AnimationRuntime.h"
#include "Animation/BlendProfile.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimNode_Inertialization.h"
#include "Animation/AnimTrace.h"

/////////////////////////////////////////////////////
// FAnimNode_BlendListBase

void FAnimNode_BlendListBase::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(Initialize_AnyThread)
	FAnimNode_Base::Initialize_AnyThread(Context);

	const int32 NumPoses = BlendPose.Num();
	const TArray<float>& CurrentBlendTimes = GetBlendTimes();
	checkSlow(CurrentBlendTimes.Num() == NumPoses);

	BlendWeights.Reset(NumPoses);
	PosesToEvaluate.Reset(NumPoses);
	if (NumPoses > 0)
	{
		// If we have at least 1 pose we initialize to full weight on
		// the first pose
		BlendWeights.AddZeroed(NumPoses);
		BlendWeights[0] = 1.0f;

		PosesToEvaluate.Add(0);

		for (int32 ChildIndex = 0; ChildIndex < NumPoses; ++ChildIndex)
		{
			BlendPose[ChildIndex].Initialize(Context);
		}
	}

	RemainingBlendTimes.Empty(NumPoses);
	RemainingBlendTimes.AddZeroed(NumPoses);
	Blends.Empty(NumPoses);
	Blends.AddZeroed(NumPoses);

	UBlendProfile* CurrentBlendProfile = GetBlendProfile();
	if (CurrentBlendProfile)
	{
		BlendStartAlphas.Empty(NumPoses);
		BlendStartAlphas.AddZeroed(NumPoses);
	}

	LastActiveChildIndex = INDEX_NONE;

	EAlphaBlendOption CurrentBlendType = GetBlendType();
	UCurveFloat* CurrentCustomBlendCurve = GetCustomBlendCurve();
	for(int32 i = 0 ; i < Blends.Num() ; ++i)
	{
		FAlphaBlend& Blend = Blends[i];

		Blend.SetBlendTime(0.0f);
		Blend.SetBlendOption(CurrentBlendType);
		Blend.SetCustomCurve(CurrentCustomBlendCurve);

		if (CurrentBlendProfile)
		{
			BlendStartAlphas[i] = 0.0f;
		}
	}
	Blends[0].SetAlpha(1.0f);

	if(CurrentBlendProfile)
	{
		BlendStartAlphas[0] = 1.0f;

		// Initialise per-bone data
		PerBoneSampleData.Empty(NumPoses);
		PerBoneSampleData.AddZeroed(NumPoses);

		for(int32 Idx = 0 ; Idx < NumPoses ; ++Idx)
		{
			FBlendSampleData& SampleData = PerBoneSampleData[Idx];
			SampleData.SampleDataIndex = Idx;
			SampleData.PerBoneBlendData.AddZeroed(CurrentBlendProfile->GetNumBlendEntries());
		}
	}
}

void FAnimNode_BlendListBase::CacheBones_AnyThread(const FAnimationCacheBonesContext& Context)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(CacheBones_AnyThread)
	for(int32 ChildIndex=0; ChildIndex<BlendPose.Num(); ChildIndex++)
	{
		BlendPose[ChildIndex].CacheBones(Context);
	}
}

void FAnimNode_BlendListBase::Update_AnyThread(const FAnimationUpdateContext& Context)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(Update_AnyThread)
	GetEvaluateGraphExposedInputs().Execute(Context);

	const int32 NumPoses = BlendPose.Num();
	const TArray<float>& CurrentBlendTimes = GetBlendTimes();
	checkSlow((CurrentBlendTimes.Num() == NumPoses) && (BlendWeights.Num() == NumPoses));

	PosesToEvaluate.Empty(NumPoses);

	if (NumPoses > 0)
	{
		UBlendProfile* CurrentBlendProfile = GetBlendProfile();
		
		// Handle a change in the active child index; adjusting the target weights
		const int32 ChildIndex = GetActiveChildIndex();
		
		if (ChildIndex != LastActiveChildIndex)
		{
			bool LastChildIndexIsInvalid = (LastActiveChildIndex == INDEX_NONE);
			
			const float CurrentWeight = BlendWeights[ChildIndex];
			const float DesiredWeight = 1.0f;
			const float WeightDifference = FMath::Clamp<float>(FMath::Abs<float>(DesiredWeight - CurrentWeight), 0.0f, 1.0f);

			// scale by the weight difference since we want always consistency:
			// - if you're moving from 0 to full weight 1, it will use the normal blend time
			// - if you're moving from 0.5 to full weight 1, it will get there in half the time
			float RemainingBlendTime;
			if (LastChildIndexIsInvalid)
			{
				RemainingBlendTime = 0.0f;
			}
			else if (GetTransitionType() == EBlendListTransitionType::Inertialization)
			{
				UE::Anim::IInertializationRequester* InertializationRequester = Context.GetMessage<UE::Anim::IInertializationRequester>();
				if (InertializationRequester)
				{
					InertializationRequester->RequestInertialization(CurrentBlendTimes[ChildIndex]);
					InertializationRequester->AddDebugRecord(*Context.AnimInstanceProxy, Context.GetCurrentNodeId());
				}
				else
				{
					FAnimNode_Inertialization::LogRequestError(Context, BlendPose[ChildIndex]);
				}
				
				RemainingBlendTime = 0.0f;
			}
			else
			{
				RemainingBlendTime = CurrentBlendTimes[ChildIndex] * WeightDifference;
			}

			for (int32 i = 0; i < RemainingBlendTimes.Num(); ++i)
			{
				RemainingBlendTimes[i] = RemainingBlendTime;
			}

			// If we have a valid previous child and we're instantly blending - update that pose with zero weight
			if(RemainingBlendTime == 0.0f && !LastChildIndexIsInvalid)
			{
				BlendPose[LastActiveChildIndex].Update(Context.FractionalWeight(0.0f));
			}

			for(int32 i = 0; i < Blends.Num(); ++i)
			{
				FAlphaBlend& Blend = Blends[i];

				Blend.SetBlendTime(RemainingBlendTime);

				if(i == ChildIndex)
				{
					Blend.SetValueRange(BlendWeights[i], 1.0f);

					if (CurrentBlendProfile)
					{
						Blend.ResetAlpha();
						BlendStartAlphas[i] = Blend.GetAlpha();
					}
				}
				else
				{
					Blend.SetValueRange(BlendWeights[i], 0.0f);
				}

				if (CurrentBlendProfile)
				{
					BlendStartAlphas[i] = Blend.GetAlpha();
				}
			}

			// when this flag is true, we'll reinitialize the children
			if (GetResetChildOnActivation())
			{
				FAnimationInitializeContext ReinitializeContext(Context.AnimInstanceProxy, Context.SharedContext);

				// reinitialize
				BlendPose[ChildIndex].Initialize(ReinitializeContext);
			}

			LastActiveChildIndex = ChildIndex;
		}

		// Advance the weights
		//@TODO: This means we advance even in a frame where the target weights/times just got modified; is that desirable?
		float SumWeight = 0.0f;
		for (int32 i = 0; i < Blends.Num(); ++i)
		{
			float& BlendWeight = BlendWeights[i];

			FAlphaBlend& Blend = Blends[i];
			Blend.Update(Context.GetDeltaTime());
			BlendWeight = Blend.GetBlendedValue();

			SumWeight += BlendWeight;
		}

		// Renormalize the weights
		if ((SumWeight > ZERO_ANIMWEIGHT_THRESH) && (FMath::Abs<float>(SumWeight - 1.0f) > ZERO_ANIMWEIGHT_THRESH))
		{
			float ReciprocalSum = 1.0f / SumWeight;
			for (int32 i = 0; i < BlendWeights.Num(); ++i)
			{
				BlendWeights[i] *= ReciprocalSum;
			}
		}

		// Update our active children
		for (int32 i = 0; i < BlendPose.Num(); ++i)
		{
			const float BlendWeight = BlendWeights[i];
			if (BlendWeight > ZERO_ANIMWEIGHT_THRESH)
			{
				BlendPose[i].Update(Context.FractionalWeight(BlendWeight));
				PosesToEvaluate.Add(i);				
			}
		}

		// If we're using a blend profile, extract the scales and build blend sample data
		if (CurrentBlendProfile)
		{
			for(int32 i = 0; i < BlendPose.Num(); ++i)
			{
				FBlendSampleData& PoseSampleData = PerBoneSampleData[i];
				PoseSampleData.TotalWeight = BlendWeights[i];
				const bool bInverse = (CurrentBlendProfile->Mode == EBlendProfileMode::WeightFactor) ? (ChildIndex != i) : false;
				CurrentBlendProfile->UpdateBoneWeights(PoseSampleData, Blends[i], BlendStartAlphas[i], BlendWeights[i], bInverse);
			}

			FBlendSampleData::NormalizeDataWeight(PerBoneSampleData);
		}
	}

#if ANIM_TRACE_ENABLED
	const int32 ChildIndex = GetActiveChildIndex();
	TRACE_ANIM_NODE_VALUE(Context, TEXT("Active Index"), ChildIndex);
	TRACE_ANIM_NODE_VALUE(Context, TEXT("Active Weight"), BlendWeights[ChildIndex]);
	TRACE_ANIM_NODE_VALUE(Context, TEXT("Active Blend Time"), CurrentBlendTimes[ChildIndex]);
#endif
}

void FAnimNode_BlendListBase::Evaluate_AnyThread(FPoseContext& Output)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(Evaluate_AnyThread)
	ANIM_MT_SCOPE_CYCLE_COUNTER(BlendPosesInGraph, !IsInGameThread());

	const int32 NumPoses = PosesToEvaluate.Num();

	if ((NumPoses > 0) && (BlendPose.Num() == BlendWeights.Num()))
	{		
		// Scratch arrays for evaluation, stack allocated
		TArray<FCompactPose, TInlineAllocator<8>> FilteredPoses;
		TArray<FBlendedCurve, TInlineAllocator<8>> FilteredCurve;
		TArray<UE::Anim::FStackAttributeContainer, TInlineAllocator<8>> FilteredAttributes;

		FilteredPoses.SetNum(NumPoses, false);
		FilteredCurve.SetNum(NumPoses, false);
		FilteredAttributes.SetNum(NumPoses, false);

		int32 NumActivePoses = 0;
		for (int32 i = 0; i < PosesToEvaluate.Num(); ++i)
		{
			int32 PoseIndex = PosesToEvaluate[i];

			FPoseContext EvaluateContext(Output);

			FPoseLink& CurrentPose = BlendPose[PoseIndex];
			CurrentPose.Evaluate(EvaluateContext);

			FilteredPoses[i].MoveBonesFrom(EvaluateContext.Pose);
			FilteredCurve[i].MoveFrom(EvaluateContext.Curve);
			FilteredAttributes[i].MoveFrom(EvaluateContext.CustomAttributes);
		}

		FAnimationPoseData OutAnimationPoseData(Output);

		// Use the calculated blend sample data if we're blending per-bone
		UBlendProfile* CurrentBlendProfile = GetBlendProfile();
		if (CurrentBlendProfile)
		{
			FAnimationRuntime::BlendPosesTogetherPerBone(FilteredPoses, FilteredCurve, FilteredAttributes, CurrentBlendProfile, PerBoneSampleData, PosesToEvaluate, OutAnimationPoseData);
		}
		else
		{
			FAnimationRuntime::BlendPosesTogether(FilteredPoses, FilteredCurve, FilteredAttributes, BlendWeights, PosesToEvaluate, OutAnimationPoseData);
		}		
	}
	else
	{
		Output.ResetToRefPose();
	}
}

void FAnimNode_BlendListBase::GatherDebugData(FNodeDebugData& DebugData)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(GatherDebugData)
	const int32 NumPoses = BlendPose.Num();
	const int32 ChildIndex = GetActiveChildIndex();

	FString DebugLine = GetNodeName(DebugData);
	const TArray<float>& CurrentBlendTimes = GetBlendTimes();
	DebugLine += FString::Printf(TEXT("(Active: (%i/%i) Weight: %.1f%% Time %.3f)"), ChildIndex+1, NumPoses, BlendWeights[ChildIndex]*100.f, CurrentBlendTimes[ChildIndex]);

	DebugData.AddDebugItem(DebugLine);
	
	for(int32 Pose = 0; Pose < NumPoses; ++Pose)
	{
		BlendPose[Pose].GatherDebugData(DebugData.BranchFlow(BlendWeights[Pose]));
	}
}

const TArray<float>& FAnimNode_BlendListBase::GetBlendTimes() const
{
	return GET_ANIM_NODE_DATA(TArray<float>, BlendTime);
}

EBlendListTransitionType FAnimNode_BlendListBase::GetTransitionType() const
{
	return GET_ANIM_NODE_DATA(EBlendListTransitionType, TransitionType);
}

EAlphaBlendOption FAnimNode_BlendListBase::GetBlendType() const
{
	return GET_ANIM_NODE_DATA(EAlphaBlendOption, BlendType);
}

bool FAnimNode_BlendListBase::GetResetChildOnActivation() const
{
	return GET_ANIM_NODE_DATA(bool, bResetChildOnActivation);
}

UCurveFloat* FAnimNode_BlendListBase::GetCustomBlendCurve() const
{
	return GET_ANIM_NODE_DATA(TObjectPtr<UCurveFloat>, CustomBlendCurve);
}

UBlendProfile* FAnimNode_BlendListBase::GetBlendProfile() const
{
	return GET_ANIM_NODE_DATA(TObjectPtr<UBlendProfile>, BlendProfile);
}