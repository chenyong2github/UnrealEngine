// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IKRigDefinition.h"
#include "SkeletalDebugRendering.h"

namespace IKRigDebugRendering
{
	static TArray BoxPoints =
	{
		FVector(0.5f, 0.5f, 0.5f),
		FVector(0.5f, -0.5f, 0.5f),
		FVector(0.5f, -0.5f, 0.5f),
		FVector(-0.5f, -0.5f, 0.5f),
		FVector(-0.5f, -0.5f, 0.5f),
		FVector(-0.5f, 0.5f, 0.5f),
		FVector(-0.5f, 0.5f, 0.5f),
		FVector(0.5f, 0.5f, 0.5f),

		FVector(0.5f, 0.5f, -0.5f),
		FVector(0.5f, -0.5f, -0.5f),
		FVector(0.5f, -0.5f, -0.5f),
		FVector(-0.5f, -0.5f, -0.5f),
		FVector(-0.5f, -0.5f, -0.5f),
		FVector(-0.5f, 0.5f, -0.5f),
		FVector(-0.5f, 0.5f, -0.5f),
		FVector(0.5f, 0.5f, -0.5f),

		FVector(0.5f, 0.5f, 0.5f),
		FVector(0.5f, 0.5f, -0.5f),
		FVector(0.5f, -0.5f, 0.5f),
		FVector(0.5f, -0.5f, -0.5f),
		FVector(-0.5f, -0.5f, 0.5f),
		FVector(-0.5f, -0.5f, -0.5f),
		FVector(-0.5f, 0.5f, 0.5f),
		FVector(-0.5f, 0.5f, -0.5f),
	};

	static void DrawWireCube(
		FPrimitiveDrawInterface* PDI,
		const FTransform& Transform,
		FLinearColor Color,
		float Size,
		float Thickness)
	{
		const float Scale = FMath::Clamp(Size, 0.1f, 1000.0f);
		for (int32 PointIndex = 0; PointIndex < BoxPoints.Num() - 1; PointIndex += 2)
		{
			PDI->DrawLine(Transform.TransformPosition(BoxPoints[PointIndex] * Scale), Transform.TransformPosition(BoxPoints[PointIndex + 1] * Scale), Color, SDPG_Foreground, Thickness);
		}
	}
}
