// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tools/PlacementBrushToolBase.h"
#include "UObject/Object.h"
#include "ToolContextInterfaces.h"
#include "InteractiveToolManager.h"
#include "InstancedFoliageActor.h"
#include "FoliageHelper.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/BrushComponent.h"
#include "Components/ModelComponent.h"
#include "LandscapeHeightfieldCollisionComponent.h"
#include "FoliageInstancedStaticMeshComponent.h"
#include "AssetPlacementSettings.h"

bool UPlacementToolBuilderBase::CanBuildTool(const FToolBuilderState& SceneState) const
{
	return PlacementSettings && PlacementSettings->PaletteItems.Num();
}

UInteractiveTool* UPlacementToolBuilderBase::BuildTool(const FToolBuilderState& SceneState) const
{
	UPlacementBrushToolBase* NewTool = FactoryToolInstance(SceneState.ToolManager);
	NewTool->PlacementSettings = PlacementSettings;

	return NewTool;
}

bool UPlacementBrushToolBase::HitTest(const FRay& Ray, FHitResult& OutHit)
{
	const FVector TraceStart(Ray.Origin);
	const FVector TraceEnd(Ray.Origin + Ray.Direction * HALF_WORLD_MAX);

	return FindHitResultWithStartAndEndTraceVectors(OutHit, TraceStart, TraceEnd);
}

double UPlacementBrushToolBase::EstimateMaximumTargetDimension()
{
	return 1000.0;
}

bool UPlacementBrushToolBase::FindHitResultWithStartAndEndTraceVectors(FHitResult& OutHit, const FVector& TraceStart, const FVector& TraceEnd, float TraceRadius)
{
	UWorld* EditingWorld = GetToolManager()->GetWorld();
	constexpr TCHAR NAME_PlacementBrushTool[] = TEXT("PlacementBrushTool");

	auto FilterFunc = [this](const UPrimitiveComponent* InComponent) {
		if (InComponent && this->PlacementSettings.IsValid())
		{
			bool bFoliageOwned = InComponent->GetOwner() && FFoliageHelper::IsOwnedByFoliage(InComponent->GetOwner());
			const bool bAllowLandscape = this->PlacementSettings->bLandscape;
			const bool bAllowStaticMesh = this->PlacementSettings->bStaticMeshes;
			const bool bAllowBSP = this->PlacementSettings->bBSP;
			const bool bAllowFoliage = this->PlacementSettings->bFoliage;
			const bool bAllowTranslucent = this->PlacementSettings->bTranslucent;

			// Whitelist
			bool bAllowed =
				(bAllowLandscape && InComponent->IsA(ULandscapeHeightfieldCollisionComponent::StaticClass())) ||
				(bAllowStaticMesh && InComponent->IsA(UStaticMeshComponent::StaticClass()) && !InComponent->IsA(UFoliageInstancedStaticMeshComponent::StaticClass()) && !bFoliageOwned) ||
				(bAllowBSP && (InComponent->IsA(UBrushComponent::StaticClass()) || InComponent->IsA(UModelComponent::StaticClass()))) ||
				(bAllowFoliage && (InComponent->IsA(UFoliageInstancedStaticMeshComponent::StaticClass()) || bFoliageOwned));

			// Blacklist
			bAllowed &=
				(bAllowTranslucent || !(InComponent->GetMaterial(0) && IsTranslucentBlendMode(InComponent->GetMaterial(0)->GetBlendMode())));

			return bAllowed;
		}

		return false; };

	return AInstancedFoliageActor::FoliageTrace(EditingWorld, OutHit, FDesiredFoliageInstance(TraceStart, TraceEnd, TraceRadius), NAME_PlacementBrushTool, false, FilterFunc);
}

FTransform UPlacementBrushToolBase::GetFinalTransformFromHitLocationAndNormal(const FVector& InLocation, const FVector& InNormal)
{
	FTransform FinalizedTransform(InLocation);

	if (!PlacementSettings.IsValid())
	{
		return FinalizedTransform;
	}

	// For now, just apply a random yaw to any placed object, until we have per object settings for random pitch and yaw angles.
	if (PlacementSettings->bAllowRandomRotation)
	{
		FRotator UpdatedRotation = FinalizedTransform.Rotator();

		// UpdatedRotation = FRotator(FMath::FRand() * ItemToPlace->RandomPitchAngle, 0.f, 0.f);

		// if (ItemToPlace.bUseRandomYaw)
		UpdatedRotation.Yaw = FMath::FRand() * 360.f;

		FinalizedTransform.SetRotation(UpdatedRotation.Quaternion());
	}

	// Align to normal
	if (PlacementSettings->bAllowAlignToNormal)
	{
		FRotator AlignRotation = InNormal.Rotation();
		// Static meshes are authored along the vertical axis rather than the X axis, so we add 90 degrees to the static mesh's Pitch.
		AlignRotation.Pitch -= 90.f;
		// Clamp its value inside +/- one rotation
		AlignRotation.Pitch = FRotator::NormalizeAxis(AlignRotation.Pitch);

		// limit the maximum pitch angle if it's > 0.
		// For now, just set the align max angle to a constant value, until it can be pulled from per object settings
		constexpr float AlignMaxAngle = 0.0f;
		if (AlignMaxAngle > 0.f)
		{
			int32 MaxPitch = AlignMaxAngle;
			if (AlignRotation.Pitch > MaxPitch)
			{
				AlignRotation.Pitch = MaxPitch;
			}
			else if (AlignRotation.Pitch < -MaxPitch)
			{
				AlignRotation.Pitch = -MaxPitch;
			}
		}

		FinalizedTransform.SetRotation(FQuat(AlignRotation) * FinalizedTransform.GetRotation());
	}

	if (PlacementSettings->bAllowRandomScale)
	{
		// Until we have per object settings, just use a uniform scale, clamped from half to double size
		FFloatInterval ScaleRange(0.5f, 2.0f);
		FVector NewScale(ScaleRange.Interpolate(FMath::FRand()));
		FinalizedTransform.SetScale3D(NewScale);
	}

	return FinalizedTransform;
}
