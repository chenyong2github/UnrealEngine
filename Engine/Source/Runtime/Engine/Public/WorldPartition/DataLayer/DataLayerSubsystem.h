// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Subsystems/WorldSubsystem.h"
#include "WorldPartition/DataLayer/ActorDataLayer.h"
#include "WorldPartition/DataLayer/DataLayer.h"
#include "WorldPartition/DataLayer/DataLayerEditorContext.h"
#include "DataLayerSubsystem.generated.h"

/**
 * UDataLayerSubsystem
 */

class UCanvas;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnDataLayerRuntimeStateChanged, const UDataLayer*, DataLayer, EDataLayerRuntimeState, State);

UCLASS()
class ENGINE_API UDataLayerSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	UDataLayerSubsystem();

	//~ Begin USubsystem Interface.
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	//~ End USubsystem Interface.

	//~ Begin Blueprint callable functions

	/** Find a Data Layer by name. */
	UFUNCTION(BlueprintCallable, Category = DataLayers)
	UDataLayer* GetDataLayer(const FActorDataLayer& InDataLayer) const;

	/** Find a Data Layer by label. */
	UFUNCTION(BlueprintCallable, Category = DataLayers)
	UDataLayer* GetDataLayerFromLabel(FName InDataLayerLabel) const;

	/** Find a Data Layer by name. */
	UFUNCTION(BlueprintCallable, Category = DataLayers)
	UDataLayer* GetDataLayerFromName(FName InDataLayerName) const;

	/** Set the Data Layer state using its name. */
	UFUNCTION(BlueprintCallable, Category = DataLayers, BlueprintAuthorityOnly)
	void SetDataLayerRuntimeState(const FActorDataLayer& InDataLayer, EDataLayerRuntimeState InState, bool bInIsRecursive = false);

	/** Set the Data Layer state using its label. */
	UFUNCTION(BlueprintCallable, Category = DataLayers, BlueprintAuthorityOnly)
	void SetDataLayerRuntimeStateByLabel(const FName& InDataLayerLabel, EDataLayerRuntimeState InState, bool bInIsRecursive = false);
		
	/** Get the Data Layer state using its name. */
	UFUNCTION(BlueprintCallable, Category = DataLayers)
	EDataLayerRuntimeState GetDataLayerRuntimeState(const FActorDataLayer& InDataLayer) const;

	/** Get the Data Layer state using its label. */
	UFUNCTION(BlueprintCallable, Category = DataLayers)
	EDataLayerRuntimeState GetDataLayerRuntimeStateByLabel(const FName& InDataLayerLabel) const;

	/** Get the Data Layer effective state using its name. */
	UFUNCTION(BlueprintCallable, Category = DataLayers)
	EDataLayerRuntimeState GetDataLayerEffectiveRuntimeState(const FActorDataLayer& InDataLayer) const;

	/** Get the Data Layer effective state using its label. */
	UFUNCTION(BlueprintCallable, Category = DataLayers)
	EDataLayerRuntimeState GetDataLayerEffectiveRuntimeStateByLabel(const FName& InDataLayerLabel) const;

	/** Called when a Data Layer changes state. */
	UPROPERTY(BlueprintAssignable)
	FOnDataLayerRuntimeStateChanged OnDataLayerRuntimeStateChanged;

	//~ End Blueprint callable functions

	void SetDataLayerRuntimeState(const UDataLayer* InDataLayer, EDataLayerRuntimeState InState, bool bInIsRecursive = false);
	void SetDataLayerRuntimeStateByName(const FName& InDataLayerName, EDataLayerRuntimeState InState, bool bInIsRecursive = false);
	EDataLayerRuntimeState GetDataLayerRuntimeState(const UDataLayer* InDataLayer) const;
	EDataLayerRuntimeState GetDataLayerRuntimeStateByName(const FName& InDataLayerName) const;
	EDataLayerRuntimeState GetDataLayerEffectiveRuntimeState(const UDataLayer* InDataLayer) const;
	EDataLayerRuntimeState GetDataLayerEffectiveRuntimeStateByName(const FName& InDataLayerName) const;
	bool IsAnyDataLayerInEffectiveRuntimeState(const TArray<FName>& InDataLayerNames, EDataLayerRuntimeState InState) const;
	const TSet<FName>& GetEffectiveActiveDataLayerNames() const;
	const TSet<FName>& GetEffectiveLoadedDataLayerNames() const;

	void GetDataLayerDebugColors(TMap<FName, FColor>& OutMapping) const;
	void DrawDataLayersStatus(UCanvas* Canvas, FVector2D& Offset) const;
	static TArray<UDataLayer*> ConvertArgsToDataLayers(UWorld* World, const TArray<FString>& InArgs);

	void DumpDataLayers(FOutputDevice& OutputDevice) const;

	//~ Begin Deprecated
	
	PRAGMA_DISABLE_DEPRECATION_WARNINGS // Suppress compiler warning on override of deprecated function

	UE_DEPRECATED(5.0, "Use SetDataLayerRuntimeState() instead.")
	UFUNCTION(BlueprintCallable, Category = DataLayers, BlueprintAuthorityOnly, meta = (DeprecatedFunction, DeprecationMessage = "Use SetDataLayerRuntimeState instead"))
	void SetDataLayerState(const FActorDataLayer& InDataLayer, EDataLayerState InState) { SetDataLayerRuntimeState(InDataLayer, (EDataLayerRuntimeState)InState); }

	UE_DEPRECATED(5.0, "Use SetDataLayerRuntimeStateByLabel() instead.")
	UFUNCTION(BlueprintCallable, Category = DataLayers, BlueprintAuthorityOnly, meta = (DeprecatedFunction, DeprecationMessage = "Use SetDataLayerRuntimeStateByLabel instead"))
	void SetDataLayerStateByLabel(const FName& InDataLayerLabel, EDataLayerState InState) { SetDataLayerRuntimeStateByLabel(InDataLayerLabel, (EDataLayerRuntimeState)InState); }

	UE_DEPRECATED(5.0, "Use SetDataLayerRuntimeState() instead.")
	void SetDataLayerState(const UDataLayer* InDataLayer, EDataLayerState InState) { SetDataLayerRuntimeState(InDataLayer, (EDataLayerRuntimeState)InState); }

	UE_DEPRECATED(5.0, "Use SetDataLayerRuntimeStateByName() instead.")
	void SetDataLayerStateByName(const FName& InDataLayerName, EDataLayerState InState) { SetDataLayerRuntimeStateByName(InDataLayerName, (EDataLayerRuntimeState)InState); }

	UE_DEPRECATED(5.0, "Use GetDataLayerRuntimeState() instead.")
	UFUNCTION(BlueprintCallable, Category = DataLayers, meta = (DeprecatedFunction, DeprecationMessage = "Use GetDataLayerRuntimeState instead"))
	EDataLayerState GetDataLayerState(const FActorDataLayer& InDataLayer) const { return (EDataLayerState)GetDataLayerRuntimeState(InDataLayer); }

	UE_DEPRECATED(5.0, "Use GetDataLayerRuntimeStateByLabel() instead.")
	UFUNCTION(BlueprintCallable, Category = DataLayers, meta = (DeprecatedFunction, DeprecationMessage = "Use GetDataLayerRuntimeStateByLabel instead"))
	EDataLayerState GetDataLayerStateByLabel(const FName& InDataLayerLabel) const { return (EDataLayerState)GetDataLayerRuntimeStateByLabel(InDataLayerLabel); }

	UE_DEPRECATED(5.0, "Use GetDataLayerRuntimeState() instead.")
	EDataLayerState GetDataLayerState(const UDataLayer* InDataLayer) const { return (EDataLayerState)GetDataLayerRuntimeState(InDataLayer); }

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
	//~ End Deprecated

#if WITH_EDITOR
public:
	FORCEINLINE const FDataLayerEditorContext& GetDataLayerEditorContext() const { return DataLayerEditorContext; }
	FORCEINLINE bool HasDataLayerEditorContext() const { return !DataLayerEditorContext.IsEmpty(); }

private:
	mutable FDataLayerEditorContext DataLayerEditorContext;
	friend struct FScopeChangeDataLayerEditorContext;
#endif

private:

	/** Console command used to toggle activation of a DataLayer */
	static class FAutoConsoleCommand ToggleDataLayerActivation;

	/** Data layers load time */
	mutable TMap<UDataLayer*, double> ActiveDataLayersLoadTime;

	/** Console command used to set Runtime DataLayer state*/
	static class FAutoConsoleCommand SetDataLayerRuntimeStateCommand;
};