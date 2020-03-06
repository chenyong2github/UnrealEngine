// Copyright Epic Games, Inc. All Rights Reserved.

#include "LuminTargetSettingsDetails.h"
#include "LuminLocalizedIconListWidget.h"
#include "LuminLocalizedAppNameListWidget.h"
#include "LuminPathPickerWidget.h"

#include "SExternalImageReference.h"
#include "EditorDirectories.h"
#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "UObject/UnrealType.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "SPlatformSetupMessage.h"
#include "DetailCategoryBuilder.h"
#include "DesktopPlatformModule.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Notifications/NotificationManager.h"
#include "SourceControlHelpers.h"
#include "LuminRuntimeSettings.h"
#include "Logging/LogMacros.h"
#include "PropertyPathHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogLuminTargetSettingsDetail, Log, All);

#define LOCTEXT_NAMESPACE "LuminTargetSettingsDetails"

TSharedRef<IDetailCustomization> FLuminTargetSettingsDetails::MakeInstance()
{
	return MakeShareable(new FLuminTargetSettingsDetails);
}

FLuminTargetSettingsDetails::FLuminTargetSettingsDetails()
	: DefaultIconModelPath(TEXT("Build/Lumin/Resources/Model"))
	, DefaultIconPortalPath(TEXT("Build/Lumin/Resources/Portal"))
	, GameLuminPath(FPaths::ProjectDir() / TEXT("Build/Lumin/Resources"))
	, GameProjectSetupPath(GameLuminPath / TEXT("IconSetup.txt"))
	, SavedLayoutBuilder(nullptr)
{
}

void FLuminTargetSettingsDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	SavedLayoutBuilder = &DetailBuilder;

	IconModelPathProp = DetailBuilder.GetProperty("IconModelPath.Path");
	IconPortalPathProp = DetailBuilder.GetProperty("IconPortalPath.Path");
	CertificateProp = DetailBuilder.GetProperty("Certificate.FilePath");

	IconModelPathAttribute.Bind(TAttribute<FString>::FGetter::CreateRaw(this, &FLuminTargetSettingsDetails::IconModelPathGetter));
	IconPortalPathAttribute.Bind(TAttribute<FString>::FGetter::CreateRaw(this, &FLuminTargetSettingsDetails::IconPortalPathGetter));
	CertificateAttribute.Bind(TAttribute<FString>::FGetter::CreateRaw(this, &FLuminTargetSettingsDetails::CertificateGetter));

	BuildAudioSection(DetailBuilder);
	BuildAppTileSection(DetailBuilder);
	BuildLocalizedAppNameSection();
}

FString FLuminTargetSettingsDetails::IconModelPathGetter()
{
	FString Result;
	if (IconModelPathProp->GetValue(Result) == FPropertyAccess::Success && !Result.IsEmpty())
	{
		// If we did the setup, the values are game/project dir relative. So we display
		// them as is. Otherwise we show the default values which will show engine exec relative
		// paths to the platform build resource locations.
		if (!SetupForPlatformAttribute.Get())
		{
			Result.Empty();
			IconModelPathProp->SetValue(Result);
		}
	}
	return Result;
}

FString FLuminTargetSettingsDetails::IconPortalPathGetter()
{
	FString Result;
	if (IconPortalPathProp->GetValue(Result) == FPropertyAccess::Success && !Result.IsEmpty())
	{
		// If we did the setup, the values are game/project dir relative. So we display
		// them as is. Otherwise we show the default values which will show engine exec relative
		// paths to the platform build resource locations.
		if (!SetupForPlatformAttribute.Get())
		{
			Result.Empty();
			IconPortalPathProp->SetValue(Result);
		}
	}
	return Result;
}

FString FLuminTargetSettingsDetails::CertificateGetter()
{
	FString Result;
	CertificateProp->GetValue(Result);
	return Result;
}

