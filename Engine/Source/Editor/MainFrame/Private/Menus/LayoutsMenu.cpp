// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#if WITH_EDITOR

#include "Menus/LayoutsMenu.h"
// Runtime/Core
#include "Containers/Array.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "GenericPlatform/GenericPlatformMath.h"
#include "HAL/FileManagerGeneric.h"
#include "Logging/MessageLog.h"
#include "Templates/SharedPointer.h"
// Runtime
#include "CoreGlobals.h"
#include "Framework/Commands/UICommandInfo.h"
#include "Framework/Docking/LayoutService.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Misc/FileHelper.h"
#include "Widgets/Notifications/SNotificationList.h"
// Developer
#include "IDesktopPlatform.h"
#include "DesktopPlatformModule.h"
#include "ToolMenus.h"
// Editor
#include "Classes/EditorStyleSettings.h"
#include "Dialogs/Dialogs.h"
#include "Editor/EditorPerProjectUserSettings.h"
#include "Frame/MainFrameActions.h"
#include "LevelViewportActions.h"
#include "UnrealEdGlobals.h"
#include "UnrealEdMisc.h"

#define LOCTEXT_NAMESPACE "MainFrameActions"

DEFINE_LOG_CATEGORY_STATIC(LogLayoutsMenu, Fatal, All);

// Get LayoutsDirectory path
FString CreateAndGetDefaultLayoutDirInternal()
{
	// Get LayoutsDirectory path
	const FString LayoutsDirectory = FPaths::EngineDefaultLayoutDir();
	// If the directory does not exist, create it (but it will not have saved Layouts inside)
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	if (!PlatformFile.DirectoryExists(*LayoutsDirectory))
	{
		PlatformFile.CreateDirectory(*LayoutsDirectory);
	}
	// Return result
	return LayoutsDirectory;
}

FString CreateAndGetUserLayoutDirInternal()
{
	// Get UserLayoutsDirectory path
	const FString UserLayoutsDirectory = FPaths::EngineUserLayoutDir();
	// If the directory does not exist, create it (but it will not have saved Layouts inside)
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	if (!PlatformFile.DirectoryExists(*UserLayoutsDirectory))
	{
		PlatformFile.CreateDirectory(*UserLayoutsDirectory);
	}
	// Return result
	return UserLayoutsDirectory;
}

TArray<FString> GetIniFilesInFolderInternal(const FString& InStringDirectory)
{
	// Find all ini files in folder
	TArray<FString> LayoutIniFileNames;
	const FString LayoutIniFilePaths = FPaths::Combine(*InStringDirectory, TEXT("*.ini"));
	FFileManagerGeneric::Get().FindFiles(LayoutIniFileNames, *LayoutIniFilePaths, true, false);
	return LayoutIniFileNames;
}

