// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "OculusPlatformToolSettings.h"
#include "Widgets/SWidget.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/STextComboBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Input/SButton.h"
#include "Engine/PostProcessVolume.h"
#include "Framework/Text/SlateHyperlinkRun.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "Async/AsyncWork.h"
#include "HAL/Event.h"
#include "HAL/ThreadSafeBool.h"
#include "OVR_Plugin.h"

class SOculusPlatformToolWidget;

// Function Delegates
DECLARE_DELEGATE_OneParam(FEnableUploadButtonDel, bool);
DECLARE_DELEGATE_OneParam(FUpdateLogTextDel, FString);
DECLARE_DELEGATE_OneParam(FSetProcessDel, FProcHandle);
DECLARE_DELEGATE_RetVal_TwoParams(bool, FFieldValidatorDel, FString, FString&);

class SOculusPlatformToolWidget : public SCompoundWidget
{
public:
	typedef void(SOculusPlatformToolWidget::*PTextComboBoxDel)(TSharedPtr<FString>, ESelectInfo::Type);
	typedef void(SOculusPlatformToolWidget::*PTextComittedDel)(const FText&, ETextCommit::Type);
	typedef FReply(SOculusPlatformToolWidget::*PButtonClickedDel)();
	typedef bool(SOculusPlatformToolWidget::*PFieldValidatorDel)(FString, FString&);

	SLATE_BEGIN_ARGS(SOculusPlatformToolWidget)
	{}
	SLATE_END_ARGS();

	SOculusPlatformToolWidget();
	void Construct(const FArguments& InArgs);
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

	static FString LogText;

private:
	TSharedPtr<SMultiLineEditableTextBox> ToolConsoleLog;
	TSharedPtr<SVerticalBox> GeneralSettingsBox;
	TSharedPtr<SHorizontalBox> ButtonToolbar;

	UEnum* PlatformEnum;
	UOculusPlatformToolSettings* PlatformSettings;
	TArray<TSharedPtr<FString>> OculusPlatforms;

	bool ActiveUploadButton;
	FProcHandle PlatformProcess;
	FThreadSafeBool LogTextUpdated;

	FEnableUploadButtonDel EnableUploadButtonDel;
	FUpdateLogTextDel UpdateLogTextDel;
	FSetProcessDel SetProcessDel;

	// Callbacks
	FReply OnStartPlatformUpload();
	FReply OnSelectRiftBuildDirectory();
	FReply OnClearRiftBuildDirectory();
	FReply OnSelectLaunchFilePath();
	FReply OnClearLaunchFilePath();
	FReply OnCancelUpload();

	void OnPlatformSettingChanged(TSharedPtr<FString> ItemSelected, ESelectInfo::Type SelectInfo);
	void OnApplicationIDChanged(const FText& InText, ETextCommit::Type InCommitType);
	void OnApplicationTokenChanged(const FText& InText, ETextCommit::Type InCommitType);
	void OnReleaseChannelChanged(const FText& InText, ETextCommit::Type InCommitType);
	void OnReleaseNoteChanged(const FText& InText, ETextCommit::Type InCommitType);
	void OnRiftBuildVersionChanged(const FText& InText, ETextCommit::Type InCommitType);

	// UI Constructors
	void BuildGeneralSettingsBox(TSharedPtr<SVerticalBox> box);
	void BuildTextComboBoxField(TSharedPtr<SVerticalBox> box, FText name, TArray<TSharedPtr<FString>>* options, TSharedPtr<FString> current, PTextComboBoxDel deleg);
	void BuildTextField(TSharedPtr<SVerticalBox> box, FText name, FText text, FText tooltip, PTextComittedDel deleg, bool isPassword = false);
	void BuildFileDirectoryField(TSharedPtr<SVerticalBox> box, FText name, FText path, FText tooltip, PButtonClickedDel deleg, PButtonClickedDel clearDeleg);
	void BuildButtonToolbar(TSharedPtr<SHorizontalBox> box);

	// Text Field Validators
	void ValidateTextField(PFieldValidatorDel del, FString text, FString name, bool& success);
	bool GenericFieldValidator(FString text, FString& error);
	bool ApplicationIDFieldValidator(FString text, FString& error);
	bool DirectoryFieldValidator(FString text, FString& error);
	bool FileFieldValidator(FString text, FString& error);

	bool ConstructArguments(FString& args);
	void EnableUploadButton(bool enabled);
	void LoadConfigSettings();
	void UpdateLogText(FString text);
	void SetPlatformProcess(FProcHandle proc);
};

class FPlatformDownloadTask : public FNonAbandonableTask
{
	friend class FAsyncTask<FPlatformDownloadTask>;

private:
	FUpdateLogTextDel UpdateLogText;
	FString ToolConsoleLog;
	FEvent* downloadCompleteEvent;
	FEvent* SaveCompleteEvent;
	TArray<uint8> httpData;

public:
	FPlatformDownloadTask(FUpdateLogTextDel textDel, FEvent* saveEvent);

	void OnDownloadRequestComplete(FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded);
	void OnRequestDownloadProgress(FHttpRequestPtr HttpRequest, int32 BytesSend, int32 InBytesReceived);

protected:
	void DoWork();
	void UpdateProgressLog(int progress);

	FORCEINLINE TStatId GetStatId() const
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(FPlatformDownloadTask, STATGROUP_ThreadPoolAsyncTasks);
	}
};

class FPlatformUploadTask : public FNonAbandonableTask
{
	friend class FAsyncTask<FPlatformUploadTask>;

public:
	FPlatformUploadTask(FString args, FEnableUploadButtonDel del, FUpdateLogTextDel textDel, FSetProcessDel procDel);

private:
	void* ReadPipe;
	void* WritePipe;

	FSetProcessDel SetProcess;
	FUpdateLogTextDel UpdateLogText;
	FEnableUploadButtonDel EnableUploadButton;
	FEvent* PlatformToolCreatedEvent;
	FString LaunchArgs;

protected:
	void DoWork();

	FORCEINLINE TStatId GetStatId() const
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(FPlatformUploadTask, STATGROUP_ThreadPoolAsyncTasks);
	}
};

