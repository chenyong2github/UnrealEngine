// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetFolderContextMenu.h"
#include "Misc/MessageDialog.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "UObject/ObjectRedirector.h"
#include "Misc/PackageName.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"
#include "Framework/Application/SlateApplication.h"
#include "Textures/SlateIcon.h"
#include "Framework/MultiBox/MultiBoxExtender.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Colors/SColorBlock.h"
#include "EditorStyleSet.h"
#include "SourceControlOperations.h"
#include "ISourceControlModule.h"
#include "SourceControlHelpers.h"
#include "AssetData.h"
#include "Editor.h"
#include "FileHelpers.h"
#include "ARFilter.h"
#include "AssetRegistryModule.h"
#include "IAssetTools.h"
#include "AssetToolsModule.h"
#include "AssetViewUtils.h"
//#include "ContentBrowserLog.h"
//#include "ContentBrowserSingleton.h"
//#include "ContentBrowserUtils.h"
#include "SourceControlWindows.h"
#include "ContentBrowserModule.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
//#include "Widgets/Colors/SColorPicker.h"
#include "Framework/Commands/GenericCommands.h"
//#include "NativeClassHierarchy.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
//#include "ContentBrowserCommands.h"
#include "ToolMenus.h"
#include "ContentBrowserMenuContexts.h"
#include "IContentBrowserDataModule.h"
#include "ContentBrowserDataSource.h"
#include "ContentBrowserDataSubsystem.h"
#include "ContentBrowserDataMenuContexts.h"

#define LOCTEXT_NAMESPACE "ContentBrowser"

void FAssetFolderContextMenu::MakeContextMenu(UToolMenu* InMenu, const TArray<FString>& InSelectedPackagePaths)
{
	SelectedPaths = InSelectedPackagePaths;

	if (SelectedPaths.Num() > 0)
	{
		AddMenuOptions(InMenu);
	}
}

