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

UCLASS(Config = Engine, PerObjectConfig, Within = WorldDataLayers, BlueprintType)
class ENGINE_API UDataLayer : public UObject
{
	GENERATED_UCLASS_BODY()

public:
#if WITH_EDITOR
	void SetDataLayerLabel(FName InDataLayerLabel);
	void SetVisible(bool bInIsVisible);
	void SetIsDynamicallyLoaded(bool bInIsDynamicallyLoaded);
	void SetIsDynamicallyLoadedInEditor(bool bInIsDynamicallyLoadedInEditor);
	void SetIsLocked(bool bInIsLocked) { bIsLocked = bInIsLocked; }

	bool IsDynamicallyLoadedInEditor() const { return !IsDynamicallyLoaded() || bIsDynamicallyLoadedInEditor; }
	bool ShouldGenerateHLODs() const { return IsDynamicallyLoaded() && bGeneratesHLODs; }
	class UHLODLayer* GetDefaultHLODLayer() const { return ShouldGenerateHLODs() ? DefaultHLODLayer : nullptr; }

	static FText GetDataLayerText(const UDataLayer* InDataLayer);

	bool IsLocked() const { return bIsLocked; }
#endif
	virtual void PostLoad() override;

	UFUNCTION(Category = DataLayer, BlueprintCallable)
	bool Equals(const FActorDataLayer& ActorDataLayer) const { return ActorDataLayer.Name == GetFName(); }
	
	UFUNCTION(Category = DataLayer, BlueprintCallable)
	FName GetDataLayerLabel() const  { return DataLayerLabel; }

	UFUNCTION(Category = DataLayer, BlueprintCallable)
	bool IsVisible() const { return bIsVisible; }

	UFUNCTION(Category = DataLayer, BlueprintCallable)
	bool IsDynamicallyLoaded() const { return bIsDynamicallyLoaded; }

	UFUNCTION(Category = DataLayer, BlueprintCallable, meta = (DeprecatedFunction, DeprecationMessage = "Use GetInitialState instead"))
	bool IsInitiallyActive() const { return IsDynamicallyLoaded() && GetInitialState() == EDataLayerState::Activated; }	

	UFUNCTION(Category = DataLayer, BlueprintCallable)
	EDataLayerState GetInitialState() const { return IsDynamicallyLoaded() ? InitialState : EDataLayerState::Unloaded; }

private:
	/** The display name of the DataLayer */
	UPROPERTY()
	FName DataLayerLabel;

	UPROPERTY(Category = DataLayer, EditAnywhere, meta = (EditConditionHides, EditCondition = "bIsDynamicallyLoaded"))
	EDataLayerState InitialState;

	/** Whether actors associated with the DataLayer are visible in the viewport */
	UPROPERTY(Category = DataLayer, EditAnywhere)
	uint32 bIsVisible : 1;

	/** Whether the DataLayer affects actor runtime loading */
	UPROPERTY(Category = DataLayer, EditAnywhere)
	uint32 bIsDynamicallyLoaded : 1;

#if WITH_EDITORONLY_DATA
	/** Whether a dynamically loaded DataLayer should be initially active at runtime */
	UPROPERTY()
	uint32 bIsInitiallyActive_DEPRECATED : 1;
	
	/** Whether the DataLayer affects actor editor loading */
	UPROPERTY(Transient)
	uint32 bIsDynamicallyLoadedInEditor : 1;

	/** Whether this data layer is locked, which means the user can't change actors assignation, remove or rename it */
	UPROPERTY()
	uint32 bIsLocked : 1;

	/** Whether HLODs should be generated for actors in this layer */
	UPROPERTY(Category = DataLayer, EditAnywhere, meta=(EditConditionHides, EditCondition="bIsDynamicallyLoaded"))
	uint32 bGeneratesHLODs : 1;

	// Default HLOD layer
	UPROPERTY(Category = DataLayer, EditAnywhere, meta=(EditConditionHides, EditCondition="bIsDynamicallyLoaded && bGeneratesHLODs"))
	TObjectPtr<class UHLODLayer> DefaultHLODLayer;
#endif
};