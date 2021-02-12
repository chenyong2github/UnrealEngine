// Copyright Epic Games, Inc. All Rights Reserved.

#include "GameFeatureAction_AddGameplayTags.h"
#include "GameplayTagsManager.h"

#define LOCTEXT_NAMESPACE "GameFeatures"

//////////////////////////////////////////////////////////////////////
// UGameFeatureAction_AddGameplayTags

void UGameFeatureAction_AddGameplayTags::OnGameFeatureRegistering()
{
	Generated_TagSourceName = GetName();

	UGameplayTagsManager& Manager = UGameplayTagsManager::Get();
	for (int32 Index = 0; Index < NativeTagSources.Num(); Index++)
	{
		Manager.AddNativeGameplayTagSource(Generated_TagSourceName + TEXT("_") + LexToString(Index), NativeTagSources[Index]);
	}
}

void UGameFeatureAction_AddGameplayTags::OnGameFeatureDeactivating(FGameFeatureDeactivatingContext& Context)
{
	UGameplayTagsManager& Manager = UGameplayTagsManager::Get();
	for (int32 Index = 0; Index < NativeTagSources.Num(); Index++)
	{
		Manager.RemoveNativeGameplayTagSource(Generated_TagSourceName + TEXT("_") + LexToString(Index));
	}
}

//////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
