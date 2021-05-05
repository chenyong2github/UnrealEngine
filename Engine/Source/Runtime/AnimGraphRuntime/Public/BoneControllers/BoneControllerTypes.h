// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/Platform.h"
#include "UObject/ObjectMacros.h"

UENUM(BlueprintType)
enum class EWarpingEvaluationMode : uint8
{
	/* Pose warping node parameters are entirely driven by the user */
	Manual,
	/**
	* Pose warping nodes may participate in being graph-driven. This means some
	* properties of the warp may be automatically configured by the accumulated root motion
	* contribution of the animation sub-graph leading into the node
	*/
	Graph
};