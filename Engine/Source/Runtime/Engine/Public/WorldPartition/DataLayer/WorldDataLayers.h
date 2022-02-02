// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "GameFramework/Info.h"
#include "WorldPartition/DataLayer/ActorDataLayer.h"
#include "WorldPartition/DataLayer/DataLayer.h"
#include "WorldDataLayers.generated.h"

class UDataLayer;

/**
 * Actor containing all data layers for a world
 */
UCLASS(hidecategories = (Actor, HLOD, Cooking, Transform, Advanced, Display, Events, Object, Attachment, Info, Input, Blueprint, Layers, Tags, Replication), notplaceable)
class ENGINE_API AWorldDataLayers : public AInfo
{
	GENERATED_UCLASS_BODY()

public:
	virtual void PostLoad() override;
	virtual void RewindForReplay() override;
	virtual void BeginPlay() override;

#if WITH_EDITOR
	virtual bool ShouldLevelKeepRefIfExternal() const override { return true; }
	virtual bool ShouldImport(FString* ActorPropString, bool IsMovingLevel) override { return false; }
	virtual bool IsLockLocation() const { return true; }
	virtual bool IsUserManaged() const override { return false; }

	static AWorldDataLayers* Create(UWorld* World);
	UDataLayer* CreateDataLayer(FName InName = TEXT("DataLayer"), EObjectFlags InObjectFlags = RF_NoFlags);
	bool RemoveDataLayer(UDataLayer* InDataLayer);
	bool RemoveDataLayers(const TArray<UDataLayer*>& InDataLayers);
	FName GenerateUniqueDataLayerLabel(const FName& InDataLayerLabel) const;
	void SetAllowRuntimeDataLayerEditing(bool bInAllowRuntimeDataLayerEditing);
	bool GetAllowRuntimeDataLayerEditing() const { return bAllowRuntimeDataLayerEditing; }

	//~ Begin Helper Functions
	TArray<const UDataLayer*> GetDataLayerObjects(const TArray<FActorDataLayer>& DataLayers) const;
	TArray<const UDataLayer*> GetDataLayerObjects(const TArray<FName>& InDataLayerNames) const;
	TArray<FName> GetDataLayerNames(const TArray<FActorDataLayer>& DataLayers) const;
	//~ End Helper Functions

	// Allows overriding of DataLayers with PlayFromHere
	void OverwriteDataLayerRuntimeStates(TArray<FActorDataLayer>* InActiveDataLayers = nullptr, TArray<FActorDataLayer>* InLoadedDataLayers = nullptr);

	// Returns the DataLayer user loaded editor states
	void GetUserLoadedInEditorStates(TArray<FName>& OutDataLayersLoadedInEditor, TArray<FName>& OutDataLayersNotLoadedInEditor) const;
#endif
	
	void DumpDataLayers(FOutputDevice& OutputDevice) const;
	bool ContainsDataLayer(const UDataLayer* InDataLayer) const;
	const UDataLayer* GetDataLayerFromName(const FName& InDataLayerName) const;
	const UDataLayer* GetDataLayerFromLabel(const FName& InDataLayerLabel) const;
	void ForEachDataLayer(TFunctionRef<bool(UDataLayer*)> Func);
	void ForEachDataLayer(TFunctionRef<bool(UDataLayer*)> Func) const;

	// DataLayer Runtime State
	void SetDataLayerRuntimeState(FActorDataLayer InDataLayer, EDataLayerRuntimeState InState, bool bIsRecursive = false);
	EDataLayerRuntimeState GetDataLayerRuntimeStateByName(FName InDataLayerName) const;
	EDataLayerRuntimeState GetDataLayerEffectiveRuntimeStateByName(FName InDataLAyerName) const;
	const TSet<FName>& GetEffectiveActiveDataLayerNames() const { return EffectiveActiveDataLayerNames; }
	const TSet<FName>& GetEffectiveLoadedDataLayerNames() const { return EffectiveLoadedDataLayerNames; }
	UFUNCTION(NetMulticast, Reliable)
	void OnDataLayerRuntimeStateChanged(const UDataLayer* InDataLayer, EDataLayerRuntimeState InState);
	static int32 GetDataLayersStateEpoch() { return DataLayersStateEpoch; }

