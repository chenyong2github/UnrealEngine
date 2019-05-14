// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "OculusPlatformToolWidget.h"
#include "Widgets/Text/SRichTextBlock.h"
#include "DesktopPlatformModule.h"
#include "Editor.h"
#include "EditorStyleSet.h"
#include "Misc/FileHelper.h"
#include "Internationalization/Regex.h"
#include "Misc/MessageDialog.h"
#include "Widgets/Layout/SExpandableArea.h"

#define LOCTEXT_NAMESPACE "OculusPlatformToolWidget"

const FString UrlPlatformUtil = "https://www.oculus.com/download_app/?id=1076686279105243";
const FString ProjectPlatformUtilPath = "Oculus/Tools/ovr-platform-util.exe";

FString SOculusPlatformToolWidget::LogText;

SOculusPlatformToolWidget::SOculusPlatformToolWidget()
{
	LogTextUpdated = false;
	ActiveUploadButton = true;

	LoadConfigSettings();

	EnableUploadButtonDel.BindRaw(this, &SOculusPlatformToolWidget::EnableUploadButton);
	UpdateLogTextDel.BindRaw(this, &SOculusPlatformToolWidget::UpdateLogText);
	SetProcessDel.BindRaw(this, &SOculusPlatformToolWidget::SetPlatformProcess);

	ovrp_SendEvent2("oculus_platform_tool", "show_window", "integration");
}

void SOculusPlatformToolWidget::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	// Update log text if it changes, otherwise constant updating will yeild the field unselectable.
	if (LogTextUpdated)
	{
		ToolConsoleLog->SetText(FText::FromString(LogText));
		LogTextUpdated = false;
	}
}

void SOculusPlatformToolWidget::Construct(const FArguments& InArgs)
{
	auto logTextBox = SNew(SMultiLineEditableTextBox).IsReadOnly(true);
	ToolConsoleLog = logTextBox;

	auto mainVerticalBox = SNew(SVerticalBox);
	GeneralSettingsBox = mainVerticalBox;

	auto buttonToolbarBox = SNew(SHorizontalBox);
	ButtonToolbar = buttonToolbarBox;
	
	BuildGeneralSettingsBox(GeneralSettingsBox);
	BuildButtonToolbar(ButtonToolbar);

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FEditorStyle::GetBrush("ToolPanel.LightGroupBorder"))
		.Padding(2)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().Padding(0, 0).FillHeight(1.f)
			[
				SNew(SScrollBox)
				+ SScrollBox::Slot()
				[
					SNew(SExpandableArea)
					.HeaderPadding(5)
					.Padding(5)
					.BorderBackgroundColor(FLinearColor(0.4f, 0.4f, 0.4f, 1.0f))
					.BodyBorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
					.BodyBorderBackgroundColor(FLinearColor::White)
					.InitiallyCollapsed(false)
					.HeaderContent()
					[
						SNew(SRichTextBlock)
						.TextStyle(FEditorStyle::Get(), "ToolBar.Heading")
						.DecoratorStyleSet(&FEditorStyle::Get()).AutoWrapText(true)
						.Text(LOCTEXT("GeneralSettings", "<RichTextBlock.Bold>General Settings</>"))
					]
					.BodyContent()
					[
						mainVerticalBox
					]
				]
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				buttonToolbarBox
			]
			+ SVerticalBox::Slot().FillHeight(1.f)
			[
				SNew(SBorder)
				.BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
				[
					logTextBox
				]
			]
		]
	];
}