bool TrySaveLayoutOrWarnInternal(const FString& InSourceFilePath, const FString& InTargetFilePath, const FText& InWhatIsThis, const bool bCleanLayoutNameAndDescriptionFieldsIfNoSameValues, const bool bShouldAskBeforeCleaningLayoutNameAndDescriptionFields = false)
{
	// If desired, ask user whether to keep the LayoutName and LayoutDescription fields
	bool bCleanLayoutNameAndDescriptionFields = false;
	// If we are checking whether to clean the fields, we only want to maintain them if we are saving the file into an existing file that already has the same field values
	if (bCleanLayoutNameAndDescriptionFieldsIfNoSameValues)
	{
		GConfig->UnloadFile(InSourceFilePath); // We must re-read it to avoid the Editor to use a previously cached name and description
		const FText LayoutNameSource = FLayoutSaveRestore::LoadSectionFromConfig(InSourceFilePath, "LayoutName");
		const FText LayoutDescriptionSource = FLayoutSaveRestore::LoadSectionFromConfig(InSourceFilePath, "LayoutDescription");
		GConfig->UnloadFile(InTargetFilePath); // We must re-read it to avoid the Editor to use a previously cached name and description
		const FText LayoutNameTarget = FLayoutSaveRestore::LoadSectionFromConfig(InTargetFilePath, "LayoutName");
		const FText LayoutDescriptionTarget = FLayoutSaveRestore::LoadSectionFromConfig(InTargetFilePath, "LayoutDescription");
		// The output target exists (overriding)
		// These fields are not empty in source
		if (!LayoutNameSource.IsEmpty() || !LayoutDescriptionSource.IsEmpty())
		{
			// These fields are different than the ones in target
			if ((LayoutNameSource.ToString() != LayoutNameTarget.ToString()) || (LayoutDescriptionSource.ToString() != LayoutDescriptionTarget.ToString()))
			{
				bCleanLayoutNameAndDescriptionFields = true;
				// We should clean the layout name and description fields, but ask user first
				if (bShouldAskBeforeCleaningLayoutNameAndDescriptionFields)
				{
					// Open Dialog
					const FText TextTitle = LOCTEXT("OverrideLayoutNameAndDescriptionFieldBodyTitle", "Clean UI Layout name and description fields");
					const FText TextBody = FText::Format(
						LOCTEXT("OverrideLayoutNameAndDescriptionFieldBody",
							"You are saving a layout that contains a custom layout name and/or description. Do you also want to copy these 2 properties?\n"
							" - Current layout name: {0}\n"
							" - Current layout description: {1}\n\n"
							"If you select \"Yes\", the displayed name and description of the original layout customization will also be copied into the new configuration file.\n\n"
							"If you select \"No\", these fields will be emptied.\n\n"
							"If you are not sure, select \"Yes\" if you are exporting the layout configuration without making any changes, or \"No\" if you have made or plan to make changes to the layout.\n\n"
						),
						LayoutNameSource, LayoutDescriptionSource);
					const int AppReturnType = OpenMsgDlgInt(EAppMsgType::YesNoCancel, TextBody, TextTitle);
					// Handle user answers
					if (AppReturnType == EAppReturnType::Yes)
					{
						bCleanLayoutNameAndDescriptionFields = false;
					}
					else if (AppReturnType == EAppReturnType::No)
					{
						bCleanLayoutNameAndDescriptionFields = true;
					}
					else if (AppReturnType == EAppReturnType::Cancel)
					{
						return false;
					}
				}
			}
		}
	}
	// Copy: Replace main layout with desired one
	const bool bShouldReplace = true;
	const bool bCopyEvenIfReadOnly = true;
	const bool bCopyAttributes = true;
	if (COPY_Fail == IFileManager::Get().Copy(*InTargetFilePath, *InSourceFilePath, bShouldReplace, bCopyEvenIfReadOnly, bCopyAttributes))
	{
		FMessageLog EditorErrors("EditorErrors");
		FText TextBody;
		FFormatNamedArguments Arguments;
		Arguments.Add(TEXT("WhatIs"), InWhatIsThis);
		// Source does not exist
		if (!FPaths::FileExists(InSourceFilePath))
		{
			Arguments.Add(TEXT("FileName"), FText::FromString(FPaths::ConvertRelativePathToFull(InSourceFilePath)));
			TextBody = FText::Format(LOCTEXT("UnsuccessfulSave_NoExist_Notification", "Unsuccessful {WhatIs}, the desired file does not exist:\n{FileName}"), Arguments);
			EditorErrors.Warning(TextBody);
		}
		// Target is read-only
		else if (IFileManager::Get().IsReadOnly(*InTargetFilePath))
		{
			Arguments.Add(TEXT("FileName"), FText::FromString(FPaths::ConvertRelativePathToFull(InTargetFilePath)));
			TextBody = FText::Format(LOCTEXT("UnsuccessfulSave_ReadOnly_Notification", "Unsuccessful {WhatIs}, the target file path is read-only\n{FileName}"), Arguments);
			EditorErrors.Warning(TextBody);
		}
		// Target and source are the same
		else if (FPaths::ConvertRelativePathToFull(InTargetFilePath) == FPaths::ConvertRelativePathToFull(InSourceFilePath))
		{
			Arguments.Add(TEXT("SourceFileName"), FText::FromString(FPaths::ConvertRelativePathToFull(InSourceFilePath)));
			Arguments.Add(TEXT("FinalFileName"), FText::FromString(FPaths::ConvertRelativePathToFull(InTargetFilePath)));
			TextBody = FText::Format(LOCTEXT("UnsuccessfulSave_Fallback_Notification",
				"Unsuccessful {WhatIs}, target and source layout file paths are the same ({SourceFileName})!\nAre you trying to import or replace a file that is already in the layouts folder? If so, remove the current file first."), Arguments);
			EditorErrors.Warning(TextBody);
		}
		// We don't specifically know why it failed, this is a fallback
		else
		{
			Arguments.Add(TEXT("SourceFileName"), FText::FromString(FPaths::ConvertRelativePathToFull(InSourceFilePath)));
			Arguments.Add(TEXT("FinalFileName"), FText::FromString(FPaths::ConvertRelativePathToFull(InTargetFilePath)));
			TextBody = FText::Format(LOCTEXT("UnsuccessfulSave_Fallback_Notification", "Unsuccessful {WhatIs} of:\n{SourceFileName}\nto\n{FinalFileName}"), Arguments);
			EditorErrors.Warning(TextBody);
		}
		EditorErrors.Notify(LOCTEXT("LoadUnsuccessful_Title", "Load Unsuccessful!"));
		// Show reason
		const FText TextTitle = LOCTEXT("UnsuccessfulCopyHeader", "Unsuccessful copy!");
		OpenMsgDlgInt(EAppMsgType::Ok, TextBody, TextTitle);
		// Return
		return false;
	}
	// Copy successful
	else
	{
		// Clean Layout Name and Description fields
		// We copy twice to make sure we can copy.
		// Problem if we only copied once: If the copy fails, the current EditorLayout.ini would be modified and no longer matches the previous one.
		// The ini file should only be modified if it has been successfully copied to the new (and modified) INI file.
		if (bCleanLayoutNameAndDescriptionFields)
		{
			// Update fields
			FLayoutSaveRestore::SaveSectionToConfig(GEditorLayoutIni, "LayoutName", FText::FromString(""));
			FLayoutSaveRestore::SaveSectionToConfig(GEditorLayoutIni, "LayoutDescription", FText::FromString(""));
			// Flush file
			const bool bRead = true;
			GConfig->Flush(bRead, GEditorLayoutIni);
			// Re-copy file
			if (FPaths::ConvertRelativePathToFull(InTargetFilePath) != FPaths::ConvertRelativePathToFull(GEditorLayoutIni))
			{
				IFileManager::Get().Copy(*InTargetFilePath, *GEditorLayoutIni, bShouldReplace, bCopyEvenIfReadOnly, bCopyAttributes);
			}
		}
		// Unload target file so it can be re-read into cache properly the next time it is used
		GConfig->UnloadFile(InTargetFilePath); // We must re-read it to avoid the Editor to use a previously cached name and description
		// Return successful copy message
		return true;
	}
}