	//~ Begin Deprecated

	PRAGMA_DISABLE_DEPRECATION_WARNINGS // Suppress compiler warning on override of deprecated function

	UE_DEPRECATED(5.0, "Use SetDataLayerRuntimeState() instead.")
	void SetDataLayerState(FActorDataLayer InDataLayer, EDataLayerState InState) { SetDataLayerRuntimeState(InDataLayer, (EDataLayerRuntimeState)InState); }

	UE_DEPRECATED(5.0, "Use GetDataLayerRuntimeStateByName() instead.")
	EDataLayerState GetDataLayerStateByName(FName InDataLayerName) const { return (EDataLayerState)GetDataLayerRuntimeStateByName(InDataLayerName); }

	PRAGMA_ENABLE_DEPRECATION_WARNINGS

	UE_DEPRECATED(5.0, "Use GetEffectiveActiveDataLayerNames() instead.")
	const TSet<FName>& GetActiveDataLayerNames() const { return GetEffectiveActiveDataLayerNames(); }

	UE_DEPRECATED(5.0, "Use GetEffectiveLoadedDataLayerNames() instead.")
	const TSet<FName>& GetLoadedDataLayerNames() const { return GetEffectiveLoadedDataLayerNames(); }
	//~ End Deprecated

protected:
	void InitializeDataLayerRuntimeStates();
	void ResetDataLayerRuntimeStates();

	UFUNCTION()
	void OnRep_ActiveDataLayerNames();

	UFUNCTION()
	void OnRep_LoadedDataLayerNames();

	UFUNCTION()
	void OnRep_EffectiveActiveDataLayerNames();

	UFUNCTION()
	void OnRep_EffectiveLoadedDataLayerNames();

private:
	void ResolveEffectiveRuntimeState(const UDataLayer* InDataLayer, bool bInNotifyChange = true);
	void DumpDataLayerRecursively(const UDataLayer* DataLayer, FString Prefix, FOutputDevice& OutputDevice) const;

#if !WITH_EDITOR
	TMap<FName, const UDataLayer*> LabelToDataLayer;
	TMap<FName, const UDataLayer*> NameToDataLayer;
#endif

#if WITH_EDITORONLY_DATA
	// True when Runtime Data Layer editing is allowed.
	UPROPERTY()
	bool bAllowRuntimeDataLayerEditing;
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

	UPROPERTY(Transient, Replicated, ReplicatedUsing=OnRep_EffectiveActiveDataLayerNames)
	TArray<FName> RepEffectiveActiveDataLayerNames;
		
	UPROPERTY(Transient, Replicated, ReplicatedUsing=OnRep_EffectiveLoadedDataLayerNames)
	TArray<FName> RepEffectiveLoadedDataLayerNames;

	// TSet do not support replication so we replicate an array and update the set in the OnRep_EffectiveActiveDataLayerNames/OnRep_EffectiveLoadedDataLayerNames
	TSet<FName> EffectiveActiveDataLayerNames;
	TSet<FName> EffectiveLoadedDataLayerNames;

	static int32 DataLayersStateEpoch;

public:
	DECLARE_DELEGATE_RetVal_ThreeParams(bool, FDataLayersFilterDelegate, FName /*DataLayerName*/, EDataLayerRuntimeState /*CurrentState*/, EDataLayerRuntimeState /*TargetState*/);

	UE_DEPRECATED(5.00, "do not use, will be replaced by another mechanism for initial release.")
	FDataLayersFilterDelegate DataLayersFilterDelegate;
};