void FAssetFolderContextMenu::AddMenuOptions(UToolMenu* Menu)
{
	UContentBrowserDataMenuContext_FolderMenu* Context = Menu->FindContext<UContentBrowserDataMenuContext_FolderMenu>();
	checkf(Context, TEXT("Required context UContentBrowserDataMenuContext_FolderMenu was missing!"));

	ParentWidget = Context->ParentWidget;

	// Cache any vars that are used in determining if you can execute any actions.
	// Useful for actions whose "CanExecute" will not change or is expensive to calculate.
	CacheCanExecuteVars();

	if(Context->bCanBeModified)
	{
		// Bulk operations section //
		{
			FToolMenuSection& Section = Menu->AddSection("PathContextBulkOperations", LOCTEXT("AssetTreeBulkMenuHeading", "Bulk Operations") );

			// Fix Up Redirectors in Folder
			{
				FToolMenuEntry& Entry = Section.AddMenuEntry(
					"FixUpRedirectorsInFolder",
					LOCTEXT("FixUpRedirectorsInFolder", "Fix Up Redirectors in Folder"),
					LOCTEXT("FixUpRedirectorsInFolderTooltip", "Finds referencers to all redirectors in the selected folders and resaves them if possible, then deletes any redirectors that had all their referencers fixed."),
					FSlateIcon(),
					FUIAction(FExecuteAction::CreateSP(this, &FAssetFolderContextMenu::ExecuteFixUpRedirectorsInFolder))
					);
				Entry.InsertPosition = FToolMenuInsert("Delete", EToolMenuInsertType::After);
			}

			if (SelectedPaths.Num() > 0)
			{
				// Migrate Folder
				FToolMenuEntry& Entry = Section.AddMenuEntry(
					"MigrateFolder",
					LOCTEXT("MigrateFolder", "Migrate..."),
					LOCTEXT("MigrateFolderTooltip", "Copies assets found in this folder and their dependencies to another game content folder."),
					FSlateIcon(),
					FUIAction( FExecuteAction::CreateSP( this, &FAssetFolderContextMenu::ExecuteMigrateFolder ) )
					);
				Entry.InsertPosition = FToolMenuInsert("FixUpRedirectorsInFolder", EToolMenuInsertType::After);
			}
		}

		// Source control section //
		{
			FToolMenuSection& Section = Menu->AddSection("PathContextSourceControl", LOCTEXT("AssetTreeSCCMenuHeading", "Source Control"));

			ISourceControlProvider& SourceControlProvider = ISourceControlModule::Get().GetProvider();
			if (SourceControlProvider.IsEnabled())
			{
				// Check out
				Section.AddMenuEntry(
					"FolderSCCCheckOut",
					LOCTEXT("FolderSCCCheckOut", "Check Out"),
					LOCTEXT("FolderSCCCheckOutTooltip", "Checks out all assets from source control which are in this folder."),
					FSlateIcon(),
					FUIAction(
						FExecuteAction::CreateSP(this, &FAssetFolderContextMenu::ExecuteSCCCheckOut),
						FCanExecuteAction::CreateSP(this, &FAssetFolderContextMenu::CanExecuteSCCCheckOut)
					)
				);

				// Open for Add
				Section.AddMenuEntry(
					"FolderSCCOpenForAdd",
					LOCTEXT("FolderSCCOpenForAdd", "Mark For Add"),
					LOCTEXT("FolderSCCOpenForAddTooltip", "Adds all assets to source control that are in this folder and not already added."),
					FSlateIcon(),
					FUIAction(
						FExecuteAction::CreateSP(this, &FAssetFolderContextMenu::ExecuteSCCOpenForAdd),
						FCanExecuteAction::CreateSP(this, &FAssetFolderContextMenu::CanExecuteSCCOpenForAdd)
					)
				);

				// Check in
				Section.AddMenuEntry(
					"FolderSCCCheckIn",
					LOCTEXT("FolderSCCCheckIn", "Check In"),
					LOCTEXT("FolderSCCCheckInTooltip", "Checks in all assets to source control which are in this folder."),
					FSlateIcon(),
					FUIAction(
						FExecuteAction::CreateSP(this, &FAssetFolderContextMenu::ExecuteSCCCheckIn),
						FCanExecuteAction::CreateSP(this, &FAssetFolderContextMenu::CanExecuteSCCCheckIn)
					)
				);

				// Sync
				Section.AddMenuEntry(
					"FolderSCCSync",
					LOCTEXT("FolderSCCSync", "Sync"),
					LOCTEXT("FolderSCCSyncTooltip", "Syncs all the assets in this folder to the latest version."),
					FSlateIcon(),
					FUIAction(
						FExecuteAction::CreateSP(this, &FAssetFolderContextMenu::ExecuteSCCSync),
						FCanExecuteAction::CreateSP(this, &FAssetFolderContextMenu::CanExecuteSCCSync)
					)
				);
			}
			else
			{
				Section.AddMenuEntry(
					"FolderSCCConnect",
					LOCTEXT("FolderSCCConnect", "Connect To Source Control"),
					LOCTEXT("FolderSCCConnectTooltip", "Connect to source control to allow source control operations to be performed on content and levels."),
					FSlateIcon(),
					FUIAction(
						FExecuteAction::CreateSP(this, &FAssetFolderContextMenu::ExecuteSCCConnect),
						FCanExecuteAction::CreateSP(this, &FAssetFolderContextMenu::CanExecuteSCCConnect)
					)
				);
			}
		}
	}
}

