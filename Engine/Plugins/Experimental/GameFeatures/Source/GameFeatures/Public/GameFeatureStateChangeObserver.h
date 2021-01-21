// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFeatureStateChangeObserver.generated.h"

class UGameFeatureData;
struct FGameFeatureDeactivatingContext;

/**
 * This class is meant to be overridden in your game to handle game-specific reactions to game feature plugins
 * being mounted or unmounted
 *
 * Generally you should prefer to use UGameFeatureAction instances on your game feature data asset instead of
 * this, especially if any data is involved
 *
 * If you do use these, create them in your UGameFeaturesProjectPolicies subclass and register them via
 * AddObserver / RemoveObserver on UGameFeaturesSubsystem
 */
UCLASS()
class GAMEFEATURES_API UGameFeatureStateChangeObserver : public UObject
{
	GENERATED_BODY()

public:

	virtual void OnGameFeatureRegistering(const UGameFeatureData* GameFeatureData, const FString& PluginName) {}

	virtual void OnGameFeatureActivating(const UGameFeatureData* GameFeatureData) {}

	virtual void OnGameFeatureLoading(const UGameFeatureData* GameFeatureData) {}

	virtual void OnGameFeatureDeactivating(const UGameFeatureData* GameFeatureData, FGameFeatureDeactivatingContext& Context) {}
};
