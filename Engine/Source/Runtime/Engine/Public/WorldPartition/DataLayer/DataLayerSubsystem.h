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

// Deprecate
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnDataLayerActivationStateChanged, const UDataLayer*, DataLayer, bool, bIsActive);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnDataLayerStateChanged, const UDataLayer*, DataLayer, EDataLayerState, State);

UCLASS()
class ENGINE_API UDataLayerSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	UDataLayerSubsystem();

	//~ Begin USubsystem Interface.
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem Interface.

	//~ Begin UWorldSubsystem Interface.
	virtual void PostInitialize() override;
	//~ End UWorldSubsystem Interface.

	//~ Begin Blueprint callable functions
	UFUNCTION(BlueprintCallable, Category = DataLayers, meta=(DeprecatedFunction, DeprecationMessage="Use SetDataLayerState instead"))
	void ActivateDataLayer(const FActorDataLayer& InDataLayer, bool bInActivate);
	
	UFUNCTION(BlueprintCallable, Category = DataLayers, meta = (DeprecatedFunction, DeprecationMessage = "Use SetDataLayerStateByLabel instead"))
	void ActivateDataLayerByLabel(const FName& InDataLayerLabel, bool bInActivate);

	UFUNCTION(BlueprintCallable, Category = DataLayers, meta = (DeprecatedFunction, DeprecationMessage = "Use GetDataLayerState instead"))
	bool IsDataLayerActive(const FActorDataLayer& InDataLayer) const;

	UFUNCTION(BlueprintCallable, Category = DataLayers, meta = (DeprecatedFunction, DeprecationMessage = "Use GetDataLayerStateByLabel instead"))
	bool IsDataLayerActiveByLabel(const FName& InDataLayerLabel) const;

	UFUNCTION(BlueprintCallable, Category = DataLayers)
	void SetDataLayerState(const FActorDataLayer& InDataLayer, EDataLayerState InState);

	UFUNCTION(BlueprintCallable, Category = DataLayers)
	void SetDataLayerStateByLabel(const FName& InDataLayerLabel, EDataLayerState InState);
		
	UFUNCTION(BlueprintCallable, Category = DataLayers)
	EDataLayerState GetDataLayerState(const FActorDataLayer& InDataLayer) const;

	UFUNCTION(BlueprintCallable, Category = DataLayers)
	EDataLayerState GetDataLayerStateByLabel(const FName& InDataLayerLabel) const;

	UPROPERTY(BlueprintAssignable)
	FOnDataLayerActivationStateChanged OnDataLayerActivationStateChanged;

	UPROPERTY(BlueprintAssignable)
	FOnDataLayerStateChanged OnDataLayerStateChanged;
	//~ End Blueprint callable functions

	UDataLayer* GetDataLayerFromLabel(const FName& InDataLayerLabel) const;
	UDataLayer* GetDataLayerFromName(const FName& InDataLayerName) const;
	void SetDataLayerState(const UDataLayer* InDataLayer, EDataLayerState InState);
	void SetDataLayerStateByName(const FName& InDataLayerName, EDataLayerState InState);
	EDataLayerState GetDataLayerState(const UDataLayer* InDataLayer) const;
	EDataLayerState GetDataLayerStateByName(const FName& InDataLayerName) const;
	
	bool IsAnyDataLayerInState(const TArray<FName>& InDataLayerNames, EDataLayerState InState) const;
	
	void Draw(class UCanvas* Canvas, class APlayerController* PC);

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

	TSet<FName> ActiveDataLayerNames;
	TSet<FName> LoadedDataLayerNames;

	FDelegateHandle	DrawHandle;
};