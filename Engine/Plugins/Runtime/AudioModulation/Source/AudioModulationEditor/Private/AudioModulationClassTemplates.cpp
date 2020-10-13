// Copyright Epic Games, Inc. All Rights Reserved.
#include "AudioModulationClassTemplates.h"

USoundModulationClassTemplate::USoundModulationClassTemplate(const FObjectInitializer& ObjectInitializer)
	: UPluginClassTemplate(ObjectInitializer)
{
	PluginName = TEXT("AudioModulation");
}

USoundModulationGeneratorClassTemplate::USoundModulationGeneratorClassTemplate(const FObjectInitializer& ObjectInitializer)
	: USoundModulationClassTemplate(ObjectInitializer)
{
	SetGeneratedBaseClass(USoundModulationGenerator::StaticClass());
}
