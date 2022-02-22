// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/PCGDensityFilter.h"
#include "Data/PCGPointData.h"

FPCGElementPtr UPCGDensityFilterSettings::CreateElement() const
{
	return MakeShared<FPCGDensityFilterElement>();
}

bool FPCGDensityFilterElement::ExecuteInternal(FPCGContextPtr Context) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGDensityFilterElement::Execute);

	const UPCGDensityFilterSettings* Settings = Context->GetInputSettings<UPCGDensityFilterSettings>();
	check(Settings);

	TArray<FPCGTaggedData> Inputs = Context->InputData.GetInputs();
	TArray<FPCGTaggedData>& Outputs = Context->OutputData.TaggedData;

	// Forward any non-input data
	Outputs.Append(Context->InputData.GetExclusions());
	Outputs.Append(Context->InputData.GetAllSettings());

	const float MinBound = FMath::Min(Settings->LowerBound, Settings->UpperBound);
	const float MaxBound = FMath::Max(Settings->LowerBound, Settings->UpperBound);

	const bool bNoResults = (MaxBound <= 0.0f && !Settings->bInvertFilter) || (MinBound == 0.0f && MaxBound >= 1.0f && Settings->bInvertFilter);
	const bool bTrivialFilter = (MinBound <= 0.0f && MaxBound >= 1.0f && !Settings->bInvertFilter) || (MinBound == 0.0f && MaxBound == 0.0f && Settings->bInvertFilter);

#if WITH_EDITORONLY_DATA
	if(bNoResults && !Settings->bKeepZeroDensityPoints)
#else
	if (bNoResults)
#endif
	{
		PCGE_LOG(Verbose, "Skipped - all inputs rejected");
		return true;
	}

	// TODO: embarassingly parallel loop
	for (const FPCGTaggedData& Input : Inputs)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FPCGDensityFilterElement::Execute::InputLoop);

		FPCGTaggedData& Output = Outputs.Add_GetRef(Input);

		if (!Input.Data || Cast<UPCGSpatialData>(Input.Data) == nullptr)
		{
			PCGE_LOG(Error, "Invalid input data");
			continue;
		}

		// Skip processing if the transformation is trivial
		if (bTrivialFilter)
		{
			PCGE_LOG(Verbose, "Skipped - trivial filter");
			continue;
		}

		const UPCGPointData* OriginalData = Cast<UPCGSpatialData>(Input.Data)->ToPointData();

		if (!OriginalData)
		{
			PCGE_LOG(Error, "Unable to get point data from input");
			continue;
		}

		const TArray<FPCGPoint>& Points = OriginalData->GetPoints();
		
		UPCGPointData* FilteredData = NewObject<UPCGPointData>();
		FilteredData->TargetActor = OriginalData->TargetActor;
		TArray<FPCGPoint>& FilteredPoints = FilteredData->GetMutablePoints();

		Output.Data = FilteredData;

		for (const FPCGPoint& Point : Points)
		{
			bool bInRange = (Point.Density >= MinBound && Point.Density <= MaxBound);
			if (bInRange != Settings->bInvertFilter)
			{
				FilteredPoints.Add(Point);
			}
#if WITH_EDITORONLY_DATA
			else if (Settings->bKeepZeroDensityPoints)
			{
				FPCGPoint& FilteredPoint = FilteredPoints.Add_GetRef(Point);
				FilteredPoint.Density = 0;
			}
#endif
		}

		PCGE_LOG(Verbose, "Generated %d points out of %d source points", FilteredPoints.Num(), Points.Num());
	}

	return true;
}