// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/PCGDifferenceElement.h"

FPCGElementPtr UPCGDifferenceSettings::CreateElement() const
{
	return MakeShared<FPCGDifferenceElement>();
}

bool FPCGDifferenceElement::ExecuteInternal(FPCGContext* Context) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGDifferenceElement::Execute);

	const UPCGDifferenceSettings* Settings = Context->GetInputSettings<UPCGDifferenceSettings>();
	check(Settings);

	TArray<FPCGTaggedData> Inputs = Context->InputData.GetInputs();
	TArray<FPCGTaggedData> Exclusions = Context->InputData.GetExclusions();
	TArray<FPCGTaggedData>& Outputs = Context->OutputData.TaggedData;

	const UPCGSpatialData* FirstSpatialData = nullptr;
	UPCGDifferenceData* DifferenceData = nullptr;
	int32 DifferenceTaggedDataIndex = -1;

	auto AddToDifference = [&FirstSpatialData, &DifferenceData, &DifferenceTaggedDataIndex, Settings, &Outputs](const UPCGSpatialData* SpatialData) {
		if (!DifferenceData)
		{
			DifferenceData = FirstSpatialData->Subtract(SpatialData);
			DifferenceData->SetDensityFunction(Settings->DensityFunction);
#if WITH_EDITORONLY_DATA
			DifferenceData->bKeepZeroDensityPoints = Settings->bKeepZeroDensityPoints;
#endif

			FPCGTaggedData& DifferenceTaggedData = Outputs[DifferenceTaggedDataIndex];
			DifferenceTaggedData.Data = DifferenceData;
		}
		else
		{
			DifferenceData->AddDifference(SpatialData);
		}
	};

	// TODO: it might not make sense to perform the difference if the first
	// data isn't a spatial data, otherwise, what would it really mean?
	for (FPCGTaggedData& Input : Inputs)
	{
		const UPCGSpatialData* SpatialData = Cast<UPCGSpatialData>(Input.Data);

		// Non-spatial data, we're not going to touch
		if (!SpatialData)
		{
			Outputs.Add(Input);
			continue;
		}

		if (!FirstSpatialData)
		{
			FirstSpatialData = SpatialData;
			DifferenceTaggedDataIndex = Outputs.Num();
			Outputs.Add(Input);

			continue;
		}

		AddToDifference(SpatialData);
	}

	// Can't have a difference if we don't have an input
	if (FirstSpatialData)
	{
		for (FPCGTaggedData& Exclusion : Exclusions)
		{
			if (const UPCGSpatialData* SpatialData = Cast<UPCGSpatialData>(Exclusion.Data))
			{
				AddToDifference(SpatialData);
			}
		}
	}

	// Finally, pass-through settings
	Outputs.Append(Context->InputData.GetAllSettings());

	return true;
}