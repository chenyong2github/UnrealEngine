// Copyright Epic Games, Inc. All Rights Reserved.

#include "Data/PCGVolumeData.h"

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

const UPCGPointData* UPCGVolumeData::CreatePointData(FPCGContextPtr Context) const
{
	UE_LOG(LogPCG, Error, TEXT("Volume data has no default point sampling"));
	return nullptr;
}

float UPCGVolumeData::GetDensityAtPosition(const FVector& InPosition) const
{
	// TODO: support fall-off between 0-1 density as needed
	// could use FBox::GetClosestPointTo
	if (GetBounds().IsInside(InPosition))
	{
		if(!Volume || GetStrictBounds().IsInside(InPosition))
		{
			return 1.0f;
		}
		else
		{
			return Volume->EncompassesPoint(InPosition) ? 1.0f : 0.0f;
		}
	}
	else
	{
		return 0.0f;
	}
}