// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Animation/AnimTrace.h"

#if ANIM_TRACE_ENABLED

#include "Trace/Trace.h"
#include "Animation/AnimationAsset.h"
#include "Animation/AnimInstanceProxy.h"
#include "ObjectTrace.h"
#include "Components/SkeletalMeshComponent.h"
#include "Misc/CommandLine.h"
#include "Engine/SkeletalMesh.h"
#include "Math/TransformNonVectorized.h"
#include "Animation/AnimNodeBase.h"
#include "Animation/AnimMontage.h"
#include "Animation/BlendSpaceBase.h"
#include "Animation/AnimNode_SequencePlayer.h"

UE_TRACE_EVENT_BEGIN(Animation, TickRecord)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(uint64, AnimInstanceId)
	UE_TRACE_EVENT_FIELD(uint64, AssetId)
	UE_TRACE_EVENT_FIELD(int32, NodeId)
	UE_TRACE_EVENT_FIELD(float, BlendWeight)
	UE_TRACE_EVENT_FIELD(float, PlaybackTime)
	UE_TRACE_EVENT_FIELD(float, RootMotionWeight)
	UE_TRACE_EVENT_FIELD(float, PlayRate)
	UE_TRACE_EVENT_FIELD(float, BlendSpacePositionX)
	UE_TRACE_EVENT_FIELD(float, BlendSpacePositionY)
	UE_TRACE_EVENT_FIELD(uint16, FrameCounter)
	UE_TRACE_EVENT_FIELD(bool, Looping)
	UE_TRACE_EVENT_FIELD(bool, IsBlendSpace)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(Animation, SkeletalMesh, Important)
	UE_TRACE_EVENT_FIELD(uint64, Id)
	UE_TRACE_EVENT_FIELD(uint32, BoneCount)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(Animation, SkeletalMeshComponent)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(uint64, ComponentId)
	UE_TRACE_EVENT_FIELD(uint64, MeshId)
	UE_TRACE_EVENT_FIELD(uint32, BoneCount)
	UE_TRACE_EVENT_FIELD(uint32, CurveCount)
	UE_TRACE_EVENT_FIELD(uint16, LodIndex)
	UE_TRACE_EVENT_FIELD(uint16, FrameCounter)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(Animation, SkeletalMeshFrame)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(uint64, ComponentId)
	UE_TRACE_EVENT_FIELD(uint16, FrameCounter)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(Animation, AnimGraph)
	UE_TRACE_EVENT_FIELD(uint64, StartCycle)
	UE_TRACE_EVENT_FIELD(uint64, EndCycle)
	UE_TRACE_EVENT_FIELD(uint64, AnimInstanceId)
	UE_TRACE_EVENT_FIELD(int32, NodeCount)
	UE_TRACE_EVENT_FIELD(uint16, FrameCounter)
	UE_TRACE_EVENT_FIELD(uint8, Phase)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(Animation, AnimNodeStart)
	UE_TRACE_EVENT_FIELD(uint64, StartCycle)
	UE_TRACE_EVENT_FIELD(uint64, AnimInstanceId)
	UE_TRACE_EVENT_FIELD(int32, PreviousNodeId)
	UE_TRACE_EVENT_FIELD(int32, NodeId)
	UE_TRACE_EVENT_FIELD(float, Weight)
	UE_TRACE_EVENT_FIELD(uint16, FrameCounter)
	UE_TRACE_EVENT_FIELD(uint8, Phase)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(Animation, AnimNodeEnd)
	UE_TRACE_EVENT_FIELD(uint64, EndCycle)
	UE_TRACE_EVENT_FIELD(uint64, AnimInstanceId)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(Animation, AnimNodeValueBool)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(uint64, AnimInstanceId)
	UE_TRACE_EVENT_FIELD(int32, NodeId)
	UE_TRACE_EVENT_FIELD(int32, KeyLength)
	UE_TRACE_EVENT_FIELD(uint16, FrameCounter)
	UE_TRACE_EVENT_FIELD(bool, Value)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(Animation, AnimNodeValueInt)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(uint64, AnimInstanceId)
	UE_TRACE_EVENT_FIELD(int32, NodeId)
	UE_TRACE_EVENT_FIELD(int32, KeyLength)
	UE_TRACE_EVENT_FIELD(int32, Value)
	UE_TRACE_EVENT_FIELD(uint16, FrameCounter)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(Animation, AnimNodeValueFloat)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(uint64, AnimInstanceId)
	UE_TRACE_EVENT_FIELD(int32, NodeId)
	UE_TRACE_EVENT_FIELD(int32, KeyLength)
	UE_TRACE_EVENT_FIELD(float, Value)
	UE_TRACE_EVENT_FIELD(uint16, FrameCounter)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(Animation, AnimNodeValueVector)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(uint64, AnimInstanceId)
	UE_TRACE_EVENT_FIELD(int32, NodeId)
	UE_TRACE_EVENT_FIELD(int32, KeyLength)
	UE_TRACE_EVENT_FIELD(float, ValueX)
	UE_TRACE_EVENT_FIELD(float, ValueY)
	UE_TRACE_EVENT_FIELD(float, ValueZ)
	UE_TRACE_EVENT_FIELD(uint16, FrameCounter)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(Animation, AnimNodeValueString)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(uint64, AnimInstanceId)
	UE_TRACE_EVENT_FIELD(int32, NodeId)
	UE_TRACE_EVENT_FIELD(int32, KeyLength)
	UE_TRACE_EVENT_FIELD(uint16, FrameCounter)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(Animation, AnimNodeValueObject)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(uint64, AnimInstanceId)
	UE_TRACE_EVENT_FIELD(uint64, Value)
	UE_TRACE_EVENT_FIELD(int32, NodeId)
	UE_TRACE_EVENT_FIELD(int32, KeyLength)
	UE_TRACE_EVENT_FIELD(uint16, FrameCounter)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(Animation, AnimNodeValueClass)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(uint64, AnimInstanceId)
	UE_TRACE_EVENT_FIELD(uint64, Value)
	UE_TRACE_EVENT_FIELD(int32, NodeId)
	UE_TRACE_EVENT_FIELD(int32, KeyLength)
	UE_TRACE_EVENT_FIELD(uint16, FrameCounter)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(Animation, AnimSequencePlayer)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(uint64, AnimInstanceId)
	UE_TRACE_EVENT_FIELD(int32, NodeId)
	UE_TRACE_EVENT_FIELD(float, Position)
	UE_TRACE_EVENT_FIELD(float, Length)
	UE_TRACE_EVENT_FIELD(int32, FrameCount)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(Animation, BlendSpacePlayer)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(uint64, AnimInstanceId)
	UE_TRACE_EVENT_FIELD(uint64, BlendSpaceId)
	UE_TRACE_EVENT_FIELD(int32, NodeId)
	UE_TRACE_EVENT_FIELD(float, PositionX)
	UE_TRACE_EVENT_FIELD(float, PositionY)
	UE_TRACE_EVENT_FIELD(float, PositionZ)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(Animation, StateMachineState)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(uint64, AnimInstanceId)
	UE_TRACE_EVENT_FIELD(int32, NodeId)
	UE_TRACE_EVENT_FIELD(int32, StateMachineIndex)
	UE_TRACE_EVENT_FIELD(int32, StateIndex)
	UE_TRACE_EVENT_FIELD(float, StateWeight)
	UE_TRACE_EVENT_FIELD(float, ElapsedTime)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(Animation, Name, Important)
	UE_TRACE_EVENT_FIELD(uint32, Id)
