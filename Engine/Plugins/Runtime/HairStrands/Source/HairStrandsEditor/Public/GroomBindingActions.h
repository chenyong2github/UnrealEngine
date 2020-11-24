// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AssetTypeActions_Base.h"
#include "Templates/SharedPointer.h"
#include "GroomBindingAsset.h"

class ISlateStyle;
/**
 * Implements an action for groom binding assets.
 */
class FGroomBindingActions : public FAssetTypeActions_Base
{
public:

	/**
	 * Creates and initializes a new instance.
	 *
	 * @param InStyle The style set to use for asset editor toolkits.
	 */
	FGroomBindingActions();

public:

	//~ FAssetTypeActions_Base overrides

	virtual bool CanFilter() override;
	virtual void GetActions(const TArray<UObject*>& InObjects, FToolMenuSection& Section) override;
	virtual uint32 GetCategories() override;
	virtual FText GetName() const override;
	virtual UClass* GetSupportedClass() const override;
	virtual FColor GetTypeColor() const override;
	virtual bool HasActions(const TArray<UObject*>& InObjects) const override;
	virtual void OpenAssetEditor(const TArray<UObject*>& InObjects, TSharedPtr<IToolkitHost> EditWithinLevelEditor = TSharedPtr<IToolkitHost>()) override;

private:

	/** Callback for rebuild binding asset action */
	bool CanRebuildBindingAsset(TArray<TWeakObjectPtr<UGroomBindingAsset>> Objects) const;
	void ExecuteRebuildBindingAsset(TArray<TWeakObjectPtr<UGroomBindingAsset>> Objects) const;
};