void FAssetFolderContextMenu::ExecuteMigrateFolder()
{
	const FString& SourcesPath = GetFirstSelectedPath();
	if ( ensure(SourcesPath.Len()) )
	{
		// @todo Make sure the asset registry has completed discovering assets, or else GetAssetsByPath() will not find all the assets in the folder! Add some UI to wait for this with a cancel button
		FAssetRegistryModule& AssetRegistryModule = FModuleManager::Get().LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		if ( AssetRegistryModule.Get().IsLoadingAssets() )
		{
			FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT( "MigrateFolderAssetsNotDiscovered", "You must wait until asset discovery is complete to migrate a folder" ));
			return;
		}

		// Get a list of package names for input into MigratePackages
		TArray<FAssetData> AssetDataList;
		TArray<FName> PackageNames;
		AssetViewUtils::GetAssetsInPaths(SelectedPaths, AssetDataList);
		for ( auto AssetIt = AssetDataList.CreateConstIterator(); AssetIt; ++AssetIt )
		{
			PackageNames.Add((*AssetIt).PackageName);
		}

		// Load all the assets in the selected paths
		FAssetToolsModule& AssetToolsModule = FModuleManager::Get().LoadModuleChecked<FAssetToolsModule>("AssetTools");
		AssetToolsModule.Get().MigratePackages( PackageNames );
	}
}

void FAssetFolderContextMenu::ExecuteFixUpRedirectorsInFolder()
{
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::Get().LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));

	// Form a filter from the paths
	FARFilter Filter;
	Filter.bRecursivePaths = true;
	for (const auto& Path : SelectedPaths)
	{
		Filter.PackagePaths.Emplace(*Path);
		Filter.ClassNames.Emplace(TEXT("ObjectRedirector"));
	}

	// Query for a list of assets in the selected paths
	TArray<FAssetData> AssetList;
	AssetRegistryModule.Get().GetAssets(Filter, AssetList);

	if (AssetList.Num() > 0)
	{
		TArray<FString> ObjectPaths;
		for (const auto& Asset : AssetList)
		{
			ObjectPaths.Add(Asset.ObjectPath.ToString());
		}

		TArray<UObject*> Objects;
		const bool bAllowedToPromptToLoadAssets = true;
		const bool bLoadRedirects = true;
		if (AssetViewUtils::LoadAssetsIfNeeded(ObjectPaths, Objects, bAllowedToPromptToLoadAssets, bLoadRedirects))
		{
			// Transform Objects array to ObjectRedirectors array
			TArray<UObjectRedirector*> Redirectors;
			for (auto Object : Objects)
			{
				auto Redirector = CastChecked<UObjectRedirector>(Object);
				Redirectors.Add(Redirector);
			}

			// Load the asset tools module
			FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
			AssetToolsModule.Get().FixupReferencers(Redirectors);
		}
	}
}

void FAssetFolderContextMenu::ExecuteSCCCheckOut()
{
	// Get a list of package names in the selected paths
	TArray<FString> PackageNames;
	GetPackageNamesInSelectedPaths(PackageNames);

	TArray<UPackage*> PackagesToCheckOut;
	for ( auto PackageIt = PackageNames.CreateConstIterator(); PackageIt; ++PackageIt )
	{
		if ( FPackageName::DoesPackageExist(*PackageIt) )
		{
			// Since the file exists, create the package if it isn't loaded or just find the one that is already loaded
			// No need to load unloaded packages. It isn't needed for the checkout process
			UPackage* Package = CreatePackage(**PackageIt);
			PackagesToCheckOut.Add( CreatePackage(**PackageIt) );
		}
	}

	if ( PackagesToCheckOut.Num() > 0 )
	{
		// Update the source control status of all potentially relevant packages
		ISourceControlModule::Get().GetProvider().Execute(ISourceControlOperation::Create<FUpdateStatus>(), PackagesToCheckOut);

		// Now check them out
		FEditorFileUtils::CheckoutPackages(PackagesToCheckOut);
	}
}

