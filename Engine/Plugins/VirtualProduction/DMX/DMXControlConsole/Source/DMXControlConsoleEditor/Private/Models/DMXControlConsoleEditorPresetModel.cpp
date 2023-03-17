// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXControlConsoleEditorPresetModel.h"

#include "DMXControlConsoleEditorFromLegacyUpgradeHandler.h"
#include "DMXControlConsoleEditorManager.h"
#include "DMXControlConsoleEditorSelection.h"
#include "DMXControlConsolePreset.h"

#include "AssetToolsModule.h"
#include "ContentBrowserModule.h"
#include "Editor.h"
#include "FileHelpers.h"
#include "IContentBrowserSingleton.h"
#include "ScopedTransaction.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Editor/Transactor.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/CoreDelegates.h"
#include "Misc/FileHelper.h"
#include "Misc/MessageDialog.h"


#define LOCTEXT_NAMESPACE "DMXControlConsoleEditorPreset"

void UDMXControlConsoleEditorPresetModel::LoadDefaultPreset()
{
	if (UDMXControlConsolePreset* DefaultPreset = Cast<UDMXControlConsolePreset>(SavedPreset.TryLoad()))
	{
		EditorPreset = DefaultPreset;
	}
	else
	{
		CreateNewPreset();
	}

	OnPresetLoadedDelegate.Broadcast();
}

void UDMXControlConsoleEditorPresetModel::CreateNewPreset()
{
	if (EditorPreset) // This may be invalid when the editor starts up and there's no console 
	{
		const FText SaveBeforeNewPresetDialogText = LOCTEXT("SaveBeforeNewDialog", "Current Control Console has unsaved changes. Would you like to save now?");

		if (EditorPreset->IsAsset() && EditorPreset->GetOutermost()->IsDirty())
		{
			const EAppReturnType::Type DialogResult = FMessageDialog::Open(EAppMsgType::YesNo, SaveBeforeNewPresetDialogText);
			if (DialogResult == EAppReturnType::Yes)
			{
				SavePreset();
			}
		}
		else if(!EditorPreset->IsAsset() && EditorPreset->GetControlConsole() && !EditorPreset->GetControlConsole()->GetAllFaderGroups().IsEmpty())
		{
			const EAppReturnType::Type DialogResult = FMessageDialog::Open(EAppMsgType::YesNo, SaveBeforeNewPresetDialogText);
			if (DialogResult == EAppReturnType::Yes)
			{
				SavePresetAs();
			}
		}
	}

	const FScopedTransaction CreateNewPresetTransaction(LOCTEXT("CreateNewPresetTransaction", "Create new Control Console Preset"));

	Modify();
	EditorPreset = NewObject<UDMXControlConsolePreset>(GetTransientPackage(), NAME_None, RF_Transactional);

	SavePresetToConfig();

	OnPresetLoadedDelegate.Broadcast();
}

void UDMXControlConsoleEditorPresetModel::SavePreset()
{
	if (!ensureMsgf(EditorPreset, TEXT("Cannot save preset as asset. Editor Preset is null.")))
	{
		return;
	}

	if (EditorPreset->IsAsset())
	{
		// Save existing preset asset
		constexpr bool bCheckDirty = false;
		constexpr bool bPromptToSave = false;
		FEditorFileUtils::EPromptReturnCode PromptReturnCode = FEditorFileUtils::PromptForCheckoutAndSave({ EditorPreset->GetPackage() }, bCheckDirty, bPromptToSave);
	}
	else
	{
		// Save as a new preset
		SavePresetAs();
	}
}

void UDMXControlConsoleEditorPresetModel::SavePresetAs()
{
	// Save a new preset asset
	FString PackagePath;
	FString AssetName;
	if (PromptSavePresetPackage(PackagePath, AssetName))
	{
		UDMXControlConsolePreset* NewPreset = CreateNewPresetAsset(PackagePath, AssetName, EditorPreset->GetControlConsole());
		if (NewPreset)
		{
			FDMXControlConsoleEditorManager::Get().GetSelectionHandler()->ClearSelection();

			const FScopedTransaction SavePresetAsTransaction(LOCTEXT("SavePresetAsTransaction", "Save Control Console Preset to new Asset"));

			Modify();
			EditorPreset = NewPreset;

			SavePresetToConfig();

			OnPresetLoadedDelegate.Broadcast();
		}
	}
}

