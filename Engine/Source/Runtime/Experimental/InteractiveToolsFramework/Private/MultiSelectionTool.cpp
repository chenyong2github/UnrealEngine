// Copyright Epic Games, Inc. All Rights Reserved.
#include "MultiSelectionTool.h"
#include "TargetInterfaces/AssetBackedTarget.h"
#include "ToolTargets/PrimitiveComponentToolTarget.h"
#include "Components/PrimitiveComponent.h"

bool UMultiSelectionTool::GetMapToSharedSourceData(TArray<int32>& MapToFirstOccurrences)
{
	bool bSharesSources = false;
	MapToFirstOccurrences.SetNumUninitialized(Targets.Num());
	for (int32 ComponentIdx = 0; ComponentIdx < Targets.Num(); ComponentIdx++)
	{
		MapToFirstOccurrences[ComponentIdx] = -1;
	}
	for (int32 ComponentIdx = 0; ComponentIdx < Targets.Num(); ComponentIdx++)
	{
		if (MapToFirstOccurrences[ComponentIdx] >= 0) // already mapped
		{
			continue;
		}

		MapToFirstOccurrences[ComponentIdx] = ComponentIdx;

		IAssetBackedTarget* Target = Cast<IAssetBackedTarget>(Targets[ComponentIdx]);
		if (!Target)
		{
			continue;
		}
		for (int32 VsIdx = ComponentIdx + 1; VsIdx < Targets.Num(); VsIdx++)
		{
			IAssetBackedTarget* OtherTarget = Cast<IAssetBackedTarget>(Targets[VsIdx]);
			if (OtherTarget && OtherTarget->GetSourceData() == Target->GetSourceData())
			{
				bSharesSources = true;
				MapToFirstOccurrences[VsIdx] = ComponentIdx;
			}
		}
	}
	return bSharesSources;
}



bool UMultiSelectionTool::SupportsWorldSpaceFocusBox()
{
	int32 PrimitiveCount = 0;
	for (const TObjectPtr<UToolTarget>& Target : Targets)
	{
		if (Cast<UPrimitiveComponentToolTarget>(Target) != nullptr)
		{
			PrimitiveCount++;
		}
	}
	return PrimitiveCount > 0;
}


FBox UMultiSelectionTool::GetWorldSpaceFocusBox()
{
	FBox AccumBox(EForceInit::ForceInit);
	for (const TObjectPtr<UToolTarget>& Target : Targets)
	{
		UPrimitiveComponentToolTarget* PrimTarget = Cast<UPrimitiveComponentToolTarget>(Target);
		if (PrimTarget)
		{
			UPrimitiveComponent* Component = PrimTarget->GetOwnerComponent();
			if (Component)
			{
				FBox ComponentBounds = Component->Bounds.GetBox();
				AccumBox += ComponentBounds;
			}
		}
	}
	return AccumBox;
}


bool UMultiSelectionTool::SupportsWorldSpaceFocusPoint()
{
	int32 PrimitiveCount = 0;
	for (const TObjectPtr<UToolTarget>& Target : Targets)
	{
		if (Cast<UPrimitiveComponentToolTarget>(Target) != nullptr)
		{
			PrimitiveCount++;
		}
	}
	return PrimitiveCount > 0;
}

bool UMultiSelectionTool::GetWorldSpaceFocusPoint(const FRay& WorldRay, FVector& PointOut)
{
	double NearestRayParam = (double)HALF_WORLD_MAX;
	PointOut = FVector::ZeroVector;

	for (const TObjectPtr<UToolTarget>& Target : Targets)
	{
		UPrimitiveComponentToolTarget* PrimTarget = Cast<UPrimitiveComponentToolTarget>(Target);
		if (PrimTarget)
		{
			FHitResult HitResult;
			if (PrimTarget->HitTestComponent(WorldRay, HitResult))
			{
				double HitRayParam = (double)WorldRay.GetParameter(HitResult.ImpactPoint);
				if (HitRayParam < NearestRayParam)
				{
					NearestRayParam = HitRayParam;
					PointOut = HitResult.ImpactPoint;
				}
			}
		}
	}

	return (NearestRayParam < (double)HALF_WORLD_MAX);
}