void FAssetFolderContextMenu::ExecuteSCCOpenForAdd()
{
	ISourceControlProvider& SourceControlProvider = ISourceControlModule::Get().GetProvider();

	// Get a list of package names in the selected paths
	TArray<FString> PackageNames;
	GetPackageNamesInSelectedPaths(PackageNames);

	TArray<FString> PackagesToAdd;
	TArray<UPackage*> PackagesToSave;
	for ( auto PackageIt = PackageNames.CreateConstIterator(); PackageIt; ++PackageIt )
	{
		FSourceControlStatePtr SourceControlState = SourceControlProvider.GetState(SourceControlHelpers::PackageFilename(*PackageIt), EStateCacheUsage::Use);
		if ( SourceControlState.IsValid() && !SourceControlState->IsSourceControlled() )
		{
			PackagesToAdd.Add(*PackageIt);

			// Make sure the file actually exists on disk before adding it
			FString Filename;
			if ( !FPackageName::DoesPackageExist(*PackageIt, NULL, &Filename) )
			{
				UPackage* Package = FindPackage(NULL, **PackageIt);
				if ( Package )
				{
					PackagesToSave.Add(Package);
				}
			}
		}
	}

	if ( PackagesToAdd.Num() > 0 )
	{
		// If any of the packages are new, save them now
		if ( PackagesToSave.Num() > 0 )
		{
			const bool bCheckDirty = false;
			const bool bPromptToSave = false;
			TArray<UPackage*> FailedPackages;
			const FEditorFileUtils::EPromptReturnCode Return = FEditorFileUtils::PromptForCheckoutAndSave(PackagesToSave, bCheckDirty, bPromptToSave, &FailedPackages);
			if(FailedPackages.Num() > 0)
			{
				// don't try and add files that failed to save - remove them from the list
				for(auto FailedPackageIt = FailedPackages.CreateConstIterator(); FailedPackageIt; FailedPackageIt++)
				{
					PackagesToAdd.Remove((*FailedPackageIt)->GetName());
				}
			}
		}

		if ( PackagesToAdd.Num() > 0 )
		{
			SourceControlProvider.Execute(ISourceControlOperation::Create<FMarkForAdd>(), SourceControlHelpers::PackageFilenames(PackagesToAdd));
		}
	}
}

void FAssetFolderContextMenu::ExecuteSCCCheckIn()
{
	// Get a list of package names in the selected paths
	TArray<FString> PackageNames;
	GetPackageNamesInSelectedPaths(PackageNames);

	// Form a list of loaded packages to prompt for save
	TArray<UPackage*> LoadedPackages;
	for ( auto PackageIt = PackageNames.CreateConstIterator(); PackageIt; ++PackageIt )
	{
		UPackage* Package = FindPackage(NULL, **PackageIt);
		if ( Package )
		{
			LoadedPackages.Add(Package);
		}
	}

	// Prompt the user to ask if they would like to first save any dirty packages they are trying to check-in
	const FEditorFileUtils::EPromptReturnCode UserResponse = FEditorFileUtils::PromptForCheckoutAndSave( LoadedPackages, true, true );

	// If the user elected to save dirty packages, but one or more of the packages failed to save properly OR if the user
	// canceled out of the prompt, don't follow through on the check-in process
	const bool bShouldProceed = ( UserResponse == FEditorFileUtils::EPromptReturnCode::PR_Success || UserResponse == FEditorFileUtils::EPromptReturnCode::PR_Declined );
	if ( bShouldProceed )
	{
		TArray<FString> PendingDeletePaths;
		for (const auto& Path : SelectedPaths)
		{
			PendingDeletePaths.Add(FPaths::ConvertRelativePathToFull(FPackageName::LongPackageNameToFilename(Path + TEXT("/"))));
		}

		const bool bUseSourceControlStateCache = false;
		FSourceControlWindows::PromptForCheckin(bUseSourceControlStateCache, PackageNames, PendingDeletePaths);
	}
	else
	{
		// If a failure occurred, alert the user that the check-in was aborted. This warning shouldn't be necessary if the user cancelled
		// from the dialog, because they obviously intended to cancel the whole operation.
		if ( UserResponse == FEditorFileUtils::EPromptReturnCode::PR_Failure )
		{
			FMessageDialog::Open( EAppMsgType::Ok, NSLOCTEXT("UnrealEd", "SCC_Checkin_Aborted", "Check-in aborted as a result of save failure.") );
		}
	}
}

void FAssetFolderContextMenu::ExecuteSCCSync() const
{
	AssetViewUtils::SyncPathsFromSourceControl(SelectedPaths);
}

