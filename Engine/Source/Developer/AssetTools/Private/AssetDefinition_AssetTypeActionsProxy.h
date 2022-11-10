// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IAssetTypeActions.h"
#include "AssetDefinitionDefault.h"

#include "AssetDefinition_AssetTypeActionsProxy.generated.h"

UCLASS(transient)
class UAssetDefinition_AssetTypeActionsProxy : public UAssetDefinitionDefault
{
	GENERATED_BODY() 
public:
	UAssetDefinition_AssetTypeActionsProxy() { }
	
	void Initialize(const TSharedRef<IAssetTypeActions>& NewActions);
	
	const TSharedPtr<IAssetTypeActions>& GetAssetType() const { return AssetType; }

	virtual FText GetAssetDisplayName() const override;
	virtual TSoftClassPtr<UObject> GetAssetClass() const override;
	virtual FLinearColor GetAssetColor() const override;
	virtual FText GetAssetDescription(const FAssetData& AssetData) const override;

	virtual FAssetSupportResponse CanLocalize(const FAssetData& InAsset) const override;
	virtual bool CanImport() const override;
	virtual bool CanMerge() const override;

	virtual EAssetCommandResult ActivateAssets(const FAssetActivateArgs& ActivateArgs) const override;

	virtual EAssetCommandResult GetFilters(TArray<FAssetFilterData>& OutFilters) const override;

	// We don't need to implement GetSourceFiles, the default one will / should work for any existing type.
	// virtual EAssetCommandResult GetSourceFiles(const FAssetSourceFileArgs& SourceFileArgs, TArray<FAssetSourceFile>& OutSourceAssets) const override;
	
protected:
	virtual bool CanRegisterStatically() const override { return false; }
	
private:
	TSharedPtr<IAssetTypeActions> AssetType;
};