UE_TRACE_EVENT_END()

// Object annotations used for tracing
FUObjectAnnotationSparseBool GSkeletalMeshTraceAnnotations;

// Map used for unique name output
TMap<FName, uint32> GAnimTraceNames;

FAnimTrace::FScopedAnimNodeTrace::FScopedAnimNodeTrace(const FAnimationInitializeContext& InContext)
	: Context(InContext)
{
	OutputAnimNodeStart(InContext, FPlatformTime::Cycles64(), InContext.GetPreviousNodeId(), InContext.GetCurrentNodeId(), 0.0f, 0.0f, (__underlying_type(EPhase))EPhase::Initialize);
}

FAnimTrace::FScopedAnimNodeTrace::FScopedAnimNodeTrace(const FAnimationUpdateContext& InContext)
	: Context(InContext)
{
	OutputAnimNodeStart(InContext, FPlatformTime::Cycles64(), InContext.GetPreviousNodeId(), InContext.GetCurrentNodeId(), InContext.GetFinalBlendWeight(), InContext.GetRootMotionWeightModifier(), (__underlying_type(EPhase))EPhase::Update);
}

FAnimTrace::FScopedAnimNodeTrace::FScopedAnimNodeTrace(const FAnimationCacheBonesContext& InContext)
	: Context(InContext)
{
	OutputAnimNodeStart(InContext, FPlatformTime::Cycles64(), InContext.GetPreviousNodeId(), InContext.GetCurrentNodeId(), 0.0f, 0.0f, (__underlying_type(EPhase))EPhase::CacheBones);
}

FAnimTrace::FScopedAnimNodeTrace::FScopedAnimNodeTrace(const FPoseContext& InContext)
	: Context(InContext)
{
	OutputAnimNodeStart(InContext, FPlatformTime::Cycles64(), InContext.GetPreviousNodeId(), InContext.GetCurrentNodeId(), 0.0f, 0.0f, (__underlying_type(EPhase))EPhase::Evaluate);
}

FAnimTrace::FScopedAnimNodeTrace::FScopedAnimNodeTrace(const FComponentSpacePoseContext& InContext)
	: Context(InContext)
{
	OutputAnimNodeStart(InContext, FPlatformTime::Cycles64(), InContext.GetPreviousNodeId(), InContext.GetCurrentNodeId(), 0.0f, 0.0f, (__underlying_type(EPhase))EPhase::Evaluate);
}

FAnimTrace::FScopedAnimNodeTrace::~FScopedAnimNodeTrace()
{
	OutputAnimNodeEnd(Context, FPlatformTime::Cycles64());
}

