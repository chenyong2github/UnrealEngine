// Copyright Epic Games, Inc. All Rights Reserved.

#include "Data/PCGVolumeData.h"
#include "PCGHelpers.h"

#include "Components/BrushComponent.h"
#include "GameFramework/Volume.h"

void UPCGVolumeData::Initialize(AVolume* InVolume, AActor* InTargetActor)
{
	check(InVolume);
	Volume = InVolume;
	TargetActor = InTargetActor ? InTargetActor : InVolume;
	
	FBoxSphereBounds BoxSphereBounds = Volume->GetBounds();
	Bounds = FBox::BuildAABB(BoxSphereBounds.Origin, BoxSphereBounds.BoxExtent);

	// TODO: Compute the strict bounds, we must find a FBox inscribed into the oriented box.
	// Currently, we'll leave the strict bounds empty and fall back to checking against the local box
}

void UPCGVolumeData::Initialize(const FBox& InBounds, AActor* InTargetActor)
{
	Bounds = InBounds;
	StrictBounds = InBounds;
	TargetActor = InTargetActor;
}

FBox UPCGVolumeData::GetBounds() const
{
	return Bounds;
}

FBox UPCGVolumeData::GetStrictBounds() const
{
	return StrictBounds;
}

const UPCGPointData* UPCGVolumeData::CreatePointData(FPCGContext* Context) const
{
	UE_LOG(LogPCG, Error, TEXT("Volume data has no default point sampling"));
	return nullptr;
}

bool UPCGVolumeData::SamplePoint(const FTransform& InTransform, const FBox& InBounds, FPCGPoint& OutPoint, UPCGMetadata* OutMetadata) const
{
	// TODO: add metadata
	// TODO: consider bounds
	const FVector InPosition = InTransform.GetLocation();
	if (PCGHelpers::IsInsideBounds(GetBounds(), InPosition))
	{
		float PointDensity = 0.0f;

		if (!Volume || PCGHelpers::IsInsideBounds(GetStrictBounds(), InPosition))
		{
			PointDensity = 1.0f;
		}
		else
		{
			PointDensity = Volume->EncompassesPoint(InPosition) ? 1.0f : 0.0f;
		}

		OutPoint.Transform = InTransform;
		OutPoint.SetLocalBounds(InBounds);
		OutPoint.Density = PointDensity;

		return OutPoint.Density > 0;
	}
	else
	{
		return false;
	}
}