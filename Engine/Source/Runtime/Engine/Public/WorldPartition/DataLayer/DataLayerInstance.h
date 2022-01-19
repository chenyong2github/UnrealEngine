// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/Object.h"
#include "WorldPartition/DataLayer/DataLayerAsset.h"
#include "WorldPartition/DataLayer/DataLayer.h"

#include "DataLayerInstance.generated.h"

UCLASS(Config = Engine, PerObjectConfig, Within = WorldDataLayers, BlueprintType, AutoCollapseCategories = ("Data Layer|Advanced"), AutoExpandCategories = ("Data Layer|Editor", "Data Layer|Advanced|Runtime"))
class ENGINE_API UDataLayerInstance : public UObject
{
	GENERATED_UCLASS_BODY()

	friend class UDataLayerConversionInfo;

public:
	virtual void PostLoad() override;

#if WITH_EDITOR
	void OnCreated(const UDataLayerAsset* Asset);

	void SetVisible(bool bIsVisible);
	void SetIsInitiallyVisible(bool bIsInitiallyVisible);
	void SetIsLoadedInEditor(bool bIsLoadedInEditor, bool bFromUserChange);
	void SetIsLocked(bool bInIsLocked) { bIsLocked = bInIsLocked; }

	bool IsLocked() const;
	bool IsInitiallyLoadedInEditor() const { return bIsInitiallyLoadedInEditor; }
	bool IsLoadedInEditor() const { return bIsLoadedInEditor; }
	bool IsEffectiveLoadedInEditor() const;
	bool IsLoadedInEditorChangedByUserOperation() const { return bIsLoadedInEditorChangedByUserOperation; }
	void ClearLoadedInEditorChangedByUserOperation() { bIsLoadedInEditorChangedByUserOperation = false; }

	const TCHAR* GetDataLayerIconName() const { return DataLayerAsset->GetDataLayerIconName(); }

	bool CanParent(const UDataLayerInstance* InParent) const;
	void SetParent(UDataLayerInstance* InParent);

	void SetChildParent(UDataLayerInstance* InParent);

	void CheckForErrors() const;
#endif

	const UDataLayerAsset* GetAsset() const { return DataLayerAsset.Get(); }

	UFUNCTION(Category = "Data Layer", BlueprintCallable)
	bool Equals(const FActorDataLayer& ActorDataLayer) const { return ActorDataLayer.Name == GetFName(); }

	UFUNCTION(Category = "Data Layer|Editor", BlueprintCallable)
	bool IsInitiallyVisible() const;

	UFUNCTION(Category = "Data Layer|Editor", BlueprintCallable)
	bool IsVisible() const;

	UFUNCTION(Category = "Data Layer|Editor", BlueprintCallable)
	bool IsEffectiveVisible() const;

	UFUNCTION(Category = "Data Layer", BlueprintCallable)
	FName GetDataLayerLabel() const { return DataLayerAsset->GetFName(); }

	UFUNCTION(Category = "Data Layer|Runtime", BlueprintCallable)
	bool IsRuntime() const { return DataLayerAsset->IsRuntime(); }

	UFUNCTION(Category = "Data Layer|Runtime", BlueprintCallable)
	FColor GetDebugColor() const { return DataLayerAsset->GetDebugColor(); }

	UFUNCTION(Category = "Data Layer|Runtime", BlueprintCallable)
	EDataLayerRuntimeState GetInitialRuntimeState() const { return IsRuntime() ? InitialRuntimeState : EDataLayerRuntimeState::Unloaded; }

	const TArray<TObjectPtr<UDataLayerInstance>>& GetChildren() const { return Children; }
	void ForEachChild(TFunctionRef<bool(const UDataLayerInstance*)> Operation) const;

	const UDataLayerInstance* GetParent() const { return Parent; }
	UDataLayerInstance* GetParent() { return Parent; }

private:
	void AddChild(UDataLayerInstance* DataLayer);
#if WITH_EDITOR
	void RemoveChild(UDataLayerInstance* DataLayer);
#endif

#if WITH_EDITORONLY_DATA
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

	UPROPERTY(Category = "Data Layer", VisibleAnywhere)
	TObjectPtr<const UDataLayerAsset> DataLayerAsset;

	UPROPERTY(Category = "Data Layer|Advanced|Runtime", EditAnywhere, meta = (EditConditionHides, EditCondition = "bIsRuntime"))
	EDataLayerRuntimeState InitialRuntimeState;

	UPROPERTY()
	TObjectPtr<UDataLayerInstance> Parent;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UDataLayerInstance>> Children;
};