FAnimTrace::FScopedAnimGraphTrace::FScopedAnimGraphTrace(const FAnimationInitializeContext& InContext)
	: StartCycle(FPlatformTime::Cycles64())
	, Context(InContext)
	, Phase(EPhase::Initialize)
{}

FAnimTrace::FScopedAnimGraphTrace::FScopedAnimGraphTrace(const FAnimationUpdateContext& InContext)
	: StartCycle(FPlatformTime::Cycles64())
	, Context(InContext)
	, Phase(EPhase::Update)
{}

FAnimTrace::FScopedAnimGraphTrace::FScopedAnimGraphTrace(const FAnimationCacheBonesContext& InContext)
	: StartCycle(FPlatformTime::Cycles64())
	, Context(InContext)
	, Phase(EPhase::CacheBones)
{}

FAnimTrace::FScopedAnimGraphTrace::FScopedAnimGraphTrace(const FPoseContext& InContext)
	: StartCycle(FPlatformTime::Cycles64())
	, Context(InContext)
	, Phase(EPhase::Evaluate)
{}

FAnimTrace::FScopedAnimGraphTrace::FScopedAnimGraphTrace(const FComponentSpacePoseContext& InContext)
	: StartCycle(FPlatformTime::Cycles64())
	, Context(InContext)
	, Phase(EPhase::Evaluate)
{}

FAnimTrace::FScopedAnimGraphTrace::~FScopedAnimGraphTrace()
{
	OutputAnimGraph(Context, StartCycle, FPlatformTime::Cycles64(), (__underlying_type(EPhase))Phase);
}

void FAnimTrace::Init()
{
	if (FParse::Param(FCommandLine::Get(), TEXT("objecttrace")))
	{
		UE_TRACE_EVENT_IS_ENABLED(Animation, TickRecord);
		UE_TRACE_EVENT_IS_ENABLED(Animation, SkeletalMesh);
		UE_TRACE_EVENT_IS_ENABLED(Animation, SkeletalMeshComponent);
		UE_TRACE_EVENT_IS_ENABLED(Animation, SkeletalMeshFrame);
		UE_TRACE_EVENT_IS_ENABLED(Animation, AnimGraph);
		UE_TRACE_EVENT_IS_ENABLED(Animation, AnimNodeStart);
		UE_TRACE_EVENT_IS_ENABLED(Animation, AnimNodeEnd);
		UE_TRACE_EVENT_IS_ENABLED(Animation, AnimNodeValueBool);
		UE_TRACE_EVENT_IS_ENABLED(Animation, AnimNodeValueInt);
		UE_TRACE_EVENT_IS_ENABLED(Animation, AnimNodeValueFloat);
		UE_TRACE_EVENT_IS_ENABLED(Animation, AnimNodeValueVector);
		UE_TRACE_EVENT_IS_ENABLED(Animation, AnimNodeValueString);
		UE_TRACE_EVENT_IS_ENABLED(Animation, AnimSequencePlayer);
		UE_TRACE_EVENT_IS_ENABLED(Animation, StateMachineState);
		UE_TRACE_EVENT_IS_ENABLED(Animation, Name);
		Trace::ToggleEvent(TEXT("Animation"), true);
	}
}

void FAnimTrace::OutputAnimTickRecord(const FAnimationBaseContext& InContext, const FAnimTickRecord& InTickRecord)
{
	bool bEventEnabled = UE_TRACE_EVENT_IS_ENABLED(Animation, TickRecord);
	if (!bEventEnabled)
	{
		return;
	}

	TRACE_OBJECT(InContext.AnimInstanceProxy->GetAnimInstanceObject());

	if(InTickRecord.SourceAsset)
	{
		TRACE_OBJECT(InTickRecord.SourceAsset);

		float PlaybackTime = *InTickRecord.TimeAccumulator;
		if(InTickRecord.SourceAsset->IsA<UAnimMontage>())
		{
			PlaybackTime = InTickRecord.Montage.CurrentPosition;
		}

		float BlendSpacePositionX = 0.0f;
		float BlendSpacePositionY = 0.0f;
		const bool bIsBlendSpace = InTickRecord.SourceAsset->IsA<UBlendSpaceBase>();
		if(bIsBlendSpace)
	{
			BlendSpacePositionX = InTickRecord.BlendSpace.BlendSpacePositionX;
			BlendSpacePositionY = InTickRecord.BlendSpace.BlendSpacePositionY;
	}

		UE_TRACE_LOG(Animation, TickRecord)
			<< TickRecord.Cycle(FPlatformTime::Cycles64())
			<< TickRecord.AnimInstanceId(FObjectTrace::GetObjectId(InContext.AnimInstanceProxy->GetAnimInstanceObject()))
			<< TickRecord.AssetId(FObjectTrace::GetObjectId(InTickRecord.SourceAsset))
			<< TickRecord.NodeId(InContext.GetCurrentNodeId())
			<< TickRecord.BlendWeight(InTickRecord.EffectiveBlendWeight)
			<< TickRecord.PlaybackTime(PlaybackTime)
			<< TickRecord.RootMotionWeight(InTickRecord.RootMotionWeightModifier)
			<< TickRecord.PlayRate(InTickRecord.PlayRateMultiplier)
			<< TickRecord.BlendSpacePositionX(BlendSpacePositionX)
			<< TickRecord.BlendSpacePositionY(BlendSpacePositionY)
			<< TickRecord.FrameCounter((uint16)(GFrameCounter % 0xffff))
			<< TickRecord.Looping(InTickRecord.bLooping)
			<< TickRecord.IsBlendSpace(bIsBlendSpace);
	}
}

