// Copyright Epic Games, Inc. All Rights Reserved.

#include "DataValidationModule.h"
#include "UObject/Object.h"
#include "UObject/SoftObjectPath.h"
#include "GameFramework/HUD.h"

#include "Framework/Application/SlateApplication.h"
#include "ToolMenus.h"
#include "EditorStyleSet.h"
#include "AssetRegistryModule.h"
#include "Misc/MessageDialog.h"
#include "DataValidationCommandlet.h"
#include "LevelEditor.h"
#include "Misc/FeedbackContext.h"
#include "Misc/OutputDeviceConsole.h"
#include "Modules/ModuleManager.h"

#include "ContentBrowserModule.h"
#include "ContentBrowserDelegates.h"

#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"
#include "EditorValidatorSubsystem.h"
#include "ISettingsModule.h"

#define LOCTEXT_NAMESPACE "DataValidationModule"

class FDataValidationModule : public IDataValidationModule
{
	// Begin IModuleInterface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	// End IModuleInterface

	/** Validates selected assets and opens a window to report the results. If bValidateDependencies is true it will also validate any assets that the selected assets depend on. */
	virtual void ValidateAssets(TArray<FAssetData> SelectedAssets, bool bValidateDependencies) override;

	void ValidateFolders(TArray<FString> SelectedFolders);

private:
	TSharedRef<FExtender> OnExtendContentBrowserAssetSelectionMenu(const TArray<FAssetData>& SelectedAssets);
	TSharedRef<FExtender> OnExtendContentBrowserPathSelectionMenu(const TArray<FString>& SelectedPaths);
	void CreateDataValidationContentBrowserAssetMenu(FMenuBuilder& MenuBuilder, TArray<FAssetData> SelectedAssets);
	void CreateDataValidationContentBrowserPathMenu(FMenuBuilder& MenuBuilder, TArray<FString> SelectedPaths);
	void OnPackageSaved(const FString& PackageFileName, UObject* PackageObj);

	// Adds Asset and any assets it depends on to the set DependentAssets
	void FindAssetDependencies(const FAssetRegistryModule& AssetRegistryModule, const FAssetData& Asset, TSet<FAssetData>& DependentAssets);

	void RegisterMenus();
	static FText Menu_ValidateDataGetTitle();
	static void Menu_ValidateData();

	FDelegateHandle ContentBrowserAssetExtenderDelegateHandle;
	FDelegateHandle ContentBrowserPathExtenderDelegateHandle;
};

IMPLEMENT_MODULE(FDataValidationModule, DataValidation)

void FDataValidationModule::StartupModule()
{	
	if (!IsRunningCommandlet() && !IsRunningGame() && FSlateApplication::IsInitialized())
	{
		// Register content browser hook
		FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
		TArray<FContentBrowserMenuExtender_SelectedAssets>& CBAssetMenuExtenderDelegates = ContentBrowserModule.GetAllAssetViewContextMenuExtenders();

		CBAssetMenuExtenderDelegates.Add(FContentBrowserMenuExtender_SelectedAssets::CreateRaw(this, &FDataValidationModule::OnExtendContentBrowserAssetSelectionMenu));
		ContentBrowserAssetExtenderDelegateHandle = CBAssetMenuExtenderDelegates.Last().GetHandle();

		TArray<FContentBrowserMenuExtender_SelectedPaths>& CBFolderMenuExtenderDelegates = ContentBrowserModule.GetAllPathViewContextMenuExtenders();

		CBFolderMenuExtenderDelegates.Add(FContentBrowserMenuExtender_SelectedPaths::CreateRaw(this, &FDataValidationModule::OnExtendContentBrowserPathSelectionMenu));
		ContentBrowserPathExtenderDelegateHandle = CBFolderMenuExtenderDelegates.Last().GetHandle();

		// add the File->DataValidation menu subsection
		FCoreDelegates::OnPostEngineInit.AddRaw(this, &FDataValidationModule::RegisterMenus);

		// Add save callback
		UPackage::PackageSavedEvent.AddRaw(this, &FDataValidationModule::OnPackageSaved);

		ISettingsModule& SettingsModule = FModuleManager::LoadModuleChecked<ISettingsModule>("Settings");
		SettingsModule.RegisterSettings("Editor", "Advanced", "DataValidation",
			LOCTEXT("DataValidationName", "Data Validation"),
			LOCTEXT("DataValidationDescription", "Settings related to validating assets in the editor."),
			GetMutableDefault<UDataValidationSettings>()
		);
	}
}

