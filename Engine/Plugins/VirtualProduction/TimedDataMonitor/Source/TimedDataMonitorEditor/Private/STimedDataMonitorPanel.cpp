// Copyright Epic Games, Inc. All Rights Reserved.

#include "STimedDataMonitorPanel.h"

#include "Editor.h"
#include "EditorFontGlyphs.h"
#include "Editor/EditorPerformanceSettings.h"
#include "Engine/Engine.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "IMessageLogListing.h"
#include "ISettingsModule.h"
#include "ITimedDataInput.h"
#include "ITimeManagementModule.h"
#include "LevelEditor.h"
#include "Logging/MessageLog.h"
#include "MessageLogModule.h"
#include "TimedDataInputCollection.h"
#include "TimedDataMonitorSubsystem.h"

#include "STimecodeProvider.h"
#include "STimedDataListView.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/TimedDataMonitorStyle.h"


#define LOCTEXT_NAMESPACE "STimedDataMonitorPanel"


TWeakPtr<STimedDataMonitorPanel> STimedDataMonitorPanel::WidgetInstance;
FDelegateHandle STimedDataMonitorPanel::LevelEditorTabManagerChangedHandle;


namespace Utilities
{
	static const FName NAME_App = FName("TimedDataSourceMonitorPanelApp");
	static const FName NAME_LevelEditorModuleName("LevelEditor");
	static const FName NAME_LogName = "Timed Data Monitor";

	TSharedRef<SDockTab> CreateTab(const FSpawnTabArgs& Args)
	{
		return SNew(SDockTab)
			.TabRole(ETabRole::NomadTab)
			[
				SNew(STimedDataMonitorPanel)
			];
	}
}


void STimedDataMonitorPanel::RegisterNomadTabSpawner(TSharedRef<FWorkspaceItem> InWorkspaceItem)
{
	auto RegisterTabSpawner = [InWorkspaceItem]()
	{
		FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>(Utilities::NAME_LevelEditorModuleName);
		TSharedPtr<FTabManager> LevelEditorTabManager = LevelEditorModule.GetLevelEditorTabManager();

		LevelEditorTabManager->RegisterTabSpawner(Utilities::NAME_App, FOnSpawnTab::CreateStatic(&Utilities::CreateTab))
			.SetDisplayName(LOCTEXT("TabTitle", "Timed Input Monitor"))
			.SetTooltipText(LOCTEXT("TooltipText", "Monitor inputs that can be time synchronized"))
			.SetGroup(InWorkspaceItem)
			.SetIcon(FSlateIcon(FTimedDataMonitorStyle::Get().GetStyleSetName(), "Img.Timecode.Small"));
	};

	FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>(Utilities::NAME_LevelEditorModuleName);
	if (LevelEditorModule.GetLevelEditorTabManager())
	{
		RegisterTabSpawner();
	}
	else
	{
		LevelEditorTabManagerChangedHandle = LevelEditorModule.OnTabManagerChanged().AddLambda(RegisterTabSpawner);
	}
}


void STimedDataMonitorPanel::UnregisterNomadTabSpawner()
{
	if (FSlateApplication::IsInitialized() && FModuleManager::Get().IsModuleLoaded(Utilities::NAME_LevelEditorModuleName))
	{
		FLevelEditorModule* LevelEditorModule = FModuleManager::GetModulePtr<FLevelEditorModule>(Utilities::NAME_LevelEditorModuleName);
		TSharedPtr<FTabManager> LevelEditorTabManager;
		if (LevelEditorModule)
		{
			LevelEditorTabManager = LevelEditorModule->GetLevelEditorTabManager();
			LevelEditorModule->OnTabManagerChanged().Remove(LevelEditorTabManagerChangedHandle);
		}

		if (LevelEditorTabManager.IsValid())
		{
			LevelEditorTabManager->UnregisterTabSpawner(Utilities::NAME_App);
		}
	}
}


TSharedPtr<STimedDataMonitorPanel> STimedDataMonitorPanel::GetPanelInstance()
{
	return STimedDataMonitorPanel::WidgetInstance.Pin();
}


BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
void STimedDataMonitorPanel::Construct(const FArguments& InArgs)
{
	WidgetInstance = StaticCastSharedRef<STimedDataMonitorPanel>(AsShared());

	FMessageLogModule& MessageLogModule = FModuleManager::LoadModuleChecked<FMessageLogModule>("MessageLog");
	if (!MessageLogModule.IsRegisteredLogListing(Utilities::NAME_LogName))
	{
		MessageLogListing = MessageLogModule.CreateLogListing(Utilities::NAME_LogName);
	}
	else
	{
		MessageLogListing = MessageLogModule.GetLogListing(Utilities::NAME_LogName);
	}

	BuildCalibrationArray();

	TSharedRef<class SWidget> MessageLogListingWidget = MessageLogListing.IsValid() ? MessageLogModule.CreateLogListingWidget(MessageLogListing.ToSharedRef()) : SNullWidget::NullWidget;

	FProperty* PerformanceThrottlingProperty = FindFieldChecked<FProperty>(UEditorPerformanceSettings::StaticClass(), GET_MEMBER_NAME_CHECKED(UEditorPerformanceSettings, bThrottleCPUWhenNotForeground));
	PerformanceThrottlingProperty->GetDisplayNameText();
	FFormatNamedArguments Arguments;
	Arguments.Add(TEXT("PropertyName"), PerformanceThrottlingProperty->GetDisplayNameText());
	FText PerformanceWarningText = FText::Format(LOCTEXT("kPerformanceWarningMessage", "Warning: The editor setting '{PropertyName}' is currently enabled\nThis will stop editor windows from updating in realtime while the editor is not in focus"), Arguments);

	const int32 ButtonBoxSize = 28;

	ChildSlot
	[
		SNew(SVerticalBox)
		// toolbar
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(10.f, 10.f, 10.f, 10.f)
		[
			SNew(SBorder)
			.VAlign(VAlign_Center)
			.Padding(FMargin(5.f, 5.f, 5.f, 5.f))
			[
				SNew(SHorizontalBox)
				// Calibrate button
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0.f, 0.f, 10.f, 0.f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.FillWidth(1.f)
					[
						SNew(SButton)
						.ToolTipText(this, &STimedDataMonitorPanel::GetCalibrateButtonTooltip)
						.ButtonStyle(FEditorStyle::Get(), "FlatButton.Success")
						.ForegroundColor(FLinearColor::White)
						.ContentPadding(FMargin(4, 0))
						.HAlign(HAlign_Center)
						.VAlign(VAlign_Center)
						.OnClicked(this, &STimedDataMonitorPanel::OnCalibrateClicked)
						[
							SNew(STextBlock)
							.TextStyle(FTimedDataMonitorStyle::Get(), "TextBlock.Large")
							.Text(this, &STimedDataMonitorPanel::GetCalibrateButtonText)
						]
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(SComboButton)
						.ButtonStyle(FEditorStyle::Get(), "FlatButton.Success")
						.ForegroundColor(FLinearColor::White)
						.HAlign(HAlign_Center)
						.VAlign(VAlign_Center)
						.OnGetMenuContent(this, &STimedDataMonitorPanel::OnCalibrateBuildMenu)
					]
				]
				// reset errors button
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.HAlign(HAlign_Center)
				.Padding(0.f)
				[
					SNew(SButton)
					.ToolTipText(LOCTEXT("ResetErrors_ToolTip", "Reset Errors"))
					.ContentPadding(FMargin(4, 0))
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					.OnClicked(this, &STimedDataMonitorPanel::OnResetErrorsClicked)
					[
						SNew(STextBlock)
						.TextStyle(FTimedDataMonitorStyle::Get(), "TextBlock.Regular")
						.Text(LOCTEXT("ResetErrors", "Reset Errors"))
					]
				]
				// Spacer
				+ SHorizontalBox::Slot()
				[
					SNew(SSpacer)
				]
				// Settings button
				+ SHorizontalBox::Slot()
				.Padding(.0f)
				.AutoWidth()
				.HAlign(HAlign_Right)
				.VAlign(VAlign_Center)
				[
					SNew(SBox)
					.WidthOverride(ButtonBoxSize)
					.HeightOverride(ButtonBoxSize)
					[
						SNew(SCheckBox)
						.Padding(4.f)
						.ToolTipText(LOCTEXT("ShowUserSettings_Tip", "Show/Hide the general user settings"))
						.Style(FEditorStyle::Get(), "ToggleButtonCheckbox")
						.ForegroundColor(FSlateColor::UseForeground())
						.IsChecked_Lambda([]() { return ECheckBoxState::Unchecked; })
						.OnCheckStateChanged_Lambda([](ECheckBoxState CheckState) { FModuleManager::LoadModuleChecked<ISettingsModule>("Settings").ShowViewer("Editor", "Plugins", "Timed Data Monitor"); })
						[
							SNew(STextBlock)
							.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.14"))
							.Text(FEditorFontGlyphs::Cogs)
						]
					]
				]
			]
		]
		// timing element
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(10.f, 0.f, 10.f, 10.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(0.5f)
			.Padding(0.f, 0.f, 10.f, 0.f)
			[
				SNew(SBorder)
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("Placeholder", "Placehoder for GL"))
				]
			]
			+ SHorizontalBox::Slot()
			.FillWidth(0.5f)
			.Padding(0.f)
			[
				SNew(SBorder)
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				[
					SNew(STimecodeProvider)
					.DisplayFrameRate(true)
				]
			]
		]
		// sources list
		+ SVerticalBox::Slot()
		.FillHeight(1.f)
		.Padding(10.f, 0.f, 10.f, 10.f)
		[
			SNew(SScrollBox)
			.Orientation(Orient_Vertical)
			+ SScrollBox::Slot()
			[
				SAssignNew(TimedDataSourceList, STimedDataInputListView)
			]
		]
		// sources list
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(10.f, 0.f, 10.f, 10.f)
		[
			SNew(SBorder)
			.Padding(FMargin(5.f, 5.f, 5.f, 5.f))
			.Visibility(this, &STimedDataMonitorPanel::ShowMessageLog)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					MessageLogListingWidget
				]
			]
		]
		+SVerticalBox::Slot()
		.AutoHeight()
		.Padding(10.f, 0.f, 10.f, 10.f)
		[
			SNew(SBorder)
			.BorderImage(FEditorStyle::GetBrush("SettingsEditor.CheckoutWarningBorder"))
			.BorderBackgroundColor(FColor(166, 137, 0))
			.Padding(FMargin(5.f, 5.f, 5.f, 5.f))
			.Visibility(this, &STimedDataMonitorPanel::ShowEditorPerformanceThrottlingWarning)
			[
				SNew(SHorizontalBox)
				+SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(PerformanceWarningText)
					.Font(FEditorStyle::GetFontStyle("PropertyWindow.NormalFont"))
					.ShadowColorAndOpacity(FLinearColor::Black.CopyWithNewOpacity(0.3f))
					.ShadowOffset(FVector2D::UnitVector)
				]
				+SHorizontalBox::Slot()
				.Padding(FMargin(0.f, 0.f, 10.f, 0.f))
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(SButton)
					.OnClicked(this, &STimedDataMonitorPanel::DisableEditorPerformanceThrottling)
					.Text(LOCTEXT("PerformanceWarningDisable", "Disable"))
				]
			]
		]
	];
}
END_SLATE_FUNCTION_BUILD_OPTIMIZATION


