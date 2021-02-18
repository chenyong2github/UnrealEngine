// Copyright Epic Games, Inc. All Rights Reserved.

#include "GameFeatureAction_AddGameplayTags.h"
#include "GameplayTagsManager.h"
#include "NativeGameplayTags.h"

#define LOCTEXT_NAMESPACE "GameFeatures"

//////////////////////////////////////////////////////////////////////
// UGameFeatureAction_AddGameplayTags

void UGameFeatureAction_AddGameplayTags::OnGameFeatureRegistering()
{
	UGameplayTagsManager& Manager = UGameplayTagsManager::Get();
	for (TSharedRef<const FNativeGameplayTagSource> TagSource : NativeTagSources)
	{
		TagSource->Register();
	}
}

void UGameFeatureAction_AddGameplayTags::OnGameFeatureDeactivating(FGameFeatureDeactivatingContext& Context)
{
	UGameplayTagsManager& Manager = UGameplayTagsManager::Get();
	for (TSharedRef<const FNativeGameplayTagSource> TagSource : NativeTagSources)
	{
		TagSource->Unregister();
	}
}

//////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
