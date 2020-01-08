// Copyright Epic Games, Inc. All Rights Reserved.
#include "EpicSynth1PresetBank.h"

#include "SynthComponents/EpicSynth1Component.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"


#define LOCTEXT_NAMESPACE "AssetTypeActions"

UClass* FAssetTypeActions_ModularSynthPresetBank::GetSupportedClass() const
{
	return UModularSynthPresetBank::StaticClass();
}

const TArray<FText>& FAssetTypeActions_ModularSynthPresetBank::GetSubMenus() const
{
	static const TArray<FText> SubMenus
	{
		FText(LOCTEXT("AssetSoundSynthesisSubMenu", "Synthesis"))
	};

	return SubMenus;
}

UModularSynthPresetBankFactory::UModularSynthPresetBankFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SupportedClass = UModularSynthPresetBank::StaticClass();

	bCreateNew = true;
	bEditorImport = false;
	bEditAfterNew = true;
}

UObject* UModularSynthPresetBankFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	UModularSynthPresetBank* NewPresetBank = NewObject<UModularSynthPresetBank>(InParent, InName, Flags);

	return NewPresetBank;
}

#undef LOCTEXT_NAMESPACE