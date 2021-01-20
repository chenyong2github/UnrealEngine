// Copyright Epic Games, Inc. All Rights Reserved.

#include "GameFeatureAction_DataRegistrySource.h"
#include "Engine/AssetManager.h"
#include "GameFeaturesSubsystemSettings.h"
#include "GameFeaturesSubsystem.h"
#include "GameFeaturesProjectPolicies.h"
#include "DataRegistrySubsystem.h"
#include "Engine/Engine.h"

#define LOCTEXT_NAMESPACE "GameFeatures"

void UGameFeatureAction_DataRegistrySource::OnGameFeatureActivating()
{
	Super::OnGameFeatureActivating();

	UDataRegistrySubsystem* DataRegistrySubsystem = UDataRegistrySubsystem::Get();
	if (ensure(DataRegistrySubsystem))
	{
		UGameFeaturesProjectPolicies& Policy = UGameFeaturesSubsystem::Get().GetPolicy<UGameFeaturesProjectPolicies>();
		bool bIsClient, bIsServer;

		Policy.GetGameFeatureLoadingMode(bIsClient, bIsServer);

		for (const FDataRegistrySourceToAdd& RegistrySource : SourcesToAdd)
		{
			const bool bShouldAdd = (bIsServer && RegistrySource.bServerSource) || (bIsClient && RegistrySource.bClientSource);
			if(bShouldAdd)
			{
				TMap<FDataRegistryType, TArray<FSoftObjectPath>> AssetMap;
				TArray<FSoftObjectPath>& AssetList = AssetMap.Add(RegistrySource.RegistryToAddTo);

				if (!RegistrySource.DataTableToAdd.IsNull())
				{
					AssetList.Add(RegistrySource.DataTableToAdd.ToSoftObjectPath());
				}

				if (!RegistrySource.CurveTableToAdd.IsNull())
				{
					AssetList.Add(RegistrySource.CurveTableToAdd.ToSoftObjectPath());
				}

#if !UE_BUILD_SHIPPING
				// If we're after data registry startup, then this asset should already exist in memory from either the bundle preload or game-specific logic
				if (DataRegistrySubsystem->AreRegistriesInitialized())
				{
					if (!RegistrySource.DataTableToAdd.IsNull() && !RegistrySource.DataTableToAdd.IsValid())
					{
						UE_LOG(LogGameFeatures, Log, TEXT("OnGameFeatureActivating %s: DataRegistry source asset %s was not loaded before activation, this may cause a long hitch"), *GetPathName(), *RegistrySource.DataTableToAdd.ToString())
					}

					if (!RegistrySource.CurveTableToAdd.IsNull() && !RegistrySource.CurveTableToAdd.IsValid())
					{
						UE_LOG(LogGameFeatures, Log, TEXT("OnGameFeatureActivating %s: DataRegistry source asset %s was not loaded before activation, this may cause a long hitch"), *GetPathName(), *RegistrySource.DataTableToAdd.ToString())
					}
				}

				// @TODO: If game features get an editor refresh function, this code should be changed to handle it
				// @TODO: Registry sources that are late-loaded may not show correct picker UI in editor
#endif

				// This will either load the sources immediately, or schedule them for load when registries are initialized
				DataRegistrySubsystem->PreregisterSpecificAssets(AssetMap, RegistrySource.AssetPriority);
			}
		}
	}
}

void UGameFeatureAction_DataRegistrySource::OnGameFeatureDeactivating(FGameFeatureDeactivatingContext& Context)
{
	Super::OnGameFeatureDeactivating(Context);

	UDataRegistrySubsystem* DataRegistrySubsystem = UDataRegistrySubsystem::Get();
	if (ensure(DataRegistrySubsystem))
	{
		for (const FDataRegistrySourceToAdd& RegistrySource : SourcesToAdd)
		{
			if (!RegistrySource.DataTableToAdd.IsNull())
			{
				DataRegistrySubsystem->UnregisterSpecificAsset(RegistrySource.RegistryToAddTo, RegistrySource.DataTableToAdd.ToSoftObjectPath());
			}

			if (!RegistrySource.CurveTableToAdd.IsNull())
			{
				DataRegistrySubsystem->UnregisterSpecificAsset(RegistrySource.RegistryToAddTo, RegistrySource.CurveTableToAdd.ToSoftObjectPath());
			}
		}
	}
}

