// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/PCGDensityRemapElement.h"

#include "PCGHelpers.h"
#include "Data/PCGPointData.h"
#include "Helpers/PCGAsync.h"
#include "Helpers/PCGSettingsHelpers.h"

#include "Math/RandomStream.h"

FPCGElementPtr UPCGDensityRemapSettings::CreateElement() const
{
	return MakeShared<FPCGDensityRemapElement>();
}

bool FPCGDensityRemapElement::ExecuteInternal(FPCGContext* Context) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGDensityRemapElement::Execute);

	const UPCGDensityRemapSettings* Settings = Context->GetInputSettings<UPCGDensityRemapSettings>();
	check(Settings);

	TArray<FPCGTaggedData> Inputs = Context->InputData.GetInputs();
	TArray<FPCGTaggedData>& Outputs = Context->OutputData.TaggedData;
	UPCGParamData* Params = Context->InputData.GetParams();

	const float InRangeMin = PCG_GET_OVERRIDEN_VALUE(Settings, InRangeMin, Params);
	const float InRangeMax = PCG_GET_OVERRIDEN_VALUE(Settings, InRangeMax, Params);
	const float OutRangeMin = PCG_GET_OVERRIDEN_VALUE(Settings, OutRangeMin, Params);
	const float OutRangeMax = PCG_GET_OVERRIDEN_VALUE(Settings, OutRangeMax, Params);
	const bool bExcludeValuesOutsideInputRange = PCG_GET_OVERRIDEN_VALUE(Settings, bExcludeValuesOutsideInputRange, Params);

	// used to determine if a density value lies between TrueMin and TrueMax
	const float InRangeTrueMin = FMath::Min(InRangeMin, InRangeMax);
	const float InRangeTrueMax = FMath::Max(InRangeMin, InRangeMax);

	const float InRangeDifference = InRangeMax - InRangeMin;
	const float OutRangeDifference = OutRangeMax - OutRangeMin;

	float Slope;
	float Intercept;

	// When InRange is a point leave the Slope at 0 so that Density = Intercept
	if (InRangeDifference == 0)
	{
		Slope = 0;
		Intercept = (OutRangeMin + OutRangeMax) / 2.f;
	}
	else
	{
		Slope = OutRangeDifference / InRangeDifference;
		Intercept = OutRangeMin;
	}

	// TODO: embarassingly parallel loop
	for (const FPCGTaggedData& Input : Inputs)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FPCGDensityRemapElement::Execute::InputLoop);
		FPCGTaggedData& Output = Outputs.Add_GetRef(Input);

		const UPCGSpatialData* SpatialData = Cast<UPCGSpatialData>(Input.Data);

		if (!SpatialData)
		{
			PCGE_LOG(Error, "Unable to get SpatialData from input");
			continue;
		}

		const UPCGPointData* PointData = SpatialData->ToPointData(Context);

		if (!PointData)
		{
			PCGE_LOG(Error, "Unable to get PointData from input");
			continue;
		}

		const TArray<FPCGPoint>& Points = PointData->GetPoints();

		UPCGPointData* SampledData = NewObject<UPCGPointData>();
		SampledData->InitializeFromData(PointData);
		TArray<FPCGPoint>& SampledPoints = SampledData->GetMutablePoints();
		Output.Data = SampledData;

		FPCGAsync::AsyncPointProcessing(Context, Points.Num(), SampledPoints, [&](int32 Index, FPCGPoint& OutPoint)
		{
			OutPoint = Points[Index];
			const float SourceDensity = Points[Index].Density;

			if (!bExcludeValuesOutsideInputRange || (SourceDensity >= InRangeTrueMin && SourceDensity <= InRangeTrueMax))
			{
				const float UnclampedDensity = Slope * (SourceDensity - InRangeMin) + Intercept;
				OutPoint.Density = FMath::Clamp(UnclampedDensity, 0.f, 1.f);
			}

			return true;
		});
	}

	// Forward any non-input data
	Outputs.Append(Context->InputData.GetAllSettings());

	return true;
}
