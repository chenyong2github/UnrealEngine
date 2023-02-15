// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXControlConsoleEditorManager.h"

#include "DMXControlConsole.h"
#include "DMXControlConsoleEditorSelection.h"
#include "DMXControlConsoleEditorSettings.h"
#include "DMXControlConsolePreset.h"

#include "AssetToolsModule.h"
#include "ContentBrowserModule.h"
#include "FileHelpers.h"
#include "IContentBrowserSingleton.h"
#include "ScopedTransaction.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/CoreDelegates.h"
#include "Misc/FileHelper.h"
#include "Misc/MessageDialog.h"


#define LOCTEXT_NAMESPACE "DMXControlConsoleEditorManager"

TSharedPtr<FDMXControlConsoleEditorManager> FDMXControlConsoleEditorManager::Instance;

FDMXControlConsoleEditorManager::~FDMXControlConsoleEditorManager()
{
	FCoreDelegates::OnFEngineLoopInitComplete.RemoveAll(this);
	FCoreDelegates::OnEnginePreExit.RemoveAll(this);
}

void FDMXControlConsoleEditorManager::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(ControlConsole);
}

FDMXControlConsoleEditorManager& FDMXControlConsoleEditorManager::Get()
{
	if (!Instance.IsValid())
	{
		Instance = MakeShareable(new FDMXControlConsoleEditorManager());
	}
	checkf(Instance.IsValid() && Instance->ControlConsole, TEXT("Unexpected: Invalid DMX Control Console manager instance, or DMX Control Console is null."));

	return *Instance.Get();
}

TSharedRef<FDMXControlConsoleEditorSelection> FDMXControlConsoleEditorManager::GetSelectionHandler()
{
	if (!SelectionHandler.IsValid())
	{
		SelectionHandler = MakeShareable(new FDMXControlConsoleEditorSelection(AsShared()));
	}

	return SelectionHandler.ToSharedRef();
}

UDMXControlConsolePreset* FDMXControlConsoleEditorManager::GetPreset() const
{
	return ControlConsole ? Cast<UDMXControlConsolePreset>(ControlConsole->GetOuter()) : nullptr;
}

UDMXControlConsole* FDMXControlConsoleEditorManager::CreateNewTransientConsole()
{
	if (ControlConsole)
	{
		// Don't create the new console if the current one is already transient
		if (ControlConsole->GetPackage() == GetTransientPackage())
		{
			return nullptr;
		}

		ControlConsole->StopSendingDMX();
	}

	ControlConsole = NewObject<UDMXControlConsole>(GetTransientPackage(), NAME_None, RF_Transactional);
	OnControlConsoleLoaded.Broadcast();

	return ControlConsole;
}

UDMXControlConsolePreset* FDMXControlConsoleEditorManager::CreateNewPresetAsset(FString DesiredPackageName, UDMXControlConsole* SourceControlConsole)
{
	FString PackageName;
	FString AssetName;

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
	AssetToolsModule.Get().CreateUniqueAssetName(DesiredPackageName, TEXT(""), PackageName, AssetName);

	UPackage* Package = CreatePackage(*PackageName);
	check(Package);
	Package->FullyLoad();

	UDMXControlConsolePreset* NewPreset = NewObject<UDMXControlConsolePreset>(Package, FName(AssetName), RF_Public | RF_Standalone | RF_Transactional);
	UDMXControlConsole* CurrentControlConsole = SourceControlConsole ? SourceControlConsole : ControlConsole.Get();
	
	NewPreset->CopyControlConsole(CurrentControlConsole);

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	FAssetRegistryModule::AssetCreated(NewPreset);

	constexpr bool bCheckDirty = false;
	constexpr bool bPromptToSave = false;
	FEditorFileUtils::EPromptReturnCode PromptReturnCode = FEditorFileUtils::PromptForCheckoutAndSave({ NewPreset->GetPackage() }, bCheckDirty, bPromptToSave);
	if (PromptReturnCode != FEditorFileUtils::EPromptReturnCode::PR_Success)
	{
		return nullptr;
	}

	return NewPreset;
}

void FDMXControlConsoleEditorManager::Save()
{
	if (UDMXControlConsolePreset* CurrentPreset = GetPreset())
	{
		constexpr bool bCheckDirty = false;
		constexpr bool bPromptToSave = false;
		FEditorFileUtils::EPromptReturnCode PromptReturnCode = FEditorFileUtils::PromptForCheckoutAndSave({ CurrentPreset->GetPackage() }, bCheckDirty, bPromptToSave);
	}
	else
	{
		SaveAs();
	}

	SaveConfig();
}

