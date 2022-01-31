// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/PCGPointSampler.h"
#include "PCGHelpers.h"
#include "Data/PCGPointData.h"
#include "Math/RandomStream.h"

FPCGElementPtr UPCGPointSamplerSettings::CreateElement() const
{
	return MakeShared<FPCGPointSamplerElement>();
}

bool FPCGPointSamplerElement::ExecuteInternal(FPCGContextPtr Context) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGPointSamplerElement::Execute);
	// TODO: make time-sliced implementation
	const UPCGPointSamplerSettings* Settings = Context->GetInputSettings<UPCGPointSamplerSettings>();
	check(Settings);

	TArray<FPCGTaggedData> Inputs = Context->InputData.GetInputs();
	TArray<FPCGTaggedData> Exclusions = Context->InputData.GetExclusions();
	TArray<FPCGTaggedData>& Outputs = Context->OutputData.TaggedData;

	// TODO: embarassingly parallel loop
	for (const FPCGTaggedData& Input : Inputs)
	{
		FPCGTaggedData& Output = Outputs.Emplace_GetRef();
		Output = Input;

		if (!Input.Data || Cast<UPCGSpatialData>(Input.Data) == nullptr)
		{
			continue;
		}

		const UPCGPointData* OriginalData = Cast<UPCGSpatialData>(Input.Data)->ToPointData();

		if (!OriginalData)
		{
			continue;
		}

		const TArray<FPCGPoint>& Points = OriginalData->GetPoints();
		const int OriginalPointCount = Points.Num();

		if(Settings->Ratio >= 1.0f && Exclusions.IsEmpty())
		{
			Output.Data = OriginalData;
		}
		else
		{
			UPCGPointData* SampledData = NewObject<UPCGPointData>();
			SampledData->TargetActor = OriginalData->TargetActor;

			Output.Data = SampledData;

			// TODO: randomize on the fractional number of points
			int TargetNumPoints = OriginalPointCount * Settings->Ratio;

			// Early out
			if (TargetNumPoints == 0)
			{
				return true;
			}

			TArray<int> SelectedIndices;
			// Build indices in a deterministic manner
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(UPCGComponent::CreatePCGData::SelectIndices)

				// Approximate upper bound
				SelectedIndices.Reserve(4 * TargetNumPoints / 3);

				for (int Index = 0; Index < OriginalPointCount; ++Index)
				{
					const FPCGPoint& Point = Points[Index];

					// Apply a high-pass filter based on selected ratio
					FRandomStream RandomSource(PCGHelpers::ComputeSeed(Settings->Seed, Point.Seed));
					float Chance = RandomSource.FRand();

					if (Chance < Settings->Ratio)
					{
						// Remap density to a value between 0.5 and 1.0
						float Density = Point.Density * (0.5f + 0.5f * (Settings->Ratio - Chance));

						for (const FPCGTaggedData& Exclusion : Exclusions)
						{
							if (!Exclusion.Data)
							{
								continue;
							}

							Density *= (1.0f - Cast<UPCGSpatialData>(Exclusion.Data)->GetDensityAtPosition(Point.Transform.GetLocation()));

							if (Density == 0)
							{
								break;
							}
						}

						// Implicit threshold at 0.5f based on cumulated density
						if(Density >= 0.5f)
						{
							SelectedIndices.Add(Index);
						}
					}
				}
			}

			// Copy points using the indices
			// TODO: pass thread info for parallel for
			SampledData->CopyPointsFrom(OriginalData, SelectedIndices);
		}
	}

	return true;
}