void FDataValidationModule::ShutdownModule()
{
	if (!IsRunningCommandlet() && !IsRunningGame() && !IsRunningDedicatedServer())
	{
		FContentBrowserModule* ContentBrowserModule = FModuleManager::GetModulePtr<FContentBrowserModule>(TEXT("ContentBrowser"));
		if (ContentBrowserModule)
		{
			TArray<FContentBrowserMenuExtender_SelectedAssets>& CBMenuExtenderDelegates = ContentBrowserModule->GetAllAssetViewContextMenuExtenders();
			CBMenuExtenderDelegates.RemoveAll([this](const FContentBrowserMenuExtender_SelectedAssets& Delegate) { return Delegate.GetHandle() == ContentBrowserAssetExtenderDelegateHandle; });
			CBMenuExtenderDelegates.RemoveAll([this](const FContentBrowserMenuExtender_SelectedAssets& Delegate) { return Delegate.GetHandle() == ContentBrowserPathExtenderDelegateHandle; });
		}

		// remove menu extension
		UToolMenus::UnregisterOwner(this);
		FCoreDelegates::OnPostEngineInit.RemoveAll(this);

		UPackage::PackageSavedEvent.RemoveAll(this);
	}
}

void FDataValidationModule::RegisterMenus()
{
	FToolMenuOwnerScoped OwnerScoped(this);
	UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.File");
	FToolMenuSection& Section = Menu->AddSection("DataValidation", LOCTEXT("DataValidation", "DataValidation"), FToolMenuInsert("FileLoadAndSave", EToolMenuInsertType::After));
	Section.AddEntry(FToolMenuEntry::InitMenuEntry(
		"ValidateData",
		TAttribute<FText>::Create(&Menu_ValidateDataGetTitle),
		LOCTEXT("ValidateDataTooltip", "Validates all user data in content directory."),
		FSlateIcon(FEditorStyle::GetStyleSetName(), "DeveloperTools.MenuIcon"),
		FUIAction(FExecuteAction::CreateStatic(&FDataValidationModule::Menu_ValidateData))
	));
}

FText FDataValidationModule::Menu_ValidateDataGetTitle()
{
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	if (AssetRegistryModule.Get().IsLoadingAssets())
	{
		return LOCTEXT("ValidateDataTitleDA", "Validate Data [Discovering Assets]");
	}
	return LOCTEXT("ValidateDataTitle", "Validate Data...");
}

void FDataValidationModule::Menu_ValidateData()
{
	// make sure the asset registry is finished building
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	if (AssetRegistryModule.Get().IsLoadingAssets())
	{
		FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("AssetsStillScanningError", "Cannot run data validation while still discovering assets."));
		return;
	}

	// validate the data
	bool bSuccess = UDataValidationCommandlet::ValidateData();

	// display an error if the task failed
	if (!bSuccess)
	{
		FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("DataValidationError", "An error was encountered during data validation. See the log for details."));
		return;
	}
}

void FDataValidationModule::FindAssetDependencies(const FAssetRegistryModule& AssetRegistryModule, const FAssetData& Asset, TSet<FAssetData>& DependentAssets)
{
	if (Asset.IsValid())
	{
		UObject* Obj = Asset.GetAsset();
		if (Obj)
		{
			const FName SelectedPackageName = Obj->GetOutermost()->GetFName();
			FString PackageString = SelectedPackageName.ToString();
			FString ObjectString = FString::Printf(TEXT("%s.%s"), *PackageString, *FPackageName::GetLongPackageAssetName(PackageString));

			if (!DependentAssets.Contains(Asset))
			{
				DependentAssets.Add(Asset);

				TArray<FName> Dependencies;
				AssetRegistryModule.Get().GetDependencies(SelectedPackageName, Dependencies, EAssetRegistryDependencyType::Packages);

				for (const FName Dependency : Dependencies)
				{
					const FString DependencyPackageString = Dependency.ToString();
					FString DependencyObjectString = FString::Printf(TEXT("%s.%s"), *DependencyPackageString, *FPackageName::GetLongPackageAssetName(DependencyPackageString));

					// recurse on each dependency
					FName ObjectPath(*DependencyObjectString);
					FAssetData DependentAsset = AssetRegistryModule.Get().GetAssetByObjectPath(ObjectPath);

					FindAssetDependencies(AssetRegistryModule, DependentAsset, DependentAssets);
				}
			}
		}
	}
}

void FDataValidationModule::ValidateAssets(TArray<FAssetData> SelectedAssets, bool bValidateDependencies)
{
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));

	TSet<FAssetData> DependentAssets;
	if (bValidateDependencies)
	{
		for (const FAssetData& Asset : SelectedAssets)
		{
			FindAssetDependencies(AssetRegistryModule, Asset, DependentAssets);
		}

		SelectedAssets = DependentAssets.Array();
	}

	UEditorValidatorSubsystem* EditorValidationSubsystem = GEditor->GetEditorSubsystem<UEditorValidatorSubsystem>();
	if (EditorValidationSubsystem)
	{
		EditorValidationSubsystem->ValidateAssets(SelectedAssets, false);
	}
}

