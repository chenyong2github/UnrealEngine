// Copyright Epic Games, Inc. All Rights Reserved.

#include "GameFeatureAction_AddComponents.h"
#include "Components/GameFrameworkComponentManager.h"
#include "GameFeaturesSubsystemSettings.h"
#include "Engine/AssetManager.h"

//@TODO: Just for log category
#include "GameFeaturesSubsystem.h"

#define LOCTEXT_NAMESPACE "GameFeatures"

//////////////////////////////////////////////////////////////////////
// UGameFeatureAction_AddComponents

void UGameFeatureAction_AddComponents::OnGameFeatureActivating()
{
	GameInstanceStartHandle = FWorldDelegates::OnStartGameInstance.AddUObject(this, &UGameFeatureAction_AddComponents::HandleGameInstanceStart);

	check(ComponentRequestHandles.Num() == 0);

	// Add to any worlds with associated game instances that have already been initialized
	for (const FWorldContext& WorldContext : GEngine->GetWorldContexts())
	{
		AddToWorld(WorldContext);
	}
}

void UGameFeatureAction_AddComponents::OnGameFeatureDeactivating(FGameFeatureDeactivatingContext& Context)
{
	FWorldDelegates::OnStartGameInstance.Remove(GameInstanceStartHandle);

	// Releasing the handles will also remove the components from any registered actors too
	ComponentRequestHandles.Empty();
}

#if WITH_EDITORONLY_DATA
void UGameFeatureAction_AddComponents::AddAdditionalAssetBundleData(FAssetBundleData& AssetBundleData)
{
	if (UAssetManager::IsValid())
	{
		for (const FGameFeatureComponentEntry& Entry : ComponentList)
		{
			if (Entry.bClientComponent)
			{
				AssetBundleData.AddBundleAsset(UGameFeaturesSubsystemSettings::LoadStateClient, Entry.ComponentClass.ToSoftObjectPath());
			}
			if (Entry.bServerComponent)
			{
				AssetBundleData.AddBundleAsset(UGameFeaturesSubsystemSettings::LoadStateServer, Entry.ComponentClass.ToSoftObjectPath());
			}
		}
	}
}
#endif

#if WITH_EDITOR
EDataValidationResult UGameFeatureAction_AddComponents::IsDataValid(TArray<FText>& ValidationErrors)
{
	EDataValidationResult Result = CombineDataValidationResults(Super::IsDataValid(ValidationErrors), EDataValidationResult::Valid);

	int32 EntryIndex = 0;
	for (const FGameFeatureComponentEntry& Entry : ComponentList)
	{
		if (Entry.ActorClass.IsNull())
		{
			Result = EDataValidationResult::Invalid;
			ValidationErrors.Add(FText::Format(LOCTEXT("ComponentEntryHasNullActor", "Null ActorClass at index {0} in ComponentList"), FText::AsNumber(EntryIndex)));
		}

		if (Entry.ComponentClass.IsNull())
		{
			Result = EDataValidationResult::Invalid;
			ValidationErrors.Add(FText::Format(LOCTEXT("ComponentEntryHasNullComponent", "Null ComponentClass at index {0} in ComponentList"), FText::AsNumber(EntryIndex)));
		}

		++EntryIndex;
	}

	return Result;
}
#endif

void UGameFeatureAction_AddComponents::AddToWorld(const FWorldContext& WorldContext)
{
	UWorld* World = WorldContext.World();
	UGameInstance* GameInstance = WorldContext.OwningGameInstance;

	if ((GameInstance != nullptr) && (World != nullptr) && World->IsGameWorld())
	{
		if (UGameFrameworkComponentManager* GFCM = UGameInstance::GetSubsystem<UGameFrameworkComponentManager>(GameInstance))
		{
			UE_LOG(LogGameFeatures, Verbose, TEXT("Adding components for %s to world %s"), *GetPathNameSafe(this), *World->GetDebugDisplayName());
			
			bool bIsServer = IsRunningDedicatedServer();
#if WITH_EDITOR
			checkSlow(GameInstance->GetWorldContext());
			bIsServer |= GameInstance->GetWorldContext()->RunAsDedicated;
#endif

			//@TODO: GameFeaturePluginEnginePush: RIP listen servers (don't intend to add support for them, but we should surface that and warn if the world is NM_ListenServer or something like that)
			const bool bIsClient = !bIsServer;

			for (const FGameFeatureComponentEntry& Entry : ComponentList)
			{
				const bool bShouldAddRequest = (bIsServer && Entry.bServerComponent) || (bIsClient && Entry.bClientComponent);
				if (bShouldAddRequest)
				{
					if (!Entry.ActorClass.IsNull())
					{
						TSubclassOf<UActorComponent> ComponentClass = Entry.ComponentClass.LoadSynchronous();
						if (ComponentClass)
						{
							ComponentRequestHandles.Add(GFCM->AddComponentRequest(Entry.ActorClass, ComponentClass));
						}
						else if (!Entry.ComponentClass.IsNull())
						{
							UE_LOG(LogGameFeatures, Error, TEXT("[GameFeatureData %s]: Failed to load component class %s. Not applying component."), *GetPathNameSafe(this), *Entry.ComponentClass.ToString());
						}
					}
				}
			}
		}
	}
}

void UGameFeatureAction_AddComponents::HandleGameInstanceStart(UGameInstance* GameInstance)
{
	if (FWorldContext* WorldContext = GameInstance->GetWorldContext())
	{
		AddToWorld(*WorldContext);
	}
}

//////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
