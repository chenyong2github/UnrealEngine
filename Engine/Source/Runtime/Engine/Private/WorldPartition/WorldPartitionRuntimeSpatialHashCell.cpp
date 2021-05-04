// Copyright Epic Games, Inc. All Rights Reserved.
#include "WorldPartition/WorldPartitionRuntimeSpatialHashCell.h"
#include "WorldPartition/WorldPartitionStreamingSource.h"

static float GRuntimeSpatialHashCellToSourceAngleContributionToCellImportance = 0.4f; // Value between [0, 1]
static FAutoConsoleVariableRef CVarRuntimeSpatialHashCellToSourceAngleContributionToCellImportance(
	TEXT("wp.Runtime.RuntimeSpatialHashCellToSourceAngleContributionToCellImportance"),
	GRuntimeSpatialHashCellToSourceAngleContributionToCellImportance,
	TEXT("Value between 0 and 1 that modulates the contribution of the angle between streaming source-to-cell vector and source-forward vector to the cell importance. The closest to 0, the less the angle will contribute to the cell importance."));

UWorldPartitionRuntimeSpatialHashCell::UWorldPartitionRuntimeSpatialHashCell(const FObjectInitializer& ObjectInitializer)
: Super(ObjectInitializer)
, Level(0)
, CachedSourceMinDistance(0.f)
{}

#if WITH_EDITOR
void UWorldPartitionRuntimeSpatialHashCell::PostDuplicate(bool bDuplicateForPIE)
{
	Super::PostDuplicate(bDuplicateForPIE);

	if (ActorContainer)
	{
		for (auto& ActorPair : ActorContainer->Actors)
		{
			// Don't use AActor::Rename here since the actor is not par of the world, it's only a duplication template.
			ActorPair.Value->UObject::Rename(nullptr, ActorContainer);
		}
	}
}
#endif

bool UWorldPartitionRuntimeSpatialHashCell::CacheStreamingSourceInfo(const FWorldPartitionStreamingSource& Source) const
{
	const bool bWasCacheDirtied = Super::CacheStreamingSourceInfo(Source);
	
	const float AngleContribution = FMath::Clamp(GRuntimeSpatialHashCellToSourceAngleContributionToCellImportance, 0.f, 1.f);
	const float SqrDistance = FVector::DistSquared(Source.Location, Position);
	float AngleFactor = 1.f;
	if (!FMath::IsNearlyZero(AngleContribution))
	{
		const FVector2D SourceForward(Source.Rotation.Quaternion().GetForwardVector());
		const FVector2D SourceToCell(Position - Source.Location);
		const float Dot = FVector2D::DotProduct(SourceForward.GetSafeNormal(), SourceToCell.GetSafeNormal());
		const float NormalizedAngle = FMath::Clamp(FMath::Abs(FMath::Acos(Dot) / PI), 0.f, 1.f);
		AngleFactor = FMath::Pow(NormalizedAngle, AngleContribution);
	}
	// Modulate distance to cell by angle relative to source forward vector (to prioritize cells in front)
	float ModulatedDistance = SqrDistance * AngleFactor;

	// If cache was dirtied, use value, else use minimum with existing cached value
	CachedSourceMinDistance = bWasCacheDirtied ? ModulatedDistance : FMath::Min(ModulatedDistance, CachedSourceMinDistance);

	return bWasCacheDirtied;
}

int32 UWorldPartitionRuntimeSpatialHashCell::SortCompare(const UWorldPartitionRuntimeCell* InOther) const
{
	int32 Result = Super::SortCompare(InOther);
	if (Result == 0)
	{
		const UWorldPartitionRuntimeSpatialHashCell* Other = Cast<const UWorldPartitionRuntimeSpatialHashCell>(InOther);
		check(Other);
		
		// Level (higher value is higher prio)
		Result = Other->Level - Level;
		if (Result == 0)
		{
			// Closest distance (lower value is higher prio)
			const float Diff = CachedSourceMinDistance - Other->CachedSourceMinDistance;
			Result = Diff < 0.f ? -1 : (Diff > 0.f ? 1 : 0);
		}
	}
	return Result;
}