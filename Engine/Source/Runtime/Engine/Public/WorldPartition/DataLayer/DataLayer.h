// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/Object.h"
#include "ActorDataLayer.h"
#include "Math/Color.h"

#include "DataLayer.generated.h"

UENUM(BlueprintType, meta = (ScriptName = "DataLayerStateType"))
enum class UE_DEPRECATED(5.0, "Use EDataLayerRuntimeState instead.") EDataLayerState : uint8
{
	Unloaded,
	Loaded,
	Activated
};

UENUM(BlueprintType)
enum class EDataLayerRuntimeState : uint8
{
	Unloaded,
	Loaded,
	Activated
};

const inline TCHAR* GetDataLayerRuntimeStateName(EDataLayerRuntimeState State)
{
	switch(State)
	{
	case EDataLayerRuntimeState::Unloaded: return TEXT("Unloaded");
	case EDataLayerRuntimeState::Loaded: return TEXT("Loaded");
	case EDataLayerRuntimeState::Activated: return TEXT("Activated");
	default: check(0);
	}
	return TEXT("Invalid");
}

// Used for debugging
bool inline GetDataLayerRuntimeStateFromName(const FString& InStateName, EDataLayerRuntimeState& OutState)
{
	if (InStateName.Equals(GetDataLayerRuntimeStateName(EDataLayerRuntimeState::Unloaded), ESearchCase::IgnoreCase))
	{
		OutState = EDataLayerRuntimeState::Unloaded;
		return true;
	}
	else if (InStateName.Equals(GetDataLayerRuntimeStateName(EDataLayerRuntimeState::Loaded), ESearchCase::IgnoreCase))
	{
		OutState = EDataLayerRuntimeState::Loaded;
		return true;
	}
	else if (InStateName.Equals(GetDataLayerRuntimeStateName(EDataLayerRuntimeState::Activated), ESearchCase::IgnoreCase))
	{
		OutState = EDataLayerRuntimeState::Activated;
		return true;
	}
	return false;
}

static_assert(EDataLayerRuntimeState::Unloaded < EDataLayerRuntimeState::Loaded && EDataLayerRuntimeState::Loaded < EDataLayerRuntimeState::Activated, "Streaming Query code is dependent on this being true");

UCLASS(Config = Engine, PerObjectConfig, Within = WorldDataLayers, BlueprintType, AutoCollapseCategories = ("Data Layer|Advanced"), AutoExpandCategories = ("Data Layer|Editor", "Data Layer|Advanced|Runtime"))
class ENGINE_API UDataLayer : public UObject
{
	GENERATED_UCLASS_BODY()

public:
#if WITH_EDITOR
	virtual bool CanEditChange(const FProperty* Property) const;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;

	void SetDataLayerLabel(FName DataLayerLabel);
	void SetVisible(bool bIsVisible);
	void SetIsInitiallyVisible(bool bIsInitiallyVisible);
	void SetIsRuntime(bool bIsRuntime);
	void SetIsLoadedInEditor(bool bIsLoadedInEditor, bool bFromUserChange);
	void SetIsLocked(bool bInIsLocked) { bIsLocked = bInIsLocked; }

	bool IsLocked() const;
	bool IsInitiallyLoadedInEditor() const { return bIsInitiallyLoadedInEditor; }
	bool IsLoadedInEditor() const { return bIsLoadedInEditor; }
	bool IsEffectiveLoadedInEditor() const;
	bool IsLoadedInEditorChangedByUserOperation() const { return bIsLoadedInEditorChangedByUserOperation; }
	void ClearLoadedInEditorChangedByUserOperation() { bIsLoadedInEditorChangedByUserOperation = false; }

	bool CanParent(const UDataLayer* InParent) const;
	void SetParent(UDataLayer* InParent);
	void SetChildParent(UDataLayer* InParent);

	static FText GetDataLayerText(const UDataLayer* InDataLayer);
	const TCHAR* GetDataLayerIconName() const;
#endif
	const TArray<TObjectPtr<UDataLayer>>& GetChildren() const { return Children; }
	void ForEachChild(TFunctionRef<bool(const UDataLayer*)> Operation) const;

	const UDataLayer* GetParent() const { return Parent; }
	UDataLayer* GetParent() { return Parent; }

	virtual void PostLoad() override;

	UFUNCTION(Category = "Data Layer", BlueprintCallable)
	bool Equals(const FActorDataLayer& ActorDataLayer) const { return ActorDataLayer.Name == GetFName(); }

	UFUNCTION(Category = "Data Layer", BlueprintCallable)
	FName GetDataLayerLabel() const { return DataLayerLabel; }

