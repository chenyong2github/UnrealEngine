// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFeatureAction.h"
#include "GameFramework/CheatManager.h"

#include "GameFeatureAction_AddGameplayTags.generated.h"

struct FNativeGameplayTagSource;

//////////////////////////////////////////////////////////////////////
// UGameFeatureAction_AddGameplayTags

/**
 * Adds native gameplay tags for a given module.
 */
UCLASS(Abstract, meta=(Hidden, DisplayName="Add Gameplay Tags"))
class GAMEFEATURES_API UGameFeatureAction_AddGameplayTags : public UGameFeatureAction
{
	GENERATED_BODY()

public:
	//~UGameFeatureAction interface
	virtual void OnGameFeatureRegistering() override;
	virtual void OnGameFeatureDeactivating(FGameFeatureDeactivatingContext& Context) override;
	//~End of UGameFeatureAction interface


protected:
	TArray<TSharedRef<const FNativeGameplayTagSource>> NativeTagSources;

private:
	FString Generated_TagSourceName;
};
