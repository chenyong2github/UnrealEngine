// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Subsystems/WorldSubsystem.h"
#include "WorldPartition/DataLayer/ActorDataLayer.h"
#include "WorldPartition/DataLayer/DataLayerInstance.h"
#include "WorldPartition/DataLayer/DataLayerEditorContext.h"
#include "DataLayerSubsystem.generated.h"

/**
 * UDataLayerSubsystem
 */

class UCanvas;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnDataLayerRuntimeStateChanged, const UDataLayerInstance*, DataLayer, EDataLayerRuntimeState, State);

UCLASS()
class ENGINE_API UDataLayerSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	UDataLayerSubsystem();

	//~ Begin USubsystem Interface.
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	//~ End USubsystem Interface.

	//~ Begin Blueprint callable functions

	/** Find a Data Layer by its asset. */
	UFUNCTION(BlueprintCallable, Category = DataLayers)
	UDataLayerInstance* GetDataLayerFromAsset(const UDataLayerAsset* InDataLayerAsset) const;

	UFUNCTION(BlueprintCallable, Category = DataLayers)
	EDataLayerRuntimeState GetDataLayerInstanceRuntimeState(const UDataLayerAsset* InDataLayerAsset) const;

	UFUNCTION(BlueprintCallable, Category = DataLayers)
	EDataLayerRuntimeState GetDataLayerInstanceEffectiveRuntimeState(const UDataLayerAsset* InDataLayerAsset) const;

	/** Set the Data Layer state using its name. */
	UFUNCTION(BlueprintCallable, Category = DataLayers, BlueprintAuthorityOnly)
	void SetDataLayerInstanceRuntimeState(const UDataLayerAsset* InDataLayerAsset, EDataLayerRuntimeState InState, bool bInIsRecursive = false);

	/** Called when a Data Layer changes state. */
	UPROPERTY(BlueprintAssignable)
	FOnDataLayerRuntimeStateChanged OnDataLayerRuntimeStateChanged;

	//~ End Blueprint callable functions

	UDataLayerInstance* GetDataLayerInstance(const FName& InDataLayerInstanceName) const;
	void SetDataLayerRuntimeState(const UDataLayerInstance* InDataLayerInstance, EDataLayerRuntimeState InState, bool bInIsRecursive = false);
	EDataLayerRuntimeState GetDataLayerRuntimeState(const UDataLayerInstance* InDataLayer) const;
	EDataLayerRuntimeState GetDataLayerRuntimeStateByName(const FName& InDataLayerName) const;
	EDataLayerRuntimeState GetDataLayerEffectiveRuntimeState(const UDataLayerInstance* InDataLayer) const;
	EDataLayerRuntimeState GetDataLayerEffectiveRuntimeStateByName(const FName& InDataLayerName) const;
	bool IsAnyDataLayerInEffectiveRuntimeState(const TArray<FName>& InDataLayerNames, EDataLayerRuntimeState InState) const;
	const TSet<FName>& GetEffectiveActiveDataLayerNames() const;
	const TSet<FName>& GetEffectiveLoadedDataLayerNames() const;

	void GetDataLayerDebugColors(TMap<FName, FColor>& OutMapping) const;
	void DrawDataLayersStatus(UCanvas* Canvas, FVector2D& Offset) const;
	static TArray<UDataLayerInstance*> ConvertArgsToDataLayers(UWorld* World, const TArray<FString>& InArgs);

	void DumpDataLayers(FOutputDevice& OutputDevice) const;

	//~ Begin Deprecated
	
	PRAGMA_DISABLE_DEPRECATION_WARNINGS // Suppress compiler warning on override of deprecated function

	UE_DEPRECATED(5.0, "Use SetDataLayerRuntimeState() instead.")
	UFUNCTION(BlueprintCallable, Category = DataLayers, BlueprintAuthorityOnly, meta = (DeprecatedFunction, DeprecationMessage = "Use SetDataLayerRuntimeState instead"))
	void SetDataLayerState(const FActorDataLayer& InDataLayer, EDataLayerState InState) { SetDataLayerRuntimeState(InDataLayer, (EDataLayerRuntimeState)InState); }

	UE_DEPRECATED(5.0, "Use SetDataLayerRuntimeStateByLabel() instead.")
	UFUNCTION(BlueprintCallable, Category = DataLayers, BlueprintAuthorityOnly, meta = (DeprecatedFunction, DeprecationMessage = "Use SetDataLayerRuntimeStateByLabel instead"))
	void SetDataLayerStateByLabel(const FName& InDataLayerLabel, EDataLayerState InState) { SetDataLayerRuntimeStateByLabel(InDataLayerLabel, (EDataLayerRuntimeState)InState); }

	UE_DEPRECATED(5.0, "Use GetDataLayerRuntimeState() instead.")
	UFUNCTION(BlueprintCallable, Category = DataLayers, meta = (DeprecatedFunction, DeprecationMessage = "Use GetDataLayerRuntimeState instead"))
	EDataLayerState GetDataLayerState(const FActorDataLayer& InDataLayer) const { return (EDataLayerState)GetDataLayerRuntimeState(InDataLayer); }

	UE_DEPRECATED(5.0, "Use GetDataLayerRuntimeStateByLabel() instead.")
	UFUNCTION(BlueprintCallable, Category = DataLayers, meta = (DeprecatedFunction, DeprecationMessage = "Use GetDataLayerRuntimeStateByLabel instead"))
	EDataLayerState GetDataLayerStateByLabel(const FName& InDataLayerLabel) const { return (EDataLayerState)GetDataLayerRuntimeStateByLabel(InDataLayerLabel); }

	UE_DEPRECATED(5.0, "Use GetDataLayerRuntimeState() instead.")
	EDataLayerState GetDataLayerState(const UDataLayerInstance* InDataLayer) const { return (EDataLayerState)GetDataLayerRuntimeState(InDataLayer); }

	UE_DEPRECATED(5.0, "Use GetDataLayerRuntimeStateByName() instead.")
	EDataLayerState GetDataLayerStateByName(const FName& InDataLayerName) const { return (EDataLayerState)GetDataLayerRuntimeStateByName(InDataLayerName); }

	UE_DEPRECATED(5.0, "Use IsAnyDataLayerInEffectiveRuntimeState() instead.")
	bool IsAnyDataLayerInState(const TArray<FName>& InDataLayerNames, EDataLayerState InState) const { return IsAnyDataLayerInEffectiveRuntimeState(InDataLayerNames, (EDataLayerRuntimeState)InState); }

	PRAGMA_ENABLE_DEPRECATION_WARNINGS

	UE_DEPRECATED(5.0, "GetActiveDataLayerNames will be removed.")
	UFUNCTION(BlueprintCallable, Category = DataLayers, meta = (DeprecatedFunction, DeprecationMessage = "GetActiveDataLayerNames will be removed."))
	const TSet<FName>& GetActiveDataLayerNames() const { return GetEffectiveActiveDataLayerNames(); }

	UE_DEPRECATED(5.0, "GetLoadedDataLayerNames will be removed.")
	UFUNCTION(BlueprintCallable, Category = DataLayers, meta = (DeprecatedFunction, DeprecationMessage = "GetLoadedDataLayerNames will be removed."))
	const TSet<FName>& GetLoadedDataLayerNames() const { return GetEffectiveLoadedDataLayerNames(); }

	UE_DEPRECATED(5.1, "Use GetDataLayerFromAsset() instead.")
	UFUNCTION(BlueprintCallable, Category = DataLayers)
	UDataLayerInstance* GetDataLayer(const FActorDataLayer& InDataLayer) const;

	UE_DEPRECATED(5.1, "Use GetDataLayerFromAsset() instead.")
	UFUNCTION(BlueprintCallable, Category = DataLayers)
	UDataLayerInstance* GetDataLayerFromName(FName InDataLayerName) const;

	UE_DEPRECATED(5.1, "Use GetDataLayerFromAsset() instead.")
	UFUNCTION(BlueprintCallable, Category = DataLayers)
	UDataLayerInstance* GetDataLayerFromLabel(FName InDataLayerLabel) const;

	UE_DEPRECATED(5.1, "Use GetDataLayerInstanceRuntimeState() instead.")
	UFUNCTION(BlueprintCallable, Category = DataLayers)
	EDataLayerRuntimeState GetDataLayerRuntimeState(const FActorDataLayer& InDataLayer) const;

	UE_DEPRECATED(5.1, "Use GetDataLayerInstanceRuntimeState() instead.")
	UFUNCTION(BlueprintCallable, Category = DataLayers)
	EDataLayerRuntimeState GetDataLayerRuntimeStateByLabel(const FName& InDataLayerLabel) const;

	UE_DEPRECATED(5.1, "Use GetDataLayerInstanceEffectiveRuntimeState() instead.")
	UFUNCTION(BlueprintCallable, Category = DataLayers)
	EDataLayerRuntimeState GetDataLayerEffectiveRuntimeState(const FActorDataLayer& InDataLayer) const;

	UE_DEPRECATED(5.1, "Use GetDataLayerInstanceEffectiveRuntimeState() instead.")
	UFUNCTION(BlueprintCallable, Category = DataLayers)
	EDataLayerRuntimeState GetDataLayerEffectiveRuntimeStateByLabel(const FName& InDataLayerLabel) const;

	UE_DEPRECATED(5.1, "Use SetDataLayerRuntimeState() with UDataLayerAsset* overload instead.")
	UFUNCTION(BlueprintCallable, Category = DataLayers, BlueprintAuthorityOnly)
	void SetDataLayerRuntimeState(const FActorDataLayer& InDataLayer, EDataLayerRuntimeState InState, bool bInIsRecursive = false);

	UE_DEPRECATED(5.1, "Use SetDataLayerInstanceRuntimeState() instead.")
	UFUNCTION(BlueprintCallable, Category = DataLayers, BlueprintAuthorityOnly)
	void SetDataLayerRuntimeStateByLabel(const FName& InDataLayerLabel, EDataLayerRuntimeState InState, bool bInIsRecursive = false);

	//~ End Deprecated

#if WITH_EDITOR
public:
	uint32 GetDataLayerEditorContextHash() const;

private:
#endif

private:

	/** Console command used to toggle activation of a DataLayer */
	static class FAutoConsoleCommand ToggleDataLayerActivation;

	/** Data layers load time */
	mutable TMap<const UDataLayerInstance*, double> ActiveDataLayersLoadTime;

	/** Console command used to set Runtime DataLayer state*/
	static class FAutoConsoleCommand SetDataLayerRuntimeStateCommand;
};