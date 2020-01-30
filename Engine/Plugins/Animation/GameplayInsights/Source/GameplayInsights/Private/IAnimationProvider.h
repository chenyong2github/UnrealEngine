// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "TraceServices/Model/AnalysisSession.h"
#include "TraceServices/Containers/Timelines.h"

struct FAnimTickRecordTrace;

struct FSkeletalMeshInfo
{
	uint64 ParentIndicesStartIndex = 0;
	uint64 Id = 0;
	uint32 BoneCount = 0;
};

struct FSkeletalMeshNamedCurve
{
	uint32 Id = 0;
	float Value = 0.0f;
};

struct FSkeletalMeshPoseMessage
{
	FTransform ComponentToWorld;
	uint64 TransformStartIndex = 0;
	uint64 CurveStartIndex = 0;
	uint64 ComponentId = 0;	
	uint64 MeshId = 0;	
	uint16 NumTransforms = 0;
	uint16 NumCurves = 0;
	uint16 FrameCounter = 0;
	uint16 LodIndex = 0;
};

struct FSkeletalMeshFrameMessage
{
	uint64 ComponentId = 0;
	uint16 FrameCounter = 0;
};

struct FTickRecordMessage
{
	uint64 ComponentId = 0;
	uint64 AnimInstanceId = 0;
	uint64 AssetId = 0;
	int32 NodeId = -1;
	float BlendWeight = 0.0f;
	float PlaybackTime = 0.0f;
	float RootMotionWeight = 0.0f;
	float PlayRate = 0.0f;
	float BlendSpacePositionX = 0.0f;
	float BlendSpacePositionY = 0.0f;
	uint16 FrameCounter = 0;
	bool bLooping = false;
	bool bIsBlendSpace = false;
	bool bContinuous = true;
};

enum class EAnimGraphPhase : uint8
{
	Initialize = 0,
	PreUpdate = 1,
	Update = 2,
	CacheBones = 3,
	Evaluate = 4,
};

struct FAnimGraphMessage
{
	uint64 AnimInstanceId = 0;
	int32 NodeCount = 0;
	uint16 FrameCounter = 0;
	EAnimGraphPhase Phase = EAnimGraphPhase::Initialize;
};

struct FAnimNodeMessage
{
	const TCHAR* NodeName = nullptr;
	uint64 AnimInstanceId = 0;
	int32 PreviousNodeId = -1;
	int32 NodeId = -1;
	float Weight = 0.0f;
	float RootMotionWeight = 0.0f;
	uint16 FrameCounter = 0;
	EAnimGraphPhase Phase = EAnimGraphPhase::Initialize;
};

enum class EAnimNodeValueType : uint8
{
	Bool,
	Int32,
	Float,
	Vector,
	String,
	Object,
	Class,
};

struct FAnimNodeValueMessage
{
	FAnimNodeValueMessage()
		: Vector(FVector::ZeroVector)
	{
	}

	struct FVectorEntry
	{
		FVectorEntry(const FVector& InVector)
			: Value(InVector)
		{}

		FVector Value;
	};

	const TCHAR* Key = nullptr;
	uint64 AnimInstanceId = 0;
	union
	{
		struct
		{
			bool bValue;
		} Bool;
		struct
		{
			int32 Value;
		} Int32;
		struct
		{
			float Value;
		} Float;
		FVectorEntry Vector;
		struct
		{
			const TCHAR* Value;
		} String;
		struct
		{
			uint64 Value;
		} Object;
		struct
		{
			uint64 Value;
		} Class;
	};
	int32 NodeId = -1;
	uint16 FrameCounter = 0;
	EAnimNodeValueType Type = EAnimNodeValueType::Bool;
};

struct FAnimSequencePlayerMessage
{
	uint64 AnimInstanceId = 0;
	int32 NodeId = -1;
	float Position = 0.0f;
	float Length = 0.0f;
	int32 FrameCount = 0;
};

struct FBlendSpacePlayerMessage
{
	uint64 AnimInstanceId = 0;
	uint64 BlendSpaceId = 0;
	int32 NodeId = -1;
	float PositionX = 0.0f;
	float PositionY = 0.0f;
	float PositionZ = 0.0f;
};

struct FAnimStateMachineMessage
{
	uint64 AnimInstanceId = 0;
	int32 NodeId = -1;
	int32 StateMachineIndex = -1;
	int32 StateIndex = -1;
	float StateWeight = 0.0f;
	float ElapsedTime = 0.0f;
};

class IAnimationProvider : public Trace::IProvider
{
public:
	typedef Trace::ITimeline<FTickRecordMessage> TickRecordTimeline;
	typedef Trace::ITimeline<FSkeletalMeshPoseMessage> SkeletalMeshPoseTimeline;
	typedef Trace::ITimeline<FAnimGraphMessage> AnimGraphTimeline;
	typedef Trace::ITimeline<FAnimNodeMessage> AnimNodesTimeline;
	typedef Trace::ITimeline<FAnimNodeValueMessage> AnimNodeValuesTimeline;
	typedef Trace::ITimeline<FAnimSequencePlayerMessage> AnimSequencePlayersTimeline;
	typedef Trace::ITimeline<FAnimStateMachineMessage> StateMachinesTimeline;
	typedef Trace::ITimeline<FBlendSpacePlayerMessage> BlendSpacePlayersTimeline;

	virtual bool ReadSkeletalMeshPoseTimeline(uint64 InObjectId, TFunctionRef<void(const SkeletalMeshPoseTimeline&)> Callback) const = 0;
	virtual void GetSkeletalMeshComponentSpacePose(const FSkeletalMeshPoseMessage& InMessage, const FSkeletalMeshInfo& InMeshInfo, FTransform& OutComponentToWorld, TArray<FTransform>& OutTransforms) const = 0;
	virtual void EnumerateTickRecordTimelines(uint64 InObjectId, TFunctionRef<void(uint64, int32, const TickRecordTimeline&)> Callback) const = 0;
	virtual bool ReadTickRecordTimeline(uint64 InObjectId, uint64 InAssetId, int32 InNodeId, TFunctionRef<void(const TickRecordTimeline&)> Callback) const = 0;
	virtual bool ReadAnimGraphTimeline(uint64 InObjectId, TFunctionRef<void(const AnimGraphTimeline&)> Callback) const = 0;
	virtual bool ReadAnimNodesTimeline(uint64 InObjectId, TFunctionRef<void(const AnimNodesTimeline&)> Callback) const = 0;
	virtual bool ReadAnimNodeValuesTimeline(uint64 InObjectId, TFunctionRef<void(const AnimNodeValuesTimeline&)> Callback) const = 0;
	virtual bool ReadAnimSequencePlayersTimeline(uint64 InObjectId, TFunctionRef<void(const AnimSequencePlayersTimeline&)> Callback) const = 0;
	virtual bool ReadAnimBlendSpacePlayersTimeline(uint64 InObjectId, TFunctionRef<void(const BlendSpacePlayersTimeline&)> Callback) const = 0;
	virtual bool ReadStateMachinesTimeline(uint64 InObjectId, TFunctionRef<void(const StateMachinesTimeline&)> Callback) const = 0;
	virtual const FSkeletalMeshInfo* FindSkeletalMeshInfo(uint64 InObjectId) const = 0;
	virtual const TCHAR* GetName(uint32 InId) const = 0;
};