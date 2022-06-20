// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"

class FPrimitiveDrawInterface;

namespace SkeletalDebugRendering
{
	static const int32 NumSphereSides = 10;
	static const int32 NumConeSides = 4;

/**
 * Draw a wireframe bone from InStart to InEnd 
 * @param	PDI					Primitive draw interface to use
 * @param	InStart				The start location of the bone
 * @param	InEnd				The end location of the bone
 * @param	InColor				The color to draw the bone with
 * @param	InDepthPriority		The scene depth priority group to use
 * @param	SphereRadius		SphereRadius
 */
ENGINE_API void DrawWireBone(
	FPrimitiveDrawInterface* PDI,
	const FVector& InStart,
	const FVector& InEnd,
	const FLinearColor& InColor,
	ESceneDepthPriorityGroup InDepthPriority,
	const float SphereRadius = 1.f);

/**
 * Draw a set of axes to represent a transform
 * @param	PDI					Primitive draw interface to use
 * @param	InTransform			The transform to represent
 * @param	InDepthPriority		The scene depth priority group to use
 * @param	Thickness			Thickness of Axes
 * @param	AxisLength			AxisLength
 */
ENGINE_API void DrawAxes(
	FPrimitiveDrawInterface* PDI,
	const FTransform& InTransform,
	ESceneDepthPriorityGroup InDepthPriority,
	const float Thickness = 0.f,
	const float AxisLength = 4.f);

/**
 * Draw a set of axes to represent a transform
 * @param	PDI					Primitive draw interface to use
 * @param	InBoneTransform		The bone transform
 * @param	InChildLocations	The positions of all the children of the bone
 * @param   InChildColors		The colors to use when drawing the cone to each child
 * @param	InColor				The color to use for the bone
 * @param	InDepthPriority		The scene depth priority group to use
 * @param	SphereRadius		Radius of the ball drawn at the bone location
 * @param	bDrawAxes			If true, will draw small coordinate axes inside the joint sphere
 */
ENGINE_API	void DrawWireBoneAdvanced(
	FPrimitiveDrawInterface* PDI,
	const FTransform& InBoneTransform,
	const TArray<FVector>& InChildLocations,
	const TArray<FLinearColor>& InChildColors,
	const FLinearColor& InColor,
	ESceneDepthPriorityGroup InDepthPriority,
	const float SphereRadius,
	const bool bDrawAxes);

/**
 * Draw a red cone showing offset of root bone from the component origin
 * @param	PDI					Primitive draw interface to use
 * @param	InBoneTransform		The bone transform
 * @param	ComponentOrigin		The position in world space of the component this bone lives within
 * @param	SphereRadius		The radius of the root bone
 */
ENGINE_API	void DrawRootCone(
	FPrimitiveDrawInterface* PDI,
	const FTransform& InBoneTransform,
	const FVector& ComponentOrigin,
	const float SphereRadius);
}