void SOculusPlatformToolWidget::BuildGeneralSettingsBox(TSharedPtr<SVerticalBox> box)
{
	box.Get()->ClearChildren();

	BuildTextComboBoxField(GeneralSettingsBox, LOCTEXT("TargetPlatform", "Target Platform"),
		&OculusPlatforms, OculusPlatforms[PlatformSettings->GetTargetPlatform()],
		&SOculusPlatformToolWidget::OnPlatformSettingChanged);

	// Build field for Oculus Application ID.
	BuildTextField(box, LOCTEXT("AppID", "Oculus Application ID"), FText::FromString(PlatformSettings->GetApplicationID()), 
		LOCTEXT("AppIDTT", "Specifies the ID of your app. Obtained from the API tab of your app in the Oculus Dashboard."),
		&SOculusPlatformToolWidget::OnApplicationIDChanged);

	// Build field for Oculus Application Token.
	BuildTextField(box, LOCTEXT("AppToken", "Oculus Application Token"), FText::FromString(PlatformSettings->GetApplicationToken()),
		LOCTEXT("AppTokenTT", "Specifies the app secret token. Obtained from the API tab of your app in the Oculus Dashboard."),
		&SOculusPlatformToolWidget::OnApplicationTokenChanged, true);

	// Build field for Release Channel.
	BuildTextField(box, LOCTEXT("ReleaseChannel", "Release Channel"), FText::FromString(PlatformSettings->GetReleaseChannel()),
		LOCTEXT("ReleaseChannelTT", "Specifies the release channel for uploading the build. Release channel names are not case-sensitive."),
		&SOculusPlatformToolWidget::OnReleaseChannelChanged);

	// Build field for Release Notes.
	BuildTextField(box, LOCTEXT("ReleaseNote", "Release Note"), FText::FromString(PlatformSettings->GetReleaseNote()),
		LOCTEXT("ReleaseNoteTT", "Specifies the release note text shown to users."),
		&SOculusPlatformToolWidget::OnReleaseNoteChanged);

	// Platform specific fields.
	if (PlatformSettings->GetTargetPlatform() == (uint8)EOculusPlatformTarget::Rift)
	{
		// Build field for Rift Build Directory.
		BuildFileDirectoryField(box, LOCTEXT("BuildPath", "Rift Build Directory"), FText::FromString(PlatformSettings->OculusRiftBuildDirectory),
			LOCTEXT("BuildPathTT", "Specifies the full path to the directory containing your build files."),
			&SOculusPlatformToolWidget::OnSelectRiftBuildDirectory, &SOculusPlatformToolWidget::OnClearRiftBuildDirectory);

		// Build field for Build Version.
		BuildTextField(box, LOCTEXT("BuildVersion", "Build Version"), FText::FromString(PlatformSettings->OculusRiftBuildVersion),
			LOCTEXT("BuildVersionTT", "Specifies the version number shown to users."),
			&SOculusPlatformToolWidget::OnRiftBuildVersionChanged);

		// Build field for Launch File Path.
		BuildFileDirectoryField(box, LOCTEXT("LaunchPath", "Launch File Path"), FText::FromString(PlatformSettings->GetLaunchFilePath()),
			LOCTEXT("LaunchPathTT", " Specifies the path to the executable that launches your app."),
			&SOculusPlatformToolWidget::OnSelectLaunchFilePath, &SOculusPlatformToolWidget::OnClearLaunchFilePath);
	}
	else
	{
		// Build field for APK File Path.
		BuildFileDirectoryField(box, LOCTEXT("APKLaunchPath", "APK File Path"), FText::FromString(PlatformSettings->GetLaunchFilePath()),
			LOCTEXT("APKLaunchPathTT", " Specifies the path to the APK that launches your app."),
			&SOculusPlatformToolWidget::OnSelectLaunchFilePath, &SOculusPlatformToolWidget::OnClearLaunchFilePath);
	}
}

void SOculusPlatformToolWidget::BuildTextField(TSharedPtr<SVerticalBox> box, FText name, FText text, FText tooltip, 
	PTextComittedDel deleg, bool isPassword)
{
	box.Get()->AddSlot()
	.Padding(1)
	.AutoHeight()
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().Padding(1).AutoWidth()
		[
			SNew(SBox)
			.WidthOverride(250.f)
			[
				SNew(STextBlock)
				.Text(name)
				.ToolTipText(tooltip)
			]
		]
		+ SHorizontalBox::Slot().Padding(1).FillWidth(1.f)
		[
			SNew(SEditableTextBox)
			.Text(text)
			.IsPassword(isPassword)
			.OnTextCommitted(this, deleg)
		]
	];
}

void SOculusPlatformToolWidget::BuildTextComboBoxField(TSharedPtr<SVerticalBox> box, FText name, 
	TArray<TSharedPtr<FString>>* options, TSharedPtr<FString> current, PTextComboBoxDel deleg)
{
	box.Get()->AddSlot()
	.Padding(1)
	.AutoHeight()
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().Padding(1).AutoWidth()
		[
			SNew(SBox)
			.WidthOverride(250.f)
			[
				SNew(SRichTextBlock)
				.DecoratorStyleSet(&FEditorStyle::Get())
				.Text(name)
			]
		]
		+ SHorizontalBox::Slot().Padding(1).FillWidth(1.f)
		[
			SNew(STextComboBox)
			.OptionsSource(options)
			.InitiallySelectedItem(current)
			.OnSelectionChanged(this, deleg)
		]
	];
}

