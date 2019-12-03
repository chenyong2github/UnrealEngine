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

UE_TRACE_EVENT_BEGIN(Animation, TickRecord)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(uint64, ComponentId)
	UE_TRACE_EVENT_FIELD(uint64, AnimInstanceId)
	UE_TRACE_EVENT_FIELD(uint64, AssetId)
	UE_TRACE_EVENT_FIELD(float, BlendWeight)
	UE_TRACE_EVENT_FIELD(float, PlaybackTime)
	UE_TRACE_EVENT_FIELD(float, RootMotionWeight)
	UE_TRACE_EVENT_FIELD(float, PlayRate)
	UE_TRACE_EVENT_FIELD(uint16, FrameCounter)
	UE_TRACE_EVENT_FIELD(bool, Looping)
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

UE_TRACE_EVENT_BEGIN(Animation, AnimNodeValueBool)
	UE_TRACE_EVENT_FIELD(uint64, AnimInstanceId)
	UE_TRACE_EVENT_FIELD(uint64, FrameId)
	UE_TRACE_EVENT_FIELD(int32, NodeId)
	UE_TRACE_EVENT_FIELD(int32, KeyLength)
	UE_TRACE_EVENT_FIELD(bool, Value)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(Animation, AnimNodeValueInt)
	UE_TRACE_EVENT_FIELD(uint64, AnimInstanceId)
	UE_TRACE_EVENT_FIELD(uint64, FrameId)
	UE_TRACE_EVENT_FIELD(int32, NodeId)
	UE_TRACE_EVENT_FIELD(int32, KeyLength)
	UE_TRACE_EVENT_FIELD(int32, Value)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(Animation, AnimNodeValueFloat)
	UE_TRACE_EVENT_FIELD(uint64, AnimInstanceId)
	UE_TRACE_EVENT_FIELD(uint64, FrameId)
	UE_TRACE_EVENT_FIELD(int32, NodeId)
	UE_TRACE_EVENT_FIELD(int32, KeyLength)
	UE_TRACE_EVENT_FIELD(float, Value)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(Animation, AnimNodeValueString)
	UE_TRACE_EVENT_FIELD(uint64, AnimInstanceId)
	UE_TRACE_EVENT_FIELD(uint64, FrameId)
	UE_TRACE_EVENT_FIELD(int32, NodeId)
	UE_TRACE_EVENT_FIELD(int32, KeyLength)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(Animation, PoseLink)
	UE_TRACE_EVENT_FIELD(uint64, AnimInstanceId)
	UE_TRACE_EVENT_FIELD(uint64, FrameId)
	UE_TRACE_EVENT_FIELD(int32, TargetLinkId)
	UE_TRACE_EVENT_FIELD(int32, SourceLinkId)
	UE_TRACE_EVENT_FIELD(float, Weight)
	UE_TRACE_EVENT_FIELD(int32, TargetNameLength)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(Animation, Name, Important)
	UE_TRACE_EVENT_FIELD(uint32, Id)
UE_TRACE_EVENT_END()

// Object annotations used for tracing
FUObjectAnnotationSparseBool GSkeletalMeshTraceAnnotations;

// Map used for unique name output
TMap<FName, uint32> GAnimTraceNames;

void FAnimTrace::Init()
{
	if (FParse::Param(FCommandLine::Get(), TEXT("objecttrace")))
	{
		UE_TRACE_EVENT_IS_ENABLED(Animation, TickRecord);
		UE_TRACE_EVENT_IS_ENABLED(Animation, SkeletalMesh);
		UE_TRACE_EVENT_IS_ENABLED(Animation, SkeletalMeshComponent);
// 		UE_TRACE_EVENT_IS_ENABLED(Animation, AnimNodeValueBool);
// 		UE_TRACE_EVENT_IS_ENABLED(Animation, AnimNodeValueInt);
// 		UE_TRACE_EVENT_IS_ENABLED(Animation, AnimNodeValueFloat);
// 		UE_TRACE_EVENT_IS_ENABLED(Animation, AnimNodeValueString);
// 		UE_TRACE_EVENT_IS_ENABLED(Animation, PoseLink);
		UE_TRACE_EVENT_IS_ENABLED(Animation, Name);
		Trace::ToggleEvent(TEXT("Animation"), true);
	}
}