void FAnimTrace::OutputSkeletalMesh(const USkeletalMesh* InMesh)
{
	bool bEventEnabled = UE_TRACE_EVENT_IS_ENABLED(Animation, SkeletalMesh);
	if (!bEventEnabled || InMesh == nullptr)
	{
		return;
	}

	if(GSkeletalMeshTraceAnnotations.Get(InMesh))
	{
		return;
	}

	TRACE_OBJECT(InMesh);

	uint32 BoneCount = (uint32)InMesh->RefSkeleton.GetNum();

	auto CopyParentIndices = [InMesh](uint8* Out)
	{
		int32* OutParentIndices = reinterpret_cast<int32*>(Out);
		for(const FMeshBoneInfo& BoneInfo : InMesh->RefSkeleton.GetRefBoneInfo())
		{
			*OutParentIndices = BoneInfo.ParentIndex;
			OutParentIndices++;
		}
	};

	UE_TRACE_LOG(Animation, SkeletalMesh, BoneCount * sizeof(int32))
		<< SkeletalMesh.Id(FObjectTrace::GetObjectId(InMesh))
		<< SkeletalMesh.BoneCount(BoneCount)
		<< SkeletalMesh.Attachment(CopyParentIndices);

	GSkeletalMeshTraceAnnotations.Set(InMesh);
}

uint32 FAnimTrace::OutputName(const FName& InName)
{
	static uint32 CurrentId = 1;
	check(IsInGameThread());

	uint32* ExistingIdPtr = GAnimTraceNames.Find(InName);
	if(ExistingIdPtr == nullptr)
	{
		int32 NameStringLength = InName.GetStringLength() + 1;

		auto StringCopyFunc = [NameStringLength, &InName](uint8* Out)
		{
			InName.ToString(reinterpret_cast<TCHAR*>(Out), NameStringLength);
		};

		uint32 NewId = CurrentId++;

		UE_TRACE_LOG(Animation, Name, NameStringLength * sizeof(TCHAR))
			<< Name.Id(NewId)
			<< Name.Attachment(StringCopyFunc);

		GAnimTraceNames.Add(InName, NewId);
		return NewId;
	}
	else
	{
		return *ExistingIdPtr;
	}
}

void FAnimTrace::OutputSkeletalMeshComponent(const USkeletalMeshComponent* InComponent)
{
	bool bEventEnabled = UE_TRACE_EVENT_IS_ENABLED(Animation, SkeletalMeshComponent);
	if (!bEventEnabled || InComponent == nullptr)
	{
		return;
	}

	int32 BoneCount = InComponent->GetComponentSpaceTransforms().Num();
	int32 CurveCount = 0;
	UAnimInstance* AnimInstance = InComponent->GetAnimInstance();

	if(AnimInstance)
	{
		for(EAnimCurveType CurveType : TEnumRange<EAnimCurveType>())
		{
			CurveCount += AnimInstance->GetAnimationCurveList(CurveType).Num();
		}
	}
	
	if(BoneCount > 0 || CurveCount > 0)
	{
		TRACE_OBJECT(InComponent);
		TRACE_SKELETAL_MESH(InComponent->SkeletalMesh);

		auto CopyTransformsAndCurves = [&InComponent, &BoneCount, &CurveCount, &AnimInstance](uint8* Out)
		{
			FPlatformMemory::Memcpy(Out, &InComponent->GetComponentToWorld(), sizeof(FTransform));
			Out += sizeof(FTransform);

			if(BoneCount > 0)
			{
				const int32 BufferSize = BoneCount * sizeof(FTransform);
				FPlatformMemory::Memcpy(Out, InComponent->GetComponentSpaceTransforms().GetData(), BufferSize);
				Out += BufferSize;
			}
			if(CurveCount > 0 && AnimInstance)
			{
				for(EAnimCurveType CurveType : TEnumRange<EAnimCurveType>())
				{
					for(TPair<FName, float> CurvePair : AnimInstance->GetAnimationCurveList(CurveType))
					{
						*reinterpret_cast<uint32*>(Out) = OutputName(CurvePair.Key);
						Out += sizeof(uint32);
						*reinterpret_cast<float*>(Out) = CurvePair.Value;
						Out += sizeof(float);
					}
				}
			}
		};

		UE_TRACE_LOG(Animation, SkeletalMeshComponent, ((BoneCount + 1) * sizeof(FTransform)) + (CurveCount * (sizeof(float) + sizeof(uint32))))
			<< SkeletalMeshComponent.Cycle(FPlatformTime::Cycles64())
			<< SkeletalMeshComponent.ComponentId(FObjectTrace::GetObjectId(InComponent))
			<< SkeletalMeshComponent.MeshId(FObjectTrace::GetObjectId(InComponent->SkeletalMesh))
			<< SkeletalMeshComponent.BoneCount((uint32)BoneCount + 1)
			<< SkeletalMeshComponent.CurveCount((uint32)CurveCount)
			<< SkeletalMeshComponent.LodIndex((uint16)InComponent->PredictedLODLevel)
			<< SkeletalMeshComponent.FrameCounter((uint16)(GFrameCounter % 0xffff))
			<< SkeletalMeshComponent.Attachment(CopyTransformsAndCurves);
	}
}