void FAssetFolderContextMenu::ExecuteSCCConnect() const
{
	ISourceControlModule::Get().ShowLoginDialog(FSourceControlLoginClosed(), ELoginWindowMode::Modeless);
}

bool FAssetFolderContextMenu::CanExecuteSCCCheckOut() const
{
	return bCanExecuteSCCCheckOut && SelectedPaths.Num() > 0;
}

bool FAssetFolderContextMenu::CanExecuteSCCOpenForAdd() const
{
	return bCanExecuteSCCOpenForAdd && SelectedPaths.Num() > 0;
}

bool FAssetFolderContextMenu::CanExecuteSCCCheckIn() const
{
	return bCanExecuteSCCCheckIn && SelectedPaths.Num() > 0;
}

bool FAssetFolderContextMenu::CanExecuteSCCSync() const
{
	return SelectedPaths.Num() > 0;
}

bool FAssetFolderContextMenu::CanExecuteSCCConnect() const
{
	return (!ISourceControlModule::Get().IsEnabled() || !ISourceControlModule::Get().GetProvider().IsAvailable()) && SelectedPaths.Num() > 0;
}

void FAssetFolderContextMenu::CacheCanExecuteVars()
{
	// Cache whether we can execute any of the source control commands
	bCanExecuteSCCCheckOut = false;
	bCanExecuteSCCOpenForAdd = false;
	bCanExecuteSCCCheckIn = false;

	ISourceControlProvider& SourceControlProvider = ISourceControlModule::Get().GetProvider();
	if ( SourceControlProvider.IsEnabled() && SourceControlProvider.IsAvailable() )
	{
		TArray<FString> PackageNames;
		GetPackageNamesInSelectedPaths(PackageNames);

		// Check the SCC state for each package in the selected paths
		for ( auto PackageIt = PackageNames.CreateConstIterator(); PackageIt; ++PackageIt )
		{
			FSourceControlStatePtr SourceControlState = SourceControlProvider.GetState(SourceControlHelpers::PackageFilename(*PackageIt), EStateCacheUsage::Use);
			if(SourceControlState.IsValid())
			{
				if ( SourceControlState->CanCheckout() )
				{
					bCanExecuteSCCCheckOut = true;
				}
				else if ( !SourceControlState->IsSourceControlled() )
				{
					bCanExecuteSCCOpenForAdd = true;
				}
				else if ( SourceControlState->CanCheckIn() )
				{
					bCanExecuteSCCCheckIn = true;
				}
			}

			if ( bCanExecuteSCCCheckOut && bCanExecuteSCCOpenForAdd && bCanExecuteSCCCheckIn )
			{
				// All SCC options are available, no need to keep iterating
				break;
			}
		}
	}
}

void FAssetFolderContextMenu::GetPackageNamesInSelectedPaths(TArray<FString>& OutPackageNames) const
{
	// Load the asset registry module
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::Get().LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));

	// Form a filter from the paths
	FARFilter Filter;
	Filter.bRecursivePaths = true;
	for (int32 PathIdx = 0; PathIdx < SelectedPaths.Num(); ++PathIdx)
	{
		const FString& Path = SelectedPaths[PathIdx];
		Filter.PackagePaths.Add(FName(*Path));
	}

	// Query for a list of assets in the selected paths
	TArray<FAssetData> AssetList;
	AssetRegistryModule.Get().GetAssets(Filter, AssetList);

	// Form a list of unique package names from the assets
	TSet<FName> UniquePackageNames;
	for (int32 AssetIdx = 0; AssetIdx < AssetList.Num(); ++AssetIdx)
	{
		UniquePackageNames.Add(AssetList[AssetIdx].PackageName);
	}

	// Add all unique package names to the output
	for ( auto PackageIt = UniquePackageNames.CreateConstIterator(); PackageIt; ++PackageIt )
	{
		OutPackageNames.Add( (*PackageIt).ToString() );
	}
}

FString FAssetFolderContextMenu::GetFirstSelectedPath() const
{
	return SelectedPaths.Num() > 0 ? SelectedPaths[0] : TEXT("");
}

#undef LOCTEXT_NAMESPACE