void SOculusPlatformToolWidget::BuildFileDirectoryField(TSharedPtr<SVerticalBox> box, FText name, FText path, FText tooltip, 
	PButtonClickedDel deleg, PButtonClickedDel clearDeleg)
{
	EVisibility cancelButtonVisibility = path.IsEmpty() ? EVisibility::Hidden : EVisibility::Visible;

	box.Get()->AddSlot()
	.Padding(1)
	.AutoHeight()
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().Padding(1).AutoWidth()
		[
			SNew(SBox)
			.WidthOverride(250.f)
			[
				SNew(STextBlock)
				.Text(name)
				.ToolTipText(tooltip)
			]
		]
		+ SHorizontalBox::Slot().Padding(1).FillWidth(1.f)
		[
			SNew(SEditableText)
			.Text(path)
			.IsReadOnly(true)
			.Justification(ETextJustify::Left)
		]
		+ SHorizontalBox::Slot().Padding(1).AutoWidth().HAlign(EHorizontalAlignment::HAlign_Right)
		[
			SNew(SButton)
			.Text(FText::FromString("X"))
			.Visibility(cancelButtonVisibility)
			.OnClicked(this, clearDeleg)
			.ButtonColorAndOpacity(FLinearColor(0.36f, 0.1f, 0.05f))
		]
		+ SHorizontalBox::Slot().Padding(1).AutoWidth().HAlign(EHorizontalAlignment::HAlign_Right)
		[
			SNew(SButton)
			.Text((LOCTEXT("Choose", "Choose...")))
			.OnClicked(this, deleg)
		]
	];
}

void SOculusPlatformToolWidget::BuildButtonToolbar(TSharedPtr<SHorizontalBox> box)
{
	box.Get()->ClearChildren();

	box.Get()->AddSlot().FillWidth(1.f);
	box.Get()->AddSlot().AutoWidth().Padding(2.f)
	[
		SNew(SButton)
		.Text((LOCTEXT("Upload", "Upload")))
		.OnClicked(this, &SOculusPlatformToolWidget::OnStartPlatformUpload)
		.IsEnabled(ActiveUploadButton)
	];
	box.Get()->AddSlot().AutoWidth().Padding(2.f)
	[
		SNew(SButton)
		.Text((LOCTEXT("Cancel", "Cancel")))
		.OnClicked(this, &SOculusPlatformToolWidget::OnCancelUpload)
		.IsEnabled(!ActiveUploadButton)
	];
	box.Get()->AddSlot().FillWidth(1.f);
}

