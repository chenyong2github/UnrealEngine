// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetDefinition_AssetTypeActionsProxy.h"

#include "Misc/AssetFilterData.h"

void UAssetDefinition_AssetTypeActionsProxy::Initialize(const TSharedRef<IAssetTypeActions>& NewActions)
{
	AssetType = NewActions;
}

FText UAssetDefinition_AssetTypeActionsProxy::GetAssetDisplayName() const
{
	return AssetType->GetName();
}

FText UAssetDefinition_AssetTypeActionsProxy::GetAssetDescription(const FAssetData& AssetData) const
{
	return AssetType->GetAssetDescription(AssetData);
}

TSoftClassPtr<UObject> UAssetDefinition_AssetTypeActionsProxy::GetAssetClass() const
{
	if (UClass* SupportedClass = AssetType->GetSupportedClass())
	{
		return TSoftClassPtr<UObject>(SupportedClass);
	}

	return TSoftClassPtr<UObject>(FSoftObjectPath(AssetType->GetClassPathName()));
}

FLinearColor UAssetDefinition_AssetTypeActionsProxy::GetAssetColor() const
{
	return FLinearColor(AssetType->GetTypeColor());
}

FAssetSupportResponse UAssetDefinition_AssetTypeActionsProxy::CanLocalize(const FAssetData& InAsset) const
{
	return AssetType->CanLocalize() ? FAssetSupportResponse::Supported() : FAssetSupportResponse::NotSupported();
}

bool UAssetDefinition_AssetTypeActionsProxy::CanImport() const
{
	return AssetType->IsImportedAsset();
}

bool UAssetDefinition_AssetTypeActionsProxy::CanMerge() const
{
	return AssetType->CanMerge();
}

EAssetCommandResult UAssetDefinition_AssetTypeActionsProxy::ActivateAssets(const FAssetActivateArgs& ActivateArgs) const
{
	EAssetTypeActivationMethod::Type ActivationType = EAssetTypeActivationMethod::Opened;
	switch(ActivateArgs.ActivationMethod)
	{
	case EAssetActivationMethod::DoubleClicked:
		ActivationType = EAssetTypeActivationMethod::DoubleClicked;
		break;
	case EAssetActivationMethod::Opened:
		ActivationType = EAssetTypeActivationMethod::Opened;
		break;
	case EAssetActivationMethod::Previewed:
		ActivationType = EAssetTypeActivationMethod::Previewed;
		break;
	}

	const TArray<UObject*> Assets = ActivateArgs.LoadObjects<UObject>();
	const bool bResult = AssetType->AssetsActivatedOverride(Assets, ActivationType);
	return bResult ? EAssetCommandResult::Handled : EAssetCommandResult::Unhandled;
}

EAssetCommandResult UAssetDefinition_AssetTypeActionsProxy::GetFilters(TArray<FAssetFilterData>& OutFilters) const
{
	if (AssetType->CanFilter())
	{
		FAssetFilterData Data;
		Data.Name = AssetType->GetFilterName().ToString();
		Data.DisplayText = AssetType->GetName();
		AssetType->BuildBackendFilter(Data.Filter);
		OutFilters.Add(MoveTemp(Data));

		return EAssetCommandResult::Handled;
	}

	return EAssetCommandResult::Unhandled;
}