#if WITH_EDITORONLY_DATA
void UGameFeatureAction_DataRegistrySource::AddAdditionalAssetBundleData(FAssetBundleData& AssetBundleData)
{
	Super::AddAdditionalAssetBundleData(AssetBundleData);
	for (const FDataRegistrySourceToAdd& RegistrySource : SourcesToAdd)
	{
		// Register table assets for preloading, this will only work if the game uses client/server bundle states
		// @TODO: If another way of preloading data is added, client+server sources should use that instead

		if(!RegistrySource.DataTableToAdd.IsNull())
		{
			const FSoftObjectPath DataTableSourcePath = RegistrySource.DataTableToAdd.ToSoftObjectPath();
			if (RegistrySource.bClientSource)
			{
				AssetBundleData.AddBundleAsset(UGameFeaturesSubsystemSettings::LoadStateClient, DataTableSourcePath);
			}
			if (RegistrySource.bServerSource)
			{
				AssetBundleData.AddBundleAsset(UGameFeaturesSubsystemSettings::LoadStateServer, DataTableSourcePath);
			}
		}

		if (!RegistrySource.CurveTableToAdd.IsNull())
		{
			const FSoftObjectPath CurveTableSourcePath = RegistrySource.CurveTableToAdd.ToSoftObjectPath();
			if (RegistrySource.bClientSource)
			{
				AssetBundleData.AddBundleAsset(UGameFeaturesSubsystemSettings::LoadStateClient, CurveTableSourcePath);
			}
			if (RegistrySource.bServerSource)
			{
				AssetBundleData.AddBundleAsset(UGameFeaturesSubsystemSettings::LoadStateServer, CurveTableSourcePath);
			}
		}
	}
}
#endif // WITH_EDITORONLY_DATA

#if WITH_EDITOR
EDataValidationResult UGameFeatureAction_DataRegistrySource::IsDataValid(TArray<FText>& ValidationErrors)
{
	EDataValidationResult Result = CombineDataValidationResults(Super::IsDataValid(ValidationErrors), EDataValidationResult::Valid);

	int32 EntryIndex = 0;
	for (const FDataRegistrySourceToAdd& Entry : SourcesToAdd)
	{
		if (Entry.CurveTableToAdd.IsNull() && Entry.DataTableToAdd.IsNull())
		{
			ValidationErrors.Add(FText::Format(LOCTEXT("DataRegistrySourceMissingSource", "No valid data table or curve table specified at index {0} in SourcesToAdd"), FText::AsNumber(EntryIndex)));
			Result = EDataValidationResult::Invalid;
		}

		if (Entry.bServerSource == false && Entry.bClientSource == false)
		{
			ValidationErrors.Add(FText::Format(LOCTEXT("DataRegistrySourceNeverUsed", "Source not specified to load on either client or server, it will be unused at index {0} in SourcesToAdd"), FText::AsNumber(EntryIndex)));
			Result = EDataValidationResult::Invalid;
		}

		if (Entry.RegistryToAddTo.IsNone())
		{
			ValidationErrors.Add(FText::Format(LOCTEXT("DataRegistrySourceInvalidRegistry", "Source specified an invalid name (NONE) as the target registry at index {0} in SourcesToAdd"), FText::AsNumber(EntryIndex)));
			Result = EDataValidationResult::Invalid;
		}

		++EntryIndex;
	}

	return Result;
}
#endif // WITH_EDITOR

#undef LOCTEXT_NAMESPACE
