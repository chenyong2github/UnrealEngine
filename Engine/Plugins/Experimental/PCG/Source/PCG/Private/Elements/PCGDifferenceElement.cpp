// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/PCGDifferenceElement.h"
#include "Helpers/PCGSettingsHelpers.h"

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
	UPCGParamData* Params = Context->InputData.GetParams();

	const EPCGDifferenceDensityFunction DensityFunction = PCGSettingsHelpers::GetValue(GET_MEMBER_NAME_CHECKED(UPCGDifferenceSettings, DensityFunction), Settings->DensityFunction, Params);
#if WITH_EDITORONLY_DATA
	const bool bKeepZeroDensityPoints = PCGSettingsHelpers::GetValue(GET_MEMBER_NAME_CHECKED(UPCGDifferenceSettings, bKeepZeroDensityPoints), Settings->bKeepZeroDensityPoints, Params);
#else
	const bool bKeepZeroDensityPoints = false;
#endif

	TArray<FPCGTaggedData>& Outputs = Context->OutputData.TaggedData;

	const UPCGSpatialData* FirstSpatialData = nullptr;
	UPCGDifferenceData* DifferenceData = nullptr;
	int32 DifferenceTaggedDataIndex = -1;

	auto AddToDifference = [&FirstSpatialData, &DifferenceData, &DifferenceTaggedDataIndex, DensityFunction, bKeepZeroDensityPoints, &Outputs](const UPCGSpatialData* SpatialData) {
		if (!DifferenceData)
		{
			DifferenceData = FirstSpatialData->Subtract(SpatialData);
			DifferenceData->SetDensityFunction(DensityFunction);
#if WITH_EDITORONLY_DATA
			DifferenceData->bKeepZeroDensityPoints = bKeepZeroDensityPoints;
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

	// Finally, pass-through settings
	Outputs.Append(Context->InputData.GetAllSettings());

	return true;
}