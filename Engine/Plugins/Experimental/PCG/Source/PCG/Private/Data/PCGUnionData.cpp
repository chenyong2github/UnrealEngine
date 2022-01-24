// Copyright Epic Games, Inc. All Rights Reserved.

#include "Data/PCGUnionData.h"
#include "Data/PCGPointData.h"

namespace PCGUnionDataMaths
{
	float ComputeDensity(float InDensityToUpdate, float InOtherDensity, EPCGUnionDensityFunction DensityFunction)
	{
		if (DensityFunction == EPCGUnionDensityFunction::ClampedAddition)
		{
			return FMath::Min(InDensityToUpdate + InOtherDensity, 1.0f);
		}
		else
		{
			return FMath::Max(InDensityToUpdate, InOtherDensity);
		}
	}

	float UpdateDensity(float& InDensityToUpdate, float InOtherDensity, EPCGUnionDensityFunction DensityFunction)
	{
		InDensityToUpdate = ComputeDensity(InDensityToUpdate, InOtherDensity, DensityFunction);
		return InDensityToUpdate;
	}
}

void UPCGUnionData::Initialize(const UPCGSpatialData* InA, const UPCGSpatialData* InB)
{
	check(InA && InB);
	AddData(InA);
	AddData(InB);
}

void UPCGUnionData::AddData(const UPCGSpatialData* InData)
{
	check(InData);

	Data.Add(InData);

	if (Data.Num() == 1)
	{
		TargetActor = InData->TargetActor;
		CachedBounds = InData->GetBounds();
		CachedStrictBounds = InData->GetStrictBounds();
		CachedDimension = InData->GetDimension();
	}
	else
	{
		CachedBounds += InData->GetBounds();
		CachedStrictBounds = CachedStrictBounds.Overlap(InData->GetStrictBounds());
		CachedDimension = FMath::Max(CachedDimension, InData->GetDimension());
	}

	if (!FirstNonTrivialTransformData && InData->HasNonTrivialTransform())
	{
		FirstNonTrivialTransformData = InData;
	}
}

int UPCGUnionData::GetDimension() const
{
	return CachedDimension;
}

FBox UPCGUnionData::GetBounds() const
{
	return CachedBounds;
}

FBox UPCGUnionData::GetStrictBounds() const
{
	return CachedStrictBounds;
}

float UPCGUnionData::GetDensityAtPosition(const FVector& InPosition) const
{
	// Early exits
	if (!CachedBounds.IsInside(InPosition))
	{
		return 0;
	}
	else if (CachedStrictBounds.IsInside(InPosition))
	{
		return 1.0f;
	}

	// Check for presence in any strict bounds of the data.
	// Note that it can be superfluous in some instances as we will might end up testing
	// the strict bounds twice per data, but it will perform better in the worst case.
	for (int32 DataIndex = 0; DataIndex < Data.Num(); ++DataIndex)
	{
		if (Data[DataIndex]->GetStrictBounds().IsInside(InPosition))
		{
			return 1.0f;
		}
	}

	float Density = 0.0f;

	for (TObjectPtr<const UPCGSpatialData> Datum : Data)
	{
		if (PCGUnionDataMaths::UpdateDensity(Density, Datum->GetDensityAtPosition(InPosition), DensityFunction) == 1.0f)
		{
			break;
		}
	}

	return Density;
}

FVector UPCGUnionData::TransformPosition(const FVector& InPosition) const
{
	if (FirstNonTrivialTransformData)
	{
		return FirstNonTrivialTransformData->TransformPosition(InPosition);
	}
	else
	{
		return Super::TransformPosition(InPosition);
	}
}

