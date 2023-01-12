// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/PCGDensityRemapElement.h"

#include "PCGContext.h"
#include "PCGCustomVersion.h"
#include "Helpers/PCGSettingsHelpers.h"

#include "PCGPoint.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PCGDensityRemapElement)

TArray<FPCGPinProperties> UPCGDensityRemapSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	// TODO in the future type checking of edges will be stricter and a conversion node will be added to convert from other types
	PinProperties.Emplace(PCGPinConstants::DefaultInputLabel, EPCGDataType::Point);
	PinProperties.Emplace(PCGPinConstants::DefaultParamsLabel, EPCGDataType::Param, /*bInAllowMultipleConnections=*/false);

	return PinProperties;
}

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

	ProcessPoints(Context, Inputs, Outputs, [bExcludeValuesOutsideInputRange, InRangeTrueMin, InRangeTrueMax, Slope, InRangeMin, Intercept](const FPCGPoint& InPoint, FPCGPoint& OutPoint)
	{
		OutPoint = InPoint;
		const float SourceDensity = InPoint.Density;

		if (!bExcludeValuesOutsideInputRange || (SourceDensity >= InRangeTrueMin && SourceDensity <= InRangeTrueMax))
		{
			const float UnclampedDensity = Slope * (SourceDensity - InRangeMin) + Intercept;
			OutPoint.Density = FMath::Clamp(UnclampedDensity, 0.f, 1.f);
		}

		return true;
	});

	// Forward any non-input data
	Outputs.Append(Context->InputData.GetAllSettings());

	return true;
}

#if WITH_EDITOR
void UPCGDensityRemapSettings::ApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins)
{
	Super::ApplyDeprecationBeforeUpdatePins(InOutNode, InputPins, OutputPins);

	check(InOutNode);

	if (DataVersion < FPCGCustomVersion::MoveParamsOffFirstPinDensityNodes)
	{
		PCGSettingsHelpers::DeprecationBreakOutParamsToNewPin(InOutNode, InputPins, OutputPins);
	}
}
#endif // WITH_EDITOR
