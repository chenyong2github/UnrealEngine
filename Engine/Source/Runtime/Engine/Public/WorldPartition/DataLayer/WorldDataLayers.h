// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "GameFramework/Info.h"
#include "WorldDataLayers.generated.h"

class UDataLayer;

/**
 * Actor containing all data layers for a world
 */
UCLASS(hidecategories = (Actor, Advanced, Display, Events, Object, Attachment, Info, Input, Blueprint, Layers, Tags, Replication), notplaceable)
class ENGINE_API AWorldDataLayers : public AInfo
{
	GENERATED_UCLASS_BODY()

public:
	static const AWorldDataLayers* Get(UWorld* World);
	virtual void PostLoad() override;

#if WITH_EDITOR
	static AWorldDataLayers* Get(UWorld* World, bool bCreateIfNotFound);
	UDataLayer* CreateDataLayer();
	bool RemoveDataLayer(UDataLayer* InDataLayer);
	bool RemoveDataLayers(const TArray<UDataLayer*>& InDataLayers);
	FName GenerateUniqueDataLayerLabel(const FName& InDataLayerLabel) const;
#endif
	
	bool ContainsDataLayer(const UDataLayer* InDataLayer) const;
	const UDataLayer* GetDataLayerFromName(const FName& InDataLayerName) const;
	const UDataLayer* GetDataLayerFromLabel(const FName& InDataLayerLabel) const;
	void ForEachDataLayer(TFunctionRef<bool(class UDataLayer*)> Func);
	void ForEachDataLayer(TFunctionRef<bool(class UDataLayer*)> Func) const;

private:
#if !WITH_EDITOR
	TMap<FName, const UDataLayer*> LabelToDataLayer;
	TMap<FName, const UDataLayer*> NameToDataLayer;
#endif

	UPROPERTY()
	TSet<UDataLayer*> WorldDataLayers;
};