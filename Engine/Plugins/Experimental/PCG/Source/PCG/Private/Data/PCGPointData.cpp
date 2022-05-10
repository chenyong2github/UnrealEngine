// Copyright Epic Games, Inc. All Rights Reserved.

#include "Data/PCGPointData.h"

#include "PCGHelpers.h"
#include "Metadata/PCGMetadataAccessor.h"

#include "GameFramework/Actor.h"
#include "Misc/ScopeLock.h"

namespace PCGPointHelpers
{
	bool GetDistanceRatios(const FPCGPoint& InPoint, const FVector& InPosition, FVector& OutRatios)
	{
		FVector LocalPosition = InPoint.Transform.InverseTransformPosition(InPosition);
		LocalPosition -= (InPoint.BoundsMax + InPoint.BoundsMin) / 2;
		LocalPosition /= InPoint.GetExtents();

		// ]-2+s, 2-s] is the valid range of values
		const FVector::FReal LowerBound = InPoint.Steepness - 2;
		const FVector::FReal HigherBound = 2 - InPoint.Steepness;

		if (LocalPosition.X <= LowerBound || LocalPosition.X > HigherBound ||
			LocalPosition.Y <= LowerBound || LocalPosition.Y > HigherBound ||
			LocalPosition.Z <= LowerBound || LocalPosition.Z > HigherBound)
		{
			return false;
		}

		// [-s, +s] is the range where the density is 1 on that axis
		const FVector::FReal XDist = FMath::Max(0, FMath::Abs(LocalPosition.X) - InPoint.Steepness);
		const FVector::FReal YDist = FMath::Max(0, FMath::Abs(LocalPosition.Y) - InPoint.Steepness);
		const FVector::FReal ZDist = FMath::Max(0, FMath::Abs(LocalPosition.Z) - InPoint.Steepness);

		const FVector::FReal DistanceScale = FMath::Max(2 - 2 * InPoint.Steepness, KINDA_SMALL_NUMBER);

		OutRatios.X = XDist / DistanceScale;
		OutRatios.Y = YDist / DistanceScale;
		OutRatios.Z = ZDist / DistanceScale;
		return true;
	}

	float ManhattanDensity(const FPCGPoint& InPoint, const FVector& InPosition)
	{
		FVector Ratios;
		if (GetDistanceRatios(InPoint, InPosition, Ratios))
		{
			return InPoint.Density * (1 - Ratios.X) * (1 - Ratios.Y) * (1 - Ratios.Z);
		}
		else
		{
			return 0;
		}
	}

	float InverseEuclidianDistance(const FPCGPoint& InPoint, const FVector& InPosition)
	{
		FVector Ratios;
		if (GetDistanceRatios(InPoint, InPosition, Ratios))
		{
			return 1 - Ratios.Length();
		}
		else
		{
			return 0;
		}
	}

	float VolumeOverlap(const FPCGPoint& InPoint, const FBox& InTransformedBounds)
	{
		const FBox PointTransformedBounds = InPoint.GetLocalBounds().TransformBy(InPoint.Transform);
		const FBox Overlap = PointTransformedBounds.Overlap(InTransformedBounds);
		if (Overlap.IsValid)
		{
			return Overlap.GetVolume();
		}
		else
		{
			return 0;
		}
	}

	/** Helper function for additive blending of quaternions (copied from ControlRig) */
	FQuat AddQuatWithWeight(const FQuat& Q, const FQuat& V, float Weight)
	{
		FQuat BlendQuat = V * Weight;

		if ((Q | BlendQuat) >= 0.0f)
			return Q + BlendQuat;
		else
			return Q - BlendQuat;
	}	
}

FPCGPointRef::FPCGPointRef(const FPCGPoint& InPoint)
{
	Point = &InPoint;
	Bounds = InPoint.GetDensityBounds();
}

FPCGPointRef::FPCGPointRef(const FPCGPointRef& InPointRef)
{
	Point = InPointRef.Point;
	Bounds = InPointRef.Bounds;
}

TArray<FPCGPoint>& UPCGPointData::GetMutablePoints()
{
	bOctreeIsDirty = true;
	bBoundsAreDirty = true;
	return Points;
}

