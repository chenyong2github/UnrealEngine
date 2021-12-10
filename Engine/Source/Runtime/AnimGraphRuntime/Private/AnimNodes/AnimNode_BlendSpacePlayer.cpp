// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimNodes/AnimNode_BlendSpacePlayer.h"
#include "Animation/BlendSpace.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimInstanceProxy.h"
#include "AnimGraphRuntimeTrace.h"
#include "Animation/AnimSync.h"
#include "Animation/AnimSyncScope.h"

/////////////////////////////////////////////////////
// FAnimNode_BlendSpacePlayer

float FAnimNode_BlendSpacePlayer::GetCurrentAssetTime() const
{
	if(const FBlendSampleData* HighestWeightedSample = GetHighestWeightedSample())
	{
		return HighestWeightedSample->Time;
	}

	// No sample
	return 0.0f;
}

float FAnimNode_BlendSpacePlayer::GetCurrentAssetTimePlayRateAdjusted() const
{
	float Length = GetCurrentAssetLength();
	return GetPlayRate() < 0.0f ? Length - InternalTimeAccumulator * Length : Length * InternalTimeAccumulator;
}

float FAnimNode_BlendSpacePlayer::GetCurrentAssetLength() const
{
	if(const FBlendSampleData* HighestWeightedSample = GetHighestWeightedSample())
	{
		UBlendSpace* CurrentBlendSpace = GetBlendSpace();
		if (CurrentBlendSpace != nullptr)
		{
			const FBlendSample& Sample = CurrentBlendSpace->GetBlendSample(HighestWeightedSample->SampleDataIndex);
			return Sample.Animation->GetPlayLength();
		}
	}

	// No sample
	return 0.0f;
}

void FAnimNode_BlendSpacePlayer::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(Initialize_AnyThread)
	FAnimNode_AssetPlayerBase::Initialize_AnyThread(Context);

	GetEvaluateGraphExposedInputs().Execute(Context);

	Reinitialize();

	PreviousBlendSpace = GetBlendSpace();
}

void FAnimNode_BlendSpacePlayer::CacheBones_AnyThread(const FAnimationCacheBonesContext& Context)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(CacheBones_AnyThread)
}

void FAnimNode_BlendSpacePlayer::UpdateAssetPlayer(const FAnimationUpdateContext& Context)
{
	GetEvaluateGraphExposedInputs().Execute(Context);

	UpdateInternal(Context);
}

void FAnimNode_BlendSpacePlayer::UpdateInternal(const FAnimationUpdateContext& Context)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(UpdateInternal)

	UBlendSpace* CurrentBlendSpace = GetBlendSpace();
	if ((CurrentBlendSpace != nullptr) && (Context.AnimInstanceProxy->IsSkeletonCompatible(CurrentBlendSpace->GetSkeleton())))
	{	
		if (PreviousBlendSpace != CurrentBlendSpace)
		{
			Reinitialize(ShouldResetPlayTimeWhenBlendSpaceChanges());
		}

		const FVector Position = GetPosition();

		// Create a tick record and push into the closest scope
		UE::Anim::FAnimSyncGroupScope& SyncScope = Context.GetMessageChecked<UE::Anim::FAnimSyncGroupScope>();

		FAnimTickRecord TickRecord(
			CurrentBlendSpace, Position, BlendSampleDataCache, BlendFilter, GetLoop(), GetPlayRate(), ShouldTeleportToTime(), 
			IsEvaluator(), Context.GetFinalBlendWeight(), /*inout*/ InternalTimeAccumulator, MarkerTickRecord);
		TickRecord.RootMotionWeightModifier = Context.GetRootMotionWeightModifier();
		TickRecord.DeltaTimeRecord = &DeltaTimeRecord;

		UE::Anim::FAnimSyncParams SyncParams(GetGroupName(), GetGroupRole(), GetGroupMethod());
		TickRecord.GatherContextData(Context);

		SyncScope.AddTickRecord(TickRecord, SyncParams, UE::Anim::FAnimSyncDebugInfo(Context));

		TRACE_ANIM_TICK_RECORD(Context, TickRecord);

#if WITH_EDITORONLY_DATA
		if (FAnimBlueprintDebugData* DebugData = Context.AnimInstanceProxy->GetAnimBlueprintDebugData())
		{
			DebugData->RecordBlendSpacePlayer(Context.GetCurrentNodeId(), CurrentBlendSpace, Position, BlendFilter.GetFilterLastOutput());
		}
#endif

		PreviousBlendSpace = CurrentBlendSpace;
	}

	TRACE_BLENDSPACE_PLAYER(Context, *this);
	TRACE_ANIM_NODE_VALUE(Context, TEXT("Name"), CurrentBlendSpace ? *CurrentBlendSpace->GetName() : TEXT("None"));
	TRACE_ANIM_NODE_VALUE(Context, TEXT("Blend Space"), CurrentBlendSpace);
	TRACE_ANIM_NODE_VALUE(Context, TEXT("Playback Time"), InternalTimeAccumulator);
}