void FAnimTrace::OutputSkeletalMeshFrame(const USkeletalMeshComponent* InComponent)
{
	bool bEventEnabled = UE_TRACE_EVENT_IS_ENABLED(Animation, SkeletalMeshFrame);
	if (!bEventEnabled || InComponent == nullptr)
	{
		return;
	}

	TRACE_OBJECT(InComponent);

	UE_TRACE_LOG(Animation, SkeletalMeshFrame)
		<< SkeletalMeshFrame.Cycle(FPlatformTime::Cycles64())
		<< SkeletalMeshFrame.ComponentId(FObjectTrace::GetObjectId(InComponent))
		<< SkeletalMeshFrame.FrameCounter((uint16)(GFrameCounter % 0xffff));
}

void FAnimTrace::OutputAnimGraph(const FAnimationBaseContext& InContext, uint64 InStartCycle, uint64 InEndCycle, uint8 InPhase)
{
	bool bEventEnabled = UE_TRACE_EVENT_IS_ENABLED(Animation, AnimGraph);
	if (!bEventEnabled)
	{
		return;
	}

	check(InContext.AnimInstanceProxy);

	const UAnimInstance* AnimInstance = Cast<UAnimInstance>(InContext.AnimInstanceProxy->GetAnimInstanceObject());
	const UAnimBlueprintGeneratedClass* BPClass = Cast<UAnimBlueprintGeneratedClass>(AnimInstance->GetClass());

	TRACE_OBJECT(AnimInstance);

	UE_TRACE_LOG(Animation, AnimGraph)
		<< AnimGraph.StartCycle(InStartCycle)
		<< AnimGraph.EndCycle(InEndCycle)
		<< AnimGraph.AnimInstanceId(FObjectTrace::GetObjectId(AnimInstance))
		<< AnimGraph.NodeCount(BPClass ? BPClass->AnimNodeProperties.Num() : 0)
		<< AnimGraph.FrameCounter((uint16)(GFrameCounter % 0xffff))
		<< AnimGraph.Phase(InPhase);
}

void FAnimTrace::OutputAnimNodeStart(const FAnimationBaseContext& InContext, uint64 InStartCycle, int32 InPreviousNodeId, int32 InNodeId, float InBlendWeight, float InRootMotionWeight, uint8 InPhase)
{
	bool bEventEnabled = UE_TRACE_EVENT_IS_ENABLED(Animation, AnimNodeStart);
	if (!bEventEnabled)
	{
		return;
	}

	check(InContext.AnimInstanceProxy);

	TRACE_OBJECT(InContext.AnimInstanceProxy->GetAnimInstanceObject());

	IAnimClassInterface* AnimBlueprintClass = InContext.GetAnimClass();
	check(AnimBlueprintClass);
	const TArray<FStructPropertyPath>& AnimNodeProperties = AnimBlueprintClass->GetAnimNodeProperties();
	check(AnimNodeProperties.IsValidIndex(InNodeId));
	FStructProperty* LinkedProperty = AnimNodeProperties[InNodeId].Get();
	check(LinkedProperty->Struct);

#if WITH_EDITOR
	FString DisplayNameString = LinkedProperty->Struct->GetDisplayNameText().ToString();
#else
	FString DisplayNameString = LinkedProperty->Struct->GetName();
#endif

	DisplayNameString.RemoveFromStart(TEXT("Anim Node "));

	UE_TRACE_LOG(Animation, AnimNodeStart, (DisplayNameString.Len() + 1) * sizeof(TCHAR))
		<< AnimNodeStart.StartCycle(InStartCycle)
		<< AnimNodeStart.AnimInstanceId(FObjectTrace::GetObjectId(InContext.AnimInstanceProxy->GetAnimInstanceObject()))
		<< AnimNodeStart.PreviousNodeId(InPreviousNodeId)
		<< AnimNodeStart.NodeId(InNodeId)
		<< AnimNodeStart.Weight(InBlendWeight)
		<< AnimNodeStart.FrameCounter((uint16)(GFrameCounter % 0xffff))
		<< AnimNodeStart.Phase(InPhase)
		<< AnimNodeStart.Attachment(*DisplayNameString, (DisplayNameString.Len() + 1) * sizeof(TCHAR));
}

