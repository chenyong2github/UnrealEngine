// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "ObjectTrace.h"
#include "CoreMinimal.h"

#define ANIM_TRACE_ENABLED OBJECT_TRACE_ENABLED

#if ANIM_TRACE_ENABLED

struct FAnimInstanceProxy;
struct FAnimTickRecord;
struct FAnimationBaseContext;
struct FAnimationUpdateContext;
class USkeletalMesh;
class USkeletalMeshComponent;
struct FAnimationInitializeContext;
struct FAnimationUpdateContext;
struct FAnimationBaseContext;
struct FAnimationCacheBonesContext;
struct FPoseContext;
struct FComponentSpacePoseContext;
class FName;
struct FVector;
struct FRotator;
struct FAnimNode_SequencePlayer;

struct FAnimTrace
{
	/** The various phases of anim graph processing */
	enum class EPhase : uint8
	{
		Initialize = 0,
		PreUpdate = 1,
		Update = 2,
		CacheBones = 3,
		Evaluate = 4,
	};

	/** Helper for outputting anim nodes */
	struct FScopedAnimNodeTrace
	{
		FScopedAnimNodeTrace(const FAnimationInitializeContext& InContext);
		FScopedAnimNodeTrace(const FAnimationUpdateContext& InContext);
		FScopedAnimNodeTrace(const FAnimationCacheBonesContext& InContext);
		FScopedAnimNodeTrace(const FPoseContext& InContext);
		FScopedAnimNodeTrace(const FComponentSpacePoseContext& InContext);

		~FScopedAnimNodeTrace();

		const FAnimationBaseContext& Context;
	};

	/** Helper for outputting anim graphs */
	struct FScopedAnimGraphTrace
	{
		FScopedAnimGraphTrace(const FAnimationInitializeContext& InContext);
		FScopedAnimGraphTrace(const FAnimationUpdateContext& InContext);
		FScopedAnimGraphTrace(const FAnimationCacheBonesContext& InContext);
		FScopedAnimGraphTrace(const FPoseContext& InContext);
		FScopedAnimGraphTrace(const FComponentSpacePoseContext& InContext);

		~FScopedAnimGraphTrace();

		uint64 StartCycle;
		const FAnimationBaseContext& Context;
		EPhase Phase;
	};

	/** Describes a debug line output to the world */
	struct FDebugLine
	{
		FDebugLine(const FVector& InStartLocation, const FVector& InEndLocation, const FColor& InColor, bool bInPersistentLines = false, float InLifeTime = -1.0f, float InThickness = 0.0f)
			: StartLocation(InStartLocation)
			, EndLocation(InEndLocation)
			, Color(InColor)
			, LifeTime(InLifeTime)
			, Thickness(InThickness)
			, bPersistentLines(bInPersistentLines)
		{}

		FVector StartLocation;
		FVector EndLocation;
		FColor Color;
		float LifeTime;
		float Thickness;
		bool bPersistentLines;
	};

	/** Initialize animation tracing */
	ENGINE_API static void Init();

	/** Helper function to output a tick record */
	ENGINE_API static void OutputAnimTickRecord(const FAnimationBaseContext& InContext, const FAnimTickRecord& InTickRecord);

	/** Helper function to output a skeletal mesh */
	ENGINE_API static void OutputSkeletalMesh(const USkeletalMesh* InMesh);

	/** Helper function to output a skeletal mesh pose, curves etc. */
	ENGINE_API static void OutputSkeletalMeshComponent(const USkeletalMeshComponent* InComponent);

	/** Helper function to output a skeletal mesh frame marker */
	ENGINE_API static void OutputSkeletalMeshFrame(const USkeletalMeshComponent* InComponent);

	/** Helper function to output an anim graph's execution event */
	ENGINE_API static void OutputAnimGraph(const FAnimationBaseContext& InContext, uint64 InStartCycle, uint64 InEndCycle, uint8 InPhase);

	/** Helper function to output an anim node's execution event */
	ENGINE_API static void OutputAnimNodeStart(const FAnimationBaseContext& InContext, uint64 InStartCycle, int32 InPreviousNodeId, int32 InNodeId, float InBlendWeight, float InRootMotionWeight, uint8 InPhase);
	ENGINE_API static void OutputAnimNodeEnd(const FAnimationBaseContext& InContext, uint64 InEndCycle);

