// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "Math/Box.h"
#include "Math/Color.h"
#include "HAL/Platform.h"

// non ModelDef specific debug helpers
namespace NetworkPredictionDebug
{
	NETWORKPREDICTION_API void DrawDebugOutline(FTransform Transform, FBox BoundingBox, FColor Color, float Lifetime);
	NETWORKPREDICTION_API void DrawDebugText3D(const TCHAR* Str, FTransform Transform, FColor, float Lifetime);
};