void STimedDataMonitorPanel::BuildCalibrationArray()
{
	CalibrationUIAction[(int32)ETimedDataMonitorEditorCalibrationType::CalibrateWithTimecode] = FUIAction(
		FExecuteAction::CreateSP(this, &STimedDataMonitorPanel::CalibrateWithTimecode));
	CalibrationUIAction[(int32)ETimedDataMonitorEditorCalibrationType::JamWithPlatformTime] = FUIAction(
		FExecuteAction::CreateSP(this, &STimedDataMonitorPanel::Jam, false));
	CalibrationUIAction[(int32)ETimedDataMonitorEditorCalibrationType::JamWithTimecode] = FUIAction(
		FExecuteAction::CreateSP(this, &STimedDataMonitorPanel::Jam, true));
	CalibrationSlateIcon[(int32)ETimedDataMonitorEditorCalibrationType::CalibrateWithTimecode] = FSlateIcon(FTimedDataMonitorStyle::Get().GetStyleSetName(), "Img.Timecode.Small");
	CalibrationSlateIcon[(int32)ETimedDataMonitorEditorCalibrationType::JamWithPlatformTime] = FSlateIcon(FTimedDataMonitorStyle::Get().GetStyleSetName(), "Img.Timecode.Small");
	CalibrationSlateIcon[(int32)ETimedDataMonitorEditorCalibrationType::JamWithTimecode] = FSlateIcon(FTimedDataMonitorStyle::Get().GetStyleSetName(), "Img.Timecode.Small");
	CalibrationName[(int32)ETimedDataMonitorEditorCalibrationType::CalibrateWithTimecode] = LOCTEXT("CalibrateButtonLabel", "Calibrate");
	CalibrationName[(int32)ETimedDataMonitorEditorCalibrationType::JamWithPlatformTime] = LOCTEXT("JamWithTimeButtonLabel", "Jam (with time)");
	CalibrationName[(int32)ETimedDataMonitorEditorCalibrationType::JamWithTimecode] = LOCTEXT("JamWithTimecodeButtonLabel", "Jam (with timecode)");
	CalibrationTooltip[(int32)ETimedDataMonitorEditorCalibrationType::CalibrateWithTimecode] = LOCTEXT("CalibrateButtonTooltip", "Find range of frames that all inputs has. If it can be found, change the Timecode Provider delay, resize buffers or delay them to accomadate.");
	CalibrationTooltip[(int32)ETimedDataMonitorEditorCalibrationType::JamWithPlatformTime] = LOCTEXT("JamWithTimeButtonTooltip", "Set the offset of all inputs to match with the current Platform Time.");
	CalibrationTooltip[(int32)ETimedDataMonitorEditorCalibrationType::JamWithTimecode] = LOCTEXT("JamWithTimecodeButtonTooltip", "Set the offset of all inputs to match with the current Timecode.");
}


