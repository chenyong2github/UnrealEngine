// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/PCGPointSampler.h"
#include "Data/PCGSpatialData.h"
#include "PCGHelpers.h"
#include "Data/PCGPointData.h"
#include "Helpers/PCGAsync.h"
#include "Math/RandomStream.h"
#include "PCGContext.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PCGPointSampler)

UPCGPointSamplerSettings::UPCGPointSamplerSettings()
{
	bUseSeed = true;
}

FPCGElementPtr UPCGPointSamplerSettings::CreateElement() const
{
	return MakeShared<FPCGPointSamplerElement>();
}

bool FPCGPointSamplerElement::ExecuteInternal(FPCGContext* Context) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGPointSamplerElement::Execute);
	// TODO: make time-sliced implementation
	const UPCGPointSamplerSettings* Settings = Context->GetInputSettings<UPCGPointSamplerSettings>();
	check(Settings);

	TArray<FPCGTaggedData> Inputs = Context->InputData.GetInputs();

	TArray<FPCGTaggedData>& Outputs = Context->OutputData.TaggedData;

	// Forward any non-input data, excluding params
	Outputs.Append(Context->InputData.GetAllSettings());

	const float Ratio = Settings->Ratio;
#if WITH_EDITOR
	const bool bKeepZeroDensityPoints = Settings->bKeepZeroDensityPoints;
#else
	const bool bKeepZeroDensityPoints = false;
#endif

	const int Seed = Context->GetSeed();

	const bool bNoSampling = (Ratio <= 0.0f);
	const bool bTrivialSampling = (Ratio >= 1.0f);

	// Early exit when nothing will be generated out of this sampler
	if (bNoSampling && !bKeepZeroDensityPoints)
	{
		PCGE_LOG(Verbose, "Skipped - all inputs rejected");
		return true;
	}

	// TODO: embarassingly parallel loop
	for (const FPCGTaggedData& Input : Inputs)
	{
		FPCGTaggedData& Output = Outputs.Add_GetRef(Input);

		if (!Input.Data || Cast<UPCGSpatialData>(Input.Data) == nullptr)
		{
			PCGE_LOG(Error, "Invalid input data");
			continue;
		}

		// Skip processing if the transformation would be trivial
		if (bTrivialSampling)
		{
			PCGE_LOG(Verbose, "Skipped - trivial sampling");
			continue;
		}

		const UPCGPointData* OriginalData = Cast<UPCGSpatialData>(Input.Data)->ToPointData(Context);

		if (!OriginalData)
		{
			PCGE_LOG(Error, "Unable to get point data from input");
			continue;
		}

		const TArray<FPCGPoint>& Points = OriginalData->GetPoints();
		const int OriginalPointCount = Points.Num();

		UPCGPointData* SampledData = NewObject<UPCGPointData>();
		SampledData->InitializeFromData(OriginalData);
		TArray<FPCGPoint>& SampledPoints = SampledData->GetMutablePoints();

		Output.Data = SampledData;

		// TODO: randomize on the fractional number of points
#if WITH_EDITOR
		int TargetNumPoints = (bKeepZeroDensityPoints ? OriginalPointCount : OriginalPointCount * Ratio);
#else
		int TargetNumPoints = OriginalPointCount * Ratio;
#endif

		// Early out
		if (TargetNumPoints == 0)
		{
			PCGE_LOG(Verbose, "Skipped - all points rejected");
			continue;
		}
		else
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(FPCGPointSamplerElement::Execute::SelectPoints);

			FPCGAsync::AsyncPointProcessing(Context, OriginalPointCount, SampledPoints, [&Points, Seed, Ratio, bKeepZeroDensityPoints](int32 Index, FPCGPoint& OutPoint)
			{
				const FPCGPoint& Point = Points[Index];

				// Apply a high-pass filter based on selected ratio
				FRandomStream RandomSource(PCGHelpers::ComputeSeed(Seed, Point.Seed));
				float Chance = RandomSource.FRand();

				if (Chance < Ratio)
				{
					OutPoint = Point;
					return true;
				}
#if WITH_EDITOR
				else if (bKeepZeroDensityPoints)
				{
					OutPoint = Point;
					OutPoint.Density = 0;
					return true;
				}
#endif
				else
				{
					return false;
				}
			});

			PCGE_LOG(Verbose, "Generated %d points from %d source points", SampledPoints.Num(), OriginalPointCount);
		}
	}

	return true;
}

