// Copyright Epic Games, Inc. All Rights Reserved.

#include "LevelInstanceActorFactory.h"
#include "LevelInstanceEditorSettings.h"
#include "LevelInstance/LevelInstanceInterface.h"
#include "Settings/EditorExperimentalSettings.h"

ULevelInstanceActorFactory::ULevelInstanceActorFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	NewActorClassName = GetDefault<ULevelInstanceEditorSettings>()->LevelInstanceClassName;
}

void ULevelInstanceActorFactory::PostSpawnActor(UObject* Asset, AActor* NewActor)
{
	ILevelInstanceInterface* LevelInstanceInterface = CastChecked<ILevelInstanceInterface>(NewActor);
	LevelInstanceInterface->SetWorldAsset(Asset);
	LevelInstanceInterface->LoadLevelInstance();
}

bool ULevelInstanceActorFactory::CanCreateActorFrom(const FAssetData& AssetData, FText& OutErrorMsg)
{
	if (!GetDefault<UEditorExperimentalSettings>()->bLevelInstance)
	{
		OutErrorMsg = NSLOCTEXT("LevelInstanceActorFactory", "ExperimentalSettings", "Level Instance must be enabled in experimental settings.");
		return false;
	}

	if (!AssetData.IsValid() || !AssetData.IsInstanceOf(UWorld::StaticClass()))
	{
		OutErrorMsg = NSLOCTEXT("LevelInstanceActorFactory", "NoWorld", "A valid world must be specified.");
		return false;
	}

	return true;
}
