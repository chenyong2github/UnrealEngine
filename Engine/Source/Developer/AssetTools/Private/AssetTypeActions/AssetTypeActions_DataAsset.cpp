// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetTypeActions/AssetTypeActions_DataAsset.h"

#define LOCTEXT_NAMESPACE "AssetTypeActions"

static const FName NAME_NativeClass(TEXT("NativeClass"));

FText FAssetTypeActions_DataAsset::GetDisplayNameFromAssetData(const FAssetData& AssetData) const
{
	if (AssetData.IsValid())
	{
		const FAssetDataTagMapSharedView::FFindTagResult NativeClassTag = AssetData.TagsAndValues.FindTag(NAME_NativeClass);
		if (NativeClassTag.IsSet())
		{
			if (UClass* FoundClass = FindObjectSafe<UClass>(nullptr, *NativeClassTag.GetValue()))
			{
				return FText::Format(LOCTEXT("DataAssetWithType", "Data Asset ({0})"), FoundClass->GetDisplayNameText());
			}
		}
	}

	return FText::GetEmpty();
}

#undef LOCTEXT_NAMESPACE
