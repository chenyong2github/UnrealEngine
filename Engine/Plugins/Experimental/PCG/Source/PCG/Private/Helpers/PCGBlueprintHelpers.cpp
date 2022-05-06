// Copyright Epic Games, Inc. All Rights Reserved.

#include "Helpers/PCGBlueprintHelpers.h"
#include "PCGComponent.h"
#include "PCGHelpers.h"
#include "PCGSettings.h"
#include "Grid/PCGPartitionActor.h"

int UPCGBlueprintHelpers::ComputeSeedFromPosition(const FVector& InPosition)
{
	// TODO: should have a config to drive this
	return PCGHelpers::ComputeSeed((int)InPosition.X, (int)InPosition.Y, (int)InPosition.Z);
}

void UPCGBlueprintHelpers::SetSeedFromPosition(FPCGPoint& InPoint)
{
	InPoint.Seed = ComputeSeedFromPosition(InPoint.Transform.GetLocation());
}

FRandomStream UPCGBlueprintHelpers::GetRandomStream(const FPCGPoint& InPoint, const UPCGSettings* OptionalSettings)
{
	if (OptionalSettings)
	{
		return FRandomStream(PCGHelpers::ComputeSeed(InPoint.Seed, OptionalSettings->Seed));
	}
	else
	{
		return FRandomStream(InPoint.Seed);
	}
}

UPCGData* UPCGBlueprintHelpers::GetActorData(FPCGContext& Context)
{
	return Context.SourceComponent ? Context.SourceComponent->GetActorPCGData() : nullptr;
}

UPCGData* UPCGBlueprintHelpers::GetInputData(FPCGContext& Context)
{
	return Context.SourceComponent ? Context.SourceComponent->GetInputPCGData() : nullptr;
}

TArray<UPCGData*> UPCGBlueprintHelpers::GetExclusionData(FPCGContext& Context)
{
	return Context.SourceComponent ? Context.SourceComponent->GetPCGExclusionData() : TArray<UPCGData*>();
}

UPCGComponent* UPCGBlueprintHelpers::GetComponent(FPCGContext& Context)
{
	return Context.SourceComponent;
}

UPCGComponent* UPCGBlueprintHelpers::GetOriginalComponent(FPCGContext& Context)
{
	if (Context.SourceComponent &&
		Cast<APCGPartitionActor>(Context.SourceComponent->GetOwner()) &&
		Cast<APCGPartitionActor>(Context.SourceComponent->GetOwner())->GetOriginalComponent(Context.SourceComponent))
	{
		return Cast<APCGPartitionActor>(Context.SourceComponent->GetOwner())->GetOriginalComponent(Context.SourceComponent);
	}
	else
	{
		return Context.SourceComponent;
	}
}

void UPCGBlueprintHelpers::SetExtents(FPCGPoint& InPoint, const FVector& InExtents)
{
	InPoint.SetExtents(InExtents);
}

FVector UPCGBlueprintHelpers::GetExtents(const FPCGPoint& InPoint)
{
	return InPoint.GetExtents();
}

void UPCGBlueprintHelpers::SetLocalCenter(FPCGPoint& InPoint, const FVector& InLocalCenter)
{
	InPoint.SetLocalCenter(InLocalCenter);
}

FVector UPCGBlueprintHelpers::GetLocalCenter(const FPCGPoint& InPoint)
{
	return InPoint.GetLocalCenter();
}

