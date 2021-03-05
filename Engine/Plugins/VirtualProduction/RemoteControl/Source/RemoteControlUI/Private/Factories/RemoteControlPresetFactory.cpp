// Copyright Epic Games, Inc. All Rights Reserved.

#include "Factories/RemoteControlPresetFactory.h"
#include "RemoteControlPreset.h"

#define LOCTEXT_NAMESPACE "RemoteControlPresetFactory"

URemoteControlPresetFactory::URemoteControlPresetFactory()
{
	bCreateNew = true;
	bEditAfterNew = true;
	SupportedClass = URemoteControlPreset::StaticClass();
}

UObject* URemoteControlPresetFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	URemoteControlPreset* RemoteControlPreset = NewObject<URemoteControlPreset>(InParent, Name, Flags);
	RemoteControlPreset->CreatePresetId();
	return RemoteControlPreset;
}

bool URemoteControlPresetFactory::ShouldShowInNewMenu() const
{
	return true;
}

#undef LOCTEXT_NAMESPACE
