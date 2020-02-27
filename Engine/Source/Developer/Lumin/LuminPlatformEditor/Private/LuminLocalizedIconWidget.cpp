// Copyright Epic Games, Inc. All Rights Reserved.

#include "LuminLocalizedIconWidget.h"
#include "LuminPathPickerWidget.h"
#include "LuminLocalePickerWidget.h"
#include "LuminLocalizedIconListWidget.h"
#include "SourceControlHelpers.h"
#include "DesktopPlatformModule.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"

#define LOCTEXT_NAMESPACE "SLuminLocalizedIconWidget"

static constexpr float ExpireDuration = 5.0f;

void SLuminLocalizedIconWidget::Construct(const FArguments& Args)
{
	GameLuminPath = Args._GameLuminPath.Get();
	LocalizedIconInfo = Args._LocalizedIconInfo.Get();
	ListWidget = Args._ListWidget.Get();

	ChildSlot
	[
		SNew(SHorizontalBox)

		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			SNew(SBorder)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(2)
				[
					SNew(SLuminLocalePickerWidget)
					.InitiallySelectedLocale(LocalizedIconInfo.LanguageCode)
					.OnPickLocale(FOnPickLocale::CreateRaw(this, &SLuminLocalizedIconWidget::OnPickLocale))
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(2)
				[
					SNew(SLuminPathPickerWidget)
					.FilePickerTitle(LOCTEXT("FolderDialogTitle", "Choose a directory").ToString())
					.StartDirectory(LocalizedIconInfo.IconModelPath.Path)
					.PathLabel(LOCTEXT("IconModelPath", "Icon Model Path:"))
					.ToolTipText(LOCTEXT("PickIconModelButton_Tooltip", "Select the icon model to use for the application. The files will be copied to the project build folder."))
					.OnPickPath(FOnPickPath::CreateRaw(this, &SLuminLocalizedIconWidget::OnPickIconModelPath))
					.OnClearPath(FOnClearPath::CreateRaw(this, &SLuminLocalizedIconWidget::OnClearIconModelPath))
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(2)
				[
					SNew(SLuminPathPickerWidget)
					.FilePickerTitle(LOCTEXT("FolderDialogTitle", "Choose a directory").ToString())
					.StartDirectory(LocalizedIconInfo.IconPortalPath.Path)
					.PathLabel(LOCTEXT("IconPortalPath", "Icon Portal Path:"))
					.ToolTipText(LOCTEXT("PickIconPortalButton_Tooltip", "Select the icon portal to use for the application. The files will be copied to the project build folder."))
					.OnPickPath(FOnPickPath::CreateRaw(this, &SLuminLocalizedIconWidget::OnPickIconPortalPath))
					.OnClearPath(FOnClearPath::CreateRaw(this, &SLuminLocalizedIconWidget::OnClearIconPortalPath))
				]
			]
		]

		+ SHorizontalBox::Slot()
		.VAlign(EVerticalAlignment::VAlign_Fill)
		.HAlign(EHorizontalAlignment::HAlign_Fill)
		.Padding(2.0f, 0.0f)
		[
			SNew(SBorder)
			.ToolTipText(LOCTEXT("RemoveLocalizedIconAsset", "Remove this localized icon asset."))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.VAlign(EVerticalAlignment::VAlign_Center)
				.HAlign(EHorizontalAlignment::HAlign_Center)
				.Padding(2)
				[
					SNew(SButton)
					.ButtonStyle(FEditorStyle::Get(), "HoverHintOnly")
					.ContentPadding(2.0f)
					.ForegroundColor(FSlateColor::UseForeground())
					.IsFocusable(false)
					.OnClicked(this, &SLuminLocalizedIconWidget::OnRemove)
					[
						SNew(SImage)
						.Image(FEditorStyle::GetBrush("PropertyWindow.Button_EmptyArray"))
						.ColorAndOpacity(FSlateColor::UseForeground())
					]
				]
			]
		]
	];
}

const FLocalizedIconInfo& SLuminLocalizedIconWidget::GetLocalizedIconInfo() const
{
	return LocalizedIconInfo;
}

FReply SLuminLocalizedIconWidget::OnPickIconModelPath(const FString& DirPath)
{
	FString ProjectModelPath = GameLuminPath / TEXT("Model") / LocalizedIconInfo.LanguageCode;
	if (ProjectModelPath != DirPath)
	{
		// Copy the contents of the selected path to the project build path.
		const bool bModelFilesUpdated = CopyDir(DirPath, ProjectModelPath);
		if (bModelFilesUpdated)
		{
			FNotificationInfo SuccessInfo(LOCTEXT("ModelFilesUpdatedLabel", "Icon model files updated."));
			SuccessInfo.ExpireDuration = ExpireDuration;
			FSlateNotificationManager::Get().AddNotification(SuccessInfo);
			LocalizedIconInfo.IconModelPath.Path = DirPath;
			ListWidget->SaveIconData();
			return FReply::Handled();
		}
	}
	return FReply::Unhandled();
}