// Name into display text
FText GetDisplayTextInternal(const FString& InString)
{
	const FText DisplayNameText = FText::FromString(FPaths::GetBaseFilename(*InString));
	const bool bIsBool = false;
	const FText DisplayName = FText::FromString(FName::NameToDisplayString(DisplayNameText.ToString(), bIsBool));
	return DisplayName;
}
FText GetTooltipTextInternal(const FString& InKindOfFile, const FText& InDisplayName, const FString& InLayoutFilePath)
{
	return FText::FromString(InKindOfFile + FString(" name:\n") + InDisplayName.ToString() + FString("\n\nFull file path:\n") + InLayoutFilePath);
}

void DisplayLayoutsInternal(FToolMenuSection& InSection, const TArray<TSharedPtr<FUICommandInfo>>& InXLayoutCommands, const TArray<FString> InLayoutIniFileNames, const FString InLayoutsDirectory)
{
	// If there are Layout ini files, read them
	for (int32 LayoutIndex = 0; LayoutIndex < InLayoutIniFileNames.Num() && LayoutIndex < InXLayoutCommands.Num(); ++LayoutIndex)
	{
		const FString LayoutFilePath = IFileManager::Get().ConvertToAbsolutePathForExternalAppForRead(*FPaths::Combine(InLayoutsDirectory, InLayoutIniFileNames[LayoutIndex]));
		// Make sure it is a layout file
		GConfig->UnloadFile(LayoutFilePath); // We must re-read it to avoid the Editor to use a previously cached name and description
		if (FLayoutSaveRestore::IsValidConfig(LayoutFilePath))
		{
			// Read and display localization name from INI file
			const FText LayoutName = FLayoutSaveRestore::LoadSectionFromConfig(LayoutFilePath, "LayoutName");
			const FText LayoutDescription = FLayoutSaveRestore::LoadSectionFromConfig(LayoutFilePath, "LayoutDescription");
			// If no localization name, then display the file name
			const FText DisplayName = (!LayoutName.IsEmpty() ? LayoutName : GetDisplayTextInternal(InLayoutIniFileNames[LayoutIndex]));
			const FText Tooltip = (!LayoutDescription.IsEmpty() ? LayoutDescription : GetTooltipTextInternal("Default layout", DisplayName, LayoutFilePath));
			InSection.AddMenuEntry(InXLayoutCommands[LayoutIndex], DisplayName, Tooltip);
		}
	}
}

void MakeXLayoutsMenuInternal(UToolMenu* InToolMenu, const TArray<TSharedPtr<FUICommandInfo>>& InXLayoutCommands, const TArray<TSharedPtr<FUICommandInfo>>& InXUserLayoutCommands, const bool bDisplayDefaultLayouts)
{
	// Update GEditorLayoutIni file. Otherwise, we could not track the changes the user did since the layout was loaded
	FLayoutsMenuSave::SaveLayout();
	// UE Default Layouts
	if (bDisplayDefaultLayouts)
	{
		FToolMenuSection& Section = InToolMenu->AddSection("DefaultLayouts", LOCTEXT("DefaultLayoutsHeading", "Default Layouts"));
		// Get LayoutsDirectory path
		const FString LayoutsDirectory = CreateAndGetDefaultLayoutDirInternal();
		// Get Layout init files
		const TArray<FString> LayoutIniFileNames = GetIniFilesInFolderInternal(LayoutsDirectory);
		// If there are user Layout ini files, read and display them
		DisplayLayoutsInternal(Section, InXLayoutCommands, LayoutIniFileNames, LayoutsDirectory);
	}
	// User Layouts
	{
		FToolMenuSection& Section = InToolMenu->AddSection("UserDefaultLayouts", LOCTEXT("UserDefaultLayoutsHeading", "User Layouts"));
		// (Create if it does not exist and) Get UserLayoutsDirectory path
		const FString UserLayoutsDirectory = CreateAndGetUserLayoutDirInternal();
		// Get User Layout init files
		const TArray<FString> UserLayoutIniFileNames = GetIniFilesInFolderInternal(UserLayoutsDirectory);
		// If there are user Layout ini files, read and display them
		DisplayLayoutsInternal(Section, InXUserLayoutCommands, UserLayoutIniFileNames, UserLayoutsDirectory);
	}
}

// All can be read
/**
 * Static
 * Checks if the selected layout can be read (e.g., when loading layouts).
 * @param	InLayoutIndex  Index from the selected layout.
 * @return true if the selected layout can be read.
 */
bool CanChooseLayoutWhenRead(const int32 InLayoutIndex)
{
	return true;
}
/**
 * Static
 * Checks if the selected user-created layout can be read (e.g., when loading user layouts).
 * @param	InLayoutIndex  Index from the selected user-created layout.
 * @return true if the selected user-created layout can be read.
 */
