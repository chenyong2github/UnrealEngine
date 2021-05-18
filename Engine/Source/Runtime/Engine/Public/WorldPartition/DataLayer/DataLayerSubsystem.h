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
	//~ End USubsystem Interface.

	//~ Begin Blueprint callable functions
	UFUNCTION(BlueprintCallable, Category = DataLayers)
	void SetDataLayerState(const FActorDataLayer& InDataLayer, EDataLayerState InState);

	UFUNCTION(BlueprintCallable, Category = DataLayers)
	UDataLayer* GetDataLayer(const FActorDataLayer& InDataLayer) const;

	UFUNCTION(BlueprintCallable, Category = DataLayers)
	void SetDataLayerStateByLabel(const FName& InDataLayerLabel, EDataLayerState InState);
		
	UFUNCTION(BlueprintCallable, Category = DataLayers)
	EDataLayerState GetDataLayerState(const FActorDataLayer& InDataLayer) const;

	UFUNCTION(BlueprintCallable, Category = DataLayers)
	EDataLayerState GetDataLayerStateByLabel(const FName& InDataLayerLabel) const;

	UPROPERTY(BlueprintAssignable)
	FOnDataLayerStateChanged OnDataLayerStateChanged;

	UFUNCTION(BlueprintCallable, Category = DataLayers)
	UDataLayer* GetDataLayerFromLabel(FName InDataLayerLabel) const;

	UFUNCTION(BlueprintCallable, Category = DataLayers)
	UDataLayer* GetDataLayerFromName(FName InDataLayerName) const;
	//~ End Blueprint callable functions

	UFUNCTION(BlueprintCallable, Category = DataLayers)
	const TSet<FName>& GetActiveDataLayerNames() const;

	UFUNCTION(BlueprintCallable, Category = DataLayers)
	const TSet<FName>& GetLoadedDataLayerNames() const;

	void SetDataLayerState(const UDataLayer* InDataLayer, EDataLayerState InState);
	void SetDataLayerStateByName(const FName& InDataLayerName, EDataLayerState InState);
	EDataLayerState GetDataLayerState(const UDataLayer* InDataLayer) const;
	EDataLayerState GetDataLayerStateByName(const FName& InDataLayerName) const;
	
	bool IsAnyDataLayerInState(const TArray<FName>& InDataLayerNames, EDataLayerState InState) const;
	
	void GetDataLayerDebugColors(TMap<FName, FColor>& OutMapping) const;
	void DrawDataLayersStatus(UCanvas* Canvas, FVector2D& Offset) const;
	static TArray<UDataLayer*> ConvertArgsToDataLayers(UWorld* World, const TArray<FString>& InArgs);

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
};