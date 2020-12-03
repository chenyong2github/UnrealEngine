// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AssetRegistry/AssetData.h"
#include "Elements/Framework/TypedElementHandle.h"
#include "UObject/Interface.h"
#include "EditorSubsystem.h"

#include "PlacementSubsystem.generated.h"

class IAssetFactoryInterface;

USTRUCT()
struct EDITORFRAMEWORK_API FAssetPlacementInfo
{
	GENERATED_BODY()

	// The asset data which should be placed.
	UPROPERTY()
	FAssetData AssetToPlace;

	// If set, will override the name on placed elements instead of factory defined defaults.
	UPROPERTY()
	FName NameOverride;

	// If set, the factory will attempt to place inside the given level. World partitioning may ultimately override this preference.
	UPROPERTY()
	TWeakObjectPtr<ULevel> PreferredLevel;

	// The finalized transform where the factory should be place the asset. This should include any location snapping or other considerations from viewports or editor settings.
	UPROPERTY()
	FTransform FinalizedTransform;

	// If set, will use the given factory to place the asset, instead of allowing the placement subsystem to determine which factory to use.
	UPROPERTY()
	TScriptInterface<IAssetFactoryInterface> FactoryOverride;
};

USTRUCT()
struct EDITORFRAMEWORK_API FPlacementOptions
{
	GENERATED_BODY()

	// If true, asset factory implementations should defer to placing instanced items (i.e. instanced static mesh instead of individual static mesh actors).
	UPROPERTY()
	bool bPreferInstancedPlacement;

	// If true, asset factory implementations should prefer a batch placement algorithm (like duplicating an object) over a single placement algorithm.
	UPROPERTY()
	bool bPreferBatchPlacement;
};

UCLASS(Transient)
class EDITORFRAMEWORK_API UPlacementSubsystem : public UEditorSubsystem
{
	GENERATED_BODY()

public:
	// USubsystem Interface
	void Initialize(FSubsystemCollectionBase& Collection) override;
	void Deinitialize() override;
	// End USubsystem Interface

	/**
	 * Places a single asset based on the given FAssetPlacementInfo and FPlacementOptions.
	 * @returns an array of FTypedElementHandles corresponding to any successfully placed elements.
	 */
	TArray<FTypedElementHandle> PlaceAsset(const FAssetPlacementInfo& InPlacementInfo, const FPlacementOptions& InPlacementOptions);

	/**
	 * Places a multiple asset based on the given FAssetPlacementInfos and FPlacementOptions.
	 * @returns an array of FTypedElementHandles corresponding to any successfully placed elements.
	 */
	TArray<FTypedElementHandle> PlaceAssets(TArrayView<const FAssetPlacementInfo> InPlacementInfos, const FPlacementOptions& InPlacementOptions);

	/**
	 * Finds a registered AssetFactory for the given FAssetData.
	 * @returns the first found factory, or nullptr if no valid factory is found.
	 */
	TScriptInterface<IAssetFactoryInterface> FindAssetFactoryFromAssetData(const FAssetData& InAssetData);

private:
	void RegisterPlacementFactories();
	void UnregisterPlacementFactories();

	UPROPERTY()
	TArray<TScriptInterface<IAssetFactoryInterface>> AssetFactories;
};
