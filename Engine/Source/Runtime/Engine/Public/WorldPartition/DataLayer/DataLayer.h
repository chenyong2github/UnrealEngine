// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/Object.h"
#include "WorldDataLayers.h"
#include "ActorDataLayer.h"

#include "DataLayer.generated.h"

UENUM(BlueprintType)
enum class EDataLayerState : uint8
{
	Unloaded,
	Loaded,
	Activated
};

static_assert(EDataLayerState::Unloaded < EDataLayerState::Loaded && EDataLayerState::Loaded < EDataLayerState::Activated, "Streaming Query code is dependent on this being true");

UCLASS(Config = Engine, PerObjectConfig, Within = WorldDataLayers, BlueprintType)
class ENGINE_API UDataLayer : public UObject
{
	GENERATED_UCLASS_BODY()

public:
#if WITH_EDITOR
	void SetDataLayerLabel(FName InDataLayerLabel);
	void SetVisible(bool bInIsVisible);
	void SetIsInitiallyVisible(bool bInIsInitiallyVisible);
	void SetIsDynamicallyLoaded(bool bInIsDynamicallyLoaded);
	void SetIsDynamicallyLoadedInEditor(bool bInIsDynamicallyLoadedInEditor);
	void SetIsLocked(bool bInIsLocked) { bIsLocked = bInIsLocked; }

	bool IsDynamicallyLoadedInEditor() const { return bIsDynamicallyLoadedInEditor; }
	bool ShouldGenerateHLODs() const { return IsDynamicallyLoaded() && bGeneratesHLODs; }
	class UHLODLayer* GetDefaultHLODLayer() const { return ShouldGenerateHLODs() ? DefaultHLODLayer : nullptr; }

	static FText GetDataLayerText(const UDataLayer* InDataLayer);

	bool IsLocked() const { return bIsLocked; }
#endif
	virtual void PostLoad() override;

	UFUNCTION(Category = "Data Layer", BlueprintCallable)
	bool Equals(const FActorDataLayer& ActorDataLayer) const { return ActorDataLayer.Name == GetFName(); }
	
	UFUNCTION(Category = "Data Layer", BlueprintCallable)
	FName GetDataLayerLabel() const  { return DataLayerLabel; }

	UFUNCTION(Category = "Data Layer - Editor", BlueprintCallable)
	bool IsInitiallyVisible() const;

	UFUNCTION(Category = "Data Layer - Editor", BlueprintCallable)
	bool IsVisible() const;

	UFUNCTION(Category = "Data Layer - Runtime", BlueprintCallable)
	bool IsDynamicallyLoaded() const { return bIsDynamicallyLoaded; }

	UFUNCTION(Category = "Data Layer - Runtime", BlueprintCallable, meta = (DeprecatedFunction, DeprecationMessage = "Use GetInitialState instead"))
	bool IsInitiallyActive() const { return IsDynamicallyLoaded() && GetInitialState() == EDataLayerState::Activated; }	

	UFUNCTION(Category = "Data Layer - Runtime", BlueprintCallable)
	EDataLayerState GetInitialState() const { return IsDynamicallyLoaded() ? InitialState : EDataLayerState::Unloaded; }

private:
#if WITH_EDITORONLY_DATA
	UPROPERTY()
	uint32 bIsInitiallyActive_DEPRECATED : 1;

	/** Whether actors associated with the DataLayer are visible in the viewport */
	UPROPERTY(Transient)
	uint32 bIsVisible : 1;

	/** Whether actors associated with the Data Layer should be initially visible in the viewport when loading the map */
	UPROPERTY(Category = "Data Layer - Editor", EditAnywhere)
	uint32 bIsInitiallyVisible : 1;

	/** Whether the Data Layer affects actor editor loading */
	UPROPERTY(Transient)
	uint32 bIsDynamicallyLoadedInEditor : 1;

	/** Whether this data layer is locked, which means the user can't change actors assignation, remove or rename it */
	UPROPERTY()
	uint32 bIsLocked : 1;

	/** Whether HLODs should be generated for actors in this layer */
	UPROPERTY(Category = "Data Layer - HLODs", EditAnywhere, meta = (EditConditionHides, EditCondition = "bIsDynamicallyLoaded"))
	uint32 bGeneratesHLODs : 1;

	// Default HLOD layer
	UPROPERTY(Category = "Data Layer - HLODs", EditAnywhere, meta = (EditConditionHides, EditCondition = "bIsDynamicallyLoaded && bGeneratesHLODs"))
	TObjectPtr<class UHLODLayer> DefaultHLODLayer;
#endif

	/** The display name of the Data Layer */
	UPROPERTY()
	FName DataLayerLabel;

	UPROPERTY(Category = "Data Layer - Runtime", EditAnywhere, meta = (EditConditionHides, EditCondition = "bIsDynamicallyLoaded"))
	EDataLayerState InitialState;

	/** Whether the Data Layer affects actor runtime loading */
	UPROPERTY(Category = "Data Layer - Runtime", EditAnywhere)
	uint32 bIsDynamicallyLoaded : 1;
};