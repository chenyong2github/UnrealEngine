// Copyright Epic Games, Inc. All Rights Reserved.

#include "SkeletalDebugRendering.h"
#include "DrawDebugHelpers.h"
#include "SceneManagement.h"

namespace SkeletalDebugRendering
{

/** A fast and simple bone drawing function. This draws a sphere and a pyramid connection to the PARENT bone.
 * Use this for basic debug drawing, but if the user is able to select or edit the bones, prefer DrawWireBoneAdvanced.*/
void DrawWireBone(
	FPrimitiveDrawInterface* PDI,
	const FVector& InStart,
	const FVector& InEnd,
	const FLinearColor& InColor,
	ESceneDepthPriorityGroup InDepthPriority,
	const float SphereRadius)
{
#if ENABLE_DRAW_DEBUG
	// Calc cone size 
	const FVector EndToStart = (InStart - InEnd);
	const float ConeLength = EndToStart.Size();
	const float Angle = FMath::RadiansToDegrees(FMath::Atan(SphereRadius / ConeLength));

	// Render Sphere for bone end point and a cone between it and its parent.
	DrawWireSphere(PDI, InEnd, InColor, SphereRadius, NumSphereSides, InDepthPriority, 0.0f, 1.0f);

	TArray<FVector> Verts;
	DrawWireCone(
		PDI,
		Verts,
		FRotationMatrix::MakeFromX(EndToStart) * FTranslationMatrix(InEnd),
		ConeLength,
		Angle,
		NumConeSides,
		InColor,
		InDepthPriority,
		0.0f,
		1.0f);
#endif
}

/** An advanced bone drawing function for use with interactive editors where the user can select and manipulate bones.
 *
 * Differences from DrawWireBone() include:
 * 1. Drawing all cone-connections to children as part of the "bone" itself so that the user can select the bone
 *	  by clicking on any of it's children connections (as in all DCC applications)
 * 2. Cone-connectors are drawn *between* spheres, not overlapping them (cleaner)
 * 3. Bone sphere is oriented with bone rotation.
 * 4. Connections to children can be colored individually to allow highlighting parent connections on selected children.
 *
 * This function, and the code required to structure the drawing in this manner, will incur some additional cost over
 * DrawWireBone(). So in cases where you just want to debug draw a skeleton; with no option to select or manipulate
 * the bones, it may be preferable to use DrawWireBone().
 */
void DrawWireBoneAdvanced(
	FPrimitiveDrawInterface* PDI,
	const FTransform& InBoneTransform,
	const TArray<FVector>& InChildLocations,
	const TArray<FLinearColor>& InChildColors,
	const FLinearColor& InColor,
	ESceneDepthPriorityGroup InDepthPriority,
	const float SphereRadius,
	const bool bDrawAxes)
{
#if ENABLE_DRAW_DEBUG

	const FVector BoneLocation = InBoneTransform.GetLocation();

	// draw wire sphere at joint origin, oriented with the bone
	DrawWireSphere(PDI, InBoneTransform, InColor, SphereRadius, NumSphereSides, InDepthPriority, 0.0f, 1.0f);

	// draw axes at joint location
	if (bDrawAxes)
	{
		SkeletalDebugRendering::DrawAxes(PDI, InBoneTransform, SDPG_Foreground, 0.f , SphereRadius);
	}

	// draw wire cones to each child
	for (int32 ChildIndex=0; ChildIndex<InChildLocations.Num(); ++ChildIndex)
	{
		const FVector& ChildPoint = InChildLocations[ChildIndex];
		// offset start/end based on bone radius
		const FVector RadiusOffset = (ChildPoint - BoneLocation).GetSafeNormal() * SphereRadius;
		const FVector Start = BoneLocation + RadiusOffset;
		const FVector End = ChildPoint - RadiusOffset;
			
		// calc cone size
		const FVector EndToStart = (Start - End);
		const float ConeLength = EndToStart.Size();
		const float Angle = FMath::RadiansToDegrees(FMath::Atan(SphereRadius / ConeLength));
		TArray<FVector> Verts;
		DrawWireCone(
			PDI,
			Verts,
			FRotationMatrix::MakeFromX(EndToStart) * FTranslationMatrix(End),
			ConeLength,
			Angle,
			NumConeSides,
			InChildColors[ChildIndex],
			InDepthPriority,
			0.0f,
			1.0f);
	}
#endif
}

void DrawAxes(
	FPrimitiveDrawInterface* PDI,
	const FTransform& Transform,
	ESceneDepthPriorityGroup InDepthPriority,
	const float Thickness,
	const float AxisLength)
{
#if ENABLE_DRAW_DEBUG
	// Display colored coordinate system axes for this joint.
	const FVector Origin = Transform.GetLocation();

	// Red = X
	FVector XAxis = Transform.TransformVector(FVector(1.0f, 0.0f, 0.0f));
	XAxis.Normalize();
	PDI->DrawLine(Origin, Origin + XAxis * AxisLength, FColor(255, 80, 80), InDepthPriority, Thickness, 1.0f);

	// Green = Y
	FVector YAxis = Transform.TransformVector(FVector(0.0f, 1.0f, 0.0f));
	YAxis.Normalize();
	PDI->DrawLine(Origin, Origin + YAxis * AxisLength, FColor(80, 255, 80), InDepthPriority, Thickness, 1.0f);

	// Blue = Z
	FVector ZAxis = Transform.TransformVector(FVector(0.0f, 0.0f, 1.0f));
	ZAxis.Normalize();
	PDI->DrawLine(Origin, Origin + ZAxis * AxisLength, FColor(80, 80, 255), InDepthPriority, Thickness, 1.0f);
#endif
}

void DrawRootCone(
	FPrimitiveDrawInterface* PDI,
	const FTransform& InBoneTransform,
	const FVector& ComponentOrigin,
	const float SphereRadius)
{
#if ENABLE_DRAW_DEBUG
	// offset start/end based on bone radius
	const FVector RadiusOffset = (ComponentOrigin - InBoneTransform.GetLocation()).GetSafeNormal() * SphereRadius;
	const FVector Start = InBoneTransform.GetLocation() + RadiusOffset;
	const FVector End = ComponentOrigin;
			
	// calc cone size
	const FVector EndToStart = (Start - End);
	const float ConeLength = EndToStart.Size();
	const float Angle = FMath::RadiansToDegrees(FMath::Atan(SphereRadius / ConeLength));
	TArray<FVector> Verts;
	DrawWireCone(
		PDI,
		Verts,
		FRotationMatrix::MakeFromX(EndToStart) * FTranslationMatrix(End),
		ConeLength,
		Angle,
		NumConeSides,
		FLinearColor::Red,
		ESceneDepthPriorityGroup::SDPG_Foreground,
		0.0f,
		1.0f);
#endif
}

}