void UDMXControlConsoleEditorPresetModel::LoadPreset(const FAssetData& AssetData)
{	
	UDMXControlConsolePreset* NewEditorPreset = Cast<UDMXControlConsolePreset>(AssetData.GetAsset());
	if (NewEditorPreset)
	{
		FDMXControlConsoleEditorManager::Get().GetSelectionHandler()->ClearSelection();

		const FScopedTransaction LoadPresetTransaction(LOCTEXT("LoadPresetTransaction", "Load Control Console Preset"));

		Modify();
		EditorPreset = NewEditorPreset;

		SavePresetToConfig();

		OnPresetLoadedDelegate.Broadcast();
	}
}

UDMXControlConsolePreset* UDMXControlConsoleEditorPresetModel::CreateNewPresetAsset(const FString& SavePackagePath, const FString& SaveAssetName, UDMXControlConsole* SourceControlConsole) const
{
	if (!ensureMsgf(SourceControlConsole, TEXT("Cannot create a preset asset from a null control console")))
	{
		return nullptr;
	}

	if (!ensureMsgf(FPackageName::IsValidLongPackageName(SavePackagePath / SaveAssetName), TEXT("Invalid package name when trying to save create Control Console Preset asset. Failed to create asset.")))
	{
		return nullptr;
	}

	const FString PackageName = SavePackagePath / SaveAssetName;
	UPackage* Package = CreatePackage(*PackageName);
	check(Package);
	Package->FullyLoad();

	UDMXControlConsolePreset* NewPreset = NewObject<UDMXControlConsolePreset>(Package, FName(SaveAssetName), RF_Public | RF_Standalone | RF_Transactional);
	NewPreset->CopyControlConsole(SourceControlConsole);

	constexpr bool bCheckDirty = false;
	constexpr bool bPromptToSave = false;
	FEditorFileUtils::EPromptReturnCode PromptReturnCode = FEditorFileUtils::PromptForCheckoutAndSave({ NewPreset->GetPackage() }, bCheckDirty, bPromptToSave);
	if (PromptReturnCode != FEditorFileUtils::EPromptReturnCode::PR_Success)
	{
		return nullptr;
	}

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	FAssetRegistryModule::AssetCreated(NewPreset);

	return NewPreset;
}

void UDMXControlConsoleEditorPresetModel::PostInitProperties()
{
	Super::PostInitProperties();

	if (ensureMsgf(HasAnyFlags(RF_ClassDefaultObject), TEXT("DMXControlConsoleEditorPresetModel should not be instantiated. Instead refer to the CDO.")))
	{
		// Deffer initialization to engine being fully loaded
		FCoreDelegates::OnFEngineLoopInitComplete.AddUObject(this, &UDMXControlConsoleEditorPresetModel::OnFEngineLoopInitComplete);
	}
}

void UDMXControlConsoleEditorPresetModel::BeginDestroy()
{
	Super::BeginDestroy();

	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		SavePresetToConfig();
	}
}

void UDMXControlConsoleEditorPresetModel::SavePresetToConfig()
{
	if (SavedPreset != EditorPreset)
	{
		SavedPreset = IsValid(EditorPreset) && EditorPreset->IsAsset() ? EditorPreset : nullptr;
		SaveConfig();
	}
}

void UDMXControlConsoleEditorPresetModel::FinalizeLoadPreset(UDMXControlConsolePreset* PresetToLoad)
{
	// Stop sending DMX before loading a new console
	UDMXControlConsole* CurrentControlConsole = EditorPreset->GetControlConsole();
	if (CurrentControlConsole)
	{
		CurrentControlConsole->StopSendingDMX();
	}

	FDMXControlConsoleEditorManager::Get().GetSelectionHandler()->ClearSelection();

	EditorPreset = PresetToLoad;

	SavePresetToConfig();

	GEditor->Trans->Reset(LOCTEXT("ClearUndoTransAfterLoad", "Loaded Control Console Preset"));

	OnPresetLoadedDelegate.Broadcast();
}