bool SOculusPlatformToolWidget::ConstructArguments(FString& args)
{
	// Build the args string that will be passed to the CLI. Print all errors that occur to the log.
	bool success = true;

	switch (PlatformSettings->GetTargetPlatform())
	{
		case (uint8)EOculusPlatformTarget::Rift:
			args = "upload-rift-build";
			break;
		case (uint8)EOculusPlatformTarget::Quest:
			args = "upload-quest-build";
			break;
		case (uint8)EOculusPlatformTarget::Mobile:
			args = "upload-mobile-build";
			break;
		default:
			UpdateLogText(LogText + "ERROR: Invalid target platform selected");
			success = false;
			break;
	}

	// Oculus Application ID check and command.
	ValidateTextField(&SOculusPlatformToolWidget::ApplicationIDFieldValidator, PlatformSettings->GetApplicationID(),
		LOCTEXT("ApplicationID", "Application ID").ToString(), success);
	args += " --app_id \"" + PlatformSettings->GetApplicationID() + "\"";

	// Oculus Application Token check and command.
	ValidateTextField(&SOculusPlatformToolWidget::GenericFieldValidator, PlatformSettings->GetApplicationToken(),
		LOCTEXT("ApplicationToken", "Application Token").ToString(), success);
	args += " --app_secret \"" + PlatformSettings->GetApplicationToken() + "\"";

	// Release Channel check and command.
	ValidateTextField(&SOculusPlatformToolWidget::GenericFieldValidator, PlatformSettings->GetReleaseChannel(),
		LOCTEXT("ReleaseChannel", "Release Channel").ToString(), success);
	args += " --channel \"" + PlatformSettings->GetReleaseChannel() + "\"";

	// Release Note check and command. Not a required command.
	if (!PlatformSettings->GetReleaseNote().IsEmpty())
	{
		FString SanatizedReleaseNote = PlatformSettings->GetReleaseNote();
		SanatizedReleaseNote = SanatizedReleaseNote.Replace(TEXT("\""), TEXT("\"\""));
		args += " --notes \"" + SanatizedReleaseNote + "\"";
	}

	// Platform specific commands
	if (PlatformSettings->GetTargetPlatform() == (uint8)EOculusPlatformTarget::Rift)
	{
		// Launch File Path check and command.
		ValidateTextField(&SOculusPlatformToolWidget::FileFieldValidator, PlatformSettings->GetLaunchFilePath(), 
			LOCTEXT("LaunchFile", "Launch File Path").ToString(), success);
		args += " --launch-file \"" + PlatformSettings->GetLaunchFilePath() + "\"";

		// Rift Build Directory check and command.
		ValidateTextField(&SOculusPlatformToolWidget::DirectoryFieldValidator, PlatformSettings->OculusRiftBuildDirectory,
			LOCTEXT("RiftBuildDir", "Rift Build Directory").ToString(), success);
		args += " --build_dir \"" + PlatformSettings->OculusRiftBuildDirectory + "\"";

		// Rift Build Version check and command.
		ValidateTextField(&SOculusPlatformToolWidget::GenericFieldValidator, PlatformSettings->OculusRiftBuildVersion,
			LOCTEXT("BuildVersion", "Build Version").ToString(), success);
		args += " --version \"" + PlatformSettings->OculusRiftBuildVersion + "\"";
	}
	else
	{
		// APK File Path check and command.
		ValidateTextField(&SOculusPlatformToolWidget::FileFieldValidator, PlatformSettings->GetLaunchFilePath(),
			LOCTEXT("APKLaunchFile", "APK File Path").ToString(), success);
		args += " --apk \"" + PlatformSettings->GetLaunchFilePath() + "\"";
	}

	return success;
}

void SOculusPlatformToolWidget::EnableUploadButton(bool enabled)
{
	ActiveUploadButton = enabled;
	BuildButtonToolbar(ButtonToolbar);
}

void SOculusPlatformToolWidget::LoadConfigSettings()
{
	PlatformSettings = GetMutableDefault<UOculusPlatformToolSettings>();
	PlatformEnum = StaticEnum<EOculusPlatformTarget>();

	OculusPlatforms.Empty();
	for (uint8 i = 0; i < (uint8)EOculusPlatformTarget::Length; i++)
	{
		OculusPlatforms.Add(MakeShareable(new FString(PlatformEnum->GetDisplayNameTextByIndex((int64)i).ToString())));
	}
}

FReply SOculusPlatformToolWidget::OnStartPlatformUpload()
{
	FString launchArgs;

	UpdateLogText("");
	ovrp_SendEvent2("oculus_platform_tool", "upload", "integration");
	if (ConstructArguments(launchArgs))
	{
		UpdateLogText(LogText + LOCTEXT("StartUpload", "Starting Platform Tool Upload Process . . .\n").ToString());
		(new FAsyncTask<FPlatformUploadTask>(launchArgs, EnableUploadButtonDel, UpdateLogTextDel, SetProcessDel))->StartBackgroundTask();
	}
	return FReply::Handled();
}

void SOculusPlatformToolWidget::OnPlatformSettingChanged(TSharedPtr<FString> ItemSelected, ESelectInfo::Type SelectInfo)
{
	if (!ItemSelected.IsValid())
	{
		return;
	}

	for (uint8 i = 0; i < (uint8)EOculusPlatformTarget::Length; i++)
	{
		if (PlatformEnum->GetDisplayNameTextByIndex(i).EqualTo(FText::FromString(*ItemSelected)))
		{
			if (PlatformSettings != NULL)
			{
				PlatformSettings->SetTargetPlatform(i);
				PlatformSettings->SaveConfig();

				LoadConfigSettings();
				BuildGeneralSettingsBox(GeneralSettingsBox);
				break;
			}
		}
	}
}