FPCGPoint UPCGUnionData::TransformPoint(const FPCGPoint& InPoint) const
{
	if (FirstNonTrivialTransformData)
	{
		FPCGPoint TransformedPoint = FirstNonTrivialTransformData->TransformPoint(InPoint);

		const int32 DataCount = Data.Num();
		for(int32 DataIndex = 0; DataIndex < DataCount && TransformedPoint.Density < 1.0f; ++DataIndex)
		{
			if (Data[DataIndex] != FirstNonTrivialTransformData)
			{
				PCGUnionDataMaths::UpdateDensity(TransformedPoint.Density, Data[DataIndex]->GetDensityAtPosition(TransformedPoint.Transform.GetLocation()), DensityFunction);
			}
		}

		return TransformedPoint;
	}
	else
	{
		return Super::TransformPoint(InPoint);
	}
}

bool UPCGUnionData::HasNonTrivialTransform() const
{
	return (FirstNonTrivialTransformData != nullptr || Super::HasNonTrivialTransform());
}

const UPCGPointData* UPCGUnionData::CreatePointData() const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UPCGUnionData::CreatePointData);

	// Trivial results
	if (Data.Num() == 0)
	{
		return nullptr;
	}
	else if (Data.Num() == 1)
	{
		return Data[0]->ToPointData();
	}

	UPCGPointData* PointData = NewObject<UPCGPointData>(const_cast<UPCGUnionData*>(this));
	PointData->TargetActor = TargetActor;

	switch (UnionType)
	{
	case EPCGUnionType::LeftToRightPriority:
	default:
		CreateSequentialPointData(PointData, /*bLeftToRight=*/true);
		break;

	case EPCGUnionType::RightToLeftPriority:
		CreateSequentialPointData(PointData, /*bLeftToRight=*/false);
		break;

	case EPCGUnionType::KeepAll:
		{
			TArray<FPCGPoint>& TargetPoints = PointData->GetMutablePoints();
			for (TObjectPtr<const UPCGSpatialData> Datum : Data)
			{
				TargetPoints.Append(Datum->ToPointData()->GetPoints());
			}
		}
		break;
	}

	return PointData;
}

void UPCGUnionData::CreateSequentialPointData(UPCGPointData* PointData, bool bLeftToRight) const
{
	check(PointData);

	TArray<FPCGPoint>& TargetPoints = PointData->GetMutablePoints();

	int32 FirstDataIndex = (bLeftToRight ? 0 : Data.Num() - 1);
	int32 LastDataIndex = (bLeftToRight ? Data.Num() : -1);
	int32 DataIndexIncrement = (bLeftToRight ? 1 : -1);

	// Note: this is a O(N^2) implementation. 
	// TODO: It is easy to implement a kind of divide & conquer algorithm here, but it will require some temporary storage.
	for (int32 DataIndex = FirstDataIndex; DataIndex != LastDataIndex; DataIndex += DataIndexIncrement)
	{
		// For each point, if it is not already "processed" by previous data,
		// add it & compute its final density
		const TArray<FPCGPoint>& Points = Data[DataIndex]->ToPointData()->GetPoints();
		for (const FPCGPoint& Point : Points)
		{
			// Discard point if it is already covered by a previous data
			bool bPointToExclude = false;
			for (int32 PreviousDataIndex = FirstDataIndex; PreviousDataIndex != DataIndex; PreviousDataIndex += DataIndexIncrement)
			{
				if (Data[PreviousDataIndex]->GetDensityAtPosition(Point.Transform.GetLocation()) != 0)
				{
					bPointToExclude = true;
					break;
				}
			}

			if (bPointToExclude)
			{
				continue;
			}

			FPCGPoint& TargetPoint = TargetPoints.Add_GetRef(Point);

			// Compute final density based on current & following data
			for (int32 FollowingDataIndex = DataIndex + DataIndexIncrement; FollowingDataIndex != LastDataIndex; FollowingDataIndex += DataIndexIncrement)
			{
				if (PCGUnionDataMaths::UpdateDensity(TargetPoint.Density, Data[FollowingDataIndex]->GetDensityAtPosition(TargetPoint.Transform.GetLocation()), DensityFunction) == 1.0f)
				{
					break;
				}
			}
		}
	}
}