void FLuminTargetSettingsDetails::CopySetupFilesIntoProject()
{
	// Start out with the hard-wired default paths.
	FString SourceModelPath = FPaths::EngineDir() / DefaultIconModelPath;
	FString SourcePortalPath = FPaths::EngineDir() / DefaultIconPortalPath;
	// Override with soft-wired defaults from the engine config. These are going to be engine
	// root relative as we are copying from the engine tree to the project tree.
	// But only override if those soft-wired engine paths exists. If they don't it's likely
	// some old invalid value. This prevents someone from using obsolete data.
	FString PropSourceModelPath;
	FString PropSourcePortalPath;
	if (IconModelPathProp->GetValue(PropSourceModelPath) == FPropertyAccess::Success && !PropSourceModelPath.IsEmpty() && FPaths::DirectoryExists(FPaths::EngineDir() / PropSourceModelPath))
	{
		SourceModelPath = FPaths::EngineDir() / PropSourceModelPath;
	}
	if (IconPortalPathProp->GetValue(PropSourcePortalPath) == FPropertyAccess::Success && !PropSourcePortalPath.IsEmpty() && FPaths::DirectoryExists(FPaths::EngineDir() / PropSourcePortalPath))
	{
		SourcePortalPath = FPaths::EngineDir() / PropSourcePortalPath;
	}
	const FString TargetModelPath = GameLuminPath / TEXT("Model");
	const FString TargetPortalPath = GameLuminPath / TEXT("Portal");
	bool DidModelCopy = CopyDir(*SourceModelPath, *TargetModelPath);
	bool DidPortalCopy = CopyDir(*SourcePortalPath, *TargetPortalPath);
	if (DidModelCopy && DidPortalCopy)
	{
		// Touch the setup file to indicate we did the copies.
		delete IPlatformFile::GetPlatformPhysical().OpenWrite(*GameProjectSetupPath);
		// And set the icon path config vars to the project directory now that we have it.
		// This makes it so that the packaging will use these instead of the engine
		// files directly. The values for both are fixed to the project root relative locations.
		if (IconModelPathProp->SetValue(TargetModelPath.Replace(*FPaths::ProjectDir(), TEXT(""))) != FPropertyAccess::Success ||
			IconPortalPathProp->SetValue(TargetPortalPath.Replace(*FPaths::ProjectDir(), TEXT(""))) != FPropertyAccess::Success)
		{
			UE_LOG(LogLuminTargetSettingsDetail, Error, TEXT("Failed to update icon or portal "));
		}
	}
}

void FLuminTargetSettingsDetails::BuildAudioSection(IDetailLayoutBuilder& DetailBuilder)
{
	AudioPluginManager.BuildAudioCategory(DetailBuilder, FString(TEXT("Lumin")));
}

void FLuminTargetSettingsDetails::BuildAppTileSection(IDetailLayoutBuilder& DetailBuilder)
{
	////////// UI for icons..

	IDetailCategoryBuilder& AppTitleCategory = DetailBuilder.EditCategory(TEXT("Magic Leap App Tile"));
	DetailBuilder.HideProperty("IconModelPath");
	DetailBuilder.HideProperty("IconPortalPath");
	DetailBuilder.HideProperty("LocalizedIconInfos");
	TSharedRef<SPlatformSetupMessage> PlatformSetupMessage = SNew(SPlatformSetupMessage, GameProjectSetupPath)
		.PlatformName(LOCTEXT("LuminPlatformName", "Magic Leap"))
		.OnSetupClicked(FSimpleDelegate::CreateLambda([this] { this->CopySetupFilesIntoProject(); }));
	SetupForPlatformAttribute = PlatformSetupMessage->GetReadyToGoAttribute();
	AppTitleCategory.AddCustomRow(LOCTEXT("Warning", "Warning"), false)
		.WholeRowWidget
		[
			PlatformSetupMessage
		];
	AppTitleCategory.AddCustomRow(LOCTEXT("BuildFolderLabel", "Build Folder"), false)
		.IsEnabled(SetupForPlatformAttribute)
		.NameContent()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.Padding(FMargin(0, 1, 0, 1))
			.FillWidth(1.0f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("BuildFolderLabel", "Build Folder"))
				.Font(DetailBuilder.GetDetailFont())
			]
		]
		.ValueContent()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SButton)
				.Text(LOCTEXT("OpenBuildFolderButton", "Open Build Folder"))
				.ToolTipText(LOCTEXT("OpenManifestFolderButton_Tooltip", "Opens the folder containing the build files in Explorer or Finder (it's recommended you check these in to source control to share with your team)"))
				.OnClicked(this, &FLuminTargetSettingsDetails::OpenBuildFolder)
			]
		];

	BuildIconPickers(AppTitleCategory);
		
	////////// UI for signing cert..

	IDetailCategoryBuilder& DistributionSigningCategory = DetailBuilder.EditCategory(TEXT("Distribution Signing"));
	DetailBuilder.HideProperty("Certificate");
	
	BuildCertificatePicker(DistributionSigningCategory, false);
}