void SOculusPlatformToolWidget::OnApplicationIDChanged(const FText& InText, ETextCommit::Type InCommitType)
{
	if (PlatformSettings != NULL)
	{
		PlatformSettings->SetApplicationID(InText.ToString());
		PlatformSettings->SaveConfig();
	}
}

void SOculusPlatformToolWidget::OnApplicationTokenChanged(const FText& InText, ETextCommit::Type InCommitType)
{
	if (PlatformSettings != NULL)
	{
		PlatformSettings->SetApplicationToken(InText.ToString());
		PlatformSettings->SaveConfig();
	}
}

void SOculusPlatformToolWidget::OnReleaseChannelChanged(const FText& InText, ETextCommit::Type InCommitType)
{
	if (PlatformSettings != NULL)
	{
		PlatformSettings->SetReleaseChannel(InText.ToString());
		PlatformSettings->SaveConfig();
	}
}

void SOculusPlatformToolWidget::OnReleaseNoteChanged(const FText& InText, ETextCommit::Type InCommitType)
{
	if (PlatformSettings != NULL)
	{
		PlatformSettings->SetReleaseNote(InText.ToString());
		PlatformSettings->SaveConfig();
	}
}

void SOculusPlatformToolWidget::OnRiftBuildVersionChanged(const FText& InText, ETextCommit::Type InCommitType)
{
	if (PlatformSettings != NULL)
	{
		PlatformSettings->OculusRiftBuildVersion = InText.ToString();
		PlatformSettings->SaveConfig();
	}
}

FReply SOculusPlatformToolWidget::OnSelectRiftBuildDirectory()
{
	TSharedPtr<SWindow> parentWindow = FSlateApplication::Get().FindWidgetWindow(AsShared());
	const void* parentWindowHandle = (parentWindow.IsValid() && parentWindow->GetNativeWindow().IsValid()) ? parentWindow->GetNativeWindow()->GetOSWindowHandle() : nullptr;

	if (PlatformSettings != NULL)
	{
		FString path;
		FString defaultPath = PlatformSettings->OculusRiftBuildDirectory.IsEmpty() ? FPaths::ProjectContentDir() : PlatformSettings->OculusRiftBuildDirectory;
		if (FDesktopPlatformModule::Get()->OpenDirectoryDialog(parentWindowHandle, "Choose Rift Build Directory", defaultPath, path))
		{
			PlatformSettings->OculusRiftBuildDirectory = path;
			PlatformSettings->SaveConfig();
			BuildGeneralSettingsBox(GeneralSettingsBox);
		}
	}
	return FReply::Handled();
}

FReply SOculusPlatformToolWidget::OnClearRiftBuildDirectory()
{
	if (PlatformSettings != NULL)
	{
		PlatformSettings->OculusRiftBuildDirectory.Empty();
		PlatformSettings->SaveConfig();
		BuildGeneralSettingsBox(GeneralSettingsBox);
	}
	return FReply::Handled();
}

FReply SOculusPlatformToolWidget::OnSelectLaunchFilePath()
{
	TSharedPtr<SWindow> parentWindow = FSlateApplication::Get().FindWidgetWindow(AsShared());
	const void* parentWindowHandle = (parentWindow.IsValid() && parentWindow->GetNativeWindow().IsValid()) ? parentWindow->GetNativeWindow()->GetOSWindowHandle() : nullptr;

	if (PlatformSettings != NULL)
	{
		TArray<FString> path;
		FString defaultPath = PlatformSettings->GetLaunchFilePath().IsEmpty() ? FPaths::ProjectContentDir() : PlatformSettings->GetLaunchFilePath();
		FString fileType = PlatformSettings->GetTargetPlatform() == (uint8)EOculusPlatformTarget::Rift ? "Executables (*.exe)|*.exe" : "APKs (*.apk)|*.apk";
		if (FDesktopPlatformModule::Get()->OpenFileDialog(parentWindowHandle, "Choose Launch File", defaultPath, defaultPath, fileType, EFileDialogFlags::None, path))
		{
			if (path.Num() > 0)
			{
				PlatformSettings->SetLaunchFilePath(FPaths::ConvertRelativePathToFull(path[0]));
			}
			PlatformSettings->SaveConfig();
			BuildGeneralSettingsBox(GeneralSettingsBox);
		}
	}
	return FReply::Handled();
}

