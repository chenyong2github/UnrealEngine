// Copyright Epic Games, Inc. All Rights Reserved.

#include "PCGInputOutputSettings.h"
#include "PCGCommon.h"

#include "Algo/Transform.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PCGInputOutputSettings)

#define LOCTEXT_NAMESPACE "PCGInputOutputElement"

bool FPCGInputOutputElement::ExecuteInternal(FPCGContext* Context) const
{
	// Essentially a pass-through element
	Context->OutputData = Context->InputData;
	return true;
}

UPCGGraphInputOutputSettings::UPCGGraphInputOutputSettings(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	StaticInLabels.Emplace(PCGPinConstants::DefaultInputLabel, LOCTEXT("InputOutputInPinTooltip",
		"Provides the result of the Input pin, but with 'Excluded Tags' actors removed (configured on the PCG Component)."
	));
	StaticAdvancedInLabels.Emplace(PCGInputOutputConstants::DefaultInputLabel, LOCTEXT("InputOutputInputPinTooltip",
		"Takes the output of the Actor pin and if the 'Input Type' setting on the PCG Component is set to Landscape, combines it with the result of the Landscape pin. "
		"If the Actor data is two dimensional it will be projected onto the landscape, otherwise it will be intersected."
	));
	StaticAdvancedInLabels.Emplace(PCGInputOutputConstants::DefaultActorLabel, LOCTEXT("InputOutputActorPinTooltip",
		"If this is a partitioned component, then this will be the intersection of the current partition actor bounds with the following. "
		"If the actor is a Landscape Proxy, then this provide a landscape data. "
		"Otherwise if the actor is a volume, this will provide a volume shape matching the actor bounds. "
		"Otherwise if the 'Parse Actor Components' setting is enabled on the PCG Component, this will be all compatible components on the actor (Landscape Splines, Splines, Shapes, Primitives) unioned together. "
		"Otherwise a single point will be provided at the actor position."
	));
	StaticAdvancedInLabels.Emplace(PCGInputOutputConstants::DefaultOriginalActorLabel, LOCTEXT("InputOutputOriginalActorPinTooltip",
		"If the actor is a partition actor, this will pull data from the generating PCG actor. Otherwise it will provide the same data as the Actor pin."
	));
	StaticAdvancedInLabels.Emplace(PCGInputOutputConstants::DefaultLandscapeLabel,
		LOCTEXT("InputOutputLandscapePinTooltip", "Provides the landscape represented by this actor if it is a Landscape Proxy, otherwise it returns any landscapes overlapping this actor in the level."
	));
	StaticAdvancedInLabels.Emplace(PCGInputOutputConstants::DefaultLandscapeHeightLabel, LOCTEXT("InputOutputLandscapeHeightPinTooltip",
		"Similar to Landscape pin, but only provides height data and not other layers."
	));
	StaticAdvancedInLabels.Emplace(PCGInputOutputConstants::DefaultExcludedActorsLabel, LOCTEXT("InputOutputExcludedPinTooltip",
		"Returns union of data read from actors have any tag specified in the 'Excluded Tags' setting on the PCG Component."
	));
	
	StaticOutLabels.Emplace(PCGPinConstants::DefaultOutputLabel);
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
	const bool bIsInputPin = bIsInput;
	const EPCGDataType DefaultPinDataType = bIsInput ? EPCGDataType::Composite : EPCGDataType::Any;
	Algo::Transform(StaticLabels(), PinProperties, [bIsInputPin, DefaultPinDataType](const FLabelAndTooltip& InLabelAndTooltip) {
		return FPCGPinProperties(InLabelAndTooltip.Label, DefaultPinDataType, /*bMultiConnections=*/true, /*bMultiData=*/true, InLabelAndTooltip.Tooltip);
	});
	
	if (bShowAdvancedPins)
	{
		Algo::Transform(StaticAdvancedLabels(), PinProperties, [DefaultPinDataType](const FLabelAndTooltip& InLabelAndTooltip) {
			const bool bIsLandscapePin = (InLabelAndTooltip.Label == PCGInputOutputConstants::DefaultLandscapeLabel || InLabelAndTooltip.Label == PCGInputOutputConstants::DefaultLandscapeHeightLabel);
			const EPCGDataType PinType = bIsLandscapePin ? EPCGDataType::Surface : DefaultPinDataType;
			return FPCGPinProperties(InLabelAndTooltip.Label, PinType, /*bMultiConnection=*/true, /*bMultiData=*/false, InLabelAndTooltip.Tooltip);
		});
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

bool UPCGGraphInputOutputSettings::IsPinAdvanced(const UPCGPin* Pin) const
{
	return Pin && StaticAdvancedLabels().FindByPredicate([Pin](const FLabelAndTooltip& InLabelAndTooltip) -> bool { return InLabelAndTooltip.Label == Pin->Properties.Label; });
}

void UPCGGraphInputOutputSettings::SetShowAdvancedPins(bool bValue)
{
	if (bValue != bShowAdvancedPins)
	{
		Modify();
		bShowAdvancedPins = bValue;
	}
}

void UPCGGraphInputOutputSettings::AddCustomPin(const FPCGPinProperties& NewCustomPinProperties)
{
	Modify();
	CustomPins.Add(NewCustomPinProperties);
}

#undef LOCTEXT_NAMESPACE