FReply SLuminLocalizedIconWidget::OnPickIconPortalPath(const FString& DirPath)
{
	FString ProjectPortalPath = GameLuminPath / TEXT("Portal") / LocalizedIconInfo.LanguageCode;
	if (ProjectPortalPath != DirPath)
	{
		// Copy the contents of the selected path to the project build path.
		const bool bPortalFilesUpdated = CopyDir(DirPath, ProjectPortalPath);
		if (bPortalFilesUpdated)
		{
			FNotificationInfo SuccessInfo(LOCTEXT("PortalFilesUpdatedLabel", "Icon portal files updated."));
			SuccessInfo.ExpireDuration = ExpireDuration;
			FSlateNotificationManager::Get().AddNotification(SuccessInfo);
			LocalizedIconInfo.IconPortalPath.Path = DirPath;
			ListWidget->SaveIconData();
			return FReply::Handled();
		}
	}
	
	return FReply::Unhandled();
}

FReply SLuminLocalizedIconWidget::OnClearIconModelPath()
{
	LocalizedIconInfo.IconModelPath.Path.Empty();
	ListWidget->SaveIconData();
	return FReply::Handled();
}

FReply SLuminLocalizedIconWidget::OnClearIconPortalPath()
{
	LocalizedIconInfo.IconPortalPath.Path.Empty();
	ListWidget->SaveIconData();
	return FReply::Handled();
}

FReply SLuminLocalizedIconWidget::OnRemove()
{
	ListWidget->RemoveIconWidget(this);
	return FReply::Handled();
}

void SLuminLocalizedIconWidget::OnPickLocale(const FString& Locale)
{
	LocalizedIconInfo.LanguageCode = Locale;
	ListWidget->SaveIconData();
}

bool SLuminLocalizedIconWidget::CopyDir(FString SourceDir, FString TargetDir)
{
	FPaths::NormalizeDirectoryName(SourceDir);
	FPaths::NormalizeDirectoryName(TargetDir);
	if (!IPlatformFile::GetPlatformPhysical().DirectoryExists(*SourceDir))
	{
		return false;
	}
	// The source control utilities only deal with single files at a time. Hence need to collect
	// the files we are copying and copy each one in turn.
	TArray<FString> FilesToCopy;
	int FilesCopiedCount = 0;
	IPlatformFile::GetPlatformPhysical().FindFilesRecursively(FilesToCopy, *SourceDir, nullptr);
	FText Description = FText::FromString(FPaths::GetBaseFilename(TargetDir));

	// Delete files already existing in TargetDir but not in SourceDir i.e. the ones that won't be replaced.
	TArray<FString> ExistingFiles;
	IPlatformFile::GetPlatformPhysical().FindFilesRecursively(ExistingFiles, *TargetDir, nullptr);
	for (const FString& ExistingFile : ExistingFiles)
	{
		const FString ExistingFileInSourceDir = ExistingFile.Replace(*TargetDir, *SourceDir);
		if (!IPlatformFile::GetPlatformPhysical().FileExists(*ExistingFileInSourceDir))
		{
			if (SourceControlHelpers::IsEnabled())
			{
				SourceControlHelpers::MarkFileForDelete(ExistingFile);
			}
			else
			{
				IPlatformFile::GetPlatformPhysical().DeleteFile(*ExistingFile);
			}
		}
	}

	for (FString& FileToCopy : FilesToCopy)
	{
		FString NewFile = FileToCopy.Replace(*SourceDir, *TargetDir);
		if (IPlatformFile::GetPlatformPhysical().FileExists(*FileToCopy))
		{
			FString ToCopySubDir = FPaths::GetPath(FileToCopy);
			if (!IPlatformFile::GetPlatformPhysical().DirectoryExists(*ToCopySubDir))
			{
				IPlatformFile::GetPlatformPhysical().CreateDirectoryTree(*ToCopySubDir);
			}
			FText ErrorMessage;
			if (SourceControlHelpers::CopyFileUnderSourceControl(NewFile, FileToCopy, Description, ErrorMessage))
			{
				FilesCopiedCount += 1;
			}
			else
			{
				FNotificationInfo Info(ErrorMessage);
				Info.ExpireDuration = 3.0f;
				FSlateNotificationManager::Get().AddNotification(Info);
			}
		}
	}

	if (FilesCopiedCount == 0)
	{
		FNotificationInfo Info(LOCTEXT("NoValidSourceFilesFound", "Failed to locate valid icon source files in selected directory."));
		Info.ExpireDuration = 3.0f;
		FSlateNotificationManager::Get().AddNotification(Info);
		return false;
	}

	return true;
}

#undef LOCTEXT_NAMESPACE
