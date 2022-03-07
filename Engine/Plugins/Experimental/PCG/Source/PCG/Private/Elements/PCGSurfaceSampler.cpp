// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/PCGSurfaceSampler.h"
#include "Data/PCGPointData.h"
#include "Data/PCGSpatialData.h"
#include "Helpers/PCGAsync.h"
#include "PCGHelpers.h"
#include "Math/RandomStream.h"

struct FPCGSurfaceSamplerLoopData
{
	const UPCGSurfaceSamplerSettings* Settings;

	FVector PointExtents;
	FVector InterstitialDistance;
	FVector InnerCellSize;
	FVector CellSize;

	int32 CellMinX;
	int32 CellMaxX;
	int32 CellMinY;
	int32 CellMaxY;

	int32 CellCount;

	int64 TargetPointCount;

	float Ratio;

	bool Initialize(const UPCGSurfaceSamplerSettings* InSettings, FPCGContext* Context, const FBox& InputBounds)
	{
		Settings = InSettings;

		// Conceptually, we will break down the surface bounds in a N x M grid
		InterstitialDistance = Settings->PointExtents * 2;
		InnerCellSize = InterstitialDistance * Settings->Looseness;
		CellSize = InterstitialDistance + InnerCellSize;
		check(CellSize.X > 0 && CellSize.Y > 0);

		// By using scaled indices in the world, we can easily make this process deterministic
		CellMinX = FMath::CeilToInt((InputBounds.Min.X) / CellSize.X);
		CellMaxX = FMath::FloorToInt((InputBounds.Max.X) / CellSize.X);
		CellMinY = FMath::CeilToInt((InputBounds.Min.Y) / CellSize.Y);
		CellMaxY = FMath::FloorToInt((InputBounds.Max.Y) / CellSize.Y);

		if (CellMinX > CellMaxX || CellMinY > CellMaxY)
		{
			PCGE_LOG_C(Verbose, Context, "Skipped - invalid cell bounds");
			return false;
		}

		CellCount = (1 + CellMaxX - CellMinX) * (1 + CellMaxY - CellMinY);
		check(CellCount > 0);

		const FVector::FReal InvSquaredMeterUnits = 1.0 / (100.0 * 100.0);
		TargetPointCount = (InputBounds.Max.X - InputBounds.Min.X) * (InputBounds.Max.Y - InputBounds.Min.Y) * Settings->PointsPerSquaredMeter * InvSquaredMeterUnits;

		if (TargetPointCount == 0)
		{
			PCGE_LOG_C(Verbose, Context, "Skipped - density yields no points");
			return false;
		}
		else if (TargetPointCount > CellCount)
		{
			TargetPointCount = CellCount;
		}

		Ratio = TargetPointCount / (FVector::FReal)CellCount;

		return true;
	}

	void ComputeCellIndices(int32 Index, int32& CellX, int32& CellY)
	{
		check(Index >= 0 && Index < CellCount);
		const int32 CellCountX = CellMaxX - CellMinX;

		CellX = CellMinX + (Index % CellCountX);
		CellY = CellMinY + (Index / CellCountX);
	}
};

void UPCGSurfaceSamplerSettings::PostLoad()
{
	Super::PostLoad();

	if (PointRadius_DEPRECATED != 0)
	{
		PointExtents = FVector(PointRadius_DEPRECATED);
		PointRadius_DEPRECATED = 0;
	}
}

FPCGElementPtr UPCGSurfaceSamplerSettings::CreateElement() const
{
	return MakeShared<FPCGSurfaceSamplerElement>();
}

bool FPCGSurfaceSamplerElement::ExecuteInternal(FPCGContext* Context) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGSurfaceSamplerElement::Execute);
	// TODO: time-sliced implementation
	const UPCGSurfaceSamplerSettings* Settings = Context->GetInputSettings<UPCGSurfaceSamplerSettings>();
	check(Settings);

	// Early out on invalid settings
	// TODO: we could compute an approximate radius based on the points per squared meters if that's useful
	if(Settings->PointExtents.X <= 0 || Settings->PointExtents.Y <= 0)
	{
		PCGE_LOG(Warning, "Skipped - Invalid point extents");
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

		FPCGSurfaceSamplerLoopData LoopData;
		if (!LoopData.Initialize(Settings, Context, InputBounds))
		{
			continue;
		}

		// Finally, create data
		FPCGTaggedData& Output = Outputs.Emplace_GetRef();
		Output = Input;

		UPCGPointData* SampledData = NewObject<UPCGPointData>();
		SampledData->TargetActor = SpatialInput->TargetActor;
		Output.Data = SampledData;

		TArray<FPCGPoint>& SampledPoints = SampledData->GetMutablePoints();

		FPCGAsync::AsyncPointProcessing(Context, LoopData.CellCount, SampledPoints, [&LoopData, SpatialInput](int32 Index, FPCGPoint& OutPoint)
		{
			int32 CellX;
			int32 CellY;
			LoopData.ComputeCellIndices(Index, CellX, CellY);

			const FVector::FReal CurrentX = CellX * LoopData.CellSize.X;
			const FVector::FReal CurrentY = CellY * LoopData.CellSize.Y;
			const FVector InnerCellSize = LoopData.InnerCellSize;

			FRandomStream RandomSource(PCGHelpers::ComputeSeed(LoopData.Settings->Seed, CellX, CellY));
			float Chance = RandomSource.FRand();

			const float Ratio = LoopData.Ratio;

			if (Chance < Ratio)
			{
				const float RandX = RandomSource.FRand();
				const float RandY = RandomSource.FRand();

				OutPoint.Transform = FTransform(FVector(CurrentX + RandX * InnerCellSize.X, CurrentY + RandY * InnerCellSize.Y, 0));
				OutPoint.Extents = FVector(LoopData.Settings->PointExtents);
				OutPoint.Density = LoopData.Settings->bApplyDensityToPoints ? ((Ratio - Chance) / Ratio) : 1.0f;
				OutPoint.Steepness = LoopData.Settings->PointSteepness;
				OutPoint.Seed = RandomSource.GetCurrentSeed();

				OutPoint = SpatialInput->TransformPoint(OutPoint);

#if WITH_EDITORONLY_DATA
				return OutPoint.Density > 0 || LoopData.Settings->bKeepZeroDensityPoints;
#else
				return OutPoint.Density > 0;
#endif
			}
			else
			{
				return false;
			}
		});

		PCGE_LOG(Verbose, "Generated %d points in %d cells", SampledPoints.Num(), LoopData.CellCount);
	}

	// Finally, forward any exclusions/settings
	Outputs.Append(Context->InputData.GetExclusions());
	Outputs.Append(Context->InputData.GetAllSettings());

	return true;
}