bool CanChooseUserLayoutWhenRead(const int32 InLayoutIndex)
{
	return true;
}
// Only the layouts created by the user can be modified
/**
 * Static
 * Checks if the selected layout can be modified (e.g., when overriding or removing layouts).
 * @param	InLayoutIndex  Index from the selected layout.
 * @return true if the selected layout can be modified/removed.
 */
bool CanChooseLayoutWhenWrite(const int32 InLayoutIndex)
{
	return false;
}
/**
 * Static
 * Checks if the selected user-created layout can be modified (e.g., when overriding or removing user layouts).
 * @param	InLayoutIndex  Index from the selected user-created layout.
 * @return true if the selected user-created layout can be modified/removed.
 */
bool CanChooseUserLayoutWhenWrite(const int32 InLayoutIndex)
{
	return true;
}



void FLayoutsMenuLoad::MakeLoadLayoutsMenu(UToolMenu* InToolMenu)
{
	// MakeLoadLayoutsMenu
	const bool bDisplayDefaultLayouts = true;
	MakeXLayoutsMenuInternal(InToolMenu, FMainFrameCommands::Get().MainFrameLayoutCommands.LoadLayoutCommands, FMainFrameCommands::Get().MainFrameLayoutCommands.LoadUserLayoutCommands, bDisplayDefaultLayouts);

	// Additional sections
	if (GetDefault<UEditorStyleSettings>()->bEnableUserEditorLayoutManagement)
	{
		FToolMenuSection& Section = InToolMenu->FindOrAddSection("UserDefaultLayouts");

		// Separator
		if (FLayoutsMenuBase::IsThereUserLayouts())
		{
			Section.AddMenuSeparator("AdditionalSectionsSeparator");
		}

		// Import...
		{
			Section.AddMenuEntry(FMainFrameCommands::Get().MainFrameLayoutCommands.ImportLayout);
		}
	}
}

bool FLayoutsMenuLoad::CanLoadChooseLayout(const int32 InLayoutIndex)
{
	if (GEngine && GEngine->GameViewport)
	{
		return false;
	}
	return !FLayoutsMenuBase::IsLayoutChecked(InLayoutIndex) && CanChooseLayoutWhenRead(InLayoutIndex);
}
bool FLayoutsMenuLoad::CanLoadChooseUserLayout(const int32 InLayoutIndex)
{
	if (GEngine && GEngine->GameViewport)
	{
		return false;
	}
	return !FLayoutsMenuBase::IsUserLayoutChecked(InLayoutIndex) && CanChooseUserLayoutWhenRead(InLayoutIndex);
}

void FLayoutsMenuLoad::ReloadCurrentLayout()
{
	// Editor is reset on-the-fly
	FUnrealEdMisc::Get().AllowSavingLayoutOnClose(false);
	EditorReinit();
	FUnrealEdMisc::Get().AllowSavingLayoutOnClose(true);
}

void FLayoutsMenuLoad::LoadLayout(const FString& InLayoutPath)
{
	// Replace main layout with desired one
	const FString& SourceFilePath = InLayoutPath;
	const FString& TargetFilePath = GEditorLayoutIni;
	const bool bCleanLayoutNameAndDescriptionFieldsIfNoSameValues = false;
	const bool SucessfullySaved = TrySaveLayoutOrWarnInternal(SourceFilePath, TargetFilePath, LOCTEXT("LoadLayoutText", "layout load"), bCleanLayoutNameAndDescriptionFieldsIfNoSameValues);
	// Reload current layout
	if (SucessfullySaved)
	{
		FLayoutsMenuLoad::ReloadCurrentLayout();
	}
}

void FLayoutsMenuLoad::LoadLayout(const int32 InLayoutIndex)
{
	// Replace main layout with desired one, reset layout & restart Editor
	LoadLayout(FLayoutsMenuBase::GetLayout(InLayoutIndex));
}

void FLayoutsMenuLoad::LoadUserLayout(const int32 InLayoutIndex)
{
	// Replace main layout with desired one, reset layout & restart Editor
	LoadLayout(FLayoutsMenuBase::GetUserLayout(InLayoutIndex));
}