void FAnimTrace::OutputAnimNodeEnd(const FAnimationBaseContext& InContext, uint64 InEndCycle)
{
	bool bEventEnabled = UE_TRACE_EVENT_IS_ENABLED(Animation, AnimNodeEnd);
	if (!bEventEnabled)
	{
		return;
	}

	check(InContext.AnimInstanceProxy);

	TRACE_OBJECT(InContext.AnimInstanceProxy->GetAnimInstanceObject());

	UE_TRACE_LOG(Animation, AnimNodeEnd)
		<< AnimNodeEnd.EndCycle(InEndCycle)
		<< AnimNodeEnd.AnimInstanceId(FObjectTrace::GetObjectId(InContext.AnimInstanceProxy->GetAnimInstanceObject()));
}

void FAnimTrace::OutputAnimNodeValue(const FAnimationBaseContext& InContext, const TCHAR* InKey, bool InValue)
{
	bool bEventEnabled = UE_TRACE_EVENT_IS_ENABLED(Animation, AnimNodeValueBool);
	if (!bEventEnabled)
	{
		return;
	}

	check(InContext.AnimInstanceProxy);

	TRACE_OBJECT(InContext.AnimInstanceProxy->GetAnimInstanceObject());

	int32 KeyLength = FCString::Strlen(InKey) + 1;

	UE_TRACE_LOG(Animation, AnimNodeValueBool, KeyLength * sizeof(TCHAR))
		<< AnimNodeValueBool.Cycle(FPlatformTime::Cycles64())
		<< AnimNodeValueBool.AnimInstanceId(FObjectTrace::GetObjectId(InContext.AnimInstanceProxy->GetAnimInstanceObject()))
		<< AnimNodeValueBool.NodeId(InContext.GetCurrentNodeId())
		<< AnimNodeValueBool.KeyLength(KeyLength)
		<< AnimNodeValueBool.Value(InValue)
		<< AnimNodeValueBool.FrameCounter((uint16)(GFrameCounter % 0xffff))
		<< AnimNodeValueBool.Attachment(InKey, KeyLength * sizeof(TCHAR));
}

void FAnimTrace::OutputAnimNodeValue(const FAnimationBaseContext& InContext, const TCHAR* InKey, int32 InValue)
{
	bool bEventEnabled = UE_TRACE_EVENT_IS_ENABLED(Animation, AnimNodeValueInt);
	if (!bEventEnabled)
	{
		return;
	}

	check(InContext.AnimInstanceProxy);

	TRACE_OBJECT(InContext.AnimInstanceProxy->GetAnimInstanceObject());

	int32 KeyLength = FCString::Strlen(InKey) + 1;

	UE_TRACE_LOG(Animation, AnimNodeValueInt, KeyLength * sizeof(TCHAR))
		<< AnimNodeValueInt.Cycle(FPlatformTime::Cycles64())
		<< AnimNodeValueInt.AnimInstanceId(FObjectTrace::GetObjectId(InContext.AnimInstanceProxy->GetAnimInstanceObject()))
		<< AnimNodeValueInt.NodeId(InContext.GetCurrentNodeId())
		<< AnimNodeValueInt.KeyLength(KeyLength)
		<< AnimNodeValueInt.Value(InValue)
		<< AnimNodeValueInt.FrameCounter((uint16)(GFrameCounter % 0xffff))
		<< AnimNodeValueInt.Attachment(InKey, KeyLength * sizeof(TCHAR));
}

void FAnimTrace::OutputAnimNodeValue(const FAnimationBaseContext& InContext, const TCHAR* InKey, float InValue)
{
	bool bEventEnabled = UE_TRACE_EVENT_IS_ENABLED(Animation, AnimNodeValueFloat);
	if (!bEventEnabled)
	{
		return;
	}

	check(InContext.AnimInstanceProxy);

	TRACE_OBJECT(InContext.AnimInstanceProxy->GetAnimInstanceObject());

	int32 KeyLength = FCString::Strlen(InKey) + 1;

	UE_TRACE_LOG(Animation, AnimNodeValueFloat, KeyLength * sizeof(TCHAR))
		<< AnimNodeValueFloat.Cycle(FPlatformTime::Cycles64())
		<< AnimNodeValueFloat.AnimInstanceId(FObjectTrace::GetObjectId(InContext.AnimInstanceProxy->GetAnimInstanceObject()))
		<< AnimNodeValueFloat.NodeId(InContext.GetCurrentNodeId())
		<< AnimNodeValueFloat.KeyLength(KeyLength)
		<< AnimNodeValueFloat.Value(InValue)
		<< AnimNodeValueFloat.FrameCounter((uint16)(GFrameCounter % 0xffff))
		<< AnimNodeValueFloat.Attachment(InKey, KeyLength * sizeof(TCHAR));
}

void FAnimTrace::OutputAnimNodeValue(const FAnimationBaseContext& InContext, const TCHAR* InKey, const FRotator& Value)
{
	const FVector VectorValue(Value.Roll, Value.Pitch, Value.Yaw);
	OutputAnimNodeValue(InContext, InKey, VectorValue);
}

