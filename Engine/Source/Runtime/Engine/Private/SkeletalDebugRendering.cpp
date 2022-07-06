// Copyright Epic Games, Inc. All Rights Reserved.

#include "SkeletalDebugRendering.h"
#include "DrawDebugHelpers.h"
#include "SceneManagement.h"
#include "Engine/PoseWatch.h"
#include "Components/SkeletalMeshComponent.h"
#include "BonePose.h"
#include "Animation/BlendProfile.h"
#include "HitProxies.h"


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

void DrawBonesFromPoseWatch(
	const FCompactHeapPose& Pose,
	USkeletalMeshComponent* MeshComponent,
	FPrimitiveDrawInterface* PDI,
	const UPoseWatch* PoseWatch)
{
#if WITH_EDITOR
	if (Pose.GetNumBones() == 0 ||
		Pose.GetBoneContainer().GetCompactPoseNumBones() == 0 ||
		!MeshComponent ||
		!MeshComponent->GetSkeletalMesh() ||
		!PoseWatch
		)
	{
		return;
	}

	// optionally override draw color
	const FLinearColor BoneColor = FLinearColor(PoseWatch->GetColor());

	TArray<FTransform> WorldTransforms;
	WorldTransforms.AddUninitialized(Pose.GetBoneContainer().GetNumBones());

	TArray<uint16> RequiredBones;
	const UBlendProfile* ViewportMask = PoseWatch->ViewportMask;

	const FVector RelativeOffset = MeshComponent->GetComponentRotation().RotateVector(PoseWatch->ViewportOffset);

	// we could cache parent bones as we calculate, but right now I'm not worried about perf issue of this
	for (FCompactPoseBoneIndex BoneIndex : Pose.ForEachBoneIndex())
	{
		FMeshPoseBoneIndex MeshBoneIndex = Pose.GetBoneContainer().MakeMeshPoseIndex(BoneIndex);

		int32 ParentIndex = Pose.GetBoneContainer().GetParentBoneIndex(MeshBoneIndex.GetInt());

		if (ParentIndex == INDEX_NONE)
		{
			WorldTransforms[MeshBoneIndex.GetInt()] = Pose[BoneIndex] * MeshComponent->GetComponentTransform();
			WorldTransforms[MeshBoneIndex.GetInt()].AddToTranslation(RelativeOffset);
		}
		else
		{
			WorldTransforms[MeshBoneIndex.GetInt()] = Pose[BoneIndex] * WorldTransforms[ParentIndex];
		}

		if (ViewportMask == nullptr)
		{
			RequiredBones.Add(MeshBoneIndex.GetInt());
		}
		else
		{
			const FName& BoneName = MeshComponent->GetBoneName(MeshBoneIndex.GetInt());
			const bool bBoneBlendScaleMeetsThreshold = ViewportMask->GetBoneBlendScale(BoneName) > PoseWatch->BlendScaleThreshold;
			if ((!PoseWatch->bInvertViewportMask && bBoneBlendScaleMeetsThreshold) || (PoseWatch->bInvertViewportMask && !bBoneBlendScaleMeetsThreshold))
			{
				RequiredBones.Add(MeshBoneIndex.GetInt());
			}
		}
	}

	FSkelDebugDrawConfig DrawConfig;
	DrawConfig.BoneDrawMode = EBoneDrawMode::All;
	DrawConfig.BoneDrawSize = 1.f;
	DrawConfig.bAddHitProxy = false;
	DrawConfig.bForceDraw = true;
	DrawConfig.DefaultBoneColor = BoneColor;
	DrawConfig.AffectedBoneColor = BoneColor;
	DrawConfig.SelectedBoneColor = BoneColor;
	DrawConfig.ParentOfSelectedBoneColor = BoneColor;

	SkeletalDebugRendering::DrawBones(
		PDI,
		MeshComponent->GetComponentLocation() + RelativeOffset,
		RequiredBones,
		MeshComponent->GetSkeletalMesh()->GetRefSkeleton(),
		WorldTransforms,
		/*SelectedBones*/TArray<int32>(),
		/*BoneColors*/TArray<FLinearColor>(),
		/*HitProxies*/TArray<HHitProxy*>(),
		DrawConfig
	);
#endif
}