FReply SOculusPlatformToolWidget::OnClearLaunchFilePath()
{
	if (PlatformSettings != NULL)
	{
		PlatformSettings->SetLaunchFilePath("");
		PlatformSettings->SaveConfig();
		BuildGeneralSettingsBox(GeneralSettingsBox);
	}
	return FReply::Handled();
}

FReply SOculusPlatformToolWidget::OnCancelUpload()
{
	if (FMessageDialog::Open(EAppMsgType::OkCancel, LOCTEXT("CancelUploadWarning", "Are you sure you want to cancel the upload process?")) == EAppReturnType::Ok)
	{
		if (PlatformProcess.IsValid())
		{
			FPlatformProcess::TerminateProc(PlatformProcess);
			UpdateLogText(LogText + LOCTEXT("UploadCancel", "Upload process was canceled.").ToString());
		}
	}
	return FReply::Handled();
}

void SOculusPlatformToolWidget::ValidateTextField(PFieldValidatorDel del, FString text, FString name, bool& success)
{
	FString error = "";
	FFieldValidatorDel fieldValidator;

	// Check the given field with the given field validator and print the error if it fails.
	fieldValidator.BindSP(this, del);
	if (!fieldValidator.Execute(text, error))
	{
		FString errorMessage = LOCTEXT("Error", "ERROR: Please verify that the {0} is correct. ").ToString();
		errorMessage = FString::Format(*errorMessage, { name });
		UpdateLogText(LogText + errorMessage + (error.IsEmpty() ? "\n" : error + "\n"));
		success = false;
	}
}

bool SOculusPlatformToolWidget::GenericFieldValidator(FString text, FString& error)
{
	if (text.IsEmpty())
	{
		error = LOCTEXT("FieldEmpty", "The field is empty.").ToString();
		return false;
	}
	return true;
}

bool SOculusPlatformToolWidget::ApplicationIDFieldValidator(FString text, FString& error)
{
	const FRegexPattern RegExPat(TEXT("^[0-9]+$"));
	FRegexMatcher RegMatcher(RegExPat, text);

	if (!GenericFieldValidator(text, error))
	{
		return false;
	}
	else if (!RegMatcher.FindNext())
	{
		error = LOCTEXT("InvalidChar", "The field contains invalid characters.").ToString();
		return false;
	}
	return true;
}

bool SOculusPlatformToolWidget::DirectoryFieldValidator(FString text, FString& error)
{
	if (!GenericFieldValidator(text, error))
	{
		return false;
	}
	if (!FPaths::DirectoryExists(text))
	{
		error = LOCTEXT("DirectoryNull", "The directory does not exist.").ToString();
		return false;
	}
	return true;
}

bool SOculusPlatformToolWidget::FileFieldValidator(FString text, FString& error)
{
	if (!GenericFieldValidator(text, error))
	{
		return false;
	}
	if (!FPaths::FileExists(text))
	{
		error = LOCTEXT("FileNull", "The file does not exist.").ToString();
		return false;
	}
	return true;
}

void SOculusPlatformToolWidget::UpdateLogText(FString text)
{
	// Make sure that log text updating happens on the right thread.
	LogText = text;
	LogTextUpdated = true;
}

void SOculusPlatformToolWidget::SetPlatformProcess(FProcHandle proc)
{
	PlatformProcess = proc;
}

//=======================================================================================
//FPlatformDownloadTask

FPlatformDownloadTask::FPlatformDownloadTask(FUpdateLogTextDel textDel, FEvent* saveEvent)
{
	UpdateLogText = textDel;
	SaveCompleteEvent = saveEvent;

	ovrp_SendEvent2("oculus_platform_tool", "provision_util", "integration");
}