FReply STimedDataMonitorPanel::OnCalibrateClicked()
{
	ETimedDataMonitorEditorCalibrationType CalibrationType = GetDefault<UTimedDataMonitorEditorSettings>()->LastCalibrationType;
	if (CalibrationUIAction[(int32)CalibrationType].CanExecuteAction.Execute())
	{
		CalibrationUIAction[(int32)CalibrationType].ExecuteAction.Execute();
	}

	return FReply::Handled();
}


TSharedRef<SWidget> STimedDataMonitorPanel::OnCalibrateBuildMenu()
{
	FMenuBuilder MenuBuilder(true, nullptr);

	for (int32 Index = 0; Index < CalibrationArrayCount; ++Index)
	{
		MenuBuilder.AddMenuEntry(CalibrationName[Index], CalibrationTooltip[Index], CalibrationSlateIcon[Index], CalibrationUIAction[Index], NAME_None, EUserInterfaceActionType::RadioButton);
	}

	return MenuBuilder.MakeWidget();
}


FText STimedDataMonitorPanel::GetCalibrateButtonTooltip() const
{
	ETimedDataMonitorEditorCalibrationType CalibrationType = GetDefault<UTimedDataMonitorEditorSettings>()->LastCalibrationType;
	return CalibrationTooltip[(int32)CalibrationType];
}


FText STimedDataMonitorPanel::GetCalibrateButtonText() const
{
	ETimedDataMonitorEditorCalibrationType CalibrationType = GetDefault<UTimedDataMonitorEditorSettings>()->LastCalibrationType;
	return CalibrationName[(int32)CalibrationType];
}


FReply STimedDataMonitorPanel::OnResetErrorsClicked()
{
	UTimedDataMonitorSubsystem* TimedDataMonitorSubsystem = GEngine->GetEngineSubsystem<UTimedDataMonitorSubsystem>();
	check(TimedDataMonitorSubsystem);
	TimedDataMonitorSubsystem->ResetAllBufferStats();
	MessageLogListing->ClearMessages();
	return FReply::Handled();
}


EVisibility STimedDataMonitorPanel::ShowMessageLog() const
{
	return MessageLogListing && MessageLogListing->NumMessages(EMessageSeverity::Info) > 0 ? EVisibility::Visible : EVisibility::Collapsed;
}


EVisibility STimedDataMonitorPanel::ShowEditorPerformanceThrottlingWarning() const
{
	const UEditorPerformanceSettings* Settings = GetDefault<UEditorPerformanceSettings>();
	return Settings->bThrottleCPUWhenNotForeground ? EVisibility::Visible : EVisibility::Collapsed;
}


