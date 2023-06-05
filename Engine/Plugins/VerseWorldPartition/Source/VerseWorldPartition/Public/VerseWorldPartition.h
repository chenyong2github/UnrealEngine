// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "VerseWorldPartition.generated.h"

class UDataLayerManager;
class UDataLayerInstance;

UCLASS(Abstract, Within = DataLayerManager)
class VERSEWORLDPARTITION_API UVerseDataLayerManagerBase : public UObject
{
	GENERATED_UCLASS_BODY()

public:
	void Initialize();
	static UVerseDataLayerManagerBase* GetVerseDataLayerManager(UDataLayerManager* InDataLayerManager);
};

#if WITH_VERSE

#include "Assets/AssetTypes.h"

#include "WorldPartition.VerseWorldPartition.data_layer_runtime_state.gen.h"
#include "WorldPartition.VerseWorldPartition.data_layer_asset.gen.h"
#include "WorldPartition.VerseWorldPartition.data_layer_manager.gen.h"

namespace verse
{

class VERSEWORLDPARTITION_API data_layer_asset : public asset
{
	DATA_LAYER_ASSET_BODY()

public:

	virtual void PostInitProperties() override;
};

class VERSEWORLDPARTITION_API data_layer_manager : public UVerseDataLayerManagerBase
{
	DATA_LAYER_MANAGER_BODY()

public:
	// Verse API
	bool SetDataLayerRuntimeState(TNonNullPtr<data_layer_asset> DataLayerAsset, Edata_layer_runtime_state RuntimeState, bool IsRecursive);
	Edata_layer_runtime_state GetDataLayerEffectiveRuntimeState(TNonNullPtr<data_layer_asset> DataLayerAsset);
	// End Verse API
};

}

#endif // WITH_VERSE