bool FLuminTargetSettingsDetails::CopyDir(FString SourceDir, FString TargetDir)
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

	return FilesCopiedCount > 0;
}

void FLuminTargetSettingsDetails::BuildIconPickers(IDetailCategoryBuilder& Category)
{
	FDetailWidgetRow& DefaultIconModelRow = Category.AddCustomRow(LOCTEXT("IconModelPath", "Default Icon Model Path"), false)
		.NameContent()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.Padding(FMargin(0, 1, 0, 1))
			.FillWidth(1.0f)
			[
				SNew(STextBlock)
				.ToolTipText(LOCTEXT("DefaultIconModelPathToolTip", "The default icon model files to use for undefined locales."))
				.Text(LOCTEXT("DefaultIconModelPath", "Default Icon Model Path"))
				.Font(SavedLayoutBuilder->GetDetailFont())
			]
		]
		.ValueContent()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SLuminPathPickerWidget)
				.FilePickerTitle(LOCTEXT("IconModelFolderDialogTitle", "Choose an icon model source.").ToString())
				.FileFilter(TEXT("(*.fbx;*.obj)|*.fbx;*.obj"))
				.StartDirectory(IconModelPathAttribute)
				.ToolTipText(LOCTEXT("PickIconPortalButton_Tooltip", "Select the default (non-locale-specific) icon model to use for the application. All files in the parent folder of the selected model will be copied to the project build folder."))
				.OnPickPath(FOnPickPath::CreateRaw(this, &FLuminTargetSettingsDetails::OnPickDefaultIconModel))
				.OnClearPath(FOnClearPath::CreateRaw(this, &FLuminTargetSettingsDetails::OnClearDefaultIconModel))
			]
		];

	DefaultIconModelRow.IsEnabled(SetupForPlatformAttribute);

	FDetailWidgetRow& DefaultIconPortalRow = Category.AddCustomRow(LOCTEXT("IconPortalPath", "Default Icon Portal Path"), false)
		.NameContent()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.Padding(FMargin(0, 1, 0, 1))
			.FillWidth(1.0f)
			[
				SNew(STextBlock)
				.ToolTipText(LOCTEXT("DefaultIconPortalPathToolTip", "The default icon portal files to use for undefined locales."))
				.Text(LOCTEXT("DefaultIconPortalPath", "Default Icon Portal Path"))
				.Font(SavedLayoutBuilder->GetDetailFont())
			]
		]
		.ValueContent()
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SLuminPathPickerWidget)
				.FilePickerTitle(LOCTEXT("IconPortalFolderDialogTitle", "Choose an icon portal source.").ToString())
				.FileFilter(TEXT("(*.fbx;*.obj)|*.fbx;*.obj"))
				.StartDirectory(IconPortalPathAttribute)
				.ToolTipText(LOCTEXT("PickIconModelButton_Tooltip", "Select the default (non-locale-specific) icon portal to use for the application. All files in the parent folder of the selected portal will be copied to the project build folder."))
				.OnPickPath(FOnPickPath::CreateRaw(this, &FLuminTargetSettingsDetails::OnPickDefaultIconPortal))
				.OnClearPath(FOnClearPath::CreateRaw(this, &FLuminTargetSettingsDetails::OnClearDefaultIconPortal))
			]
		];

	DefaultIconPortalRow.IsEnabled(SetupForPlatformAttribute);
		
	FDetailWidgetRow& LocalizedIconsRow = Category.AddCustomRow(LOCTEXT("LocalizedIconsLabel", "Localized Icon Settings"), false)
		.NameContent()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.Padding(FMargin(0, 1, 0, 1))
			.FillWidth(1.0f)
			[
				SNew(STextBlock)
				.ToolTipText(LOCTEXT("PickIconsButton_Tooltip", "Select the locale specific icon assets to use for the application.  The files will be copied to the project build folder."))
				.Text(LOCTEXT("LocalizedIconsLabel", "Localized Icon Settings"))
				.Font(SavedLayoutBuilder->GetDetailFont())
			]
		]
		.ValueContent()
		[
			SNew(SHorizontalBox)
			+SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SLuminLocalizedIconListWidget)
				.ToolTipText(LOCTEXT("PickIconsButton_Tooltip", "Select the locale specific icon assets to use for the application.  The files will be copied to the project build folder."))
				.GameLuminPath(GameLuminPath)
				.DetailLayoutBuilder(SavedLayoutBuilder)
			]
		];

	LocalizedIconsRow.IsEnabled(SetupForPlatformAttribute);
}