FReply STimedDataMonitorPanel::DisableEditorPerformanceThrottling()
{
	UEditorPerformanceSettings* Settings = GetMutableDefault<UEditorPerformanceSettings>();
	Settings->bThrottleCPUWhenNotForeground = false;
	Settings->PostEditChange();
	Settings->SaveConfig();
	return FReply::Handled();
}


void STimedDataMonitorPanel::CalibrateWithTimecode()
{
	UTimedDataMonitorSubsystem* TimedDataMonitorSubsystem = GEngine->GetEngineSubsystem<UTimedDataMonitorSubsystem>();
	check(TimedDataMonitorSubsystem);

	MessageLogListing->ClearMessages();

	FTimedDataMonitorCalibrationResult Result = TimedDataMonitorSubsystem->CalibrateWithTimecodeProvider();

	//if (Result.ReturnCode != ETimedDataMonitorCalibrationReturnCode::Succeeded)
	//{
	//	switch (Result.ReturnCode)
	//	{
	//	case ETimedDataMonitorCalibrationReturnCode::Failed_NoTimecode:
	//		FMessageLog(Utilities::NAME_LogName).Error(LOCTEXT("CalibrationFailed_NoTimecode", "The timecode provider doesn't have a proper timecode value."));
	//		break;
	//	case ETimedDataMonitorCalibrationReturnCode::Failed_UnresponsiveInput:
	//		for (const FTimedDataMonitorChannelIdentifier& Identifier : Result.FailureChannelIdentifiers)
	//		{
	//			FMessageLog(Utilities::NAME_LogName).Error(FText::Format(LOCTEXT("CalibrationFailed_UnresponsiveInput", "The input '{0}' is unresponsive."), TimedDataMonitorSubsystem->GetChannelDisplayName(Identifier)));
	//		}
	//		break;
	//	case ETimedDataMonitorCalibrationReturnCode::Failed_InvalidFrameRate:
	//		for (const FTimedDataMonitorChannelIdentifier& Identifier : Result.FailureChannelIdentifiers)
	//		{
	//			FMessageLog(Utilities::NAME_LogName).Error(FText::Format(LOCTEXT("CalibrationFailed_InvalidFrameRate", "The input '{0}' has an invalid frame rate."), TimedDataMonitorSubsystem->GetChannelDisplayName(Identifier)));
	//		}
	//		break;
	//	case ETimedDataMonitorCalibrationReturnCode::Failed_NoDataBuffered:
	//		for (const FTimedDataMonitorChannelIdentifier& Identifier : Result.FailureChannelIdentifiers)
	//		{
	//			FMessageLog(Utilities::NAME_LogName).Error(FText::Format(LOCTEXT("CalibrationFailed_NoDataBuffered", "The input '{0}' has not data buffured."), TimedDataMonitorSubsystem->GetChannelDisplayName(Identifier)));
	//		}
	//		break;
	//	default:
	//		break;
	//	}
	//}

	GetMutableDefault<UTimedDataMonitorEditorSettings>()->LastCalibrationType = ETimedDataMonitorEditorCalibrationType::CalibrateWithTimecode;
	GetMutableDefault<UTimedDataMonitorEditorSettings>()->SaveConfig();
}


