// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "MonoWaveTablePresetBank.h"
#include "SynthComponents/SynthComponentMonoWaveTable.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"

UClass* FAssetTypeActions_MonoWaveTableSynthPreset::GetSupportedClass() const
{
	return UMonoWaveTableSynthPreset::StaticClass();
}

UMonoWaveTableSynthPresetFactory::UMonoWaveTableSynthPresetFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SupportedClass = UMonoWaveTableSynthPreset::StaticClass();

	bCreateNew = true;
	bEditorImport = false;
	bEditAfterNew = true;
}

UObject* UMonoWaveTableSynthPresetFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	UMonoWaveTableSynthPreset* NewPresetBank = NewObject<UMonoWaveTableSynthPreset>(InParent, InName, Flags);

	return NewPresetBank;
}