	UFUNCTION(Category = "Data Layer|Editor", BlueprintCallable)
	bool IsInitiallyVisible() const;

	UFUNCTION(Category = "Data Layer|Editor", BlueprintCallable)
	bool IsVisible() const;

	UFUNCTION(Category = "Data Layer|Editor", BlueprintCallable)
	bool IsEffectiveVisible() const;

	UFUNCTION(Category = "Data Layer|Runtime", BlueprintCallable)
	bool IsRuntime() const { return bIsRuntime; }
	
	UFUNCTION(Category = "Data Layer|Runtime", BlueprintCallable)
	EDataLayerRuntimeState GetInitialRuntimeState() const { return IsRuntime() ? InitialRuntimeState : EDataLayerRuntimeState::Unloaded; }

	UFUNCTION(Category = "Data Layer|Runtime", BlueprintCallable)
	FColor GetDebugColor() const { return DebugColor; }

	/** Returns a sanitized version of the provided Data Layer Label */
	static FName GetSanitizedDataLayerLabel(FName DataLayerLabel);

	//~ Begin Deprecated

	UE_DEPRECATED(5.0, "Use IsRuntime() instead.")
	UFUNCTION(Category = "Data Layer|Runtime", BlueprintCallable, meta = (DeprecatedFunction, DeprecationMessage = "Use IsRuntime instead"))
	bool IsDynamicallyLoaded() const { return IsRuntime(); }

	UE_DEPRECATED(5.0, "Use GetInitialRuntimeState() instead.")
	UFUNCTION(Category = "Data Layer|Runtime", BlueprintCallable, meta = (DeprecatedFunction, DeprecationMessage = "Use GetInitialRuntimeState instead"))
	bool IsInitiallyActive() const { return IsRuntime() && GetInitialRuntimeState() == EDataLayerRuntimeState::Activated; }

	PRAGMA_DISABLE_DEPRECATION_WARNINGS // Suppress compiler warning on override of deprecated function
	UE_DEPRECATED(5.0, "Use GetInitialRuntimeState() instead.")
	UFUNCTION(Category = "Data Layer|Runtime", BlueprintCallable, meta = (DeprecatedFunction, DeprecationMessage = "Use GetInitialRuntimeState instead"))
	EDataLayerState GetInitialState() const { return (EDataLayerState)GetInitialRuntimeState(); }
	PRAGMA_ENABLE_DEPRECATION_WARNINGS

	//~ End Deprecated
private:

	void AddChild(UDataLayer* DataLayer);
#if WITH_EDITOR
	void RemoveChild(UDataLayer* DataLayer);
	void PropagateIsRuntime();
#endif

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	uint32 bIsInitiallyActive_DEPRECATED : 1;

	/** Whether actors associated with the DataLayer are visible in the viewport */
	UPROPERTY(Transient)
	uint32 bIsVisible : 1;

	/** Whether actors associated with the Data Layer should be initially visible in the viewport when loading the map */
	UPROPERTY(Category = "Data Layer|Editor", EditAnywhere)
	uint32 bIsInitiallyVisible : 1;

	/** Determines the default value of the data layer's loaded state in editor if it hasn't been changed in data layer outliner by the user */
	UPROPERTY(Category = "Data Layer|Editor", EditAnywhere, meta = (DisplayName = "Is Initially Loaded"))
	uint32 bIsInitiallyLoadedInEditor : 1;

	/** Wheter the data layer is loaded in editor (user setting) */
	UPROPERTY(Transient)
	uint32 bIsLoadedInEditor : 1;

	/** Whether this data layer editor visibility was changed by a user operation */
	UPROPERTY(Transient)
	uint32 bIsLoadedInEditorChangedByUserOperation : 1;

	/** Whether this data layer is locked, which means the user can't change actors assignation, remove or rename it */
	UPROPERTY()
	uint32 bIsLocked : 1;
#endif

	/** The display name of the Data Layer */
	UPROPERTY()
	FName DataLayerLabel;

	/** Whether the Data Layer affects actor runtime loading */
	UPROPERTY(Category = "Data Layer|Advanced", EditAnywhere)
	uint32 bIsRuntime : 1;

	UPROPERTY(Category = "Data Layer|Advanced|Runtime", EditAnywhere, meta = (EditConditionHides, EditCondition = "bIsRuntime"))
	EDataLayerRuntimeState InitialRuntimeState;

	UPROPERTY(Category = "Data Layer|Advanced|Runtime", EditAnywhere, meta = (EditConditionHides, EditCondition = "bIsRuntime"))
	FColor DebugColor;

	UPROPERTY()
	TObjectPtr<UDataLayer> Parent;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UDataLayer>> Children;
};