void FAnimTrace::OutputAnimNodeValue(const FAnimationBaseContext& InContext, const TCHAR* InKey, const FVector& InValue)
{
	bool bEventEnabled = UE_TRACE_EVENT_IS_ENABLED(Animation, AnimNodeValueVector);
	if (!bEventEnabled)
	{
		return;
	}

	check(InContext.AnimInstanceProxy);

	TRACE_OBJECT(InContext.AnimInstanceProxy->GetAnimInstanceObject());

	int32 KeyLength = FCString::Strlen(InKey) + 1;

	UE_TRACE_LOG(Animation, AnimNodeValueVector, KeyLength * sizeof(TCHAR))
		<< AnimNodeValueVector.Cycle(FPlatformTime::Cycles64())
		<< AnimNodeValueVector.AnimInstanceId(FObjectTrace::GetObjectId(InContext.AnimInstanceProxy->GetAnimInstanceObject()))
		<< AnimNodeValueVector.NodeId(InContext.GetCurrentNodeId())
		<< AnimNodeValueVector.KeyLength(KeyLength)
		<< AnimNodeValueVector.ValueX(InValue.X)
		<< AnimNodeValueVector.ValueY(InValue.Y)
		<< AnimNodeValueVector.ValueZ(InValue.Z)
		<< AnimNodeValueVector.FrameCounter((uint16)(GFrameCounter % 0xffff))
		<< AnimNodeValueVector.Attachment(InKey, KeyLength * sizeof(TCHAR));
}

void FAnimTrace::OutputAnimNodeValue(const FAnimationBaseContext& InContext, const TCHAR* InKey, const FName& InValue)
{
	bool bEventEnabled = UE_TRACE_EVENT_IS_ENABLED(Animation, AnimNodeValueString);
	if (!bEventEnabled)
	{
		return;
	}

	check(InContext.AnimInstanceProxy);

	TRACE_OBJECT(InContext.AnimInstanceProxy->GetAnimInstanceObject());

	int32 KeyLength = FCString::Strlen(InKey) + 1;
	int32 ValueLength = InValue.GetStringLength() + 1;

	auto StringCopyFunc = [KeyLength, ValueLength, InKey, &InValue](uint8* Out)
	{
		FCString::Strncpy(reinterpret_cast<TCHAR*>(Out), InKey, KeyLength);
		InValue.ToString(reinterpret_cast<TCHAR*>(Out) + KeyLength, ValueLength);
	};

	UE_TRACE_LOG(Animation, AnimNodeValueString, (KeyLength + ValueLength) * sizeof(TCHAR))
		<< AnimNodeValueString.Cycle(FPlatformTime::Cycles64())
		<< AnimNodeValueString.AnimInstanceId(FObjectTrace::GetObjectId(InContext.AnimInstanceProxy->GetAnimInstanceObject()))
		<< AnimNodeValueString.NodeId(InContext.GetCurrentNodeId())
		<< AnimNodeValueString.KeyLength(KeyLength)
		<< AnimNodeValueString.FrameCounter((uint16)(GFrameCounter % 0xffff))
		<< AnimNodeValueString.Attachment(StringCopyFunc);
}

void FAnimTrace::OutputAnimNodeValue(const FAnimationBaseContext& InContext, const TCHAR* InKey, const TCHAR* InValue)
{
	bool bEventEnabled = UE_TRACE_EVENT_IS_ENABLED(Animation, AnimNodeValueString);
	if (!bEventEnabled)
	{
		return;
	}

	check(InContext.AnimInstanceProxy);

	TRACE_OBJECT(InContext.AnimInstanceProxy->GetAnimInstanceObject());

	int32 KeyLength = FCString::Strlen(InKey) + 1;
	int32 ValueLength = FCString::Strlen(InValue) + 1;

	auto StringCopyFunc = [KeyLength, ValueLength, InKey, InValue](uint8* Out)
	{
		FCString::Strncpy(reinterpret_cast<TCHAR*>(Out), InKey, KeyLength);
		FCString::Strncpy(reinterpret_cast<TCHAR*>(Out) + KeyLength, InValue, ValueLength);
	};

	UE_TRACE_LOG(Animation, AnimNodeValueString, (KeyLength + ValueLength) * sizeof(TCHAR))
		<< AnimNodeValueString.Cycle(FPlatformTime::Cycles64())
		<< AnimNodeValueString.AnimInstanceId(FObjectTrace::GetObjectId(InContext.AnimInstanceProxy->GetAnimInstanceObject()))
		<< AnimNodeValueString.NodeId(InContext.GetCurrentNodeId())
		<< AnimNodeValueString.KeyLength(KeyLength)
		<< AnimNodeValueString.FrameCounter((uint16)(GFrameCounter % 0xffff))
		<< AnimNodeValueString.Attachment(StringCopyFunc);
}