const UPCGPointData::PointOctree& UPCGPointData::GetOctree() const
{
	if (bOctreeIsDirty)
	{
		RebuildOctree();
	}

	return Octree;
}

FBox UPCGPointData::GetBounds() const
{
	if (bBoundsAreDirty)
	{
		RecomputeBounds();
	}

	return Bounds;
}

void UPCGPointData::RecomputeBounds() const
{
	FScopeLock Lock(&CachedDataLock);

	if (!bBoundsAreDirty)
	{
		return;
	}

	FBox NewBounds(EForceInit::ForceInit);
	for (const FPCGPoint& Point : Points)
	{
		FBoxSphereBounds PointBounds = Point.GetDensityBounds();
		NewBounds += FBox::BuildAABB(PointBounds.Origin, PointBounds.BoxExtent);
	}

	Bounds = NewBounds;
	bBoundsAreDirty = false;
}

void UPCGPointData::CopyPointsFrom(const UPCGPointData* InData, const TArray<int>& InDataIndices)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UPCGPointData::CopyPointsFrom);
	check(InData);
	Points.SetNum(InDataIndices.Num());

	// TODO: parallel-for this?
	for (int PointIndex = 0; PointIndex < InDataIndices.Num(); ++PointIndex)
	{
		Points[PointIndex] = InData->Points[InDataIndices[PointIndex]];
	}

	bBoundsAreDirty = true;
	bOctreeIsDirty = true;
}

void UPCGPointData::SetPoints(const TArray<FPCGPoint>& InPoints)
{
	GetMutablePoints() = InPoints;
}

void UPCGPointData::InitializeFromActor(AActor* InActor)
{
	check(InActor);

	Points.SetNum(1);
	Points[0].Transform = InActor->GetActorTransform();

	const FVector& Position = Points[0].Transform.GetLocation();
	Points[0].Seed = PCGHelpers::ComputeSeed((int)Position.X, (int)Position.Y, (int)Position.Z);

	TargetActor = InActor;
	Metadata = NewObject<UPCGMetadata>(this);
}

FPCGPoint UPCGPointData::GetPoint(int32 Index) const
{
	if (Points.IsValidIndex(Index))
	{
		return Points[Index];
	}
	else
	{
		UE_LOG(LogPCG, Error, TEXT("Invalid index in GetPoint call"));
		return FPCGPoint();
	}
}

