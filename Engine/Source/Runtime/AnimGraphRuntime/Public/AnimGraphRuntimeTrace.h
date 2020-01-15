// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimTrace.h"

#if ANIM_TRACE_ENABLED

struct FAnimationBaseContext;
struct FAnimNode_BlendSpacePlayer;

struct FAnimGraphRuntimeTrace
{
	/** Initialize animation graph runtime tracing */
	ANIMGRAPHRUNTIME_API static void Init();

	/** Helper function to output debug info for blendspace player nodes */
	ANIMGRAPHRUNTIME_API static void OutputBlendSpacePlayer(const FAnimationBaseContext& InContext, const FAnimNode_BlendSpacePlayer& InNode);
};

#define TRACE_BLENDSPACE_PLAYER(Context, Node) \
	FAnimGraphRuntimeTrace::OutputBlendSpacePlayer(Context, Node);

#else

#define TRACE_BLENDSPACE_PLAYER(Context, Node)

#endif