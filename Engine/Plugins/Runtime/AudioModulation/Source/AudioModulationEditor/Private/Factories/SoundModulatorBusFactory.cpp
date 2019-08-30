// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SoundModulatorBusFactory.h"
#include "SoundModulatorBus.h"


USoundVolumeModulatorBusFactory::USoundVolumeModulatorBusFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SupportedClass = USoundVolumeModulatorBus::StaticClass();
	bCreateNew     = true;
	bEditorImport  = false;
	bEditAfterNew  = true;
}

UObject* USoundVolumeModulatorBusFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	return NewObject<USoundVolumeModulatorBus>(InParent, Name, Flags);
}

USoundPitchModulatorBusFactory::USoundPitchModulatorBusFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SupportedClass = USoundPitchModulatorBus::StaticClass();
	bCreateNew = true;
	bEditorImport = false;
	bEditAfterNew = true;
}

UObject* USoundPitchModulatorBusFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	return NewObject<USoundPitchModulatorBus>(InParent, Name, Flags);
}

USoundLPFModulatorBusFactory::USoundLPFModulatorBusFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SupportedClass = USoundLPFModulatorBus::StaticClass();
	bCreateNew = true;
	bEditorImport = false;
	bEditAfterNew = true;
}

UObject* USoundLPFModulatorBusFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	return NewObject<USoundLPFModulatorBus>(InParent, Name, Flags);
}

USoundHPFModulatorBusFactory::USoundHPFModulatorBusFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SupportedClass = USoundHPFModulatorBus::StaticClass();
	bCreateNew = true;
	bEditorImport = false;
	bEditAfterNew = true;
}

UObject* USoundHPFModulatorBusFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	return NewObject<USoundHPFModulatorBus>(InParent, Name, Flags);
}