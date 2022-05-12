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

TArray<FPCGPinProperties> UPCGGraphInputOutputSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	Algo::Transform(StaticLabels(), PinProperties, [](const FName& InLabel) { return FPCGPinProperties(InLabel, EPCGDataType::Spatial); });
	
	if (bShowAdvancedPins)
	{
		Algo::Transform(StaticAdvancedLabels(), PinProperties, [](const FName& InLabel) { return FPCGPinProperties(InLabel, EPCGDataType::Spatial); });
	}

	PinProperties.Append(CustomPins);
	return PinProperties;
}

TArray<FPCGPinProperties> UPCGGraphInputOutputSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	Algo::Transform(StaticLabels(), PinProperties, [](const FName& InLabel) { return FPCGPinProperties(InLabel, EPCGDataType::Spatial); });

	if (bShowAdvancedPins)
	{
		Algo::Transform(StaticAdvancedLabels(), PinProperties, [](const FName& InLabel) { return FPCGPinProperties(InLabel, EPCGDataType::Spatial); });
	}

	PinProperties.Append(CustomPins);
	return PinProperties;
}