void FDataValidationModule::ValidateFolders(TArray<FString> SelectedFolders)
{
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::Get().LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));

	// Form a filter from the paths
	FARFilter Filter;
	Filter.bRecursivePaths = true;
	for (const FString& Folder : SelectedFolders)
	{
		Filter.PackagePaths.Emplace(*Folder);
	}

	// Query for a list of assets in the selected paths
	TArray<FAssetData> AssetList;
	AssetRegistryModule.Get().GetAssets(Filter, AssetList);

	ValidateAssets(AssetList, false);
}

// Extend content browser menu for groups of selected assets
TSharedRef<FExtender> FDataValidationModule::OnExtendContentBrowserAssetSelectionMenu(const TArray<FAssetData>& SelectedAssets)
{
	TSharedRef<FExtender> Extender(new FExtender());

	Extender->AddMenuExtension(
		"AssetContextAdvancedActions",
		EExtensionHook::After,
		nullptr,
		FMenuExtensionDelegate::CreateRaw(this, &FDataValidationModule::CreateDataValidationContentBrowserAssetMenu, SelectedAssets));

	return Extender;
}

// Extend content browser menu for groups of selected assets
void FDataValidationModule::CreateDataValidationContentBrowserAssetMenu(FMenuBuilder& MenuBuilder, TArray<FAssetData> SelectedAssets)
{
	MenuBuilder.AddMenuEntry
	(
		LOCTEXT("ValidateAssetsTabTitle", "Validate Assets"),
		LOCTEXT("ValidateAssetsTooltipText", "Runs data validation on these assets."),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateRaw(this, &FDataValidationModule::ValidateAssets, SelectedAssets, false))
	);
	MenuBuilder.AddMenuEntry
	(
		LOCTEXT("ValidateAssetsAndDependenciesTabTitle", "Validate Assets and Dependencies"),
		LOCTEXT("ValidateAssetsAndDependenciesTooltipText", "Runs data validation on these assets and all assets they depend on."),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateRaw(this, &FDataValidationModule::ValidateAssets, SelectedAssets, true))
	);
}

// Extend content browser menu for groups of asset paths (folders)
TSharedRef<FExtender> FDataValidationModule::OnExtendContentBrowserPathSelectionMenu(const TArray<FString>& SelectedPaths)
{
	TSharedRef<FExtender> Extender(new FExtender());

	Extender->AddMenuExtension(
		"PathContextBulkOperations",
		EExtensionHook::After,
		nullptr,
		FMenuExtensionDelegate::CreateRaw(this, &FDataValidationModule::CreateDataValidationContentBrowserPathMenu, SelectedPaths));

	return Extender;
}

// Extend content browser menu for groups of asset paths (folders)
void FDataValidationModule::CreateDataValidationContentBrowserPathMenu(FMenuBuilder& MenuBuilder, TArray<FString> SelectedPaths)
{
	MenuBuilder.AddMenuEntry
	(
		LOCTEXT("ValidateAssetsPathTabTitle", "Validate Assets in Folder"),
		LOCTEXT("ValidateAssetsPathTooltipText", "Runs data validation on the assets in the selected folder."),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([this, SelectedPaths]
		{
			FString FormattedSelectedPaths;
			for (int32 i = 0; i < SelectedPaths.Num(); ++i)
			{
				FormattedSelectedPaths.Append(SelectedPaths[i]);
				if (i < SelectedPaths.Num() - 1)
				{
					FormattedSelectedPaths.Append(LINE_TERMINATOR);
				}
			}
			FFormatNamedArguments Args;
			Args.Add(TEXT("Paths"), FText::FromString(FormattedSelectedPaths));
			const EAppReturnType::Type Result = FMessageDialog::Open(EAppMsgType::YesNo, FText::Format(LOCTEXT("DataValidationConfirmation", "Are you sure you want to proceed with validating the following folders?\n\n{Paths}"), Args));
			if (Result == EAppReturnType::Yes)
			{
				ValidateFolders(SelectedPaths);
			}
		}))
	);
}

void FDataValidationModule::OnPackageSaved(const FString& PackageFileName, UObject* PackageObj)
{
	UEditorValidatorSubsystem* EditorValidationSubsystem = GEditor->GetEditorSubsystem<UEditorValidatorSubsystem>();
	if (EditorValidationSubsystem && PackageObj)
	{
		EditorValidationSubsystem->ValidateSavedPackage(PackageObj->GetFName());
	}
}

#undef LOCTEXT_NAMESPACE
