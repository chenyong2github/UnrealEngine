// Copyright Epic Games, Inc. All Rights Reserved.

#include "GameFeatureAction_DataRegistrySource.h"
#include "Engine/AssetManager.h"
#include "GameFeaturesSubsystemSettings.h"
#include "DataRegistryId.h"
#include "DataRegistrySubsystem.h"
#include "Engine/Engine.h"

#define LOCTEXT_NAMESPACE "GameFeatures"

void UGameFeatureAction_DataRegistrySource::OnGameFeatureActivating()
{
	Super::OnGameFeatureActivating();

	UDataRegistrySubsystem* DataRegistrySubsystem = UDataRegistrySubsystem::Get();
	if (ensure(DataRegistrySubsystem))
	{
		bool bIsServer = IsRunningDedicatedServer();
		//@TODO: GameFeaturePluginEnginePush: RIP listen servers (don't intend to add support for them, but we should surface that and warn if the world is NM_ListenServer or something like that)
		bool bIsClient = !bIsServer;
#if WITH_EDITOR
		// If we have any server world contexts, we should act like we are the server
		for (const FWorldContext& WorldContext : GEngine->GetWorldContexts())
		{
			UWorld* World = WorldContext.World();
			UGameInstance* GameInstance = WorldContext.OwningGameInstance;

			if ((GameInstance != nullptr) && (World != nullptr) && World->IsGameWorld())
			{
				checkSlow(GameInstance->GetWorldContext());
				if (GameInstance->GetWorldContext()->RunAsDedicated)
				{
					bIsServer = true;
				}
				else
				{
					bIsClient = true;
				}
			}
		}
#endif

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