FReply FLuminTargetSettingsDetails::OnPickDefaultIconModel(const FString& ModelFileFullPath)
{
	FString ProjectModelPath = GameLuminPath / TEXT("Model");
	FString SourceModelPath = FPaths::GetPath(ModelFileFullPath);
	if (ProjectModelPath != SourceModelPath)
	{
		// Copy the contents of the selected path to the project build path.
		const bool bModelFilesUpdated = CopyDir(SourceModelPath, ProjectModelPath);
		if (bModelFilesUpdated)
		{
			if (IconModelPathProp->SetValue(DefaultIconModelPath / FPaths::GetCleanFilename(ModelFileFullPath)) != FPropertyAccess::Success)
			{
				UE_LOG(LogLuminTargetSettingsDetail, Error, TEXT("Failed to update icon model."));
			}
			else
			{
				FNotificationInfo SuccessInfo(LOCTEXT("ModelFilesUpdatedLabel", "Icon model files updated."));
				SuccessInfo.ExpireDuration = 5.0f;
				FSlateNotificationManager::Get().AddNotification(SuccessInfo);
			}

			return FReply::Handled();
		}
	}
	return FReply::Unhandled();
}

FReply FLuminTargetSettingsDetails::OnClearDefaultIconModel()
{
	if (IconModelPathProp->SetValue(TEXT("")) != FPropertyAccess::Success)
	{
		UE_LOG(LogLuminTargetSettingsDetail, Error, TEXT("Failed to clear icon model path."));
	}
	return FReply::Handled();
}

FReply FLuminTargetSettingsDetails::OnPickDefaultIconPortal(const FString& PortalFileFullPath)
{
	FString ProjectPortalPath = GameLuminPath / TEXT("Portal");
	FString SourcePortalPath = FPaths::GetPath(PortalFileFullPath);
	if (ProjectPortalPath != SourcePortalPath)
	{
		// Copy the contents of the selected path to the project build path.
		const bool bPortalFilesUpdated = CopyDir(SourcePortalPath, ProjectPortalPath);
		if (bPortalFilesUpdated)
		{
			if (IconPortalPathProp->SetValue(DefaultIconPortalPath / FPaths::GetCleanFilename(PortalFileFullPath)) != FPropertyAccess::Success)
			{
				UE_LOG(LogLuminTargetSettingsDetail, Error, TEXT("Failed to update icon portal."));
			}
			else
			{
				FNotificationInfo SuccessInfo(LOCTEXT("PortalFilesUpdatedLabel", "Icon portal files updated."));
				SuccessInfo.ExpireDuration = 5.0f;
				FSlateNotificationManager::Get().AddNotification(SuccessInfo);
			}

			return FReply::Handled();
		}
	}
	return FReply::Unhandled();
}

FReply FLuminTargetSettingsDetails::OnClearDefaultIconPortal()
{
	if (IconPortalPathProp->SetValue(TEXT("")) != FPropertyAccess::Success)
	{
		UE_LOG(LogLuminTargetSettingsDetail, Error, TEXT("Failed to clear icon portal path."));
	}
	return FReply::Handled();
}

void FLuminTargetSettingsDetails::BuildLocalizedAppNameSection()
{
	IDetailCategoryBuilder& Category = SavedLayoutBuilder->EditCategory(TEXT("Advanced MPK Packaging"));
	SavedLayoutBuilder->HideProperty("LocalizedAppNames");
	FDetailWidgetRow& Row = Category.AddCustomRow(LOCTEXT("LocalizedAppNamesLabel", "Localized App Names"), false)
		.NameContent()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.Padding(FMargin(0, 1, 0, 1))
			.FillWidth(1.0f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("LocalizedAppNamesLabel", "Localized App Names"))
				.ToolTipText(LOCTEXT("LocalizedAppNames_Tooltip", "Edit locale-specific names for the application"))
				.Font(SavedLayoutBuilder->GetDetailFont())
			]
		]
		.ValueContent()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SLuminLocalizedAppNameListWidget)
				.DetailLayoutBuilder(SavedLayoutBuilder)
			]
		];

	Row.IsEnabled(true);
}