void FAnimNode_BlendSpacePlayer::Evaluate_AnyThread(FPoseContext& Output)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(Evaluate_AnyThread) 

	UBlendSpace* CurrentBlendSpace = GetBlendSpace();
	if ((CurrentBlendSpace != nullptr) && (Output.AnimInstanceProxy->IsSkeletonCompatible(CurrentBlendSpace->GetSkeleton())))
	{
		FAnimationPoseData AnimationPoseData(Output);
		CurrentBlendSpace->GetAnimationPose(BlendSampleDataCache, FAnimExtractContext(InternalTimeAccumulator, Output.AnimInstanceProxy->ShouldExtractRootMotion(), DeltaTimeRecord, GetLoop()), AnimationPoseData);
	}
	else
	{
		Output.ResetToRefPose();
	}
}

void FAnimNode_BlendSpacePlayer::GatherDebugData(FNodeDebugData& DebugData)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(GatherDebugData)
	FString DebugLine = DebugData.GetNodeName(this);

	UBlendSpace* CurrentBlendSpace = GetBlendSpace();
	if (CurrentBlendSpace)
	{
		DebugLine += FString::Printf(TEXT("('%s' Play Time: %.3f)"), *CurrentBlendSpace->GetName(), InternalTimeAccumulator);

		DebugData.AddDebugItem(DebugLine, true);
	}
}

float FAnimNode_BlendSpacePlayer::GetTimeFromEnd(float CurrentTime) const
{
	// Blend-spaces use normalized time value
	const float PlayLength = 1.0f;
	return GetBlendSpace() != nullptr ? PlayLength - CurrentTime : 0.0f;
}

UAnimationAsset* FAnimNode_BlendSpacePlayer::GetAnimAsset() const
{
	return GetBlendSpace();
}

const FBlendSampleData* FAnimNode_BlendSpacePlayer::GetHighestWeightedSample() const
{
	if(BlendSampleDataCache.Num() == 0)
	{
		return nullptr;
	}

	const FBlendSampleData* HighestSample = &BlendSampleDataCache[0];

	for(int32 Idx = 1; Idx < BlendSampleDataCache.Num(); ++Idx)
	{
		if(BlendSampleDataCache[Idx].TotalWeight > HighestSample->TotalWeight)
		{
			HighestSample = &BlendSampleDataCache[Idx];
		}
	}

	return HighestSample;
}

void FAnimNode_BlendSpacePlayer::Reinitialize(bool bResetTime)
{
	BlendSampleDataCache.Empty();
	if(bResetTime)
	{
		float CurrentStartPosition = GetStartPosition();

		InternalTimeAccumulator = FMath::Clamp(CurrentStartPosition, 0.f, 1.0f);
		if (CurrentStartPosition == 0.f && GetPlayRate() < 0.0f)
		{
			// Blend spaces run between 0 and 1
			InternalTimeAccumulator = 1.0f;
		}
	}

	UBlendSpace* CurrentBlendSpace = GetBlendSpace();
	if (CurrentBlendSpace != nullptr)
	{
		CurrentBlendSpace->InitializeFilter(&BlendFilter);
	}
}

