// Copyright Epic Games, Inc. All Rights Reserved.
#include "SoundModulationSettingsFactory.h"
#include "SoundModulationPatch.h"


USoundModulationSettingsFactory::USoundModulationSettingsFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SupportedClass = USoundModulationSettings::StaticClass();
	bCreateNew = true;
	bEditorImport = false;
	bEditAfterNew = true;
}

UObject* USoundModulationSettingsFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	return NewObject<USoundModulationSettings>(InParent, Name, Flags);
}
