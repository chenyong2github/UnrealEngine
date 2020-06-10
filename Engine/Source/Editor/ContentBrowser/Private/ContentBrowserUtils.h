// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AssetViewUtils.h"
#include "Widgets/SWidget.h"
#include "Framework/SlateDelegates.h"
#include "AssetData.h"
#include "CollectionManagerTypes.h"
#include "Interfaces/IPluginManager.h"

class SAssetView;
class SPathView;

class FBlacklistNames;
class FBlacklistPaths;

struct FARFilter;
struct FContentBrowserItem;
struct FContentBrowserDataFilter;

namespace ContentBrowserUtils
{
	// Import the functions that were moved into the more common AssetViewUtils namespace
	using namespace AssetViewUtils;

	/** Displays a modeless message at the specified anchor. It is fine to specify a zero-size anchor, just use the top and left fields */
	void DisplayMessage(const FText& Message, const FSlateRect& ScreenAnchor, const TSharedRef<SWidget>& ParentContent);

	/** Displays a modeless message asking yes or no type question */
	void DisplayConfirmationPopup(const FText& Message, const FText& YesString, const FText& NoString, const TSharedRef<SWidget>& ParentContent, const FOnClicked& OnYesClicked, const FOnClicked& OnNoClicked = FOnClicked());

	/** Copies references to the specified items to the clipboard */
	void CopyItemReferencesToClipboard(const TArray<FContentBrowserItem>& ItemsToCopy);

	/** Copies file paths on disk to the specified items to the clipboard */
	void CopyFilePathsToClipboard(const TArray<FContentBrowserItem>& ItemsToCopy);

	/** Check whether the given item is considered to be developer content */
	bool IsItemDeveloperContent(const FContentBrowserItem& InItem);

	/** Check whether the given item is considered to be localized content */
	bool IsItemLocalizedContent(const FContentBrowserItem& InItem);

	/** Check whether the given item is considered to be engine content (including engine plugins) */
	bool IsItemEngineContent(const FContentBrowserItem& InItem);

	/** Check whether the given item is considered to be project content (including project plugins) */
	bool IsItemProjectContent(const FContentBrowserItem& InItem);

	/** Check whether the given item is considered to be plugin content (engine or project) */
	bool IsItemPluginContent(const FContentBrowserItem& InItem);

	/** Check to see whether the given path is rooted against a collection directory, optionally extracting the collection name and share type from the path */
	bool IsCollectionPath(const FString& InPath, FName* OutCollectionName = nullptr, ECollectionShareType::Type* OutCollectionShareType = nullptr);

	/** Given an array of paths, work out how many are rooted against class roots, and how many are rooted against asset roots */
	void CountPathTypes(const TArray<FString>& InPaths, int32& OutNumAssetPaths, int32& OutNumClassPaths);

	/** Given an array of paths, work out how many are rooted against class roots, and how many are rooted against asset roots */
	void CountPathTypes(const TArray<FName>& InPaths, int32& OutNumAssetPaths, int32& OutNumClassPaths);

	/** Given an array of "asset" data, work out how many are assets, and how many are classes */
	void CountItemTypes(const TArray<FAssetData>& InItems, int32& OutNumAssetItems, int32& OutNumClassItems);

	/** Gets the platform specific text for the "explore" command (FPlatformProcess::ExploreFolder) */
	FText GetExploreFolderText();

	/** Convert a legacy asset and path selection to their corresponding virtual paths for content browser data items */
	void ConvertLegacySelectionToVirtualPaths(TArrayView<const FAssetData> InAssets, TArrayView<const FString> InFolders, const bool InUseFolderPaths, TArray<FName>& OutVirtualPaths);
	void ConvertLegacySelectionToVirtualPaths(TArrayView<const FAssetData> InAssets, TArrayView<const FString> InFolders, const bool InUseFolderPaths, TSet<FName>& OutVirtualPaths);

	/** Append the asset registry filter and blacklists to the content browser data filter */
	void AppendAssetFilterToContentBrowserFilter(const FARFilter& InAssetFilter, const TSharedPtr<FBlacklistNames>& InAssetClassBlacklist, const TSharedPtr<FBlacklistPaths>& InFolderBlacklist, FContentBrowserDataFilter& OutDataFilter);

	/** Shared logic to know if we can perform certain operation depending on which view it occurred, either PathView or AssetView */
	bool CanDeleteFromAssetView(TWeakPtr<SAssetView> AssetView);
	bool CanRenameFromAssetView(TWeakPtr<SAssetView> AssetView);
	bool CanDeleteFromPathView(TWeakPtr<SPathView> PathView);
	bool CanRenameFromPathView(TWeakPtr<SPathView> PathView);

	/** Returns if this folder has been marked as a favorite folder */
	bool IsFavoriteFolder(const FString& FolderPath);

	void AddFavoriteFolder(const FString& FolderPath, bool bFlushConfig = true);

	void RemoveFavoriteFolder(const FString& FolderPath, bool bFlushConfig = true);

	const TArray<FString>& GetFavoriteFolders();
}