void FAnimTrace::OutputAnimNodeValue(const FAnimationBaseContext& InContext, const TCHAR* InKey, const UObject* InValue)
{
	bool bEventEnabled = UE_TRACE_EVENT_IS_ENABLED(Animation, AnimNodeValueObject);
	if (!bEventEnabled)
	{
		return;
	}

	check(InContext.AnimInstanceProxy);

	TRACE_OBJECT(InContext.AnimInstanceProxy->GetAnimInstanceObject());
	TRACE_OBJECT(InValue);

	int32 KeyLength = FCString::Strlen(InKey) + 1;

	UE_TRACE_LOG(Animation, AnimNodeValueObject, KeyLength * sizeof(TCHAR))
		<< AnimNodeValueObject.Cycle(FPlatformTime::Cycles64())
		<< AnimNodeValueObject.AnimInstanceId(FObjectTrace::GetObjectId(InContext.AnimInstanceProxy->GetAnimInstanceObject()))
		<< AnimNodeValueObject.NodeId(InContext.GetCurrentNodeId())
		<< AnimNodeValueObject.Value(FObjectTrace::GetObjectId(InValue))
		<< AnimNodeValueObject.KeyLength(KeyLength)
		<< AnimNodeValueObject.FrameCounter((uint16)(GFrameCounter % 0xffff))
		<< AnimNodeValueObject.Attachment(InKey, KeyLength * sizeof(TCHAR));
}

void FAnimTrace::OutputAnimNodeValue(const FAnimationBaseContext& InContext, const TCHAR* InKey, const UClass* InValue)
{
	bool bEventEnabled = UE_TRACE_EVENT_IS_ENABLED(Animation, AnimNodeValueObject);
	if (!bEventEnabled)
	{
		return;
	}

	check(InContext.AnimInstanceProxy);

	TRACE_OBJECT(InContext.AnimInstanceProxy->GetAnimInstanceObject());
	TRACE_CLASS(InValue);

	int32 KeyLength = FCString::Strlen(InKey) + 1;

	UE_TRACE_LOG(Animation, AnimNodeValueClass, KeyLength * sizeof(TCHAR))
		<< AnimNodeValueClass.Cycle(FPlatformTime::Cycles64())
		<< AnimNodeValueClass.AnimInstanceId(FObjectTrace::GetObjectId(InContext.AnimInstanceProxy->GetAnimInstanceObject()))
		<< AnimNodeValueClass.NodeId(InContext.GetCurrentNodeId())
		<< AnimNodeValueClass.Value(FObjectTrace::GetObjectId(InValue))
		<< AnimNodeValueClass.KeyLength(KeyLength)
		<< AnimNodeValueClass.FrameCounter((uint16)(GFrameCounter % 0xffff))
		<< AnimNodeValueClass.Attachment(InKey, KeyLength * sizeof(TCHAR));
}

void FAnimTrace::OutputAnimSequencePlayer(const FAnimationBaseContext& InContext, const FAnimNode_SequencePlayer& InNode)
{
	bool bEventEnabled = UE_TRACE_EVENT_IS_ENABLED(Animation, AnimSequencePlayer);
	if (!bEventEnabled)
	{
		return;
	}

	check(InContext.AnimInstanceProxy);

	TRACE_OBJECT(InContext.AnimInstanceProxy->GetAnimInstanceObject());

	UE_TRACE_LOG(Animation, AnimSequencePlayer)
		<< AnimSequencePlayer.Cycle(FPlatformTime::Cycles64())
		<< AnimSequencePlayer.AnimInstanceId(FObjectTrace::GetObjectId(InContext.AnimInstanceProxy->GetAnimInstanceObject()))
		<< AnimSequencePlayer.NodeId(InContext.GetCurrentNodeId())
		<< AnimSequencePlayer.Position(InNode.GetAccumulatedTime())
		<< AnimSequencePlayer.Length(InNode.Sequence ? InNode.Sequence->SequenceLength : 0.0f)
		<< AnimSequencePlayer.FrameCount(InNode.Sequence ? InNode.Sequence->GetNumberOfFrames() : 0);
}

void FAnimTrace::OutputStateMachineState(const FAnimationBaseContext& InContext, int32 InStateMachineIndex, int32 InStateIndex, float InStateWeight, float InElapsedTime)
{
	bool bEventEnabled = UE_TRACE_EVENT_IS_ENABLED(Animation, StateMachineState);
	if (!bEventEnabled)
	{
		return;
	}

	check(InContext.AnimInstanceProxy);

	TRACE_OBJECT(InContext.AnimInstanceProxy->GetAnimInstanceObject());

	UE_TRACE_LOG(Animation, StateMachineState)
		<< StateMachineState.Cycle(FPlatformTime::Cycles64())
		<< StateMachineState.AnimInstanceId(FObjectTrace::GetObjectId(InContext.AnimInstanceProxy->GetAnimInstanceObject()))
		<< StateMachineState.NodeId(InContext.GetCurrentNodeId())
		<< StateMachineState.StateMachineIndex(InStateMachineIndex)
		<< StateMachineState.StateIndex(InStateIndex)
		<< StateMachineState.StateWeight(InStateWeight)
		<< StateMachineState.ElapsedTime(InElapsedTime);
}

#endif