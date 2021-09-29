// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "LWComponentTypes.h"
#include "ZoneGraphTypes.h"
#include "MassZoneGraphAnnotationFragments.generated.h"

struct FLWComponentSystemExecutionContext;

USTRUCT()
struct MASSAIBEHAVIOR_API FMassZoneGraphAnnotationTagsFragment : public FLWComponentData
{
	GENERATED_BODY()

	/** Behavior tags for current lane */
	UPROPERTY()
	FZoneGraphTagMask Tags;
};

USTRUCT()
struct FMassZoneGraphAnnotationVariableTickChunkFragment : public FLWChunkComponent
{
	GENERATED_BODY();

	/** Update the ticking frequency of the chunk and return if this chunk should be process this frame */
	static bool UpdateChunk(FLWComponentSystemExecutionContext& Context);

	float TimeUntilNextTick = 0.0f;
};