void FLayoutsMenuLoad::ImportLayout()
{
	// Import the user-selected layout configuration files and load one of them
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (DesktopPlatform)
	{
		// Open File Dialog so user can select his/her desired layout configuration files
		TArray<FString> LayoutFilePaths;
		const FString LastDirectory = FPaths::ProjectContentDir();
		const FString DefaultDirectory = LastDirectory;
		const FString DefaultFile = "";
		const bool bWereFilesSelected = DesktopPlatform->OpenFileDialog(
			FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
			FString("Import a Layout Configuration File"),
			DefaultDirectory,
			DefaultFile,
			TEXT("Layout configuration files|*.ini|"),
			EFileDialogFlags::Multiple, //EFileDialogFlags::None, // Allow/Avoid multiple file selection
			LayoutFilePaths
		);
		// If file(s) selected, copy them into the user layouts directory and load one of them
		if (bWereFilesSelected && LayoutFilePaths.Num() > 0)
		{
			// (Create if it does not exist and) Get UserLayoutsDirectory path
			const FString UserLayoutsDirectory = CreateAndGetUserLayoutDirInternal();
			// Iterate over selected layout ini files
			FString FirstGoodLayoutFile = "";
			const FText TrySaveLayoutOrWarnInternalText = LOCTEXT("ImportLayoutText", "layout import");
			for (const FString& LayoutFilePath : LayoutFilePaths)
			{
				// If file is a layout file, import it
				GConfig->UnloadFile(LayoutFilePath); // We must re-read it to avoid the Editor to use a previously cached name and description
				if (FLayoutSaveRestore::IsValidConfig(LayoutFilePath))
				{
					if (FirstGoodLayoutFile == "")
					{
						FirstGoodLayoutFile = LayoutFilePath;
					}
					// Save in the user layout folder
					const FString& SourceFilePath = LayoutFilePath;
					const FString& TargetFilePath = FPaths::Combine(FPaths::GetPath(UserLayoutsDirectory), FPaths::GetCleanFilename(LayoutFilePath));
					const bool bCleanLayoutNameAndDescriptionFieldsIfNoSameValues = false;
					TrySaveLayoutOrWarnInternal(SourceFilePath, TargetFilePath, TrySaveLayoutOrWarnInternalText, bCleanLayoutNameAndDescriptionFieldsIfNoSameValues);
				}
				// File is not a layout file, warn the user
				else
				{
					FFormatNamedArguments Arguments;
					Arguments.Add(TEXT("FileName"), FText::FromString(FPaths::ConvertRelativePathToFull(LayoutFilePath)));
					const FText TextBody = FText::Format(LOCTEXT("UnsuccessfulImportBody", "Unsuccessful import, {FileName} is not a layout configuration file!"), Arguments);
					const FText TextTitle = LOCTEXT("UnsuccessfulImportHeader", "Unsuccessful Import!");
					OpenMsgDlgInt(EAppMsgType::Ok, TextBody, TextTitle);
				}
			}
			// If PIE running, do not reload current layout
			if (GEngine && GEngine->GameViewport)
			{
				const FText TextBody = LOCTEXT("SuccessfulImportBody",
					"The layout(s) were successfully imported into the \"User Layouts\" section. However, no layout has been loaded into your current Unreal Editor UI because PIE is currently running. In order to do so, you must stop PIE and then load the layout from the \"User Layouts\" section.");
				const FText TextTitle = LOCTEXT("UnsuccessfulImportHeader", "Successful Import!");
				OpenMsgDlgInt(EAppMsgType::Ok, TextBody, TextTitle);
				return;
			}
			// Replace current layout with first one
			if (FirstGoodLayoutFile != "")
			{
				const FString& SourceFilePath = FirstGoodLayoutFile;
				const FString& TargetFilePath = GEditorLayoutIni;
				const bool bCleanLayoutNameAndDescriptionFieldsIfNoSameValues = false;
				const bool SucessfullySaved = TrySaveLayoutOrWarnInternal(SourceFilePath, TargetFilePath, TrySaveLayoutOrWarnInternalText, bCleanLayoutNameAndDescriptionFieldsIfNoSameValues);
				// Reload current layout
				if (SucessfullySaved)
				{
					ReloadCurrentLayout();
				}
			}
		}
	}
}



void SaveExportLayoutCommon(const FString& InDefaultDirectory, const bool bMustBeSavedInDefaultDirectory, const FText& InWhatIsThis, const bool bShouldAskBeforeCleaningLayoutNameAndDescriptionFields)
{
	// Export/SaveAs the user-selected layout configuration files and load one of them
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (DesktopPlatform)
	{
		// Open the "save file" dialog so user can save his/her layout configuration file
		TArray<FString> LayoutFilePaths;
		const FString DialogTitle = "";
		const FString DefaultFile = "";
		const bool bWereFileSelected = DesktopPlatform->SaveFileDialog(
			FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
			FString("Export a Layout Configuration File"),
			InDefaultDirectory,
			DefaultFile,
			TEXT("Layout configuration files|*.ini|"),
			EFileDialogFlags::None, //EFileDialogFlags::Multiple, // Allow/Avoid multiple file selection
			LayoutFilePaths
		);
		// If file(s) selected, copy them into the user layouts directory and load one of them
		if (bWereFileSelected && LayoutFilePaths.Num() > 0)
		{
			// Iterate over selected layout ini files
			FString FirstGoodLayoutFile = "";
			for (const FString& LayoutFilePath : LayoutFilePaths)
			{
				// If writing in the right folder
				const FString LayoutFilePathAbsolute = IFileManager::Get().ConvertToAbsolutePathForExternalAppForRead(*FPaths::GetPath(LayoutFilePath));
				const FString DefaultDirectoryAbsolute = IFileManager::Get().ConvertToAbsolutePathForExternalAppForRead(*FPaths::GetPath(InDefaultDirectory));
				if (!bMustBeSavedInDefaultDirectory || (LayoutFilePathAbsolute == DefaultDirectoryAbsolute))
				{
					// Save in the user layout folder
					const FString& SourceFilePath = GEditorLayoutIni;
					const FString& TargetFilePath = LayoutFilePath;
					const bool bCleanLayoutNameAndDescriptionFieldsIfNoSameValues = true;
					TrySaveLayoutOrWarnInternal(SourceFilePath, TargetFilePath, InWhatIsThis, bCleanLayoutNameAndDescriptionFieldsIfNoSameValues, bShouldAskBeforeCleaningLayoutNameAndDescriptionFields);
				}
				// If trying to write in a different folder (which is not allowed)
				else
				{
					// Warn the user that the file will not be copied in there
					OpenMsgDlgInt(
						EAppMsgType::Ok,
						FText::Format(
							LOCTEXT("SaveAsFailedMsg",
								"In order to save the layout and allow Unreal to use it, you must save it in the predefined folder:\n{0}\n\nNevertheless, you tried to save it in:\n{1}\n\n"
								"If you simply wish to export a copy of the current configuration in {1} (e.g., to later copy it into a different machine), you could use the \"Export Layout...\""
								" functionality. However, Unreal would not be able to load it until you import it with \"Import Layout...\"."),
							FText::FromString(DefaultDirectoryAbsolute), FText::FromString(LayoutFilePathAbsolute)),
						LOCTEXT("SaveAsFailedMsg_Title", "Save As Failed"));
				}
			}
		}
	}
}

