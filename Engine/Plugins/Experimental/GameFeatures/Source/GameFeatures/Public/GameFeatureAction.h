// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFeatureAction.generated.h"

struct FGameFeatureDeactivatingContext;
struct FAssetBundleData;

/** Represents an action to be taken when a game feature is activated */
UCLASS(DefaultToInstanced, EditInlineNew, Abstract)
class GAMEFEATURES_API UGameFeatureAction : public UObject
{
	GENERATED_BODY()

public:
	virtual void OnGameFeatureRegistering() {}

	virtual void OnGameFeatureActivating() {}

	virtual void OnGameFeatureLoading() {}

	virtual void OnGameFeatureDeactivating(FGameFeatureDeactivatingContext& Context) {}

#if WITH_EDITORONLY_DATA
	virtual void AddAdditionalAssetBundleData(FAssetBundleData& AssetBundleData) {}
#endif
};
