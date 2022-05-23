// Copyright Epic Games, Inc. All Rights Reserved.

#include "Filters/CustomClassFilterData.h"
#include "Filters/SBasicFilterBar.h"

#include "IAssetTypeActions.h"
#include "AssetTypeCategories.h"
#include "AssetToolsModule.h"
#include "UObject/Object.h"

void FCustomClassFilterData::AddCategory(TSharedPtr<FFilterCategory> InCategory)
{
	Categories.Add(InCategory);
}

TSharedPtr<IAssetTypeActions> FCustomClassFilterData::GetAssetTypeActions() const
{
	return AssetTypeActions.Pin();
}

UClass* FCustomClassFilterData::GetClass() const
{
	if(TSharedPtr<IAssetTypeActions> AssetTypeActionsPin = AssetTypeActions.Pin())
	{
		return AssetTypeActionsPin->GetSupportedClass();
	}

	return Class;
}

TArray<TSharedPtr<FFilterCategory>> FCustomClassFilterData::GetCategories() const
{
	return Categories;
}

FLinearColor FCustomClassFilterData::GetColor() const
{
	if(TSharedPtr<IAssetTypeActions> AssetTypeActionsPin = AssetTypeActions.Pin())
	{
		return AssetTypeActionsPin->GetTypeColor();
	}

	return Color;
}

void FCustomClassFilterData::BuildBackendFilter(FARFilter &Filter)
{
	if ( AssetTypeActions.IsValid() )
	{
		if (AssetTypeActions.Pin()->CanFilter())
		{
			AssetTypeActions.Pin()->BuildBackendFilter(Filter);
		}
	}
	// If there is no AssetTypeAction for this filter, simply add the class name to the FARFilter
	else
	{
		Filter.ClassNames.Add(Class->GetFName());
		Filter.bRecursiveClasses = true;
	}
}

FText FCustomClassFilterData::GetName() const
{
	if(TSharedPtr<IAssetTypeActions> AssetTypeActionsPin = AssetTypeActions.Pin())
	{
		return AssetTypeActionsPin->GetName();
	}

	return Class->GetDisplayNameText();
}

FString FCustomClassFilterData::GetFilterName() const
{
	if(TSharedPtr<IAssetTypeActions> AssetTypeActionsPin = AssetTypeActions.Pin())
	{
		return AssetTypeActionsPin->GetFilterName().ToString();
	}

	return Class->GetFName().ToString();
}