	/** Helper function to output a tracked value for an anim node */
	ENGINE_API static void OutputAnimNodeValue(const FAnimationBaseContext& InContext, const TCHAR* InKey, bool InValue);
	ENGINE_API static void OutputAnimNodeValue(const FAnimationBaseContext& InContext, const TCHAR* InKey, int32 InValue);
	ENGINE_API static void OutputAnimNodeValue(const FAnimationBaseContext& InContext, const TCHAR* InKey, float InValue);
	ENGINE_API static void OutputAnimNodeValue(const FAnimationBaseContext& InContext, const TCHAR* InKey, const FVector& InValue);
	ENGINE_API static void OutputAnimNodeValue(const FAnimationBaseContext& InContext, const TCHAR* InKey, const FRotator& InValue);
	ENGINE_API static void OutputAnimNodeValue(const FAnimationBaseContext& InContext, const TCHAR* InKey, const FName& InValue);
	ENGINE_API static void OutputAnimNodeValue(const FAnimationBaseContext& InContext, const TCHAR* InKey, const TCHAR* InValue);
	ENGINE_API static void OutputAnimNodeValue(const FAnimationBaseContext& InContext, const TCHAR* InKey, const UClass* InValue);
	ENGINE_API static void OutputAnimNodeValue(const FAnimationBaseContext& InContext, const TCHAR* InKey, const UObject* InValue);

	/** Helper function to output debug info for sequence player nodes */
	ENGINE_API static void OutputAnimSequencePlayer(const FAnimationBaseContext& InContext, const FAnimNode_SequencePlayer& InNode);

	/** 
	 * Helper function to output a name to the trace stream, referenced by ID. 
	 * @return the ID used to reference the name
	 */
	ENGINE_API static uint32 OutputName(const FName& InName);

	/** Helper function to output a state machine state's info */
	ENGINE_API static void OutputStateMachineState(const FAnimationBaseContext& InContext, int32 InStateMachineIndex, int32 InStateIndex, float InStateWeight, float InElapsedTime);
};

#define TRACE_ANIM_TICK_RECORD(Context, TickRecord) \
	FAnimTrace::OutputAnimTickRecord(Context, TickRecord);

#define TRACE_SKELETAL_MESH(Mesh) \
	FAnimTrace::OutputSkeletalMesh(Mesh);

#define TRACE_SKELETAL_MESH_COMPONENT(Component) \
	FAnimTrace::OutputSkeletalMeshComponent(Component);

#define TRACE_SKELETALMESH_FRAME(Component) \
	FAnimTrace::OutputSkeletalMeshFrame(Component);

#define TRACE_SCOPED_ANIM_GRAPH(Context) \
	FAnimTrace::FScopedAnimGraphTrace _ScopedAnimGraphTrace(Context);

#define TRACE_SCOPED_ANIM_NODE(Context) \
	FAnimTrace::FScopedAnimNodeTrace _ScopedAnimNodeTrace(Context);

#define TRACE_ANIM_NODE_VALUE(Context, Key, Value) \
	FAnimTrace::OutputAnimNodeValue(Context, Key, Value);

#define TRACE_ANIM_SEQUENCE_PLAYER(Context, Node) \
	FAnimTrace::OutputAnimSequencePlayer(Context, Node);

#define TRACE_ANIM_STATE_MACHINE_STATE(Context, StateMachineIndex, StateIndex, StateWeight, ElapsedTime) \
	FAnimTrace::OutputStateMachineState(Context, StateMachineIndex, StateIndex, StateWeight, ElapsedTime);

#else

#define TRACE_ANIM_TICK_RECORD(Context, TickRecord)
#define TRACE_SKELETAL_MESH(Mesh)
#define TRACE_SKELETAL_MESH_COMPONENT(Component)
#define TRACE_SKELETALMESH_FRAME(Component)
#define TRACE_SCOPED_ANIM_GRAPH(Context)
#define TRACE_SCOPED_ANIM_NODE(Context)
#define TRACE_ANIM_NODE_VALUE(Context, Key, Value)
#define TRACE_ANIM_SEQUENCE_PLAYER(Context, Node)
#define TRACE_ANIM_STATE_MACHINE_STATE(Context, StateMachineIndex, StateIndex, StateWeight, ElapsedTime)

#endif