void STimedDataMonitorPanel::Jam(bool bWithTimecode)
{
	UTimedDataMonitorSubsystem* TimedDataMonitorSubsystem = GEngine->GetEngineSubsystem<UTimedDataMonitorSubsystem>();
	check(TimedDataMonitorSubsystem);

	MessageLogListing->ClearMessages();

	ETimedDataInputEvaluationType JamType = bWithTimecode ? ETimedDataInputEvaluationType::Timecode : ETimedDataInputEvaluationType::PlatformTime;
	FTimedDataMonitorJamResult Result = TimedDataMonitorSubsystem->JamInputs(JamType);

	switch (Result.ReturnCode)
	{
	case ETimedDataMonitorJamReturnCode::Succeeded:
		MessageLogListing->AddMessage(FTokenizedMessage::Create(EMessageSeverity::Info
			, LOCTEXT("JamSucceeded_NoTimecode", "Jam succeeded.")));
		break;
	case ETimedDataMonitorJamReturnCode::Failed_NoTimecode:
		MessageLogListing->AddMessage(FTokenizedMessage::Create(EMessageSeverity::Error
			, LOCTEXT("JamFailed_NoTimecode", "The timecode provider doesn't have a proper timecode value.")));
		break;
	case ETimedDataMonitorJamReturnCode::Failed_UnresponsiveInput:
		for (const FTimedDataMonitorChannelIdentifier& Identifier : Result.FailureChannelIdentifiers)
		{
			MessageLogListing->AddMessage(FTokenizedMessage::Create(EMessageSeverity::Error
				, FText::Format(LOCTEXT("JamFailed_UnresponsiveInput", "The channel '{0}' is unresponsive."), TimedDataMonitorSubsystem->GetChannelDisplayName(Identifier))));
		}
		break;
	case ETimedDataMonitorJamReturnCode::Failed_EvaluationTypeDoNotMatch:
		for (const FTimedDataMonitorInputIdentifier& Identifier : Result.FailureInputIdentifiers)
		{
			MessageLogListing->AddMessage(FTokenizedMessage::Create(EMessageSeverity::Error
				, FText::Format(LOCTEXT("JamFailed_InvalidFrameRate", "The input '{0}' evalution type doesn't match with the jam type."), TimedDataMonitorSubsystem->GetInputDisplayName(Identifier))));
		}
		break;
	case ETimedDataMonitorJamReturnCode::Failed_NoDataBuffered:
		for (const FTimedDataMonitorChannelIdentifier& Identifier : Result.FailureChannelIdentifiers)
		{
			MessageLogListing->AddMessage(FTokenizedMessage::Create(EMessageSeverity::Error
				, FText::Format(LOCTEXT("JamFailed_NoDataBuffered", "The channel '{0}' has not data buffered."), TimedDataMonitorSubsystem->GetChannelDisplayName(Identifier))));
		}
		break;
	case ETimedDataMonitorJamReturnCode::Failed_BufferSizeHaveBeenMaxed:
		for (const FTimedDataMonitorInputIdentifier& Identifier : Result.FailureInputIdentifiers)
		{
			MessageLogListing->AddMessage(FTokenizedMessage::Create(EMessageSeverity::Error
				, FText::Format(LOCTEXT("Failed_BufferSizeHaveBeenMaxed_Input", "The buffer size of input '{0}' could not be increased further."), TimedDataMonitorSubsystem->GetInputDisplayName(Identifier))));
		}
		for (const FTimedDataMonitorChannelIdentifier& Identifier : Result.FailureChannelIdentifiers)
		{
			MessageLogListing->AddMessage(FTokenizedMessage::Create(EMessageSeverity::Error
				, FText::Format(LOCTEXT("Failed_BufferSizeHaveBeenMaxed_Channel", "The buffer size of channel '{0}' needed to be increased further."), TimedDataMonitorSubsystem->GetChannelDisplayName(Identifier))));
		}
		break;
	case ETimedDataMonitorJamReturnCode::Retry_BufferSizeHasBeenIncreased:
		for (const FTimedDataMonitorInputIdentifier& Identifier : Result.FailureInputIdentifiers)
		{
			MessageLogListing->AddMessage(FTokenizedMessage::Create(EMessageSeverity::Warning
				, FText::Format(LOCTEXT("JamRetry_BufferSizeHasBeenIncrease_Input", "The buffer size of input '{0}' needed to be increased. Retry."), TimedDataMonitorSubsystem->GetInputDisplayName(Identifier))));
		}
		for (const FTimedDataMonitorChannelIdentifier& Identifier : Result.FailureChannelIdentifiers)
		{
			MessageLogListing->AddMessage(FTokenizedMessage::Create(EMessageSeverity::Warning
				, FText::Format(LOCTEXT("JamRetry_BufferSizeHasBeenIncrease_Channel", "The buffer size of channel '{0}' needed to be increased. Retry."), TimedDataMonitorSubsystem->GetChannelDisplayName(Identifier))));
		}
		break;
	default:
		break;
	}

	GetMutableDefault<UTimedDataMonitorEditorSettings>()->LastCalibrationType = bWithTimecode
		? ETimedDataMonitorEditorCalibrationType::JamWithTimecode
		: ETimedDataMonitorEditorCalibrationType::JamWithPlatformTime;
	GetMutableDefault<UTimedDataMonitorEditorSettings>()->SaveConfig();
}

#undef LOCTEXT_NAMESPACE
