// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "ObjectTrace.h"

#define ANIM_TRACE_ENABLED OBJECT_TRACE_ENABLED

#if ANIM_TRACE_ENABLED

struct FAnimInstanceProxy;
struct FAnimTickRecord;
struct FAnimationBaseContext;
struct FAnimationUpdateContext;
class USkeletalMesh;
class USkeletalMeshComponent;
struct FAnimationUpdateContext;
struct FAnimationBaseContext;
class FName;

struct FAnimTrace
{
	/** Initialize animation tracing */
	ENGINE_API static void Init();

	/** Helper function to output a set of tick records */
	ENGINE_API static void OutputAnimTickRecords(const FAnimInstanceProxy& InProxy, const USkeletalMeshComponent* InComponent);

	/** Helper function to output a skeletal mesh */
	ENGINE_API static void OutputSkeletalMesh(const USkeletalMesh* InMesh);

	/** Helper function to output a skeletal mesh pose */
	ENGINE_API static void OutputSkeletalMeshPose(const USkeletalMeshComponent* InComponent);

	/** Helper function to output a pose link */
	ENGINE_API static void OutputPoseLink(const FAnimationUpdateContext& InContext);

	/** Helper function to output a tracked value for an anim node */
	ENGINE_API static void OutputAnimNodeValue(const FAnimationBaseContext& InContext, const TCHAR* InKey, bool Value);
	ENGINE_API static void OutputAnimNodeValue(const FAnimationBaseContext& InContext, const TCHAR* InKey, int32 Value);
	ENGINE_API static void OutputAnimNodeValue(const FAnimationBaseContext& InContext, const TCHAR* InKey, float Value);
	ENGINE_API static void OutputAnimNodeValue(const FAnimationBaseContext& InContext, const TCHAR* InKey, const FName& Value);
	ENGINE_API static void OutputAnimNodeValue(const FAnimationBaseContext& InContext, const TCHAR* InKey, const TCHAR* Value);
};

#define TRACE_ANIM_TICK_RECORDS(Proxy, Component) \
	FAnimTrace::OutputAnimTickRecords(Proxy, Component);

#define TRACE_SKELETAL_MESH(Mesh) \
	FAnimTrace::OutputSkeletalMesh(Mesh);

#define TRACE_SKELETALMESH_POSE(Component) \
	FAnimTrace::OutputSkeletalMeshPose(Component);

#define TRACE_POSE_LINK(Context) \
	FAnimTrace::OutputPoseLink(Context);

#define TRACE_ANIM_NODE_VALUE(Context, Key, Value) \
	FAnimTrace::OutputAnimNodeValue(Context, Key, Value);

#else

#define TRACE_ANIM_TICK_RECORDS
#define TRACE_SKELETAL_MESH
#define TRACE_SKELETAL_MESH_POSE
#define TRACE_POSE_LINK
#define TRACE_ANIM_NODE_VALUE

#endif