void FDMXControlConsoleEditorManager::SaveAs()
{
	FString PackageName;
	if (!GetSavePresetPackageName(PackageName))
	{
		return;
	}

	CreateNewPresetAsset(PackageName, ControlConsole);

	SaveConfig();
}

void FDMXControlConsoleEditorManager::Load()
{
	FOpenAssetDialogConfig OpenAssetDialogConfig;
	{
		OpenAssetDialogConfig.DialogTitleOverride = LOCTEXT("OpenPresetDialogTitle", "Open Control Console Preset");
		OpenAssetDialogConfig.AssetClassNames.Add(UDMXControlConsolePreset::StaticClass()->GetClassPathName());
		OpenAssetDialogConfig.bAllowMultipleSelection = false;

		if (UDMXControlConsolePreset* LastLoadedPreset = GetPreset())
		{
			OpenAssetDialogConfig.DefaultPath = LastLoadedPreset->GetOutermost()->GetPathName();
		}
		else
		{
			OpenAssetDialogConfig.DefaultPath = FPaths::ProjectContentDir();
		}
	}

	FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
	ContentBrowserModule.Get().CreateOpenAssetDialog(OpenAssetDialogConfig,
		FOnAssetsChosenForOpen::CreateSP(this, &FDMXControlConsoleEditorManager::OnLoadDialogEnterPressedPreset),
		FOnAssetDialogCancelled());
}

void FDMXControlConsoleEditorManager::SendDMX()
{
	if (!ensureMsgf(ControlConsole, TEXT("Invalid DMX Control Console, can't send DMX correctly.")))
	{
		return;
	}

	ControlConsole->StartSendingDMX();
}

void FDMXControlConsoleEditorManager::StopDMX()
{
	if (!ensureMsgf(ControlConsole, TEXT("Invalid DMX Control Console, can't stop DMX correctly.")))
	{
		return;
	}

	ControlConsole->StopSendingDMX();
}

bool FDMXControlConsoleEditorManager::IsSendingDMX() const
{
	return IsValid(ControlConsole) ? ControlConsole->IsSendingDMX() : false;
}

void FDMXControlConsoleEditorManager::ClearAll()
{
	if (!ensureMsgf(ControlConsole, TEXT("Invalid DMX Control Console, can't clear fader group rows correctly.")))
	{
		return;
	}

	SelectionHandler->ClearSelection();

	const FScopedTransaction ClearAllTransaction(LOCTEXT("ClearAllTransaction", "Clear All"));
	ControlConsole->Modify();

	ControlConsole->Reset();
}

FDMXControlConsoleEditorManager::FDMXControlConsoleEditorManager()
{
	// Always create a valid control console even if this constructed early.
	ControlConsole = CreateNewTransientConsole();
 
	// Deffer further initialization to OnFEngineLoopInitComplete where the engine is fully initialized.
	FCoreDelegates::OnFEngineLoopInitComplete.AddRaw(this, &FDMXControlConsoleEditorManager::OnFEngineLoopInitComplete);
	FCoreDelegates::OnEnginePreExit.AddRaw(this, &FDMXControlConsoleEditorManager::OnEnginePreExit);
}

void FDMXControlConsoleEditorManager::OnLoadDialogEnterPressedPreset(const TArray<FAssetData>& InPresetAssets)
{
	if (!ensureMsgf(InPresetAssets.Num() == 1, TEXT("Unexpected none or many assets selected when loading Control Console Preset.")))
	{
		return;
	}

	OnLoadDialogSelectedPreset(InPresetAssets[0]);
}