void FLayoutsMenuSave::MakeSaveLayoutsMenu(UToolMenu* InToolMenu)
{
	if (GetDefault<UEditorStyleSettings>()->bEnableUserEditorLayoutManagement)
	{
		// MakeOverrideLayoutsMenu
		const bool bDisplayDefaultLayouts = false;
		MakeXLayoutsMenuInternal(InToolMenu, FMainFrameCommands::Get().MainFrameLayoutCommands.OverrideLayoutCommands, FMainFrameCommands::Get().MainFrameLayoutCommands.OverrideUserLayoutCommands, bDisplayDefaultLayouts);

		// Additional sections
		{
			FToolMenuSection& Section = InToolMenu->FindOrAddSection("UserDefaultLayouts");

			// Separator
			if (FLayoutsMenuBase::IsThereUserLayouts())
			{
				Section.AddMenuSeparator("AdditionalSectionsSeparator");
			}

			// Save as...
			{
				Section.AddMenuEntry(FMainFrameCommands::Get().MainFrameLayoutCommands.SaveLayoutAs);
			}

			// Export...
			{
				Section.AddMenuEntry(FMainFrameCommands::Get().MainFrameLayoutCommands.ExportLayout);
			}
		}
	}
}

bool FLayoutsMenuSave::CanSaveChooseLayout(const int32 InLayoutIndex)
{
	return !FLayoutsMenuBase::IsLayoutChecked(InLayoutIndex) && CanChooseLayoutWhenWrite(InLayoutIndex);
}
bool FLayoutsMenuSave::CanSaveChooseUserLayout(const int32 InLayoutIndex)
{
	return !FLayoutsMenuBase::IsUserLayoutChecked(InLayoutIndex) && CanChooseUserLayoutWhenWrite(InLayoutIndex);
}

void FLayoutsMenuSave::OverrideLayout(const int32 InLayoutIndex)
{
	// Default layouts should never be modified, so this function should never be called
	UE_LOG(LogLayoutsMenu, Fatal, TEXT("Default layouts should never be modified, so this function should never be called."));
}

void FLayoutsMenuSave::OverrideUserLayout(const int32 InLayoutIndex)
{
	// (Create if it does not exist and) Get UserLayoutsDirectory path
	const FString UserLayoutsDirectory = CreateAndGetUserLayoutDirInternal();
	// Get User Layout init files
	const TArray<FString> UserLayoutIniFileNames = GetIniFilesInFolderInternal(UserLayoutsDirectory);
	const FString DesiredUserLayoutFullPath = FPaths::Combine(FPaths::GetPath(UserLayoutsDirectory), UserLayoutIniFileNames[InLayoutIndex]);
	// Are you sure you want to do this?
	const FText TextFileNameToRemove = FText::FromString(FPaths::GetBaseFilename(UserLayoutIniFileNames[InLayoutIndex]));
	const FText TextBody = FText::Format(LOCTEXT("ActionOverrideLayoutMsg", "Are you sure you want to permanently override the layout profile \"{0}\" with the current layout profile? This action cannot be undone."), TextFileNameToRemove);
	const FText TextTitle = FText::Format(LOCTEXT("OverrideUILayout_Title", "Override UI Layout \"{0}\""), TextFileNameToRemove);
	if (EAppReturnType::Ok != OpenMsgDlgInt(EAppMsgType::OkCancel, TextBody, TextTitle))
	{
		return;
	}
	// Target and source files
	const FString& SourceFilePath = GEditorLayoutIni;
	const FString& TargetFilePath = DesiredUserLayoutFullPath;
	// Update GEditorLayoutIni file
	SaveLayout();
	// Replace desired layout with current one
	const bool bCleanLayoutNameAndDescriptionFieldsIfNoSameValues = true;
	const bool bShouldAskBeforeCleaningLayoutNameAndDescriptionFields = false;
	TrySaveLayoutOrWarnInternal(SourceFilePath, TargetFilePath, LOCTEXT("OverrideLayoutText", "layout override"), bCleanLayoutNameAndDescriptionFieldsIfNoSameValues);
}

void FLayoutsMenuSave::SaveLayout()
{
	// Save the layout into the Editor
	FGlobalTabmanager::Get()->SaveAllVisualState();
	// Write the saved layout to disk (if it has changed since the last time it was read/written)
	// We must set bRead = true. Otherwise, FLayoutsMenuLoad::ReloadCurrentLayout() would reload the old config file (because it would be cached on memory)
	const bool bRead = true;
	GConfig->Flush(bRead, GEditorLayoutIni);
}

