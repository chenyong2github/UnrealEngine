// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IKRigDefinition.h"
#include "SkeletalDebugRendering.h"

namespace IKRigDebugRendering
{
	static const FLinearColor DESELECTED_BONE_COLOR = FLinearColor(0.0f,0.0f,0.025f,1.0f);
	static const FLinearColor SELECTED_BONE_COLOR = FLinearColor(0.2f,1.0f,0.2f,1.0f);
	static const FLinearColor AFFECTED_BONE_COLOR = FLinearColor(1.0f,1.0f,1.0f,1.0f);
	
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

	static void DrawGoal(
		FPrimitiveDrawInterface* PDI,
		const UIKRigEffectorGoal* Goal,
		bool bIsSelected,
		float Size,
		float Thickness)
	{
		const FLinearColor Color = bIsSelected ? FLinearColor::Green : FLinearColor::Yellow;
		const float Scale = FMath::Clamp(Size, 0.1f, 1000.0f);
		const FTransform Transform = Goal->CurrentTransform;
		for (int32 PointIndex = 0; PointIndex < BoxPoints.Num() - 1; PointIndex += 2)
		{
			PDI->DrawLine(Transform.TransformPosition(BoxPoints[PointIndex] * Scale), Transform.TransformPosition(BoxPoints[PointIndex + 1] * Scale), Color, SDPG_Foreground, Thickness);
		}
	}

	// todo - refactor this and fix SkeletalDebugRendering
	// this is copied/adapted from SkeletalDebugRendering because annoyingly the bone rendering puts the joint sphere at the
	// END of the bone. This is so that bones can be rendered individually (needing only start/end point pairs).
	// Which is easier, but doesn't make sense. We need the bones to render from parent to each immediate child.
	// This causes several issues:
	// 1. Bones lack a sphere at the root.
	// 2. Selected bones render to their parent, rather than to their children.
	// Also, this function draws the cone BETWEEN the joint spheres, not through them which is a cleaner end result.
	// 
	static void DrawWireBone(
		FPrimitiveDrawInterface* PDI,
		const FTransform& InBoneTransform,
		const TArray<FVector>& InChildLocations,
		const FLinearColor& InColor,
		ESceneDepthPriorityGroup InDepthPriority,
		const float SphereRadius,
		const bool bDrawAxes)
	{
	#if ENABLE_DRAW_DEBUG
		static const int32 NumSphereSides = 10;
		static const int32 NumConeSides = 4;

		const FVector BoneLocation = InBoneTransform.GetLocation();

		// render sphere at joint origin
		DrawWireSphere(PDI, BoneLocation, InColor, SphereRadius, NumSphereSides, InDepthPriority, 0.0f, 1.0f);
		// draw axes at joint location
		if (bDrawAxes)
		{
			SkeletalDebugRendering::DrawAxes(PDI, InBoneTransform, SDPG_Foreground, 0.f , SphereRadius);
		}

		// draw wire cones to each child
		for (const FVector& ChildPoint : InChildLocations)
		{
			// offset start/end based on bone radius
			const FVector RadiusOffset = (ChildPoint - BoneLocation).GetSafeNormal() * SphereRadius;
			const FVector Start = BoneLocation + RadiusOffset;
			const FVector End = ChildPoint - RadiusOffset;
			
			// calc cone size
			const FVector EndToStart = (Start - End);
			const float ConeLength = EndToStart.Size();
			const float Angle = FMath::RadiansToDegrees(FMath::Atan(SphereRadius / ConeLength));
			TArray<FVector> Verts;
			DrawWireCone(PDI, Verts, FRotationMatrix::MakeFromX(EndToStart) * FTranslationMatrix(End), ConeLength, Angle, NumConeSides, InColor, InDepthPriority, 0.0f, 1.0f);
		}
	#endif
	}
}