void FDMXControlConsoleEditorManager::OnLoadDialogSelectedPreset(const FAssetData& PresetAsset)
{
	UDMXControlConsolePreset* Preset = Cast<UDMXControlConsolePreset>(PresetAsset.GetAsset());
	if (!ensureAlwaysMsgf(Preset && Preset->GetControlConsole(), TEXT("Invalid Preset or Control Console in preset when loading preset.")))
	{
		return;
	}

	if(Preset->GetControlConsole() == ControlConsole)
	{
		return;
	}

	if (ControlConsole && ControlConsole->GetOutermost()->IsDirty())
	{
		const FText TitleText = LOCTEXT("UnsavedControlConsoleWarningTitle", "Unsaved Changes to Control Console");
		const FText MessageText = LOCTEXT("UnsavedControlConsoleWarningMessage", "The changes made to the Control Console will be lost by importing another preset. Do you want to continue with this import?");

		if (FMessageDialog::Open(EAppMsgType::YesNo, MessageText, &TitleText) == EAppReturnType::No)
		{
			return;
		}
	}

	// Copy the console to this editor
	const FScopedTransaction Transaction(LOCTEXT("ImportPresetTransaction", "Import Control Console Preset"));

	ControlConsole->StopSendingDMX();
	ControlConsole = Preset->GetControlConsole();

	SaveConfig();

	OnControlConsoleLoaded.Broadcast();
}

bool FDMXControlConsoleEditorManager::OpenSaveDialog(const FString& InDefaultPath, const FString& InNewNameSuggestion, FString& OutPackageName) const
{
	FSaveAssetDialogConfig SaveAssetDialogConfig;
	{
		SaveAssetDialogConfig.DefaultPath = InDefaultPath;
		SaveAssetDialogConfig.DefaultAssetName = InNewNameSuggestion;
		SaveAssetDialogConfig.AssetClassNames.Add(UDMXControlConsolePreset::StaticClass()->GetClassPathName());
		SaveAssetDialogConfig.ExistingAssetPolicy = ESaveAssetDialogExistingAssetPolicy::AllowButWarn;
		SaveAssetDialogConfig.DialogTitleOverride = LOCTEXT("SaveConfigPresetDialogTitle", "Save Config Preset");
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

bool FDMXControlConsoleEditorManager::GetSavePresetPackageName(FString& OutPackageName) const
{
	FString AssetName;
	FString DialogStartPath;
	if (UDMXControlConsolePreset* DefaultPreset = GetPreset())
	{
		AssetName = DefaultPreset->GetName();
		DialogStartPath = DefaultPreset->GetOutermost()->GetName();
	}
	if (DialogStartPath.IsEmpty())
	{
		DialogStartPath = FPaths::ProjectContentDir();
	}

	FString UniquePackageName;
	FString UniqueAssetName;
	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
	AssetToolsModule.Get().CreateUniqueAssetName(DialogStartPath / AssetName, TEXT(""), UniquePackageName, UniqueAssetName);

	const FString DialogStartName = FPaths::GetCleanFilename(UniqueAssetName);

	// Show a save dialog and let the user select a package name
	FString UserPackageName;
	FString NewPackageName;
	
	bool bFilenameValid = false;
	while (!bFilenameValid)
	{
		if (!OpenSaveDialog(DialogStartPath, DialogStartName, UserPackageName))
		{
			return false;
		}

		NewPackageName = UserPackageName;

		FText OutError;
		bFilenameValid = FFileHelper::IsFilenameValidForSaving(NewPackageName, OutError);
	}

	OutPackageName = NewPackageName;
	return true;
}

void FDMXControlConsoleEditorManager::SaveConfig()
{
	UDMXControlConsoleEditorSettings* ControlConsoleEditorSettings = GetMutableDefault<UDMXControlConsoleEditorSettings>();
	ControlConsoleEditorSettings->ControlConsolePreset = GetPreset();

	ControlConsoleEditorSettings->SaveConfig();
}

void FDMXControlConsoleEditorManager::LoadConfig()
{
	const UDMXControlConsoleEditorSettings* ControlConsoleEditorSettings = GetDefault<UDMXControlConsoleEditorSettings>();
	FSoftObjectPath AssetPath = ControlConsoleEditorSettings->ControlConsolePreset;
	
	UDMXControlConsolePreset* DefaultPreset = Cast<UDMXControlConsolePreset>(AssetPath.TryLoad());
	if (DefaultPreset)
	{
		ControlConsole = DefaultPreset->GetControlConsole();
	}
	else
	{
		CreateNewTransientConsole();
	}

	OnControlConsoleLoaded.Broadcast();
}

void FDMXControlConsoleEditorManager::OnFEngineLoopInitComplete()
{
	LoadConfig();
}

void FDMXControlConsoleEditorManager::OnEnginePreExit()
{
	if (ensureMsgf(ControlConsole, TEXT("Invalid control console when shutting down Unreal Engine")))
	{
		ControlConsole->StopSendingDMX();
	}

	Instance.Reset();
}

#undef LOCTEXT_NAMESPACE