void FLayoutsMenuSave::SaveLayoutAs()
{
	// Update GEditorLayoutIni file
	SaveLayout();
	// Copy GEditorLayoutIni into desired file
	const FString DefaultDirectory = CreateAndGetUserLayoutDirInternal();
	const bool bMustBeSavedInDefaultDirectory = true;
	const bool bShouldAskBeforeCleaningLayoutNameAndDescriptionFields = false;
	SaveExportLayoutCommon(DefaultDirectory, bMustBeSavedInDefaultDirectory, LOCTEXT("SaveLayoutText", "layout save"), bShouldAskBeforeCleaningLayoutNameAndDescriptionFields);
}

void FLayoutsMenuSave::ExportLayout()
{
	// Update GEditorLayoutIni file
	SaveLayout();
	// Copy GEditorLayoutIni into desired file
	const FString DefaultDirectory = FPaths::ProjectContentDir();
	const bool bMustBeSavedInDefaultDirectory = false;
	const bool bShouldAskBeforeCleaningLayoutNameAndDescriptionFields = true;
	SaveExportLayoutCommon(DefaultDirectory, bMustBeSavedInDefaultDirectory, LOCTEXT("ExportLayoutText", "layout export"), bShouldAskBeforeCleaningLayoutNameAndDescriptionFields);
}



void FLayoutsMenuRemove::MakeRemoveLayoutsMenu(UToolMenu* InToolMenu)
{
	if (GetDefault<UEditorStyleSettings>()->bEnableUserEditorLayoutManagement)
	{
		// MakeRemoveLayoutsMenu
		const bool bDisplayDefaultLayouts = false;
		MakeXLayoutsMenuInternal(InToolMenu, FMainFrameCommands::Get().MainFrameLayoutCommands.RemoveLayoutCommands, FMainFrameCommands::Get().MainFrameLayoutCommands.RemoveUserLayoutCommands, bDisplayDefaultLayouts);

		// Additional sections
		{
			FToolMenuSection& Section = InToolMenu->FindOrAddSection("UserDefaultLayouts");

			// Separator
			if (FLayoutsMenuBase::IsThereUserLayouts())
			{
				Section.AddMenuSeparator("AdditionalSectionsSeparator");
			}

			// Remove all
			Section.AddMenuEntry(FMainFrameCommands::Get().MainFrameLayoutCommands.RemoveUserLayouts);
		}
	}
}

bool FLayoutsMenuRemove::CanRemoveChooseLayout(const int32 InLayoutIndex)
{
	return CanChooseLayoutWhenWrite(InLayoutIndex);
}
bool FLayoutsMenuRemove::CanRemoveChooseUserLayout(const int32 InLayoutIndex)
{
	return CanChooseUserLayoutWhenWrite(InLayoutIndex);
}

void FLayoutsMenuRemove::RemoveLayout(const int32 InLayoutIndex)
{
	// Default layouts should never be modified, so this function should never be called
	UE_LOG(LogLayoutsMenu, Fatal, TEXT("Default layouts should never be modified, so this function should never be called."));
}

void FLayoutsMenuRemove::RemoveUserLayout(const int32 InLayoutIndex)
{
	// (Create if it does not exist and) Get UserLayoutsDirectory path
	const FString UserLayoutsDirectory = CreateAndGetUserLayoutDirInternal();
	// Get User Layout init files
	const TArray<FString> UserLayoutIniFileNames = GetIniFilesInFolderInternal(UserLayoutsDirectory);
	const FString DesiredUserLayoutFullPath = FPaths::Combine(FPaths::GetPath(UserLayoutsDirectory), UserLayoutIniFileNames[InLayoutIndex]);
	// Are you sure you want to do this?
	const FText TextFileNameToRemove = FText::FromString(FPaths::GetBaseFilename(UserLayoutIniFileNames[InLayoutIndex]));
	const FText TextBody = FText::Format(LOCTEXT("ActionRemoveMsg", "Are you sure you want to permanently delete the layout profile \"{0}\"? This action cannot be undone."), TextFileNameToRemove);
	const FText TextTitle = FText::Format(LOCTEXT("RemoveUILayout_Title", "Remove UI Layout \"{0}\""), TextFileNameToRemove);
	if (EAppReturnType::Ok != OpenMsgDlgInt(EAppMsgType::OkCancel, TextBody, TextTitle))
	{
		return;
	}
	// Remove layout
	FPlatformFileManager::Get().GetPlatformFile().DeleteFile(*DesiredUserLayoutFullPath);
}

int32 GetNumberLayoutFiles(const FString& InLayoutsDirectory)
{
	// Get Layout init files in desired directory
	const TArray<FString> LayoutIniFileNames = GetIniFilesInFolderInternal(InLayoutsDirectory);
	// Count how many layout files exist
	int32 NumberLayoutFiles = 0;
	for (const FString& LayoutIniFileName : LayoutIniFileNames)
	{
		const FString LayoutFilePath = FPaths::Combine(InLayoutsDirectory, LayoutIniFileName);
		GConfig->UnloadFile(LayoutFilePath); // We must re-read it to avoid the Editor to use a previously cached name and description
		if (FLayoutSaveRestore::IsValidConfig(LayoutFilePath))
		{
			++NumberLayoutFiles;
		}
	}
	// Return result
	return NumberLayoutFiles;
}

