// Copyright Epic Games, Inc. All Rights Reserved.

#include "Modulators/DMXModulator_ExtraAttributes.h"


UDMXModulator_ExtraAttributes::UDMXModulator_ExtraAttributes()
{
	FDMXAttributeName Dimmer;
	Dimmer.SetFromName("Dimmer");
	ExtraAttributeNameToNormalizedValueMap.Add(Dimmer, 1.f);
}

void UDMXModulator_ExtraAttributes::Modulate_Implementation(UDMXEntityFixturePatch* FixturePatch, const TMap<FDMXAttributeName, float>& InNormalizedAttributeValues, TMap<FDMXAttributeName, float>& OutNormalizedAttributeValues)
{
	// Note, in fact implementation wise CMYtoRGB and RGBtoCYM are clones, they exist only for their names
	OutNormalizedAttributeValues = InNormalizedAttributeValues;

	for (const TTuple<FDMXAttributeName, float>& ExtraAttributeNormalizedValuePair : ExtraAttributeNameToNormalizedValueMap)
	{
		OutNormalizedAttributeValues.FindOrAdd(ExtraAttributeNormalizedValuePair.Key) = ExtraAttributeNormalizedValuePair.Value;
	}
}

void UDMXModulator_ExtraAttributes::ModulateMatrix_Implementation(UDMXEntityFixturePatch* FixturePatch, const TArray<FDMXNormalizedAttributeValueMap>& InNormalizedMatrixAttributeValues, TArray<FDMXNormalizedAttributeValueMap>& OutNormalizedMatrixAttributeValues)
{
	OutNormalizedMatrixAttributeValues = InNormalizedMatrixAttributeValues;;

	for (FDMXNormalizedAttributeValueMap& NormalizedAttributeValueMap : OutNormalizedMatrixAttributeValues)
	{
		for (const TTuple<FDMXAttributeName, float>& ExtraAttributeNormalizedValuePair : ExtraAttributeNameToNormalizedValueMap)
		{
			NormalizedAttributeValueMap.Map.Add(ExtraAttributeNormalizedValuePair.Key, ExtraAttributeNormalizedValuePair.Value);
		}
	}
}
