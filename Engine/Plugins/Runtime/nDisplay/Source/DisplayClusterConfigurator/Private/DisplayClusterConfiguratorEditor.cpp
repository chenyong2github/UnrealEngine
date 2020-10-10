// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterConfiguratorEditor.h"

#include "DisplayClusterConfiguratorEditorData.h"
#include "DisplayClusterConfiguratorEditorSubsystem.h"
#include "DisplayClusterConfiguratorToolkit.h"
#include "IDisplayClusterConfiguration.h"

#include "DesktopPlatformModule.h"
#include "EditorDirectories.h"
#include "Framework/Application/SlateApplication.h"
#include "UObject/Class.h"

#define LOCTEXT_NAMESPACE "DisplayClusterConfiguratorEditor"
TSharedPtr<FBaseAssetToolkit> UDisplayClusterConfiguratorEditor::CreateToolkit()
{
	return MakeShared<FDisplayClusterConfiguratorToolkit>(this);
}

void UDisplayClusterConfiguratorEditor::GetObjectsToEdit(TArray<UObject*>& InObjectsToEdit)
{
	InObjectsToEdit.Append(ObjectsToEdit);
}

UDisplayClusterConfiguratorEditorData* UDisplayClusterConfiguratorEditor::GetEditingObject() const
{
	check(ObjectsToEdit.Num() > 0)

	return ObjectsToEdit[0];
}

bool UDisplayClusterConfiguratorEditor::LoadWithOpenFileDialog()
{
	const FString NDisplayFileDescription = LOCTEXT("NDisplayFileDescription", "nDisplay Config").ToString();
	const FString NDisplayFileExtension = TEXT("*.ndisplay;*.cfg");
	const FString FileTypes = FString::Printf(TEXT("%s (%s)|%s"), *NDisplayFileDescription, *NDisplayFileExtension, *NDisplayFileExtension);

	// Prompt the user for the filenames
	TArray<FString> OpenFilenames;
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	bool bFileSelected = false;
	int32 FilterIndex = -1;

	// Open file dialog
	if (DesktopPlatform)
	{
		const void* ParentWindowWindowHandle = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr);

		bFileSelected = DesktopPlatform->OpenFileDialog(
			ParentWindowWindowHandle,
			LOCTEXT("ImportDialogTitle", "Import").ToString(),
			FEditorDirectories::Get().GetLastDirectory(ELastDirectory::GENERIC_IMPORT),
			TEXT(""),
			FileTypes,
			EFileDialogFlags::None,
			OpenFilenames,
			FilterIndex
		);
	}

	// Load file
	if (bFileSelected)
	{
		if (OpenFilenames.Num() > 0)
		{
			FEditorDirectories::Get().SetLastDirectory(ELastDirectory::GENERIC_IMPORT, OpenFilenames[0]);

			return LoadFromFile(GetEditingObject(), OpenFilenames[0]);
		}
	}

	return false;
}

bool UDisplayClusterConfiguratorEditor::LoadFromFile(UDisplayClusterConfiguratorEditorData* InConfiguratorEditorData, const FString& FilePath)
{
	UDisplayClusterConfiguratorEditorSubsystem* EditorSubsystem = GEditor->GetEditorSubsystem<UDisplayClusterConfiguratorEditorSubsystem>();
	if (EditorSubsystem != nullptr && EditorSubsystem->ReloadConfig(InConfiguratorEditorData, FilePath))
	{
		return true;
	}

	return false;
}

bool UDisplayClusterConfiguratorEditor::Save()
{
	UDisplayClusterConfiguratorEditorData* EditingObject = GetEditingObject();
	return SaveToFile(EditingObject->PathToConfig);
}

bool UDisplayClusterConfiguratorEditor::SaveToFile(const FString& InFilePath)
{
	UDisplayClusterConfiguratorEditorSubsystem* EditorSubsystem = GEditor->GetEditorSubsystem<UDisplayClusterConfiguratorEditorSubsystem>();
	if (EditorSubsystem != nullptr && EditorSubsystem->SaveConfig(GetEditingObject(), InFilePath))
	{
		return true;
	}

	return false;
}

bool UDisplayClusterConfiguratorEditor::SaveWithOpenFileDialog()
{
	const FString NDisplayFileDescription = LOCTEXT("NDisplayFileDescription", "nDisplay Config").ToString();
	const FString NDisplayFileExtension = TEXT("*.ndisplay");
	const FString FileTypes = FString::Printf(TEXT("%s (%s)|%s"), *NDisplayFileDescription, *NDisplayFileExtension, *NDisplayFileExtension);

	UDisplayClusterConfiguratorEditorData* EditingObject = GetEditingObject();

	if (EditingObject)
	{
		// Prompt the user for the filenames
		TArray<FString> SaveFilenames;
		IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
		bool bFileSelected = false;
		int32 FilterIndex = -1;

		// Open file dialog
		if (DesktopPlatform)
		{
			const void* ParentWindowWindowHandle = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr);

			bFileSelected = DesktopPlatform->SaveFileDialog(
				ParentWindowWindowHandle,
				LOCTEXT("ExportDialogTitle", "Export").ToString(),
				FEditorDirectories::Get().GetLastDirectory(ELastDirectory::GENERIC_EXPORT),
				TEXT(""),
				FileTypes,
				EFileDialogFlags::None,
				SaveFilenames
			);
		}

		if (bFileSelected && SaveFilenames.Num() > 0)
		{
			FEditorDirectories::Get().SetLastDirectory(ELastDirectory::GENERIC_EXPORT, FPaths::GetPath(SaveFilenames[0]));
			
			UDisplayClusterConfiguratorEditorSubsystem* EditorSubsystem = GEditor->GetEditorSubsystem<UDisplayClusterConfiguratorEditorSubsystem>();
			if (EditorSubsystem)
			{
				return SaveToFile(SaveFilenames[0]);
			}
		}
	}

	return false;
}

void UDisplayClusterConfiguratorEditor::SetObjectsToEdit(const TArray<UObject*>& InObjects)
{
	check(InObjects.Num() > 0);

	UDisplayClusterConfiguratorEditorSubsystem* EditorSubsystem = GEditor->GetEditorSubsystem<UDisplayClusterConfiguratorEditorSubsystem>();
	
	for (UObject* Obj : InObjects)
	{
		UDisplayClusterConfiguratorEditorData* EditingObject = Cast<UDisplayClusterConfiguratorEditorData>(Obj);

		if (EditorSubsystem != nullptr)
		{
			EditorSubsystem->ReloadConfig(EditingObject, EditingObject->PathToConfig);
		}

		ObjectsToEdit.Add(EditingObject);
	}
}

#undef LOCTEXT_NAMESPACE
