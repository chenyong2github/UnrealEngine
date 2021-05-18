// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "GameFramework/Info.h"
#include "WorldPartition/DataLayer/ActorDataLayer.h"
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
	virtual void PostLoad() override;
	
#if WITH_EDITOR
	static AWorldDataLayers* Create(UWorld* World);
	UDataLayer* CreateDataLayer(FName InName = TEXT("DataLayer"), EObjectFlags InObjectFlags = RF_NoFlags);
	bool RemoveDataLayer(UDataLayer* InDataLayer);
	bool RemoveDataLayers(const TArray<UDataLayer*>& InDataLayers);
	FName GenerateUniqueDataLayerLabel(const FName& InDataLayerLabel) const;

	//~ Begin Helper Functions
	TArray<const UDataLayer*> GetDataLayerObjects(const TArray<FActorDataLayer>& DataLayers) const;
	TArray<FName> GetDataLayerNames(const TArray<FActorDataLayer>& DataLayers) const;
	//~ End Helper Functions
#endif
	
	bool ContainsDataLayer(const UDataLayer* InDataLayer) const;
	const UDataLayer* GetDataLayerFromName(const FName& InDataLayerName) const;
	const UDataLayer* GetDataLayerFromLabel(const FName& InDataLayerLabel) const;
	void ForEachDataLayer(TFunctionRef<bool(class UDataLayer*)> Func);
	void ForEachDataLayer(TFunctionRef<bool(class UDataLayer*)> Func) const;

	UFUNCTION(Server, Reliable)
	void SetDataLayerState(FActorDataLayer InDataLayer, EDataLayerState InState);

	UFUNCTION(NetMulticast, Reliable)
	void OnDataLayerStateChanged(const UDataLayer* InDataLayer, EDataLayerState InState);

	EDataLayerState GetDataLayerStateByName(FName InDataLAyerName) const;

	const TSet<FName>& GetActiveDataLayerNames() const { return ActiveDataLayerNames; }
	const TSet<FName>& GetLoadedDataLayerNames() const { return LoadedDataLayerNames; }

	static int32 GetDataLayersStateEpoch() { return DataLayersStateEpoch; }

protected:
	void InitializeDataLayerStates();

	UFUNCTION()
	void OnRep_ActiveDataLayerNames();

	UFUNCTION()
	void OnRep_LoadedDataLayerNames();

private:
#if !WITH_EDITOR
	TMap<FName, const UDataLayer*> LabelToDataLayer;
	TMap<FName, const UDataLayer*> NameToDataLayer;
#endif

	UPROPERTY()
	TSet<TObjectPtr<UDataLayer>> WorldDataLayers;

	UPROPERTY(Transient, Replicated, ReplicatedUsing=OnRep_ActiveDataLayerNames)
	TArray<FName> RepActiveDataLayerNames;
		
	UPROPERTY(Transient, Replicated, ReplicatedUsing=OnRep_LoadedDataLayerNames)
	TArray<FName> RepLoadedDataLayerNames;

	// TSet do not support replication so we replicate an array and update the set in the OnRep_ActiveDataLayerNames/OnRep_LoadedDataLayerNames
	TSet<FName> ActiveDataLayerNames;
	TSet<FName> LoadedDataLayerNames;

	static int32 DataLayersStateEpoch;
};