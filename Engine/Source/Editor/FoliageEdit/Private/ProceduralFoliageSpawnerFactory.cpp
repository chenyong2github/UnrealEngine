// Copyright Epic Games, Inc. All Rights Reserved.

#include "ProceduralFoliageSpawnerFactory.h"
#include "Settings/EditorExperimentalSettings.h"
#include "AssetTypeCategories.h"
#include "ProceduralFoliageSpawner.h"

UProceduralFoliageSpawnerFactory::UProceduralFoliageSpawnerFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bCreateNew = true;
	bEditAfterNew = true;
	SupportedClass = UProceduralFoliageSpawner::StaticClass();
}

UObject* UProceduralFoliageSpawnerFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	auto NewProceduralFoliage = NewObject<UProceduralFoliageSpawner>(InParent, Class, Name, Flags | RF_Transactional);

	return NewProceduralFoliage;
}

bool UProceduralFoliageSpawnerFactory::ShouldShowInNewMenu() const
{
	return GetDefault<UEditorExperimentalSettings>()->bProceduralFoliage;
}