bool UPCGPointData::SamplePoint(const FTransform& InTransform, const FBox& InBounds, FPCGPoint& OutPoint, UPCGMetadata* OutMetadata) const
{
	if (bOctreeIsDirty)
	{
		RebuildOctree();
	}

	TArray<TPair<const FPCGPoint*, float>> Contributions;
	const bool bSampleInVolume = (InBounds.GetExtent() != FVector::ZeroVector);

	if (!bSampleInVolume)
	{
		const FVector InPosition = InTransform.GetLocation();
		Octree.FindElementsWithBoundsTest(FBoxCenterAndExtent(InPosition, FVector::Zero()), [&InPosition, &Contributions](const FPCGPointRef& InPointRef) {
			Contributions.Emplace(InPointRef.Point, PCGPointHelpers::InverseEuclidianDistance(*InPointRef.Point, InPosition));
			});
	}
	else
	{
		FBox TransformedBounds = InBounds.TransformBy(InTransform);
		Octree.FindElementsWithBoundsTest(FBoxCenterAndExtent(TransformedBounds.GetCenter(), TransformedBounds.GetExtent()), [&TransformedBounds, &Contributions](const FPCGPointRef& InPointRef) {
			Contributions.Emplace(InPointRef.Point, PCGPointHelpers::VolumeOverlap(*InPointRef.Point, TransformedBounds));
			});
	}

	float SumContributions = 0;
	float MaxContribution = 0;
	const FPCGPoint* MaxContributor = nullptr;

	for (const TPair<const FPCGPoint*, float>& Contribution : Contributions)
	{
		SumContributions += Contribution.Value;

		if (Contribution.Value > MaxContribution)
		{
			MaxContribution = Contribution.Value;
			MaxContributor = Contribution.Key;
		}
	}

	if (SumContributions <= 0)
	{
		return false;
	}

	const FVector::FReal TransformedBoundsVolume = InBounds.TransformBy(InTransform).GetVolume();
	auto ComputeDensityContribution = [TransformedBoundsVolume](float VolumeIntersection)
	{
		return (TransformedBoundsVolume > 0) ? VolumeIntersection / TransformedBoundsVolume : 1.0f;
	};

	// Computed weighted average of spatial properties
	FVector WeightedPosition = FVector::ZeroVector;
	FQuat WeightedQuat = FQuat::Identity;
	FVector WeightedScale = FVector::ZeroVector;
	float WeightedDensity = 0;
	FVector WeightedBoundsMin = FVector::ZeroVector;
	FVector WeightedBoundsMax = FVector::ZeroVector;
	FVector WeightedColor = FVector::ZeroVector;
	float WeightedSteepness = 0;

	for (const TPair<const FPCGPoint*, float> Contribution : Contributions)
	{
		const FPCGPoint& SourcePoint = *Contribution.Key;
		const float Weight = Contribution.Value / SumContributions;

		WeightedPosition += SourcePoint.Transform.GetLocation();
		WeightedQuat = PCGPointHelpers::AddQuatWithWeight(WeightedQuat, SourcePoint.Transform.GetRotation(), Weight);
		WeightedScale += SourcePoint.Transform.GetScale3D() * Weight;

		if (!bSampleInVolume)
		{
			WeightedDensity += PCGPointHelpers::ManhattanDensity(SourcePoint, InTransform.GetLocation());
		}
		else
		{
			WeightedDensity += SourcePoint.Density * Weight * ComputeDensityContribution(Contribution.Value);
		}

		WeightedBoundsMin += SourcePoint.BoundsMin * Weight;
		WeightedBoundsMax += SourcePoint.BoundsMax * Weight;
		WeightedColor += SourcePoint.Color * Weight;
		WeightedSteepness += SourcePoint.Steepness * Weight;
	}

	// Finally, apply changes to point
	WeightedQuat.Normalize();

	OutPoint.Transform.SetRotation(WeightedQuat);
	OutPoint.Transform.SetScale3D(WeightedScale);
	OutPoint.Transform.SetLocation(bSampleInVolume ? WeightedPosition : InTransform.GetLocation());
	OutPoint.Density = WeightedDensity;
	OutPoint.BoundsMin = WeightedBoundsMin;
	OutPoint.BoundsMax = WeightedBoundsMax;
	OutPoint.Color = WeightedColor;
	OutPoint.Steepness = WeightedSteepness;

	if (OutMetadata)
	{
		UPCGMetadataAccessorHelpers::InitializeMetadata(OutPoint, OutMetadata, *MaxContributor, Metadata);
		OutMetadata->ResetPointWeightedAttributes(OutPoint);

		for (const TPair<const FPCGPoint*, float> Contribution : Contributions)
		{
			const FPCGPoint& SourcePoint = *Contribution.Key;
			const float Weight = Contribution.Value / SumContributions;
			const bool bIsMaxContributor = (Contribution.Key == MaxContributor);

			OutMetadata->AccumulatePointWeightedAttributes(SourcePoint, Metadata, Weight, bIsMaxContributor, OutPoint);
		}
	}

	return true;
}

void UPCGPointData::RebuildOctree() const
{
	FScopeLock Lock(&CachedDataLock);

	if (!bOctreeIsDirty)
	{
		return;
	}

	TRACE_CPUPROFILER_EVENT_SCOPE(UPCGPointData::RebuildOctree)
	check(bOctreeIsDirty);

	FBox PointBounds = GetBounds();
	TOctree2<FPCGPointRef, FPCGPointRefSemantics> NewOctree(PointBounds.GetCenter(), PointBounds.GetExtent().Length());

	for (const FPCGPoint& Point : Points)
	{
		NewOctree.AddElement(FPCGPointRef(Point));
	}

	Octree = NewOctree;
	bOctreeIsDirty = false;
}