// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/PCGIntersectionElement.h"

FPCGElementPtr UPCGIntersectionSettings::CreateElement() const
{
	return MakeShared<FPCGIntersectionElement>();
}

bool FPCGIntersectionElement::ExecuteInternal(FPCGContextPtr Context) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGIntersectionElement::Execute);

	const UPCGIntersectionSettings* Settings = Context->GetInputSettings<UPCGIntersectionSettings>();
	check(Settings);

	TArray<FPCGTaggedData> Inputs = Context->InputData.GetInputs();
	TArray<FPCGTaggedData>& Outputs = Context->OutputData.TaggedData;

	const UPCGSpatialData* FirstSpatialData = nullptr;
	UPCGIntersectionData* IntersectionData = nullptr;
	int32 IntersectionTaggedDataIndex = -1;

	for (FPCGTaggedData& Input : Inputs)
	{
		const UPCGSpatialData* SpatialData = Cast<UPCGSpatialData>(Input.Data);

		// Non-spatial data we're not going to touch
		if (!SpatialData)
		{
			Outputs.Add(Input);
			continue;
		}

		if (!FirstSpatialData)
		{
			FirstSpatialData = SpatialData;
			IntersectionTaggedDataIndex = Outputs.Num();
			Outputs.Add(Input);
			continue;
		}

		// Create a new intersection
		IntersectionData = (IntersectionData ? IntersectionData : FirstSpatialData)->IntersectWith(SpatialData);
		// Propagate settings
		IntersectionData->DensityFunction = Settings->DensityFunction;
#if WITH_EDITORONLY_DATA
		IntersectionData->bKeepZeroDensityPoints = Settings->bKeepZeroDensityPoints;
#endif


		// Update tagged data
		FPCGTaggedData& IntersectionTaggedData = Outputs[IntersectionTaggedDataIndex];
		IntersectionTaggedData.Data = IntersectionData;
		IntersectionTaggedData.Tags.Append(Input.Tags);
	}

	// Pass-through settings & exclusions
	Outputs.Append(Context->InputData.GetExclusions());
	Outputs.Append(Context->InputData.GetAllSettings());

	return true;
}