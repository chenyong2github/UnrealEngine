// Copyright Epic Games, Inc. All Rights Reserved.

#include "Data/PCGUnionData.h"
#include "Data/PCGPointData.h"

void UPCGUnionData::Initialize(const UPCGSpatialData* InA, const UPCGSpatialData* InB)
{
	check(InA && InB);
	check(InA->GetDimension() == InB->GetDimension());
	AddData(InA);
	AddData(InB);
}

void UPCGUnionData::AddData(const UPCGSpatialData* InC)
{
	check(InC);
	check(!A || InC->GetDimension() == A->GetDimension());

	// Early tests: if we don't have a full union yet, just push in the data.
	if (!A)
	{
		check(!B);
		A = InC;
		TargetActor = InC->TargetActor;
		CachedBounds = InC->GetBounds();
		CachedStrictBounds = InC->GetStrictBounds();
	}
	else if (!B)
	{
		check(A);
		B = InC;
		CachedBounds += InC->GetBounds();
		CachedStrictBounds = A->GetStrictBounds().Overlap(InC->GetStrictBounds());
	}
	else
	{
		// TODO: Bad usage of this call could lead to sub-unions having inefficient bounds
		// We can revisit and keep the list of members and bundle them appropriately on demand 
		const bool bAIsUnion = Cast<UPCGUnionData>(A) != nullptr;
		const bool bBIsUnion = Cast<UPCGUnionData>(B) != nullptr;

		UPCGUnionData* Union = NewObject<UPCGUnionData>(this);
		Union->UnionType = UnionType;

		if (!bAIsUnion || bBIsUnion)
		{
			Union->Initialize(A, B);
			A = Union;
			B = InC;
		}
		else
		{
			Union->Initialize(B, InC);
			B = Union;
		}

		// Update bounds
		CachedBounds += InC->GetBounds();
		CachedStrictBounds = CachedStrictBounds.Overlap(InC->GetStrictBounds());
	}
}

int UPCGUnionData::GetDimension() const
{
	check(A && B && A->GetDimension() == B->GetDimension());
	return A->GetDimension();
}

FBox UPCGUnionData::GetBounds() const
{
	check(A && B);
	return CachedBounds;
}

FBox UPCGUnionData::GetStrictBounds() const
{
	check(A && B);
	return CachedStrictBounds;
}

float UPCGUnionData::GetDensityAtPosition(const FVector& InPosition) const
{
	check(A && B);
	if (!CachedBounds.IsInside(InPosition))
	{
		return 0;
	}
	else if (CachedStrictBounds.IsInside(InPosition) ||
		A->GetStrictBounds().IsInside(InPosition) ||
		B->GetStrictBounds().IsInside(InPosition))
	{
		return 1.0f;
	}
	else
	{
		return FMath::Max(A->GetDensityAtPosition(InPosition), B->GetDensityAtPosition(InPosition));
	}
}

const UPCGPointData* UPCGUnionData::CreatePointData() const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UPCGUnionData::CreatePointData);
	check(A && B);

	const UPCGPointData* APointData = A->ToPointData();
	const UPCGPointData* BPointData = B->ToPointData();

	if (!APointData || !BPointData)
	{
		return APointData ? APointData : BPointData;
	}

	UPCGPointData* Data = NewObject<UPCGPointData>(const_cast<UPCGUnionData*>(this));
	Data->TargetActor = TargetActor;

	TArray<FPCGPoint>& TargetPoints = Data->GetMutablePoints();

	auto AddNonExcludedPoints = [&TargetPoints](const UPCGPointData* PointsToAdd, const UPCGSpatialData* Exclusion) {
		check(PointsToAdd && Exclusion);
		for (const FPCGPoint& Point : PointsToAdd->GetPoints())
		{
			if (Exclusion->GetDensityAtPosition(Point.Transform.GetLocation()) == 0)
			{
				TargetPoints.Add(Point);
			}
		}
	};

	switch (UnionType)
	{
	case EPCGUnionType::LeftToRightPriority:
	case EPCGUnionType::RightToLeftPriority:
	default:
		{
			const UPCGSpatialData* PrioritizedData = (UnionType != EPCGUnionType::RightToLeftPriority ? A : B);
			const UPCGPointData* PrioritizedPointData = (UnionType != EPCGUnionType::RightToLeftPriority ? APointData : BPointData);
			const UPCGSpatialData* SecondaryData = (UnionType != EPCGUnionType::RightToLeftPriority ? B : A);
			const UPCGPointData* SecondaryPointData = (UnionType != EPCGUnionType::RightToLeftPriority ? BPointData : APointData);

			// First: add all points from the prioritized data and update its density
			TargetPoints.Append(PrioritizedPointData->GetPoints());

			for (FPCGPoint& Point : TargetPoints)
			{
				Point.Density = FMath::Max(Point.Density, SecondaryData->GetDensityAtPosition(Point.Transform.GetLocation()));
			}

			// Second: all points from the secondary data set only if they have a null density in the prioritized data set
			AddNonExcludedPoints(SecondaryPointData, PrioritizedData);
		}

		break;

	case EPCGUnionType::KeepAB:
		{
			TargetPoints.Append(APointData->GetPoints());
			TargetPoints.Append(BPointData->GetPoints());
		}

		break;

	case EPCGUnionType::ABMinusIntersection:
		{
			// First, build the intersection of A & B
			if (CachedStrictBounds.IsValid)
			{
				UPCGSpatialData* IntersectionData = A->IntersectWith(B);
				const UPCGPointData* IntersectionPointData = IntersectionData ? IntersectionData->ToPointData() : nullptr;

				if (IntersectionPointData)
				{
					TargetPoints.Append(IntersectionPointData->GetPoints());
				}
			}

			// Second: all all points from A & B that have no match in their counterpart.
			AddNonExcludedPoints(APointData, B);
			AddNonExcludedPoints(BPointData, A);
		}

		break;
	}

	return Data;
}