void UDMXControlConsoleEditorPresetModel::OnLoadDialogEnterPressedPreset(const TArray<FAssetData>& InPresetAssets)
{
	if (!ensureMsgf(InPresetAssets.Num() == 1, TEXT("Unexpected none or many assets selected when loading Control Console Preset.")))
	{
		return;
	}

	// Continue as if the preset was selected direct
	OnLoadDialogSelectedPreset(InPresetAssets[0]);
}

void UDMXControlConsoleEditorPresetModel::OnLoadDialogSelectedPreset(const FAssetData& PresetAsset)
{
	UDMXControlConsolePreset* PresetToLoad = Cast<UDMXControlConsolePreset>(PresetAsset.GetAsset());
	if (!ensureAlwaysMsgf(PresetToLoad && PresetToLoad->GetControlConsole(), TEXT("Invalid Preset or Control Console in preset when loading preset.")))
	{
		return;
	}

	FinalizeLoadPreset(PresetToLoad);
}

bool UDMXControlConsoleEditorPresetModel::OpenSaveDialog(const FString& InDefaultPath, const FString& InNewNameSuggestion, FString& OutPackageName) const
{
	FSaveAssetDialogConfig SaveAssetDialogConfig;
	{
		SaveAssetDialogConfig.DefaultPath = InDefaultPath;
		SaveAssetDialogConfig.DefaultAssetName = InNewNameSuggestion;
		SaveAssetDialogConfig.AssetClassNames.Add(UDMXControlConsolePreset::StaticClass()->GetClassPathName());
		SaveAssetDialogConfig.ExistingAssetPolicy = ESaveAssetDialogExistingAssetPolicy::AllowButWarn;
		SaveAssetDialogConfig.DialogTitleOverride = LOCTEXT("SavePresetToConfigPresetDialogTitle", "Save Config Preset");
	}

	FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
	FString SaveObjectPath = ContentBrowserModule.Get().CreateModalSaveAssetDialog(SaveAssetDialogConfig);

	if (!SaveObjectPath.IsEmpty())
	{
		OutPackageName = FPackageName::ObjectPathToPackageName(SaveObjectPath);
		return true;
	}

	return false;
}

bool UDMXControlConsoleEditorPresetModel::PromptSavePresetPackage(FString& OutSavePackagePath, FString& OutSaveAssetName) const
{
	FString AssetName;
	FString DialogStartPath;
	if (EditorPreset->GetPackage() == GetTransientPackage())
	{
		DialogStartPath = FPaths::ProjectContentDir();
	}
	else
	{
		AssetName = EditorPreset->GetName();
		DialogStartPath = EditorPreset->GetOutermost()->GetName();
	}

	FString UniquePackageName;
	FString UniqueAssetName;
	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
	AssetToolsModule.Get().CreateUniqueAssetName(DialogStartPath / AssetName, TEXT(""), UniquePackageName, UniqueAssetName);

	const FString DialogStartName = FPaths::GetCleanFilename(DialogStartPath / AssetName);

	// Show a save dialog and let the user select a package name
	FString SaveObjectPath;
	bool bFilenameValid = false;
	while (!bFilenameValid)
	{
		if (!OpenSaveDialog(DialogStartPath, DialogStartName, SaveObjectPath))
		{
			return false;
		}

		FText OutError;
		bFilenameValid = FFileHelper::IsFilenameValidForSaving(SaveObjectPath, OutError);
	}

	const FString SavePackageName = FPackageName::ObjectPathToPackageName(SaveObjectPath);
	OutSavePackagePath = FPaths::GetPath(SavePackageName);
	OutSaveAssetName = FPaths::GetBaseFilename(SavePackageName);

	return true;
}

void UDMXControlConsoleEditorPresetModel::OnFEngineLoopInitComplete()
{
	if (ensureMsgf(HasAnyFlags(RF_ClassDefaultObject), TEXT("DMXControlConsoleEditorPresetModel should not be instantiated. Use the CDO instead.")))
	{
		SetFlags(GetFlags() | RF_Transactional);

		// Try UpgradePath if configurations settings have data from Output Consoles, the Console that was used before 5.2.
		// Otherwise load the default preset
		if (!FDMXControlConsoleEditorFromLegacyUpgradeHandler::TryUpgradePathFromLegacy())
		{
			LoadDefaultPreset();
		}
	}
}

#undef LOCTEXT_NAMESPACE
