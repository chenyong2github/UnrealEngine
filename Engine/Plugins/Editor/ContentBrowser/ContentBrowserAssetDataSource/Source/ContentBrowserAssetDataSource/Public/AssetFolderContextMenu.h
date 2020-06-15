// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Input/Reply.h"

class FExtender;
class FMenuBuilder;
class SWidget;
class SWindow;
class UToolMenu;

class CONTENTBROWSERASSETDATASOURCE_API FAssetFolderContextMenu : public TSharedFromThis<FAssetFolderContextMenu>
{
public:
	/** Makes the context menu widget */
	void MakeContextMenu(
		UToolMenu* InMenu,
		const TArray<FString>& InSelectedPackagePaths
		);

private:
	/** Makes the asset tree context menu widget */
	void AddMenuOptions(UToolMenu* Menu);

	/** Handler for when "Migrate Folder" is selected */
	void ExecuteMigrateFolder();

	/** Handler for when "Fix up Redirectors in Folder" is selected */
	void ExecuteFixUpRedirectorsInFolder();

	/** Handler for when "Checkout from source control" is selected */
	void ExecuteSCCCheckOut();

	/** Handler for when "Open for Add to source control" is selected */
	void ExecuteSCCOpenForAdd();

	/** Handler for when "Checkin to source control" is selected */
	void ExecuteSCCCheckIn();

	/** Handler for when "Sync from source control" is selected */
	void ExecuteSCCSync() const;

	/** Handler for when "Connect to source control" is selected */
	void ExecuteSCCConnect() const;

	/** Handler to check to see if "Checkout from source control" can be executed */
	bool CanExecuteSCCCheckOut() const;

	/** Handler to check to see if "Open for Add to source control" can be executed */
	bool CanExecuteSCCOpenForAdd() const;

	/** Handler to check to see if "Checkin to source control" can be executed */
	bool CanExecuteSCCCheckIn() const;

	/** Handler to check to see if "Sync" can be executed */
	bool CanExecuteSCCSync() const;

	/** Handler to check to see if "Connect to source control" can be executed */
	bool CanExecuteSCCConnect() const;	

	/** Initializes some variable used to in "CanExecute" checks that won't change at runtime or are too expensive to check every frame. */
	void CacheCanExecuteVars();

	/** Returns a list of names of packages in all selected paths in the sources view */
	void GetPackageNamesInSelectedPaths(TArray<FString>& OutPackageNames) const;

	/** Gets the first selected path, if it exists */
	FString GetFirstSelectedPath() const;

private:
	TArray<FString> SelectedPaths;
	TWeakPtr<SWidget> ParentWidget;

	/** Cached SCC CanExecute vars */
	bool bCanExecuteSCCCheckOut = false;
	bool bCanExecuteSCCOpenForAdd = false;
	bool bCanExecuteSCCCheckIn = false;
};
