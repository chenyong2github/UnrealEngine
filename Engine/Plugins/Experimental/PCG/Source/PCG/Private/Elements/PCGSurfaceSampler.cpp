// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/PCGSurfaceSampler.h"
#include "Data/PCGPointData.h"
#include "Data/PCGSpatialData.h"
#include "PCGHelpers.h"
#include "Math/RandomStream.h"

FPCGElementPtr UPCGSurfaceSamplerSettings::CreateElement() const
{
	return MakeShared<FPCGSurfaceSamplerElement>();
}

bool FPCGSurfaceSamplerElement::ExecuteInternal(FPCGContextPtr Context) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGSurfaceSamplerElement::Execute);
	// TODO: time-sliced implementation
	const UPCGSurfaceSamplerSettings* Settings = Context->GetInputSettings<UPCGSurfaceSamplerSettings>();
	check(Settings);

	// Early out on invalid settings
	// TODO: we could compute an approximate radius based on the points per squared meters if that's useful
	if (Settings->PointRadius <= 0)
	{
		PCGE_LOG(Warning, "Skipped - Invalid point radius");
		return true;
	}

	TArray<FPCGTaggedData>& Outputs = Context->OutputData.TaggedData;

	// TODO: embarassingly parallel loop
	TArray<FPCGTaggedData> Inputs = Context->InputData.GetInputs();
	for (const FPCGTaggedData& Input : Inputs)
	{
		const UPCGSpatialData* SpatialInput = Cast<UPCGSpatialData>(Input.Data);

		if (!SpatialInput || !SpatialInput->TargetActor)
		{
			PCGE_LOG(Error, "Invalid input data");
			continue;
		}

		const FBox InputBounds = SpatialInput->GetBounds();

		if (!InputBounds.IsValid)
		{
			PCGE_LOG(Warning, "Input data has invalid bounds");
			continue;
		}

		// Conceptually, we will break down the surface bounds in a N x M grid
		const float PointSteepness = Settings->PointSteepness;
		const bool bApplyDensity = Settings->bApplyDensityToPoints;
		const FVector::FReal PointRadius = Settings->PointRadius;
		const FVector::FReal InterstitialDistance = 2 * PointRadius;
		const FVector::FReal InnerCellSize = 2 * Settings->Looseness * PointRadius;
		const FVector::FReal CellSize = InterstitialDistance + InnerCellSize;
		
		check(CellSize > 0);

		// By using scaled indices in the world, we can easily make this process deterministic
		const int32 CellMinX = FMath::CeilToInt((InputBounds.Min.X) / CellSize);
		const int32 CellMaxX = FMath::FloorToInt((InputBounds.Max.X) / CellSize);
		const int32 CellMinY = FMath::CeilToInt((InputBounds.Min.Y) / CellSize);
		const int32 CellMaxY = FMath::FloorToInt((InputBounds.Max.Y) / CellSize);

		if (CellMinX > CellMaxX || CellMinY > CellMaxY)
		{
			PCGE_LOG(Verbose, "Skipped - invalid cell bounds");
			continue;
		}

		const int32 CellCount = (1 + CellMaxX - CellMinX) * (1 + CellMaxY - CellMinY);
		check(CellCount > 0);

		const FVector::FReal InvSquaredMeterUnits = 1.0 / (100.0 * 100.0);
		int64 TargetPointCount = (InputBounds.Max.X - InputBounds.Min.X) * (InputBounds.Max.Y - InputBounds.Min.Y) * Settings->PointsPerSquaredMeter * InvSquaredMeterUnits;

		if (TargetPointCount == 0)
		{
			PCGE_LOG(Verbose, "Skipped - density yields no points");
			continue;
		}
		else if (TargetPointCount > CellCount)
		{
			TargetPointCount = CellCount;
		}

		const FVector::FReal Ratio = TargetPointCount / (FVector::FReal)CellCount;

		// Finally, create data
		FPCGTaggedData& Output = Outputs.Emplace_GetRef();
		Output = Input;

		UPCGPointData* SampledData = NewObject<UPCGPointData>();
		SampledData->TargetActor = SpatialInput->TargetActor;
		Output.Data = SampledData;

		TArray<FPCGPoint>& SampledPoints = SampledData->GetMutablePoints();

		const FVector::FReal CellStartX = CellMinX * CellSize;
		const FVector::FReal CellStartY = CellMinY * CellSize;

		FVector::FReal CurrentX = CellStartX;

		for(int32 CellX = CellMinX; CellX <= CellMaxX; ++CellX)
		{
			FVector::FReal CurrentY = CellStartY;

			for(int32 CellY = CellMinY; CellY <= CellMaxY; ++CellY)
			{
				FRandomStream RandomSource(PCGHelpers::ComputeSeed(Settings->Seed, CellX, CellY));
				float Chance = RandomSource.FRand();

				if (Chance < Ratio)
				{
					const float RandX = RandomSource.FRand();
					const float RandY = RandomSource.FRand();

					FPCGPoint Point;
					Point.Transform = FTransform(FVector(CurrentX + RandX * InnerCellSize, CurrentY + RandY * InnerCellSize, 0));
					Point.Extents = FVector(PointRadius);
					Point.Density = bApplyDensity ? ((Ratio - Chance) / Ratio) : 1.0f;
					Point.Steepness = PointSteepness;
					Point.Seed = RandomSource.GetCurrentSeed();

					Point = SpatialInput->TransformPoint(Point);

#if WITH_EDITORONLY_DATA
					if(Point.Density > 0 || Settings->bKeepZeroDensityPoints)
#else
					if (Point.Density > 0)
#endif
					{
						SampledPoints.Add(Point);
					}
				}

				CurrentY += CellSize;
			}

			CurrentX += CellSize;
		}

		PCGE_LOG(Verbose, "Generated %d points in %d cells", SampledPoints.Num(), CellCount);
	}

	// Finally, forward any exclusions/settings
	Outputs.Append(Context->InputData.GetExclusions());
	Outputs.Append(Context->InputData.GetAllSettings());

	return true;
}