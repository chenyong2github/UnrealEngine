// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Input/Reply.h"
#include "ContentBrowserItem.h"

class FExtender;
class FMenuBuilder;
class SWidget;
class SWindow;
class UToolMenu;

enum class EContentBrowserViewContext : uint8;

class FPathContextMenu : public TSharedFromThis<FPathContextMenu>
{
public:
	/** Constructor */
	FPathContextMenu(const TWeakPtr<SWidget>& InParentContent);

	/** Delegate for when the context menu requests a rename of a folder */
	DECLARE_DELEGATE_TwoParams(FOnRenameFolderRequested, const FContentBrowserItem& /*FolderToRename*/, EContentBrowserViewContext /*ViewContext*/);
	void SetOnRenameFolderRequested(const FOnRenameFolderRequested& InOnRenameFolderRequested);

	/** Delegate for when the context menu has successfully deleted a folder */
	DECLARE_DELEGATE(FOnFolderDeleted)
	void SetOnFolderDeleted(const FOnFolderDeleted& InOnFolderDeleted);

	/** Delegate for when the context menu has successfully toggled the favorite status of a folder */
	DECLARE_DELEGATE_OneParam(FOnFolderFavoriteToggled, const TArray<FString>& /*FoldersToToggle*/)
	void SetOnFolderFavoriteToggled(const FOnFolderFavoriteToggled& InOnFolderFavoriteToggled);

	/** Gets the currently selected folders */
	const TArray<FContentBrowserItem>& GetSelectedFolders() const;

	/** Sets the currently selected folders */
	void SetSelectedFolders(const TArray<FContentBrowserItem>& InSelectedFolders);

	/** Makes the asset tree context menu extender */
	TSharedRef<FExtender> MakePathViewContextMenuExtender(const TArray<FString>& InSelectedPaths);

	/** Makes the asset tree context menu widget */
	void MakePathViewContextMenu(UToolMenu* Menu);

	/** Makes the new asset submenu */
	void MakeNewAssetSubMenu(UToolMenu* Menu);

	/** Makes the set color submenu */
	void MakeSetColorSubMenu(UToolMenu* Menu);

	/** Handler for when "Explore" is selected */
	void ExecuteExplore();

	/** Handler to check to see if a rename command is allowed */
	bool CanExecuteRename() const;

	/** Handler for Rename */
	void ExecuteRename(EContentBrowserViewContext ViewContext);

	/** Handler to check to see if a delete command is allowed */
	bool CanExecuteDelete() const;

	/** Handler for Delete */
	void ExecuteDelete();

	/** Handler for when reset color is selected */
	void ExecuteResetColor();

	/** Handler for when new or set color is selected */
	void ExecutePickColor();

	/** Handler for favoriting */
	void ExecuteFavorite();

	/** Handler for when "Save" is selected */
	void ExecuteSaveFolder();

	/** Handler for when "Resave" is selected */
	void ExecuteResaveFolder();

	/** Handler for when "Delete" is selected and the delete was confirmed */
	FReply ExecuteDeleteFolderConfirmed();

private:
	void SaveFilesWithinSelectedFolders(EContentBrowserItemSaveFlags InSaveFlags);

	/** Checks to see if any of the selected paths use custom colors */
	bool SelectedHasCustomColors() const;

	/** Callback when the color picker dialog has been closed */
	void NewColorComplete(const TSharedRef<SWindow>& Window);

	/** Callback when the color is picked from the set color submenu */
	FReply OnColorClicked( const FLinearColor InColor );

	/** Resets the colors of the selected paths */
	void ResetColors();

private:
	TArray<FContentBrowserItem> SelectedFolders;
	TWeakPtr<SWidget> ParentContent;
	FOnRenameFolderRequested OnRenameFolderRequested;
	FOnFolderDeleted OnFolderDeleted;
	FOnFolderFavoriteToggled OnFolderFavoriteToggled;
};
