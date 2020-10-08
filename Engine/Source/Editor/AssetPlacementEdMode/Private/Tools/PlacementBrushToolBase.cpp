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

bool UPlacementBrushToolBase::HitTest(const FRay& Ray, FHitResult& OutHit)
{
	UWorld* EditingWorld = GetToolManager()->GetWorld();
	const FVector TraceStart(Ray.Origin);
	const FVector TraceEnd(Ray.Origin + Ray.Direction * HALF_WORLD_MAX);
	constexpr TCHAR NAME_PlacementBrushTool[] = TEXT("PlacementBrushTool");

	auto FilterFunc = [](const UPrimitiveComponent* InComponent) {
		if (InComponent)
		{
			bool bFoliageOwned = InComponent->GetOwner() && FFoliageHelper::IsOwnedByFoliage(InComponent->GetOwner());
			constexpr bool bAllowLandscape = true;
			constexpr bool bAllowStaticMesh = true;
			constexpr bool bAllowBSP = true;
			constexpr bool bAllowFoliage = true;
			constexpr bool bAllowTranslucent = true;

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

	return AInstancedFoliageActor::FoliageTrace(EditingWorld, OutHit, FDesiredFoliageInstance(TraceStart, TraceEnd), NAME_PlacementBrushTool, false, FilterFunc);
}
