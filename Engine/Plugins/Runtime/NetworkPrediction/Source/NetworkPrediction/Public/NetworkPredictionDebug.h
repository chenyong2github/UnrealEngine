// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

// non ModelDef specific debug helpers
namespace NetworkPredictionDebug
{
	NETWORKPREDICTION_API void DrawDebugOutline(FTransform Transform, FBox BoundingBox, FColor Color, float Lifetime);
	NETWORKPREDICTION_API void DrawDebugText3D(const TCHAR* Str, FTransform Transform, FColor, float Lifetime);
};