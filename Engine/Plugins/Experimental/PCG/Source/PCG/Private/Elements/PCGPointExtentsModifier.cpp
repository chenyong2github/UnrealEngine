// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/PCGPointExtentsModifier.h"

#include "Helpers/PCGSettingsHelpers.h"
#include "PCGContext.h"
#include "PCGPoint.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PCGPointExtentsModifier)

FPCGElementPtr UPCGPointExtentsModifierSettings::CreateElement() const
{
	return MakeShared<FPCGPointExtentsModifier>();
}

TArray<FPCGPinProperties> UPCGPointExtentsModifierSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PinProperties.Emplace(PCGPinConstants::DefaultInputLabel, EPCGDataType::Point);	
	PinProperties.Emplace(PCGPinConstants::DefaultParamsLabel, EPCGDataType::Param);
	return PinProperties;
}

bool FPCGPointExtentsModifier::ExecuteInternal(FPCGContext* Context) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGPointExtentsModifier::Execute);

	const UPCGPointExtentsModifierSettings* Settings = Context->GetInputSettings<UPCGPointExtentsModifierSettings>();
	check(Settings);

	TArray<FPCGTaggedData> Inputs = Context->InputData.GetInputsByPin(PCGPinConstants::DefaultInputLabel);
	TArray<FPCGTaggedData>& Outputs = Context->OutputData.TaggedData;
	UPCGParamData* Params = Context->InputData.GetParams();

	const EPCGPointExtentsModifierMode Mode = PCG_GET_OVERRIDEN_VALUE(Settings, Mode, Params);
	const FVector Extents = PCG_GET_OVERRIDEN_VALUE(Settings, Extents, Params);

	switch (Mode)
	{
	case EPCGPointExtentsModifierMode::Minimum:
		ProcessPoints(Context, Inputs, Outputs, [Extents](const FPCGPoint& InPoint, FPCGPoint& OutPoint){
			OutPoint = InPoint;
			OutPoint.SetExtents(FVector::Min(Extents, InPoint.GetExtents()));
			return true;
		});
		break;

	case EPCGPointExtentsModifierMode::Maximum:
		ProcessPoints(Context, Inputs, Outputs, [Extents](const FPCGPoint& InPoint, FPCGPoint& OutPoint){
			OutPoint = InPoint;
			OutPoint.SetExtents(FVector::Max(Extents, InPoint.GetExtents()));
			return true;
		});
		break;

	case EPCGPointExtentsModifierMode::Add:
		ProcessPoints(Context, Inputs, Outputs, [Extents](const FPCGPoint& InPoint, FPCGPoint& OutPoint){
			OutPoint = InPoint;
			OutPoint.SetExtents(Extents + InPoint.GetExtents());
			return true;
		});
		break;

	case EPCGPointExtentsModifierMode::Multiply:
		ProcessPoints(Context, Inputs, Outputs, [Extents](const FPCGPoint& InPoint, FPCGPoint& OutPoint){
			OutPoint = InPoint;
			OutPoint.SetExtents(Extents * InPoint.GetExtents());
			return true;
		});
		break;

	case EPCGPointExtentsModifierMode::Set:
		ProcessPoints(Context, Inputs, Outputs, [Extents](const FPCGPoint& InPoint, FPCGPoint& OutPoint){
			OutPoint = InPoint;
			OutPoint.SetExtents(Extents);
			return true;
		});
		break;
	}

	Outputs.Append(Context->InputData.GetAllSettings());
	
	return true;
}
