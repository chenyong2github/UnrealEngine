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

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnDataLayerActivationStateChanged, const UDataLayer*, DataLayer, bool, bIsActive);

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
	UFUNCTION(BlueprintCallable, Category = DataLayers)
	void ActivateDataLayer(const FActorDataLayer& InDataLayer, bool bInActivate);
	
	UFUNCTION(BlueprintCallable, Category = DataLayers)
	void ActivateDataLayerByLabel(const FName& InDataLayerLabel, bool bInActivate);

	UFUNCTION(BlueprintCallable, Category = DataLayers)
	bool IsDataLayerActive(const FActorDataLayer& InDataLayer) const;

	UFUNCTION(BlueprintCallable, Category = DataLayers)
	bool IsDataLayerActiveByLabel(const FName& InDataLayerName) const;

	UPROPERTY(BlueprintAssignable)
	FOnDataLayerActivationStateChanged OnDataLayerActivationStateChanged;
	//~ End Blueprint callable functions

	UDataLayer* GetDataLayerFromLabel(const FName& InDataLayerLabel) const;
	UDataLayer* GetDataLayerFromName(const FName& InDataLayerName) const;
	void ActivateDataLayer(const UDataLayer* InDataLayer, bool bInActivate);
	void ActivateDataLayerByName(const FName& InDataLayerName, bool bInActivate);
	bool IsDataLayerActive(const UDataLayer* InDataLayer) const;
	bool IsDataLayerActiveByName(const FName& InDataLayerName) const;
	bool IsAnyDataLayerActive(const TArray<FName>& InDataLayerNames) const;

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

	FDelegateHandle	DrawHandle;
};