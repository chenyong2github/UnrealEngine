// Copyright Epic Games, Inc. All Rights Reserved.

#include "CustomizableObjectPopulationClass.h"
#include "CustomizableObjectPopulationCharacteristic.h"
#include "CustomizableObjectPopulationCustomVersion.h"

#define LOCTEXT_NAMESPACE "CustomizableObjectPopulationClass"

using namespace CustomizableObjectPopulation;

void UCustomizableObjectPopulationClass::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	Ar.UsingCustomVersion(FCustomizableObjectPopulationCustomVersion::GUID);
}

#undef LOCTEXT_NAMESPACE