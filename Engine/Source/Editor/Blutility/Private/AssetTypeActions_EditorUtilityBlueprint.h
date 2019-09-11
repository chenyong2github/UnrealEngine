// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EditorUtilityBlueprint.h"
#include "Toolkits/IToolkitHost.h"
#include "AssetTypeActions/AssetTypeActions_Blueprint.h"


class FAssetTypeActions_EditorUtilityBlueprint : public FAssetTypeActions_Blueprint
{
public:
	// IAssetTypeActions interface
	virtual FText GetName() const override;
	virtual FColor GetTypeColor() const override;
	virtual UClass* GetSupportedClass() const override;
	virtual bool HasActions(const TArray<UObject*>& InObjects) const override;
	virtual void GetActions(const TArray<UObject*>& InObjects, struct FToolMenuSection& Section) override;
	virtual uint32 GetCategories() override;
	virtual bool CanLocalize() const override { return false; }
	// End of IAssetTypeActions interface

protected:
	typedef TArray< TWeakObjectPtr<class UEditorUtilityBlueprint> > FWeakBlueprintPointerArray;

	void ExecuteNewDerivedBlueprint(TWeakObjectPtr<class UEditorUtilityBlueprint> InObject);
	void ExecuteRun(FWeakBlueprintPointerArray Objects);
};