bool FAnimNode_BlendSpacePlayer::SetBlendSpace(UBlendSpace* InBlendSpace)
{
#if WITH_EDITORONLY_DATA
	BlendSpace = InBlendSpace;
	GET_MUTABLE_ANIM_NODE_DATA(TObjectPtr<UBlendSpace>, BlendSpace) = InBlendSpace;
#endif
	
	if(TObjectPtr<UBlendSpace>* BlendSpacePtr = GET_INSTANCE_ANIM_NODE_DATA_PTR(TObjectPtr<UBlendSpace>, BlendSpace))
	{
		*BlendSpacePtr = InBlendSpace;
		return true;
	}

	return false;
}

FVector FAnimNode_BlendSpacePlayer::GetPosition() const
{
	return FVector(GET_ANIM_NODE_DATA(float, X), GET_ANIM_NODE_DATA(float, Y), 0.0f);
}

float FAnimNode_BlendSpacePlayer::GetPlayRate() const
{
	return GET_ANIM_NODE_DATA(float, PlayRate);
}

bool FAnimNode_BlendSpacePlayer::GetLoop() const
{
	return GET_ANIM_NODE_DATA(bool, bLoop);
}

bool FAnimNode_BlendSpacePlayer::ShouldResetPlayTimeWhenBlendSpaceChanges() const
{
	return GET_ANIM_NODE_DATA(bool, bResetPlayTimeWhenBlendSpaceChanges);
}

float FAnimNode_BlendSpacePlayer::GetStartPosition() const
{
	return GET_ANIM_NODE_DATA(float, StartPosition);
}

UBlendSpace* FAnimNode_BlendSpacePlayer::GetBlendSpace() const
{
	return GET_ANIM_NODE_DATA(TObjectPtr<UBlendSpace>, BlendSpace);
}

FName FAnimNode_BlendSpacePlayer::GetGroupName() const
{
	return GET_ANIM_NODE_DATA(FName, GroupName);
}

EAnimGroupRole::Type FAnimNode_BlendSpacePlayer::GetGroupRole() const
{
	return GET_ANIM_NODE_DATA(TEnumAsByte<EAnimGroupRole::Type>, GroupRole);
}

EAnimSyncMethod FAnimNode_BlendSpacePlayer::GetGroupMethod() const
{
	return GET_ANIM_NODE_DATA(EAnimSyncMethod, Method);
}

bool FAnimNode_BlendSpacePlayer::GetIgnoreForRelevancyTest() const
{
	return GET_ANIM_NODE_DATA(bool, bIgnoreForRelevancyTest);
}

bool FAnimNode_BlendSpacePlayer::SetGroupName(FName InGroupName)
{
#if WITH_EDITORONLY_DATA
	GroupName = InGroupName;
#endif

	if(FName* GroupNamePtr = GET_INSTANCE_ANIM_NODE_DATA_PTR(FName, GroupName))
	{
		*GroupNamePtr = InGroupName;
		return true;
	}

	return false;
}

bool FAnimNode_BlendSpacePlayer::SetGroupRole(EAnimGroupRole::Type InRole)
{
#if WITH_EDITORONLY_DATA
	GroupRole = InRole;
#endif
	
	if(TEnumAsByte<EAnimGroupRole::Type>* GroupRolePtr = GET_INSTANCE_ANIM_NODE_DATA_PTR(TEnumAsByte<EAnimGroupRole::Type>, GroupRole))
	{
		*GroupRolePtr = InRole;
		return true;
	}

	return false;
}

bool FAnimNode_BlendSpacePlayer::SetGroupMethod(EAnimSyncMethod InMethod)
{
#if WITH_EDITORONLY_DATA
	Method = InMethod;
#endif

	if(EAnimSyncMethod* MethodPtr = GET_INSTANCE_ANIM_NODE_DATA_PTR(EAnimSyncMethod, Method))
	{
		*MethodPtr = InMethod;
		return true;
	}

	return false;
}

bool FAnimNode_BlendSpacePlayer::SetIgnoreForRelevancyTest(bool bInIgnoreForRelevancyTest)
{
#if WITH_EDITORONLY_DATA
	bIgnoreForRelevancyTest = bInIgnoreForRelevancyTest;
#endif

	if(bool* bIgnoreForRelevancyTestPtr = GET_INSTANCE_ANIM_NODE_DATA_PTR(bool, bIgnoreForRelevancyTest))
	{
		*bIgnoreForRelevancyTestPtr = bInIgnoreForRelevancyTest;
		return true;
	}

	return false;
}