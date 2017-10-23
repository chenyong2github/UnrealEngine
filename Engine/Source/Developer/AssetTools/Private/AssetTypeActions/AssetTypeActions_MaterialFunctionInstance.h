// Copyright 1998-2017 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AssetTypeActions_MaterialFunction.h"
#include "Materials/MaterialFunctionInstance.h"

class FMenuBuilder;

class FAssetTypeActions_MaterialFunctionInstance : public FAssetTypeActions_MaterialFunction
{
public:
	// IAssetTypeActions Implementation
	virtual FText GetName() const override { return NSLOCTEXT("AssetTypeActions", "FAssetTypeActions_MaterialFunctionInstance", "Material Function Instance"); }
	virtual UClass* GetSupportedClass() const override { return UMaterialFunctionInstance::StaticClass(); }
	virtual uint32 GetCategories() override { return EAssetTypeCategories::None; }
	virtual void GetActions( const TArray<UObject*>& InObjects, FMenuBuilder& MenuBuilder ) override;
	virtual void OpenAssetEditor( const TArray<UObject*>& InObjects, TSharedPtr<class IToolkitHost> EditWithinLevelEditor = TSharedPtr<IToolkitHost>() ) override;
	virtual class UThumbnailInfo* GetThumbnailInfo(UObject* Asset) const override;

private:
	/** Handler for when FindParent is selected */
	void ExecuteFindParent(TArray<TWeakObjectPtr<UMaterialFunctionInstance>> Objects);
};
