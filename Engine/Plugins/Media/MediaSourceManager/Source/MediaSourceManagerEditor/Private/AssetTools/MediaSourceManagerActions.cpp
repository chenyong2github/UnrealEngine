// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetTools/MediaSourceManagerActions.h"

#include "AssetRegistry/AssetData.h"
#include "MediaSourceManager.h"

#define LOCTEXT_NAMESPACE "MediaSourceManagerActions"

bool FMediaSourceManagerActions::CanFilter()
{
	return true;
}

FText FMediaSourceManagerActions::GetAssetDescription(const struct FAssetData& AssetData) const
{
	return FText::GetEmpty();
}

uint32 FMediaSourceManagerActions::GetCategories()
{
	return EAssetTypeCategories::Media;
}

FText FMediaSourceManagerActions::GetName() const
{
	return LOCTEXT("MediaSourceManager", "Media Source Manager");
}

UClass* FMediaSourceManagerActions::GetSupportedClass() const
{
	return UMediaSourceManager::StaticClass();
}

FColor FMediaSourceManagerActions::GetTypeColor() const
{
	return FColor::White;
}

#undef LOCTEXT_NAMESPACE
