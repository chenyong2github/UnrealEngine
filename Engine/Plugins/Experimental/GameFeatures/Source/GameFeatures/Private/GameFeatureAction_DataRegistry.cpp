// Copyright Epic Games, Inc. All Rights Reserved.

#include "GameFeatureAction_DataRegistry.h"
#include "Engine/AssetManager.h"
#include "GameFeaturesSubsystemSettings.h"
#include "GameFeaturesSubsystem.h"
#include "DataRegistryId.h"
#include "DataRegistrySubsystem.h"
#include "Engine/Engine.h"

#define LOCTEXT_NAMESPACE "GameFeatures"

void UGameFeatureAction_DataRegistry::OnGameFeatureActivating()
{
	Super::OnGameFeatureActivating();

	UDataRegistrySubsystem* DataRegistrySubsystem = UDataRegistrySubsystem::Get();
	if (ensure(DataRegistrySubsystem))
	{
		for (const TSoftObjectPtr<UDataRegistry>& RegistryToAdd : RegistriesToAdd)
		{
			if (!RegistryToAdd.IsNull())
			{
				const FSoftObjectPath RegistryPath = RegistryToAdd.ToSoftObjectPath();

#if !UE_BUILD_SHIPPING
				// If we're after data registry startup, then this asset should already exist in memory from either the bundle preload or game-specific logic
				if (DataRegistrySubsystem->AreRegistriesInitialized())
				{
					UDataRegistry* LoadedRegistry = RegistryToAdd.Get();
					if (!LoadedRegistry)
					{
						UE_LOG(LogGameFeatures, Log, TEXT("OnGameFeatureActivating %s: DataRegistry %s was not loaded before activation, this may cause a long hitch"), *GetPathName(), *RegistryPath.ToString())
					}
					else
					{
						// Need to verify this isn't already registered
						TArray<UDataRegistry*> RegistryList;
						DataRegistrySubsystem->GetAllRegistries(RegistryList);

						if (RegistryList.Contains(LoadedRegistry))
						{
							UE_LOG(LogGameFeatures, Log, TEXT("OnGameFeatureActivating %s: DataRegistry %s is already enabled from another source! This can cause problems on deactivation"), *GetPathName(), *RegistryPath.ToString())
						}
					}
				}

				// @TODO: If game features get an editor refresh function, this code should be changed to handle it
				// @TODO: Registries that are late-loaded may not show correct picker UI in editor
#endif

				DataRegistrySubsystem->LoadRegistryPath(RegistryPath);
			}
		}
	}
}

void UGameFeatureAction_DataRegistry::OnGameFeatureDeactivating(FGameFeatureDeactivatingContext& Context)
{
	Super::OnGameFeatureDeactivating(Context);

	UDataRegistrySubsystem* DataRegistrySubsystem = UDataRegistrySubsystem::Get();
	if (ensure(DataRegistrySubsystem))
	{
		for (const TSoftObjectPtr<UDataRegistry>& RegistryToAdd : RegistriesToAdd)
		{
			if (!RegistryToAdd.IsNull())
			{
				// This does not do any reference counting, warning above will hit if this is registered from two separate sources and then the first to deactivate will remove it
				const FSoftObjectPath RegistryPath = RegistryToAdd.ToSoftObjectPath();
				DataRegistrySubsystem->IgnoreRegistryPath(RegistryPath);
			}
		}
	}
}

#if WITH_EDITORONLY_DATA
void UGameFeatureAction_DataRegistry::AddAdditionalAssetBundleData(FAssetBundleData& AssetBundleData)
{
	Super::AddAdditionalAssetBundleData(AssetBundleData);
	for (const TSoftObjectPtr<UDataRegistry>& RegistryToAdd : RegistriesToAdd)
	{
		if(!RegistryToAdd.IsNull())
		{
			const FSoftObjectPath RegistryPath = RegistryToAdd.ToSoftObjectPath();

			// Add for both clients and servers, this will not work properly for games that do not set those bundle states
			// @TODO: If another way to preload specific assets is added, switch to that so it works regardless of bundles
			AssetBundleData.AddBundleAsset(UGameFeaturesSubsystemSettings::LoadStateClient, RegistryPath);
			AssetBundleData.AddBundleAsset(UGameFeaturesSubsystemSettings::LoadStateServer, RegistryPath);
		}
	}
}
#endif // WITH_EDITORONLY_DATA

#if WITH_EDITOR
EDataValidationResult UGameFeatureAction_DataRegistry::IsDataValid(TArray<FText>& ValidationErrors)
{
	EDataValidationResult Result = CombineDataValidationResults(Super::IsDataValid(ValidationErrors), EDataValidationResult::Valid);

	int32 EntryIndex = 0;
	for (const TSoftObjectPtr<UDataRegistry>& RegistryToAdd : RegistriesToAdd)
	{
		if (RegistryToAdd.IsNull())
		{
			ValidationErrors.Add(FText::Format(LOCTEXT("DataRegistryMissingSource", "No valid data registry specified at index {0} in RegistriesToAdd"), FText::AsNumber(EntryIndex)));
			Result = EDataValidationResult::Invalid;
		}

		++EntryIndex;
	}

	return Result;
}
#endif // WITH_EDITOR

#undef LOCTEXT_NAMESPACE
