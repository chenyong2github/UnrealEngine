// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/PCGSelfPruning.h"
#include "Data/PCGPointData.h"
#include "Data/PCGSpatialData.h"
#include "PCGHelpers.h"
#include "Math/RandomStream.h"

namespace PCGSelfPruningAlgorithms
{
	bool RandomSort(const FPCGPoint* A, const FPCGPoint* B)
	{
		return A->Seed < B->Seed;
	}

	bool SortSmallToLargeNoRandom(const FPCGPoint* A, const FPCGPoint* B, FVector::FReal SquaredRadiusEquality)
	{
		return A->Extents.SquaredLength() * SquaredRadiusEquality < B->Extents.SquaredLength();
	}

	bool SortSmallToLargeWithRandom(const FPCGPoint* A, const FPCGPoint* B, FVector::FReal SquaredRadiusEquality)
	{
		const FVector::FReal SqrLenA = A->Extents.SquaredLength();
		const FVector::FReal SqrLenB = B->Extents.SquaredLength();
		if (SqrLenA * SquaredRadiusEquality < SqrLenB)
		{
			return true;
		}
		else if (SqrLenB * SquaredRadiusEquality < SqrLenA)
		{
			return false;
		}
		else
		{
			return RandomSort(A, B);
		}
	}
}

FPCGElementPtr UPCGSelfPruningSettings::CreateElement() const
{
	return MakeShared<FPCGSelfPruningElement>();
}

bool FPCGSelfPruningElement::ExecuteInternal(FPCGContextPtr Context) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGSelfPruningElement::Execute);
	// TODO: time-sliced implementation
	const UPCGSelfPruningSettings* Settings = Context->GetInputSettings<UPCGSelfPruningSettings>();
	check(Settings);

	// Early out: if pruning is disabled
	if (Settings->PruningType == EPCGSelfPruningType::None)
	{
		Context->OutputData = Context->InputData;
		PCGE_LOG(Verbose, "Skipped - Type is none");
		return true;
	}

	const FVector::FReal RadiusEquality = 1.0f + Settings->RadiusSimilarityFactor;
	const FVector::FReal SquaredRadiusEquality = FMath::Square(RadiusEquality);

	TArray<FPCGTaggedData>& Outputs = Context->OutputData.TaggedData;

	// TODO: embarassingly parallel loop
	TArray<FPCGTaggedData> Inputs = Context->InputData.GetInputs();
	for (const FPCGTaggedData& Input : Inputs)
	{
		const UPCGSpatialData* SpatialInput = Cast<UPCGSpatialData>(Input.Data);

		if (!SpatialInput)
		{
			PCGE_LOG(Error, "Invalid input data");
			continue;
		}

		const UPCGPointData* InputPointData = SpatialInput->ToPointData();
		const TArray<FPCGPoint>& Points = InputPointData->GetPoints();
		const UPCGPointData::PointOctree& Octree = InputPointData->GetOctree();

		// Self-pruning will be done as follows:
		// For each point:
		//  if in its vicinity, there is >=1 non-rejected point with a radius significantly larger
		//  or in its range + has a randomly assigned index -> we'll look at its seed
		//  then remove this point
		TArray<const FPCGPoint*> SortedPoints;
		SortedPoints.Reserve(Points.Num());
		for (const FPCGPoint& Point : Points)
		{
			SortedPoints.Add(&Point);
		}

		// Apply proper sort algorithm
		if (Settings->PruningType == EPCGSelfPruningType::LargeToSmall)
		{
			if (Settings->bRandomizedPruning)
			{
				Algo::Sort(SortedPoints, [SquaredRadiusEquality](const FPCGPoint* A, const FPCGPoint* B) { return !PCGSelfPruningAlgorithms::SortSmallToLargeWithRandom(A, B, SquaredRadiusEquality); });
			}
			else
			{
				Algo::Sort(SortedPoints, [SquaredRadiusEquality](const FPCGPoint* A, const FPCGPoint* B) { return !PCGSelfPruningAlgorithms::SortSmallToLargeNoRandom(A, B, SquaredRadiusEquality); });
			}
		}
		else if (Settings->PruningType == EPCGSelfPruningType::SmallToLarge)
		{
			if (Settings->bRandomizedPruning)
			{
				Algo::Sort(SortedPoints, [SquaredRadiusEquality](const FPCGPoint* A, const FPCGPoint* B) { return PCGSelfPruningAlgorithms::SortSmallToLargeWithRandom(A, B, SquaredRadiusEquality); });
			}
			else
			{
				Algo::Sort(SortedPoints, [SquaredRadiusEquality](const FPCGPoint* A, const FPCGPoint* B) { return PCGSelfPruningAlgorithms::SortSmallToLargeNoRandom(A, B, SquaredRadiusEquality); });
			}
		}
		else
		{
			if (Settings->bRandomizedPruning)
			{
				Algo::Sort(SortedPoints, PCGSelfPruningAlgorithms::RandomSort);
			}
		}

		TSet<const FPCGPoint*> ExclusionPoints;
		ExclusionPoints.Reserve(Points.Num());

		TSet<const FPCGPoint*> ExcludedPoints;
		ExcludedPoints.Reserve(Points.Num());

		for (int PointIndex = 0; PointIndex < SortedPoints.Num(); ++PointIndex)
		{
			// Find all points in the vicinity of the current point and reject them, if they aren't part of the exclusions yet
			// If any of these have been seen before (e.g. in the ExclusionPoints)
			// then reject this point
			const FPCGPoint* CurrentPoint = SortedPoints[PointIndex];

			if (ExcludedPoints.Contains(CurrentPoint))
			{
				continue;
			}

			ExclusionPoints.Add(CurrentPoint);

			Octree.FindElementsWithBoundsTest(FBoxCenterAndExtent(CurrentPoint->Transform.GetLocation(), CurrentPoint->Extents), [&ExclusionPoints, &ExcludedPoints](const FPCGPointRef& InPointRef) {
				if (!ExclusionPoints.Contains(InPointRef.Point))
				{
					ExcludedPoints.Add(InPointRef.Point);
				}
			});
		}

		// Finally, output all points that are present in the ExclusionPoints.
		FPCGTaggedData& Output = Outputs.Emplace_GetRef();
		Output = Input;

		UPCGPointData* PrunedData = NewObject<UPCGPointData>();
		PrunedData->TargetActor = SpatialInput->TargetActor;

		Output.Data = PrunedData;

		TArray<FPCGPoint>& OutputPoints = PrunedData->GetMutablePoints();
		OutputPoints.Reserve(ExclusionPoints.Num());

		for (const FPCGPoint* Point : ExclusionPoints)
		{
			OutputPoints.Add(*Point);
		}

		PCGE_LOG(Verbose, "Generated %d points from %d source points", OutputPoints.Num(), Points.Num());
	}

	// Finally, forward any exclusions/settings
	Outputs.Append(Context->InputData.GetExclusions());
	Outputs.Append(Context->InputData.GetAllSettings());

	return true;
}