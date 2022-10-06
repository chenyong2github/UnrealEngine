// Copyright Epic Games, Inc. All Rights Reserved.

#include "ClothPresetFactory.h"
#include "ChaosClothAsset/ClothPreset.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(ClothPresetFactory)

UChaosClothPresetFactory::UChaosClothPresetFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bEditorImport = true;
	bEditAfterNew = true;
	SupportedClass = UChaosClothPreset::StaticClass();
}

UObject* UChaosClothPresetFactory::FactoryCreateNew(UClass* Class, UObject* Parent, FName Name, EObjectFlags Flags, UObject* /*Context*/, FFeedbackContext* /*Warn*/)
{
	UChaosClothPreset* const NewClothPreset = NewObject<UChaosClothPreset>(Parent, Class, Name, Flags | RF_Transactional | RF_Public | RF_Standalone);
	NewClothPreset->MarkPackageDirty();
	return NewClothPreset;
}