FReply FLuminTargetSettingsDetails::OnClearCertificate()
{
	CertificateProp->SetValue(TEXT(""));
	return FReply::Handled();
}

void FLuminTargetSettingsDetails::BuildCertificatePicker(IDetailCategoryBuilder& Category, bool bDisableUntilConfigured)
{
	FDetailWidgetRow& Row = Category.AddCustomRow(LOCTEXT("CertificateFilePathLabel", "Certificate File Path"), false)
		.NameContent()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.Padding(FMargin(0, 1, 0, 1))
			.FillWidth(1.0f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("CertificateFilePathLabel", "Certificate File Path"))
				.Font(SavedLayoutBuilder->GetDetailFont())
			]
		]
		.ValueContent()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SLuminPathPickerWidget)
				.StartDirectory(CertificateAttribute)
				.FilePickerTitle(FString::Printf(TEXT("%s (*.cert)|*.cert"), *LOCTEXT("CertificateFile", "Certificate File").ToString()))
				.FileFilter(FString::Printf(TEXT("%s (*.cert)|*.cert"), *LOCTEXT("CertificateFile", "Certificate File").ToString()))
				.ToolTipText(LOCTEXT("PickCertificateButton_Tooltip", "Select the certificate to use for signing a distribution package. The file will be copied to the project build folder."))
				.OnPickPath(FOnPickPath::CreateRaw(this, &FLuminTargetSettingsDetails::OnPickCertificate))
				.OnClearPath(FOnClearPath::CreateRaw(this, &FLuminTargetSettingsDetails::OnClearCertificate))
			]
		];

	if (bDisableUntilConfigured)
	{
		Row.IsEnabled(SetupForPlatformAttribute);
	}
}

FReply FLuminTargetSettingsDetails::OpenBuildFolder()
{
	const FString BuildFolder = FPaths::ConvertRelativePathToFull(GameLuminPath);
	FPlatformProcess::ExploreFolder(*BuildFolder);

	return FReply::Handled();
}

FReply FLuminTargetSettingsDetails::OnPickCertificate(const FString& SourceCertificateFile)
{
	FString TargetCertificateFile = GameLuminPath / FPaths::GetCleanFilename(SourceCertificateFile);
	if (!IPlatformFile::GetPlatformPhysical().FileExists(*SourceCertificateFile))
	{
		// Sanity check for chosen file. Do nothing if it doesn't exist.
		return FReply::Handled();
	}
	// We only ask for the certificate file.. But we also need the accompanying private key file.
	FString SourceKeyFile = FPaths::Combine(FPaths::GetPath(SourceCertificateFile), FPaths::GetBaseFilename(SourceCertificateFile) + TEXT(".privkey"));
	FString TargetKeyFile = GameLuminPath / FPaths::GetCleanFilename(SourceKeyFile);
	if (!IPlatformFile::GetPlatformPhysical().FileExists(*SourceKeyFile))
	{
		// We really need the key file.
		FNotificationInfo Info(FText(LOCTEXT("LuminMissingPrivKeyFile", "Could not find private key file.")));
		Info.ExpireDuration = 3.0f;
		FSlateNotificationManager::Get().AddNotification(Info);
		return FReply::Handled();
	}
	{
		FText Description = FText::FromString(FPaths::GetBaseFilename(TargetCertificateFile));
		FText ErrorMessage;
		if (!SourceControlHelpers::CopyFileUnderSourceControl(TargetCertificateFile, SourceCertificateFile, Description, ErrorMessage))
		{
			FNotificationInfo Info(ErrorMessage);
			Info.ExpireDuration = 3.0f;
			FSlateNotificationManager::Get().AddNotification(Info);
		}
	}
	{
		FText Description = FText::FromString(FPaths::GetBaseFilename(TargetKeyFile));
		FText ErrorMessage;
		if (!SourceControlHelpers::CopyFileUnderSourceControl(TargetKeyFile, SourceKeyFile, Description, ErrorMessage))
		{
			FNotificationInfo Info(ErrorMessage);
			Info.ExpireDuration = 3.0f;
			FSlateNotificationManager::Get().AddNotification(Info);
			return FReply::Handled();
		}
	}
	CertificateProp->SetValue(TargetCertificateFile.Replace(*FPaths::ProjectDir(), TEXT("")));
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
