// Copyright Epic Games, Inc. All Rights Reserved.

#include "PCGInputOutputSettings.h"
#include "PCGCommon.h"

#include "Algo/Transform.h"

bool FPCGInputOutputElement::ExecuteInternal(FPCGContext* Context) const
{
	// Essentially a pass-through element
	Context->OutputData = Context->InputData;
	return true;
}

UPCGGraphInputOutputSettings::UPCGGraphInputOutputSettings(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	StaticInLabels.Add(PCGPinConstants::DefaultInputLabel);
	StaticAdvancedInLabels.Add(PCGInputOutputConstants::DefaultInputLabel);
	StaticAdvancedInLabels.Add(PCGInputOutputConstants::DefaultActorLabel);
	StaticAdvancedInLabels.Add(PCGInputOutputConstants::DefaultOriginalActorLabel);
	StaticAdvancedInLabels.Add(PCGInputOutputConstants::DefaultExcludedActorsLabel);
	
	StaticOutLabels.Add(PCGPinConstants::DefaultOutputLabel);
}

void UPCGGraphInputOutputSettings::PostLoad()
{
	Super::PostLoad();

	if (!PinLabels_DEPRECATED.IsEmpty())
	{
		for (const FName& PinLabel : PinLabels_DEPRECATED)
		{
			CustomPins.Emplace(PinLabel);
		}

		PinLabels_DEPRECATED.Reset();
	}
}

TArray<FPCGPinProperties> UPCGGraphInputOutputSettings::GetPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	const EPCGDataType DefaultPinDataType = bIsInput ? EPCGDataType::Spatial : EPCGDataType::Any;
	Algo::Transform(StaticLabels(), PinProperties, [DefaultPinDataType](const FName& InLabel) { return FPCGPinProperties(InLabel, DefaultPinDataType); });
	
	if (bShowAdvancedPins)
	{
		Algo::Transform(StaticAdvancedLabels(), PinProperties, [DefaultPinDataType](const FName& InLabel) { return FPCGPinProperties(InLabel, DefaultPinDataType); });
	}

	PinProperties.Append(CustomPins);
	return PinProperties;
}

TArray<FPCGPinProperties> UPCGGraphInputOutputSettings::InputPinProperties() const
{
	return GetPinProperties();
}

TArray<FPCGPinProperties> UPCGGraphInputOutputSettings::OutputPinProperties() const
{
	return GetPinProperties();
}