void FLayoutsMenuRemove::RemoveUserLayouts()
{
	// (Create if it does not exist and) Get UserLayoutsDirectory path
	const FString UserLayoutsDirectory = CreateAndGetUserLayoutDirInternal();
	// Count how many layout files exist
	const int32 NumberUserLayoutFiles = GetNumberLayoutFiles(UserLayoutsDirectory);
	// If files to remove, warn user and remove them all
	if (NumberUserLayoutFiles > 0)
	{
		// Are you sure you want to do this?
		const FText TextNumberUserLayoutFiles  = FText::FromString(FString::FromInt(NumberUserLayoutFiles));
		const FText TextBody = FText::Format(LOCTEXT("ActionRemoveAllUserLayoutMsg", "Are you sure you want to permanently remove all the {0} layout profiles created by the user? This action cannot be undone."), TextNumberUserLayoutFiles);
		const FText TextTitle = LOCTEXT("RemoveAllUserLayouts_Title", "Remove All User-Created Layouts");
		if (EAppReturnType::Ok != OpenMsgDlgInt(EAppMsgType::OkCancel, TextBody, TextTitle))
		{
			return;
		}
		// Get User Layout init files
		const TArray<FString> UserLayoutIniFileNames = GetIniFilesInFolderInternal(UserLayoutsDirectory);
		// If there are user Layout ini files, read them
		for (const FString& UserLayoutIniFileName : UserLayoutIniFileNames)
		{
			// Remove file if it is a layout
			const FString LayoutFilePath = FPaths::Combine(UserLayoutsDirectory, UserLayoutIniFileName);
			GConfig->UnloadFile(LayoutFilePath); // We must re-read it to avoid the Editor to use a previously cached name and description
			if (FLayoutSaveRestore::IsValidConfig(LayoutFilePath))
			{
				FPlatformFileManager::Get().GetPlatformFile().DeleteFile(*LayoutFilePath);
			}
		}
	}
	// If no files to remove, warn user
	else
	{
		// Show reason
		const FText TextBody = LOCTEXT("UnsuccessfulRemoveLayoutBody", "There are no layout profile files created by the user, so none could be removed.");
		const FText TextTitle = LOCTEXT("UnsuccessfulRemoveLayoutHeader", "Unsuccessful Remove All User Layouts!");
		OpenMsgDlgInt(EAppMsgType::Ok, TextBody, TextTitle);
	}
}



bool IsLayoutCheckedInternal(const FString& InLayoutFullPath)
{
	// Checked if same file. I.e.,
		// 1. Same size
		// 2. And same internal text
	const bool bHaveSameSize = (IFileManager::Get().FileSize(*GEditorLayoutIni) == IFileManager::Get().FileSize(*InLayoutFullPath));
	// Same size --> Same layout file?
	if (bHaveSameSize)
	{
		// Read files and check whether they have the exact same text
		FString StringGEditorLayoutIni;
		FFileHelper::LoadFileToString(StringGEditorLayoutIni, *GEditorLayoutIni);
		FString StringLayoutFullPath;
		FFileHelper::LoadFileToString(StringLayoutFullPath, *InLayoutFullPath);
		// (No) same text = (No) same layout file
		return (StringGEditorLayoutIni == StringLayoutFullPath);
	}
	// No same size = No same layout file
	else
	{
		return false;
	}
}

FString FLayoutsMenuBase::GetLayout(const int32 InLayoutIndex)
{
	// Get LayoutsDirectory path, layout init files, and desired layout path
	const FString LayoutsDirectory = CreateAndGetDefaultLayoutDirInternal();
	const TArray<FString> LayoutIniFileNames = GetIniFilesInFolderInternal(LayoutsDirectory);
	const FString DesiredLayoutFullPath = FPaths::Combine(FPaths::GetPath(LayoutsDirectory), LayoutIniFileNames[InLayoutIndex]);
	// Return full path
	return DesiredLayoutFullPath;
}

FString FLayoutsMenuBase::GetUserLayout(const int32 InLayoutIndex)
{
	// (Create if it does not exist and) Get UserLayoutsDirectory path, user layout init files, and desired user layout path
	const FString UserLayoutsDirectory = CreateAndGetUserLayoutDirInternal();
	const TArray<FString> UserLayoutIniFileNames = GetIniFilesInFolderInternal(UserLayoutsDirectory);
	const FString DesiredUserLayoutFullPath = FPaths::Combine(FPaths::GetPath(UserLayoutsDirectory), UserLayoutIniFileNames[InLayoutIndex]);
	// Return full path
	return DesiredUserLayoutFullPath;
}

bool FLayoutsMenuBase::IsThereUserLayouts()
{
	// (Create if it does not exist and) Get UserLayoutsDirectory path
	const FString UserLayoutsDirectory = CreateAndGetUserLayoutDirInternal();
	// At least 1 user layout file?
	return GetNumberLayoutFiles(UserLayoutsDirectory) > 0;
}

bool FLayoutsMenuBase::IsLayoutChecked(const int32 InLayoutIndex)
{
	// Check if the desired layout file matches the one currently loaded
	return IsLayoutCheckedInternal(GetLayout(InLayoutIndex));
}

bool FLayoutsMenuBase::IsUserLayoutChecked(const int32 InLayoutIndex)
{
	// Check if the desired layout file matches the one currently loaded
	return IsLayoutCheckedInternal(GetUserLayout(InLayoutIndex));
}

#undef LOCTEXT_NAMESPACE

#endif
