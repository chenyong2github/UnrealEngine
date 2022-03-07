// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/PCGDensityRemap.h"
#include "PCGHelpers.h"
#include "Data/PCGPointData.h"
#include "Helpers/PCGAsync.h"
#include "Math/RandomStream.h"

FPCGElementPtr UPCGLinearDensityRemapSettings::CreateElement() const
{
	return MakeShared<FPCGLinearDensityRemapElement>();
}

bool FPCGLinearDensityRemapElement::ExecuteInternal(FPCGContext* Context) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGLinearDensityRemapElement::Execute);

	const UPCGLinearDensityRemapSettings* Settings = Context->GetInputSettings<UPCGLinearDensityRemapSettings>();
	check(Settings);

	TArray<FPCGTaggedData> Inputs = Context->InputData.GetInputs();
	TArray<FPCGTaggedData>& Outputs = Context->OutputData.TaggedData;

	// Forward any non-input data
	Outputs.Append(Context->InputData.GetExclusions());
	Outputs.Append(Context->InputData.GetAllSettings());

	const float RemapMin = FMath::Min(Settings->RemapMin, Settings->RemapMax);
	const float RemapMax = FMath::Max(Settings->RemapMin, Settings->RemapMax);

	const bool bTrivialRemapping = (RemapMin == 0.0f && RemapMax == 1.0f && Settings->bMultiplyDensity);

	// TODO: embarassingly parallel loop
	for (const FPCGTaggedData& Input : Inputs)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FPCGLinearDensityRemapElement::Execute::InputLoop);
		FPCGTaggedData& Output = Outputs.Add_GetRef(Input);

		if (!Input.Data || Cast<UPCGSpatialData>(Input.Data) == nullptr)
		{
			PCGE_LOG(Error, "Invalid input data");
			continue;
		}

		// Skip processing if the remapping is trivial
		if (bTrivialRemapping)
		{
			PCGE_LOG(Verbose, "Skipped - trivial remapping");
			continue;
		}

		const UPCGPointData* OriginalData = Cast<UPCGSpatialData>(Input.Data)->ToPointData(Context);

		if (!OriginalData)
		{
			PCGE_LOG(Error, "Unable to get points from input");
			continue;
		}

		const TArray<FPCGPoint>& Points = OriginalData->GetPoints();
		const int OriginalPointCount = Points.Num();

		UPCGPointData* SampledData = NewObject<UPCGPointData>();
		SampledData->TargetActor = OriginalData->TargetActor;
		TArray<FPCGPoint>& SampledPoints = SampledData->GetMutablePoints();
		Output.Data = SampledData;

		if (Settings->bMultiplyDensity)
		{
			FPCGAsync::AsyncPointProcessing(Context, OriginalPointCount, SampledPoints, [&Points, Settings, RemapMin, RemapMax](int32 Index, FPCGPoint& OutPoint)
			{
				OutPoint = Points[Index];
				FRandomStream RandomSource(PCGHelpers::ComputeSeed(Settings->Seed, OutPoint.Seed));
				OutPoint.Density *= RandomSource.FRandRange(RemapMin, RemapMax);
				return true;
			});
		}
		else
		{
			FPCGAsync::AsyncPointProcessing(Context, OriginalPointCount, SampledPoints, [&Points, Settings, RemapMin, RemapMax](int32 Index, FPCGPoint& OutPoint)
			{
				OutPoint = Points[Index];
				FRandomStream RandomSource(PCGHelpers::ComputeSeed(Settings->Seed, OutPoint.Seed));
				OutPoint.Density  = RandomSource.FRandRange(RemapMin, RemapMax);
				return true;
			});
		}
	}

	return true;
}