void FAnimTrace::OutputAnimTickRecords(const FAnimInstanceProxy& InProxy, const USkeletalMeshComponent* InComponent)
{
	bool bEventEnabled = UE_TRACE_EVENT_IS_ENABLED(Animation, TickRecord);
	if (!bEventEnabled || InComponent == nullptr)
	{
		return;
	}

	TRACE_OBJECT(InComponent);

	auto TraceTickRecord = [&InProxy, &InComponent](const FAnimTickRecord& InAnimTickRecord)
	{
		TRACE_OBJECT(InAnimTickRecord.SourceAsset);

		UE_TRACE_LOG(Animation, TickRecord)
			<< TickRecord.Cycle(FPlatformTime::Cycles64())
			<< TickRecord.ComponentId(FObjectTrace::GetObjectId(InComponent))
			<< TickRecord.AnimInstanceId(FObjectTrace::GetObjectId(InProxy.GetAnimInstanceObject()))
			<< TickRecord.AssetId(FObjectTrace::GetObjectId(InAnimTickRecord.SourceAsset))
			<< TickRecord.BlendWeight(InAnimTickRecord.EffectiveBlendWeight)
			<< TickRecord.PlaybackTime(*InAnimTickRecord.TimeAccumulator)
			<< TickRecord.RootMotionWeight(InAnimTickRecord.RootMotionWeightModifier)
			<< TickRecord.PlayRate(InAnimTickRecord.PlayRateMultiplier)
			<< TickRecord.FrameCounter((uint16)(GFrameCounter % 0xffff))
			<< TickRecord.Looping(InAnimTickRecord.bLooping);
	};

	const TArray<FAnimGroupInstance>& SyncGroups = InProxy.SyncGroupArrays[InProxy.GetSyncGroupWriteIndex()];
	const TArray<FAnimTickRecord>& UngroupedActivePlayers = InProxy.UngroupedActivePlayerArrays[InProxy.GetSyncGroupWriteIndex()];
	for(const FAnimGroupInstance& SyncGroup : SyncGroups)
	{
		for(const FAnimTickRecord& ActivePlayer : SyncGroup.ActivePlayers)
		{
			TraceTickRecord(ActivePlayer);
		}
	}

	for(const FAnimTickRecord& UngroupedActivePlayer : UngroupedActivePlayers)
	{
		TraceTickRecord(UngroupedActivePlayer);
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

	uint32 BoneCount = (uint32)InMesh->RefSkeleton.GetRawBoneNum();

	auto CopyParentIndices = [InMesh](uint8* Out)
	{
		int32* OutParentIndices = reinterpret_cast<int32*>(Out);
		for(const FMeshBoneInfo& BoneInfo : InMesh->RefSkeleton.GetRawRefBoneInfo())
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

void FAnimTrace::OutputAnimNodeValue(const FAnimationBaseContext& InContext, const TCHAR* InKey, bool InValue)
{
	bool bEventEnabled = UE_TRACE_EVENT_IS_ENABLED(Animation, AnimNodeValueBool);
	if (!bEventEnabled)
	{
		return;
	}

	check(InContext.AnimInstanceProxy);

	int32 KeyLength = FCString::Strlen(InKey) + 1;

	UE_TRACE_LOG(Animation, AnimNodeValueBool, KeyLength * sizeof(TCHAR))
		<< AnimNodeValueBool.AnimInstanceId(FObjectTrace::GetObjectId(InContext.AnimInstanceProxy->GetAnimInstanceObject()))
		<< AnimNodeValueBool.FrameId(GFrameCounter)
		<< AnimNodeValueBool.NodeId(InContext.GetCurrentNodeId())
		<< AnimNodeValueBool.KeyLength(KeyLength)
		<< AnimNodeValueBool.Value(InValue)
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

	int32 KeyLength = FCString::Strlen(InKey) + 1;

	UE_TRACE_LOG(Animation, AnimNodeValueInt, KeyLength * sizeof(TCHAR))
		<< AnimNodeValueInt.AnimInstanceId(FObjectTrace::GetObjectId(InContext.AnimInstanceProxy->GetAnimInstanceObject()))
		<< AnimNodeValueInt.FrameId(GFrameCounter)
		<< AnimNodeValueInt.NodeId(InContext.GetCurrentNodeId())
		<< AnimNodeValueInt.KeyLength(KeyLength)
		<< AnimNodeValueInt.Value(InValue)
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

	int32 KeyLength = FCString::Strlen(InKey) + 1;

	UE_TRACE_LOG(Animation, AnimNodeValueFloat, KeyLength * sizeof(TCHAR))
		<< AnimNodeValueFloat.AnimInstanceId(FObjectTrace::GetObjectId(InContext.AnimInstanceProxy->GetAnimInstanceObject()))
		<< AnimNodeValueFloat.FrameId(GFrameCounter)
		<< AnimNodeValueFloat.NodeId(InContext.GetCurrentNodeId())
		<< AnimNodeValueFloat.KeyLength(KeyLength)
		<< AnimNodeValueFloat.Value(InValue)
		<< AnimNodeValueFloat.Attachment(InKey, KeyLength * sizeof(TCHAR));
}

void FAnimTrace::OutputAnimNodeValue(const FAnimationBaseContext& InContext, const TCHAR* InKey, const FName& InValue)
{
	bool bEventEnabled = UE_TRACE_EVENT_IS_ENABLED(Animation, AnimNodeValueFloat);
	if (!bEventEnabled)
	{
		return;
	}

	check(InContext.AnimInstanceProxy);

	int32 KeyLength = FCString::Strlen(InKey) + 1;
	int32 ValueLength = InValue.GetStringLength() + 1;

	auto StringCopyFunc = [KeyLength, ValueLength, InKey, &InValue](uint8* Out)
	{
		FCString::Strncpy(reinterpret_cast<TCHAR*>(Out), InKey, KeyLength);
		InValue.ToString(reinterpret_cast<TCHAR*>(Out) + KeyLength, ValueLength);
	};

	UE_TRACE_LOG(Animation, AnimNodeValueString, KeyLength * sizeof(TCHAR))
		<< AnimNodeValueString.AnimInstanceId(FObjectTrace::GetObjectId(InContext.AnimInstanceProxy->GetAnimInstanceObject()))
		<< AnimNodeValueString.FrameId(GFrameCounter)
		<< AnimNodeValueString.NodeId(InContext.GetCurrentNodeId())
		<< AnimNodeValueString.KeyLength(KeyLength)
		<< AnimNodeValueString.Attachment(InKey, KeyLength * sizeof(TCHAR));
}

void FAnimTrace::OutputAnimNodeValue(const FAnimationBaseContext& InContext, const TCHAR* InKey, const TCHAR* InValue)
{
	bool bEventEnabled = UE_TRACE_EVENT_IS_ENABLED(Animation, AnimNodeValueFloat);
	if (!bEventEnabled)
	{
		return;
	}

	check(InContext.AnimInstanceProxy);

	int32 KeyLength = FCString::Strlen(InKey) + 1;
	int32 ValueLength = FCString::Strlen(InValue) + 1;

	auto StringCopyFunc = [KeyLength, ValueLength, InKey, InValue](uint8* Out)
	{
		FCString::Strncpy(reinterpret_cast<TCHAR*>(Out), InKey, KeyLength);
		FCString::Strncpy(reinterpret_cast<TCHAR*>(Out) + KeyLength, InValue, ValueLength);
	};

	UE_TRACE_LOG(Animation, AnimNodeValueString, KeyLength * sizeof(TCHAR))
		<< AnimNodeValueString.AnimInstanceId(FObjectTrace::GetObjectId(InContext.AnimInstanceProxy->GetAnimInstanceObject()))
		<< AnimNodeValueString.FrameId(GFrameCounter)
		<< AnimNodeValueString.NodeId(InContext.GetCurrentNodeId())
		<< AnimNodeValueString.KeyLength(KeyLength)
		<< AnimNodeValueString.Attachment(InKey, KeyLength * sizeof(TCHAR));
}

void FAnimTrace::OutputPoseLink(const FAnimationUpdateContext& InContext)
{
	bool bEventEnabled = UE_TRACE_EVENT_IS_ENABLED(Animation, PoseLink);
	if (!bEventEnabled)
	{
		return;
	}

	check(InContext.AnimInstanceProxy);

	IAnimClassInterface* AnimBlueprintClass = InContext.GetAnimClass();
	check(AnimBlueprintClass);
	const TArray<UStructProperty*>& AnimNodeProperties = AnimBlueprintClass->GetAnimNodeProperties();
	check(AnimNodeProperties.IsValidIndex(InContext.GetCurrentNodeId()));
	UProperty* LinkedProperty = AnimNodeProperties[InContext.GetCurrentNodeId()];

	int32 NameLength = LinkedProperty->GetFName().GetStringLength() + 1;

	auto StringCopyFunc = [NameLength, LinkedProperty](uint8* Out)
	{
		LinkedProperty->GetFName().ToString(reinterpret_cast<TCHAR*>(Out), NameLength);
	};

	UE_TRACE_LOG(Animation, PoseLink)
		<< PoseLink.AnimInstanceId(FObjectTrace::GetObjectId(InContext.AnimInstanceProxy->GetAnimInstanceObject()))
		<< PoseLink.FrameId(GFrameCounter)
		<< PoseLink.SourceLinkId(InContext.GetPreviousNodeId())
		<< PoseLink.TargetLinkId(InContext.GetCurrentNodeId())
		<< PoseLink.Weight(InContext.GetFinalBlendWeight())
		<< PoseLink.TargetNameLength(NameLength)
		<< PoseLink.Attachment(StringCopyFunc);
}

#endif