void DrawBones(
	FPrimitiveDrawInterface* PDI,
	const FVector& ComponentOrigin,
	const TArray<FBoneIndexType>& RequiredBones,
	const FReferenceSkeleton& RefSkeleton,
	const TArray<FTransform>& WorldTransforms,
	const TArray<int32>& InSelectedBones,
	const TArray<FLinearColor>& BoneColors,
	const TArray<HHitProxy*>& HitProxies,
	const FSkelDebugDrawConfig& DrawConfig)
{
	// first determine which bones to draw, and which to filter out
	TBitArray<> BonesToDraw(false, RefSkeleton.GetNum());
	const bool bDrawSelected = DrawConfig.BoneDrawMode == EBoneDrawMode::Selected;
	const bool bDrawSelectedAndParents = DrawConfig.BoneDrawMode == EBoneDrawMode::SelectedAndParents;
	const bool bDrawSelectedAndChildren = DrawConfig.BoneDrawMode == EBoneDrawMode::SelectedAndChildren;
	const bool bDrawSelectedAndParentsAndChildren = DrawConfig.BoneDrawMode == EBoneDrawMode::SelectedAndParentsAndChildren;

	// add selected bones
	if (bDrawSelected || bDrawSelectedAndParents || bDrawSelectedAndChildren || bDrawSelectedAndParentsAndChildren)
	{
		for (int32 BoneIndex : InSelectedBones)
		{
			if (BoneIndex != INDEX_NONE)
			{
				BonesToDraw[BoneIndex] = true;
			}
		}
	}

	// add children of selected
	if (bDrawSelectedAndChildren || bDrawSelectedAndParentsAndChildren)
	{
		for (int32 BoneIndex = 0; BoneIndex < RefSkeleton.GetNum(); ++BoneIndex)
		{
			const int32 ParentIndex = RefSkeleton.GetParentIndex(BoneIndex);
			if (ParentIndex != INDEX_NONE && BonesToDraw[ParentIndex])
			{
				BonesToDraw[BoneIndex] = true;
			}
		}
	}

	// add parents of selected
	if (bDrawSelectedAndParents || bDrawSelectedAndParentsAndChildren)
	{
		for (const int32 BoneIndex : InSelectedBones)
		{
			if (BoneIndex != INDEX_NONE)
			{
				for (int32 ParentIndex = RefSkeleton.GetParentIndex(BoneIndex); ParentIndex != INDEX_NONE; ParentIndex = RefSkeleton.GetParentIndex(ParentIndex))
				{
					BonesToDraw[ParentIndex] = true;
				}
			}
		}
	}

	// determine which bones are "affected" (these are ALL children of selected bones)
	TBitArray<> AffectedBones(false, RefSkeleton.GetNum());
	for (int32 BoneIndex = 0; BoneIndex < RefSkeleton.GetNum(); ++BoneIndex)
	{
		for (int32 ParentIndex = RefSkeleton.GetParentIndex(BoneIndex); ParentIndex != INDEX_NONE; ParentIndex = RefSkeleton.GetParentIndex(ParentIndex))
		{
			if (InSelectedBones.Contains(ParentIndex))
			{
				AffectedBones[BoneIndex] = true;
				break;
			}
		}
	}

	// spin through all required bones and render them
	const float BoneRadius = DrawConfig.BoneDrawSize;
	for (int32 Index = 0; Index < RequiredBones.Num(); ++Index)
	{
		const int32 BoneIndex = RequiredBones[Index];

		// skips bones that should not be drawn
		const bool bDoDraw = DrawConfig.bForceDraw || DrawConfig.BoneDrawMode == EBoneDrawMode::All || BonesToDraw[BoneIndex];
		if (!bDoDraw)
		{
			continue;
		}

		// determine color of bone based on selection / affected state
		const bool bIsSelected = InSelectedBones.Contains(BoneIndex);
		const bool bIsAffected = AffectedBones[BoneIndex];
		FLinearColor DefaultBoneColor = BoneColors.IsEmpty() ? DrawConfig.DefaultBoneColor : BoneColors[BoneIndex];
		FLinearColor BoneColor = bIsAffected ? DrawConfig.AffectedBoneColor : DefaultBoneColor;
		BoneColor = bIsSelected ? DrawConfig.SelectedBoneColor : BoneColor;

		// draw the little coordinate frame inside the bone ONLY if selected or affected
		const bool bDrawAxesInsideBone = bIsAffected || bIsSelected;

		// draw cone to each child
		// but use a different color if this bone is NOT selected, but the child IS selected
		TArray<FVector> ChildPositions;
		TArray<FLinearColor> ChildColors;
		for (int32 ChildIndex = 0; ChildIndex < RefSkeleton.GetNum(); ++ChildIndex)
		{
			const int32 ParentIndex = RefSkeleton.GetParentIndex(ChildIndex);
			if (ParentIndex == BoneIndex && RequiredBones.Contains(ChildIndex))
			{
				ChildPositions.Add(WorldTransforms[ChildIndex].GetLocation());
				FLinearColor ChildLineColor = BoneColor;
				if (!bIsSelected && InSelectedBones.Contains(ChildIndex))
				{
					ChildLineColor = DrawConfig.ParentOfSelectedBoneColor;
				}
				ChildColors.Add(ChildLineColor);
			}
		}

		const FTransform BoneTransform = WorldTransforms[BoneIndex];

		// Always set new hit proxy to prevent unintentionally using last drawn element's proxy
		PDI->SetHitProxy(DrawConfig.bAddHitProxy ? HitProxies[BoneIndex] : nullptr);
		SkeletalDebugRendering::DrawWireBoneAdvanced(
			PDI,
			BoneTransform,
			ChildPositions,
			ChildColors,
			BoneColor,
			SDPG_Foreground,
			BoneRadius,
			bDrawAxesInsideBone);
		if (RefSkeleton.GetParentIndex(BoneIndex) == INDEX_NONE)
		{
			SkeletalDebugRendering::DrawRootCone(PDI, BoneTransform, ComponentOrigin, BoneRadius);
		}
		PDI->SetHitProxy(nullptr);
	}
}

}