void FPlatformDownloadTask::DoWork()
{
	// Create HTTP request for downloading oculus platform tool
	downloadCompleteEvent = FGenericPlatformProcess::GetSynchEventFromPool(false);
	TSharedRef<IHttpRequest> httpRequest = FHttpModule::Get().CreateRequest();

	httpRequest->OnProcessRequestComplete().BindRaw(this, &FPlatformDownloadTask::OnDownloadRequestComplete);
	httpRequest->OnRequestProgress().BindRaw(this, &FPlatformDownloadTask::OnRequestDownloadProgress);
	httpRequest->SetURL(UrlPlatformUtil);

	httpRequest->ProcessRequest();

	UpdateLogText.Execute(SOculusPlatformToolWidget::LogText + LOCTEXT("DownloadProgress", "Downloading Platform Tool: {0}%\n").ToString());
	ToolConsoleLog = SOculusPlatformToolWidget::LogText;
	UpdateProgressLog(0);
	
	// Wait for download to complete
	downloadCompleteEvent->Wait();

	// Save HTTP data
	FString fullPath = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir()) + ProjectPlatformUtilPath;
	if (FFileHelper::SaveArrayToFile(httpData, *fullPath))
	{
		UpdateLogText.Execute(SOculusPlatformToolWidget::LogText + LOCTEXT("DownloadSuccess", "Platform tool successfully downloaded.\n").ToString());
	}
	else
	{
		UpdateLogText.Execute(SOculusPlatformToolWidget::LogText + LOCTEXT("DownloadError", "An error has occured with downloading the platform tool.\n").ToString());
	}

	if (SaveCompleteEvent != NULL)
	{
		SaveCompleteEvent->Trigger();
	}
}

void FPlatformDownloadTask::UpdateProgressLog(int progress)
{
	UpdateLogText.Execute(FString::Format(*ToolConsoleLog, { progress }));
}

void FPlatformDownloadTask::OnRequestDownloadProgress(FHttpRequestPtr HttpRequest, int32 BytesSend, int32 InBytesReceived)
{
	// Update progress on download in tool console log
	FHttpResponsePtr httpResponse = HttpRequest->GetResponse();
	if (httpResponse.IsValid())
	{
		int progress = ((float)InBytesReceived / (float)httpResponse->GetContentLength()) * 100;
		UpdateProgressLog(progress);
	}
}

void FPlatformDownloadTask::OnDownloadRequestComplete(FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded)
{
	// Extract data from HTTP response and trigger download complete event
	if (bSucceeded && HttpResponse.IsValid())
	{
		httpData = HttpResponse->GetContent();
		downloadCompleteEvent->Trigger();
	}
}

//=======================================================================================
//FPlatformUploadTask

FPlatformUploadTask::FPlatformUploadTask(FString args, FEnableUploadButtonDel del, FUpdateLogTextDel textDel, FSetProcessDel procDel)
{
	PlatformToolCreatedEvent = FGenericPlatformProcess::GetSynchEventFromPool(false);

	LaunchArgs = args;
	EnableUploadButton = del;
	UpdateLogText = textDel;
	SetProcess = procDel;

	EnableUploadButton.Execute(false);
}

void FPlatformUploadTask::DoWork()
{
	// Check if the platform tool exists in the project directory. If not, start process to download it.
	if (!FPaths::FileExists(FPaths::ProjectContentDir() + ProjectPlatformUtilPath))
	{
		UpdateLogText.Execute(SOculusPlatformToolWidget::LogText + LOCTEXT("NoCLI", "Unable to find Oculus Platform Tool. Starting download . . .\n").ToString());
		(new FAsyncTask<FPlatformDownloadTask>(UpdateLogText, PlatformToolCreatedEvent))->StartBackgroundTask();

		PlatformToolCreatedEvent->Wait();

		UpdateLogText.Execute(SOculusPlatformToolWidget::LogText + LOCTEXT("StartUploadAfterDownload", "Starting upload . . .\n").ToString());
	}
	 
	// Start up the CLI and pass in arguments.
	FPlatformProcess::CreatePipe(ReadPipe, WritePipe);
	FProcHandle PlatformProcess = FPlatformProcess::CreateProc(*(FPaths::ProjectContentDir() + ProjectPlatformUtilPath), *LaunchArgs, false, true, true, nullptr, 0, nullptr, WritePipe, ReadPipe);
	SetProcess.Execute(PlatformProcess);

	// Redirect CLI output to the tool's log.
	while (FPlatformProcess::IsProcRunning(PlatformProcess))
	{
		FString log = FPlatformProcess::ReadPipe(ReadPipe);
		if (!log.IsEmpty() && !log.Contains("\u001b"))
		{
			UpdateLogText.Execute(SOculusPlatformToolWidget::LogText + log);
		}
	}
	EnableUploadButton.Execute(true);
}

#undef LOCTEXT_NAMESPACE