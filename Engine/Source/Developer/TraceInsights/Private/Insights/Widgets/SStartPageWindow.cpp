// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SStartPageWindow.h"

#include "DesktopPlatformModule.h"
#include "EditorStyleSet.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/WorkspaceItem.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "HAL/FileManagerGeneric.h"
#include "SlateOptMacros.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SComboButton.h"
//#include "WorkspaceMenuStructure.h"
//#include "WorkspaceMenuStructureModule.h"

#if WITH_EDITOR
	#include "EngineAnalytics.h"
	#include "Runtime/Analytics/Analytics/Public/AnalyticsEventAttribute.h"
	#include "Runtime/Analytics/Analytics/Public/Interfaces/IAnalyticsProvider.h"
#endif // WITH_EDITOR

// Insights
#include "Insights/InsightsManager.h"
#include "Insights/TimingProfilerManager.h"
#include "Insights/InsightsStyle.h"
#include "Insights/Version.h"
#include "Insights/Widgets/SInsightsSettings.h"
#include "Insights/Widgets/STimingProfilerWindow.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

#define LOCTEXT_NAMESPACE "SStartPageWindow"

////////////////////////////////////////////////////////////////////////////////////////////////////

static FText GetTextForNotification(const EInsightsNotificationType NotificatonType, const ELoadingProgressState ProgressState, const FString& Filename, const float ProgressPercent = 0.0f)
{
	FText Result;

	if (NotificatonType == EInsightsNotificationType::LoadingTraceFile)
	{
		if (ProgressState == ELoadingProgressState::Started)
		{
			FFormatNamedArguments Args;
			Args.Add(TEXT("Filename"), FText::FromString(Filename));
			Result = FText::Format(LOCTEXT("DescF_OfflineCapture_Started", "Started loading a file {Filename}"), Args);
		}
		else if (ProgressState == ELoadingProgressState::InProgress)
		{
			FFormatNamedArguments Args;
			Args.Add(TEXT("Filename"), FText::FromString(Filename));
			Args.Add(TEXT("LoadingProgressPercent"), FText::AsPercent(ProgressPercent));
			Result = FText::Format(LOCTEXT("DescF_OfflineCapture_InProgress", "Loading a file {Filename} {LoadingProgressPercent}"), Args);
		}
		else if (ProgressState == ELoadingProgressState::Loaded)
		{
			FFormatNamedArguments Args;
			Args.Add(TEXT("Filename"), FText::FromString(Filename));
			Result = FText::Format(LOCTEXT("DescF_OfflineCapture_Loaded", "File {Filename} has been successfully loaded"), Args);
		}
		else if (ProgressState == ELoadingProgressState::Failed)
		{
			FFormatNamedArguments Args;
			Args.Add(TEXT("Filename"), FText::FromString(Filename));
			Result = FText::Format(LOCTEXT("DescF_OfflineCapture_Failed", "Failed to load file {Filename}"), Args);
		}
	}

	return Result;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// SConnectionRow
////////////////////////////////////////////////////////////////////////////////////////////////////

class SConnectionRow : public SMultiColumnTableRow<TSharedPtr<FRecorderConnection>>
{
	SLATE_BEGIN_ARGS(SConnectionRow) {}
	SLATE_END_ARGS()

public:
	/**
	 * Constructs the widget.
	 *
	 * @param InArgs The construction arguments.
	 * @param InConnection The connection displayed by this row.
	 * @param InOwnerTableView The table to which the row must be added.
	 */
	void Construct(const FArguments& InArgs, TSharedPtr<FRecorderConnection> InConnection, TSharedRef<SStartPageWindow> InParentWidget, const TSharedRef<STableViewBase>& InOwnerTableView)
	{
		WeakConnection = MoveTemp(InConnection);
		WeakParentWidget = InParentWidget;

		SMultiColumnTableRow<TSharedPtr<FRecorderConnection>>::Construct(FSuperRowType::FArguments(), InOwnerTableView);
	}

public:
	virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& ColumnName) override
	{
		if (ColumnName == FName(TEXT("Name")))
		{
			return SNew(SBox)
				.Padding(FMargin(4.0, 0.0))
				[
					SNew(STextBlock)
					.Text(this, &SConnectionRow::GetConnectionName)
					.ToolTipText(this, &SConnectionRow::GetConnectionName)
				];
		}
		else if (ColumnName == FName(TEXT("Uri")))
		{
			return SNew(SBox)
				.Padding(FMargin(4.0, 0.0))
				[
					SNew(STextBlock)
					.Text(this, &SConnectionRow::GetConnectionUri)
					.ToolTipText(this, &SConnectionRow::GetConnectionUri)
				];
		}
		else
		{
			return SNew(STextBlock).Text(LOCTEXT("UnknownColumn", "Unknown Column"));
		}
	}

	FText GetConnectionName() const
	{
		TSharedPtr<FRecorderConnection> ConnectionPin = WeakConnection.Pin();
		if (ConnectionPin.IsValid())
		{
			return ConnectionPin->Name;
		}
		else
		{
			return FText();
		}
	}

	FText GetConnectionUri() const
	{
		TSharedPtr<FRecorderConnection> ConnectionPin = WeakConnection.Pin();
		if (ConnectionPin.IsValid())
		{
			return ConnectionPin->Uri;
		}
		else
		{
			return FText();
		}
	}

	FText GetRowToolTip() const
	{
		TSharedPtr<FRecorderConnection> ConnectionPin = WeakConnection.Pin();
		if (ConnectionPin.IsValid())
		{
			return ConnectionPin->Uri;
		}
		else
		{
			return FText();
		}
	}

private:
	TWeakPtr<FRecorderConnection> WeakConnection;
	TWeakPtr<SStartPageWindow> WeakParentWidget;
};

////////////////////////////////////////////////////////////////////////////////////////////////////
// SStartPageWindow
////////////////////////////////////////////////////////////////////////////////////////////////////

SStartPageWindow::SStartPageWindow()
	: DurationActive(0.0f)
	, bIsAnyLiveSessionAvailable(false)
	, LastLiveSessionHandle(static_cast<Trace::FSessionHandle>(0))
	, bIsAnySessionAvailable(false)
	, LastSessionHandle(static_cast<Trace::FSessionHandle>(0))
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

SStartPageWindow::~SStartPageWindow()
{
#if WITH_EDITOR
	if (DurationActive > 0.0f && FEngineAnalytics::IsAvailable())
	{
		FEngineAnalytics::GetProvider().RecordEvent(TEXT("Editor.Usage.Profiler"), FAnalyticsEventAttribute(TEXT("Duration"), DurationActive));
	}
#endif // WITH_EDITOR
}

////////////////////////////////////////////////////////////////////////////////////////////////////

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION

void SStartPageWindow::Construct(const FArguments& InArgs)
{
	ChildSlot
		[
			SNew(SOverlay)

			// Version
			+ SOverlay::Slot()
				.HAlign(HAlign_Right)
				.VAlign(VAlign_Top)
				.Padding(0.0f, -16.0f, 0.0f, 0.0f)
				[
					SNew(STextBlock)
						.Clipping(EWidgetClipping::ClipToBoundsWithoutIntersecting)
						.Text(LOCTEXT("UnrealInsightsVersion", UNREAL_INSIGHTS_VERSION_STRING_EX))
						.ColorAndOpacity(FLinearColor(0.15f, 0.15f, 0.15f, 1.0f))
				]

			// Overlay slot for the main window area
			+ SOverlay::Slot()
				.HAlign(HAlign_Fill)
				.VAlign(VAlign_Center)
				[
					SNew(SVerticalBox)

					+ SVerticalBox::Slot()
						.AutoHeight()
						.HAlign(HAlign_Center)
						.Padding(3.0f, 3.0f)
						[
							SNew(SBorder)
							.Visibility(this, &SStartPageWindow::IsSessionOverlayVisible)
							.BorderImage(FEditorStyle::GetBrush("NotificationList.ItemBackground"))
							.Padding(8.0f)
							[
								SNew(STextBlock)
								.Text(LOCTEXT("SelectTraceOverlayText", "Please select a trace..."))
							]
						]

					+ SVerticalBox::Slot()
						.AutoHeight()
						.HAlign(HAlign_Center)
						.Padding(3.0f, 3.0f)
						[
							SNew(SBorder)
							.BorderImage(FEditorStyle::GetBrush("NotificationList.ItemBackground"))
							.Padding(8.0f)
							[
								SNew(STextBlock)
								.Text(LOCTEXT("SelectTraceHint", "\
- Use \"Live\" button to start analysis for the latest available session that is live, if any.\n\
- Use \"Latest\" button to start analysis for the latest available session, if any.\n\
- Use \"Load\" button to load a trace file (*.utrace).\n\
- Use the combo box to choose from available sessions.\n\
- Drag and drop a *.utrace file over this window."))
								//.Justification(ETextJustify::Center)
							]
						]

					+ SVerticalBox::Slot()
						.AutoHeight()
						.HAlign(HAlign_Center)
						.Padding(3.0f, 3.0f)
						[
							SNew(SHorizontalBox)

							+ SHorizontalBox::Slot()
								.AutoWidth()
								.Padding(3.0f, 0.0f)
								[
									SNew(SButton)
									.OnClicked(this, &SStartPageWindow::Live_OnClicked)
									.IsEnabled(this, &SStartPageWindow::Live_IsEnabled)
									.ToolTipText(LOCTEXT("LiveButtonTooltip", "Start analysis for the latest available session that is live, if any."))
									.ContentPadding(8.0f)
									.Content()
									[
										SNew(SHorizontalBox)

										+SHorizontalBox::Slot()
											.AutoWidth()
											[
												SNew(SImage)
												.Image(FInsightsStyle::GetBrush("Live.Icon.Large"))
											]

										+SHorizontalBox::Slot()
											.AutoWidth()
											.VAlign(VAlign_Center)
											[
												SNew(SBox)
												.WidthOverride(100.0f)
												[
													SNew(STextBlock)
													.Font(FCoreStyle::GetDefaultFontStyle("Regular", 18))
													.Text(LOCTEXT("LiveButtonText", "Live"))
												]
											]
									]
								]

							+ SHorizontalBox::Slot()
								.AutoWidth()
								.Padding(3.0f, 0.0f)
								[
									SNew(SButton)
									.OnClicked(this, &SStartPageWindow::Last_OnClicked)
									.IsEnabled(this, &SStartPageWindow::Last_IsEnabled)
									.ToolTipText(LOCTEXT("LastButtonTooltip", "Start analysis for the latest available session, if any."))
									.ContentPadding(8.0f)
									.Content()
									[
										SNew(SHorizontalBox)

										+SHorizontalBox::Slot()
											.AutoWidth()
											[
												SNew(SImage)
												.Image(FInsightsStyle::GetBrush("Live.Icon.Large"))
											]

										+SHorizontalBox::Slot()
											.AutoWidth()
											.VAlign(VAlign_Center)
											[
												SNew(SBox)
												.WidthOverride(100.0f)
												[
													SNew(STextBlock)
													.Font(FCoreStyle::GetDefaultFontStyle("Regular", 18))
													.Text(LOCTEXT("LastButtonText", "Latest"))
												]
											]
									]
								]

							+ SHorizontalBox::Slot()
								.AutoWidth()
								.Padding(3.0f, 0.0f, 0.0f, 0.0f)
								[
									SNew(SButton)
									.OnClicked(this, &SStartPageWindow::Load_OnClicked)
									.ToolTipText(LOCTEXT("LoadButtonTooltip", "Load a trace file."))
									.ContentPadding(8.0f)
									.Content()
									[
										SNew(SHorizontalBox)

										+ SHorizontalBox::Slot()
											.AutoWidth()
											[
												SNew(SImage)
												.Image(FInsightsStyle::GetBrush("Load.Icon.Large"))
											]

										+ SHorizontalBox::Slot()
											.AutoWidth()
											.VAlign(VAlign_Center)
											[
												SNew(SBox)
												.WidthOverride(100.0f)
												[
													SNew(STextBlock)
													.Font(FCoreStyle::GetDefaultFontStyle("Regular", 18))
													.Text(LOCTEXT("LoadButtonText", "Load..."))
												]
											]
									]
								]

							+ SHorizontalBox::Slot()
								.AutoWidth()
								.Padding(0.0f, 0.0f, 0.0f, 0.0f)
								[
									SNew(SComboButton)
									.ToolTipText(LOCTEXT("SessionList_Tooltip", "Choose from Most Recently Used Sessions or from Available Sessions"))
									.OnGetMenuContent(this, &SStartPageWindow::MakeSessionListMenu)
									.HasDownArrow(true)
									.ContentPadding(FMargin(1.0f, 1.0f, 1.0f, 1.0f))
								]
						]

					+ SVerticalBox::Slot()
						.AutoHeight()
						.HAlign(HAlign_Center)
						.Padding(3.0f, 3.0f)
						[
							SNew(SBorder)
							.BorderImage(FEditorStyle::GetBrush("NotificationList.ItemBackground"))
							.Padding(8.0f)
							[
								SNew(SVerticalBox)

								+ SVerticalBox::Slot()
									.AutoHeight()
									.HAlign(HAlign_Center)
									.Padding(0.0, 2.0f)
									[
										SNew(STextBlock)
										.Text(LOCTEXT("LocalSessionDirectoryText", "Local Session Directory:"))
										.ColorAndOpacity(FLinearColor::Gray)
									]

								+ SVerticalBox::Slot()
									.AutoHeight()
									.HAlign(HAlign_Center)
									.Padding(0.0, 2.0f)
									[
										SNew(SHorizontalBox)

										+ SHorizontalBox::Slot()
											.AutoWidth()
											.Padding(2.0f, 0.0f)
											.VAlign(VAlign_Center)
											[
												SNew(STextBlock)
												.Text(this, &SStartPageWindow::GetLocalSessionDirectory)
											]

										+ SHorizontalBox::Slot()
											.AutoWidth()
											.Padding(2.0f, 0.0f)
											.VAlign(VAlign_Center)
											[
												SNew(SButton)
												.Text(LOCTEXT("ExploreLocalSessionDirButton", "..."))
												.ToolTipText(LOCTEXT("ExploreLocalSessionDirButtonToolTip", "Explore the Local Session Directory"))
												.OnClicked(this, &SStartPageWindow::ExploreLocalSessionDirectory_OnClicked)
											]
									]
							]
						]

					+ SVerticalBox::Slot()
						.AutoHeight()
						.HAlign(HAlign_Center)
						.Padding(3.0f, 3.0f)
						[
							SNew(SBorder)
							.BorderImage(FEditorStyle::GetBrush("NotificationList.ItemBackground"))
							.Padding(8.0f)
							[
								ConstructRecoderPanel()
							]
						]
				]

			// Notification area overlay
			+ SOverlay::Slot()
				.HAlign(HAlign_Right)
				.VAlign(VAlign_Bottom)
				.Padding(16.0f)
				[
					SAssignNew(NotificationList, SNotificationList)
				]

			// Settings dialog overlay
			+ SOverlay::Slot()
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				.Expose(OverlaySettingsSlot)
		];
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<SWidget> SStartPageWindow::ConstructRecoderPanel()
{
	TSharedRef<SWidget> Widget = SNew(SVerticalBox)

	+ SVerticalBox::Slot()
		.AutoHeight()
		.HAlign(HAlign_Center)
		.Padding(0.0, 2.0f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("RecorderPanelTitle", "Trace Recorder"))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
			.ColorAndOpacity(FLinearColor::Gray)
		]

	+ SVerticalBox::Slot()
		.AutoHeight()
		.HAlign(HAlign_Left)
		.Padding(0.0, 2.0f)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0.0f, 0.0f, 2.0f, 0.0f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("RecorderStatusTitle", "Status:"))
					.ColorAndOpacity(FLinearColor::Gray)
				]

			+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(2.0f, 0.0f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(this, &SStartPageWindow::GetRecorderStatusText)
				]

			+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(2.0f, 0.0f)
				.VAlign(VAlign_Center)
				[
					SNew(SButton)
					.Text(LOCTEXT("StartRecorder", "Start"))
					.ToolTipText(LOCTEXT("StartRecorderToolTip", "Start the Trace Recorder"))
					.OnClicked(this, &SStartPageWindow::StartTraceRecorder_OnClicked)
					.Visibility(this, &SStartPageWindow::StartTraceRecorder_Visibility)
				]

			+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(2.0f, 0.0f)
				.VAlign(VAlign_Center)
				[
					SNew(SButton)
					.Text(LOCTEXT("StopRecorder", "Stop"))
					.ToolTipText(LOCTEXT("StopRecorderToolTip", "Stop the Trace Recorder"))
					.OnClicked(this, &SStartPageWindow::StopTraceRecorder_OnClicked)
					.Visibility(this, &SStartPageWindow::StopTraceRecorder_Visibility)
				]
		]

	+ SVerticalBox::Slot()
		.AutoHeight()
		.HAlign(HAlign_Fill)
		.Padding(0.0, 2.0f)
		[
			SNew(SHorizontalBox)
			.Visibility(this, &SStartPageWindow::StopTraceRecorder_Visibility)

			+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0.0f, 0.0f, 2.0f, 0.0f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("HostTitle", "Host:"))
					.ColorAndOpacity(FLinearColor::Gray)
				]

			+ SHorizontalBox::Slot()
				.FillWidth(1.0)
				.VAlign(VAlign_Center)
				[
					SAssignNew(HostTextBox, SEditableTextBox)
				]

			+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(2.0f, 0.0f)
				.VAlign(VAlign_Center)
				[
					SNew(SButton)
					.Text(LOCTEXT("Connect", "Connect"))
					.ToolTipText(LOCTEXT("ConnectToolTip", "Try connecting to host."))
					.OnClicked(this, &SStartPageWindow::Connect_OnClicked)
				]
		]

	+ SVerticalBox::Slot()
		.AutoHeight()
		.HAlign(HAlign_Left)
		.Padding(0.0, 2.0f, 0.0f, 1.0f)
		[
			SNew(SHorizontalBox)
			.Visibility(this, &SStartPageWindow::StopTraceRecorder_Visibility)

			+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0.0f, 0.0f, 2.0f, 0.0f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text_Lambda([this]() -> FText
					{
						return FText::Format(LOCTEXT("ConnectionListTitle", "Connections (live sessions): {0}"), FText::AsNumber(Connections.Num()));
					})
					.ColorAndOpacity(FLinearColor::Gray)
				]

			+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(2.0f, 0.0f)
				.VAlign(VAlign_Center)
				[
					SNew(SButton)
					.Text(LOCTEXT("Refresh", "Refresh"))
					.ToolTipText(LOCTEXT("RefreshToolTip", "Refresh the connection list (live sessions)."))
					.OnClicked(this, &SStartPageWindow::RefreshConnections_OnClicked)
				]
		]

	+ SVerticalBox::Slot()
		.AutoHeight()
		.HAlign(HAlign_Left)
		.Padding(0.0, 1.0f, 0.0f, 2.0f)
		[
			SNew(SBox)
			.WidthOverride(640.0f)
			.MaxDesiredHeight(78.0f)
			.Visibility(this, &SStartPageWindow::Connections_Visibility)
			[
				SAssignNew(ConnectionsListView, SListView<TSharedPtr<FRecorderConnection>>)
				.ItemHeight(20.0f)
				.SelectionMode(ESelectionMode::Single)
				.OnSelectionChanged(this, &SStartPageWindow::Connections_OnSelectionChanged)
				.ListItemsSource(&Connections)
				.OnGenerateRow(this, &SStartPageWindow::Connections_OnGenerateRow)
				.ConsumeMouseWheel(EConsumeMouseWheel::Always)
				//.OnContextMenuOpening(FOnContextMenuOpening::CreateSP(this, &SStartPageWindow::Connections_GetContextMenu))
				.HeaderRow
				(
					SNew(SHeaderRow)

					+ SHeaderRow::Column(FName(TEXT("Name")))
					.FillWidth(0.2f)
					.DefaultLabel(LOCTEXT("NameColumn", "Name"))

					+ SHeaderRow::Column(FName(TEXT("Uri")))
					.FillWidth(0.8f)
					.DefaultLabel(LOCTEXT("UriColumn", "URI"))
				)
			]
		]

	+ SVerticalBox::Slot()
		.AutoHeight()
		.HAlign(HAlign_Left)
		.Padding(0.0, 2.0f, 0.0f, 1.0f)
		[
			SNew(STextBlock)
			.Visibility(this, &SStartPageWindow::Modules_Visibility)
			.Text(LOCTEXT("ModulesListTitle", "Modules for selected connection:"))
			.ColorAndOpacity(FLinearColor::Gray)
		]

	+ SVerticalBox::Slot()
		.AutoHeight()
		.HAlign(HAlign_Left)
		.Padding(0.0, 1.0f, 0.0f, 2.0f)
		[
			ConstructModuleList()
		]
	;

	RefreshConnectionList();

	return Widget;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<SWidget> SStartPageWindow::ConstructModuleList()
{
	TSharedRef<SVerticalBox> VerticalBox = SNew(SVerticalBox)
		.Visibility(this, &SStartPageWindow::Modules_Visibility);

	TSharedRef<Trace::IModuleService> ModuleService = FInsightsManager::Get()->GetModuleService();
	ModuleService->GetAvailableModules(AvailableModules);

	constexpr bool bDefaultModuleEnableState = false;
	AvailableModulesEnabledState.Reset();

	for (int32 ModuleIndex = 0; ModuleIndex < AvailableModules.Num(); ++ModuleIndex)
	{
		const Trace::FModuleInfo& Module = AvailableModules[ModuleIndex];

		AvailableModulesEnabledState.Add(bDefaultModuleEnableState);
		ModuleService->SetModuleEnabled(Module.Name, bDefaultModuleEnableState);

		VerticalBox->AddSlot()
			.AutoHeight()
			.Padding(0.0f, 1.0f)
			[
				SNew(SCheckBox)
				.IsChecked(this, &SStartPageWindow::Module_IsChecked, ModuleIndex)
				.OnCheckStateChanged(this, &SStartPageWindow::Module_OnCheckStateChanged, ModuleIndex)
				[
					SNew(STextBlock)
					.Text(FText::FromString(Module.DisplayName))
				]
			];
	}

	return VerticalBox;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<ITableRow> SStartPageWindow::Connections_OnGenerateRow(TSharedPtr<FRecorderConnection> InConnection, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(SConnectionRow, InConnection, SharedThis(this), OwnerTable);
}

END_SLATE_FUNCTION_BUILD_OPTIMIZATION

////////////////////////////////////////////////////////////////////////////////////////////////////

EVisibility SStartPageWindow::Modules_Visibility() const
{
	TSharedRef<Trace::ISessionService> SessionService = FInsightsManager::Get()->GetSessionService();
	return (SessionService->IsRecorderServerRunning() && Connections.Num() > 0 && SelectedConnection.IsValid()) ? EVisibility::Visible : EVisibility::Collapsed;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

ECheckBoxState SStartPageWindow::Module_IsChecked(int32 ModuleIndex) const
{
	if (ModuleIndex >= 0 && ModuleIndex < AvailableModulesEnabledState.Num())
	{
		const bool bIsEnabled = AvailableModulesEnabledState[ModuleIndex];
		return bIsEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
	}
	else
	{
		return ECheckBoxState::Undetermined;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SStartPageWindow::Module_OnCheckStateChanged(ECheckBoxState NewRadioState, int32 ModuleIndex)
{
	ensure(AvailableModulesEnabledState.Num() == AvailableModules.Num());

	if (SelectedConnection.IsValid() &&
		ModuleIndex >= 0 && ModuleIndex < AvailableModules.Num())
	{
		bool bIsEnabled = NewRadioState == ECheckBoxState::Checked;

		TSharedRef<Trace::ISessionService> SessionService = FInsightsManager::Get()->GetSessionService();
		
		SessionService->SetModuleEnabled(SelectedConnection->SessionHandle, AvailableModules[ModuleIndex].Name, bIsEnabled);
		//AvailableModulesEnabledState[ModuleIndex] = bIsEnabled;
		AvailableModulesEnabledState[ModuleIndex] = SessionService->IsModuleEnabled(SelectedConnection->SessionHandle, AvailableModules[ModuleIndex].Name);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply SStartPageWindow::RefreshConnections_OnClicked()
{
	RefreshConnectionList();
	return FReply::Handled();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply SStartPageWindow::Connect_OnClicked()
{
	TSharedRef<Trace::ISessionService> SessionService = FInsightsManager::Get()->GetSessionService();

	FText HostText = HostTextBox->GetText();
	if (HostText.IsEmptyOrWhitespace())
	{
		//...
	}
	else if (SessionService->ConnectSession(*HostText.ToString()))
	{
		FNotificationInfo NotificationInfo(FText::Format(LOCTEXT("ConnectSuccess", "Successfully connected to \"{0}\"!"), HostText));
		NotificationInfo.bFireAndForget = false;
		NotificationInfo.bUseLargeFont = false;
		NotificationInfo.bUseSuccessFailIcons = true;
		NotificationInfo.ExpireDuration = 10.0f;
		SNotificationItemWeak NotificationItem = NotificationList->AddNotification(NotificationInfo);
		NotificationItem.Pin()->SetCompletionState(SNotificationItem::CS_Success);
		NotificationItem.Pin()->ExpireAndFadeout();
		ActiveNotifications.Add(TEXT("ConnectSuccess"), NotificationItem);
	}
	else
	{
		FNotificationInfo NotificationInfo(FText::Format(LOCTEXT("ConnectFailed", "Failed to connect to \"{0}\"!"), HostText));
		NotificationInfo.bFireAndForget = false;
		NotificationInfo.bUseLargeFont = false;
		NotificationInfo.bUseSuccessFailIcons = true;
		NotificationInfo.ExpireDuration = 10.0f;
		SNotificationItemWeak NotificationItem = NotificationList->AddNotification(NotificationInfo);
		NotificationItem.Pin()->SetCompletionState(SNotificationItem::CS_Fail);
		NotificationItem.Pin()->ExpireAndFadeout();
		ActiveNotifications.Add(TEXT("ConnectFailed"), NotificationItem);
	}

	RefreshConnectionList();
	return FReply::Handled();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SStartPageWindow::RefreshConnectionList()
{
	TSharedRef<Trace::ISessionService> SessionService = FInsightsManager::Get()->GetSessionService();

	TArray<Trace::FSessionHandle> LiveSessions;
	SessionService->GetLiveSessions(LiveSessions);

	TSharedPtr<FRecorderConnection> NewSelectedConnection;

	Connections.Reset();

	for (Trace::FSessionHandle SessionHandle : LiveSessions)
	{
		Trace::FSessionInfo SessionInfo;
		SessionService->GetSessionInfo(SessionHandle, SessionInfo);

		Connections.Add(MakeShareable(new FRecorderConnection(SessionHandle, SessionInfo)));

		if (SelectedConnection && SelectedConnection->SessionHandle == SessionHandle)
		{
			NewSelectedConnection = Connections.Last();
		}
	}

	ConnectionsListView->RebuildList();

	if (NewSelectedConnection.IsValid())
	{
		ConnectionsListView->SetItemSelection(NewSelectedConnection, true);
		ConnectionsListView->RequestScrollIntoView(NewSelectedConnection);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SStartPageWindow::Connections_OnSelectionChanged(TSharedPtr<FRecorderConnection> Connection, ESelectInfo::Type SelectInfo)
{
	SelectedConnection = Connection;

	if (Connection.IsValid())
	{
		TSharedRef<Trace::ISessionService> SessionService = FInsightsManager::Get()->GetSessionService();
		for (int32 Index = 0; Index < AvailableModulesEnabledState.Num(); ++Index)
		{
			AvailableModulesEnabledState[Index] = SessionService->IsModuleEnabled(SelectedConnection->SessionHandle, AvailableModules[Index].Name);
		}
	}
	else
	{
		for (int32 Index = 0; Index < AvailableModulesEnabledState.Num(); ++Index)
		{
			AvailableModulesEnabledState[Index] = false;
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

EVisibility SStartPageWindow::Connections_Visibility() const
{
	TSharedRef<Trace::ISessionService> SessionService = FInsightsManager::Get()->GetSessionService();
	return (SessionService->IsRecorderServerRunning() && Connections.Num() > 0) ? EVisibility::Visible : EVisibility::Collapsed;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SStartPageWindow::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	// We need to check if there is any available session, but not too often.
	static uint64 NextTimestamp = 0;
	uint64 Time = FPlatformTime::Cycles64();
	if (Time > NextTimestamp)
	{
		const uint64 WaitTime = static_cast<uint64>(0.5 / FPlatformTime::GetSecondsPerCycle64()); // 500ms
		NextTimestamp = Time + WaitTime;

		bIsAnySessionAvailable = FInsightsManager::Get()->IsAnySessionAvailable(LastSessionHandle);
		//bIsAnyLiveSessionAvailable = FInsightsManager::Get()->IsAnyLiveSessionAvailable(LastLiveSessionHandle);

		TSharedRef<Trace::ISessionService> SessionService = FInsightsManager::Get()->GetSessionService();
		TArray<Trace::FSessionHandle> LiveSessions;
		SessionService->GetLiveSessions(LiveSessions);

		if (LiveSessions.Num() > 0)
		{
			LastLiveSessionHandle = LiveSessions.Last();
			bIsAnyLiveSessionAvailable = true;
		}
		else
		{
			bIsAnyLiveSessionAvailable = false;
		}

		if (LiveSessions.Num() != Connections.Num())
		{
			RefreshConnectionList();
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

EVisibility SStartPageWindow::IsSessionOverlayVisible() const
{
	if (FInsightsManager::Get()->GetSession().IsValid())
	{
		return EVisibility::Hidden;
	}

	return EVisibility::Visible;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool SStartPageWindow::IsSessionValid() const
{
	const bool bIsActive = FInsightsManager::Get()->GetSession().IsValid();
	return bIsActive;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SStartPageWindow::ManageLoadingProgressNotificationState(const FString& Filename, const EInsightsNotificationType NotificatonType, const ELoadingProgressState ProgressState, const float LoadingProgress)
{
	const FString BaseFilename = FPaths::GetBaseFilename(Filename);

	if (ProgressState == ELoadingProgressState::Started)
	{
		const bool bContains = ActiveNotifications.Contains(Filename);
		if (!bContains)
		{
			FNotificationInfo NotificationInfo(GetTextForNotification(NotificatonType, ProgressState, BaseFilename));
			NotificationInfo.bFireAndForget = false;
			NotificationInfo.bUseLargeFont = false;

			// Add two buttons, one for cancel, one for loading the received file.
			if (NotificatonType == EInsightsNotificationType::LoadingTraceFile)
			{
				const FText CancelButtonText = LOCTEXT("CancelButton_Text", "Cancel");
				const FText CancelButtonTT = LOCTEXT("CancelButton_TTText", "Hides this notification");
				const FText LoadButtonText = LOCTEXT("LoadButton_Text", "Load file");
				const FText LoadButtonTT = LOCTEXT("LoadButton_TTText", "Loads the received file and hides this notification");

				NotificationInfo.ButtonDetails.Add(FNotificationButtonInfo(CancelButtonText, CancelButtonTT,
					FSimpleDelegate::CreateSP(this, &SStartPageWindow::SendingServiceSideCapture_Cancel, Filename), SNotificationItem::CS_Success));
				NotificationInfo.ButtonDetails.Add(FNotificationButtonInfo(LoadButtonText, LoadButtonTT,
					FSimpleDelegate::CreateSP(this, &SStartPageWindow::SendingServiceSideCapture_Load, Filename), SNotificationItem::CS_Success));
			}

			SNotificationItemWeak NotificationItem = NotificationList->AddNotification(NotificationInfo);
			NotificationItem.Pin()->SetCompletionState(SNotificationItem::CS_Pending);
			ActiveNotifications.Add(Filename, NotificationItem);
		}
	}
	else if (ProgressState == ELoadingProgressState::InProgress)
	{
		const SNotificationItemWeak* LoadingProgressPtr = ActiveNotifications.Find(Filename);
		if (LoadingProgressPtr)
		{
			TSharedPtr<SNotificationItem> LoadingProcessPinned = LoadingProgressPtr->Pin();
			LoadingProcessPinned->SetText(GetTextForNotification(NotificatonType, ProgressState, BaseFilename, LoadingProgress));
			LoadingProcessPinned->SetCompletionState(SNotificationItem::CS_Pending);
		}
	}
	else if (ProgressState == ELoadingProgressState::Loaded)
	{
		const SNotificationItemWeak* LoadingProgressPtr = ActiveNotifications.Find(Filename);
		if (LoadingProgressPtr)
		{
			TSharedPtr<SNotificationItem> LoadingProcessPinned = LoadingProgressPtr->Pin();
			LoadingProcessPinned->SetText(GetTextForNotification(NotificatonType, ProgressState, BaseFilename));
			LoadingProcessPinned->SetCompletionState(SNotificationItem::CS_Success);

			// Notifications for received files are handled by the user.
			if (NotificatonType == EInsightsNotificationType::LoadingTraceFile)
			{
				LoadingProcessPinned->ExpireAndFadeout();
				ActiveNotifications.Remove(Filename);
			}
		}
	}
	else if (ProgressState == ELoadingProgressState::Failed)
	{
		const SNotificationItemWeak* LoadingProgressPtr = ActiveNotifications.Find(Filename);
		if (LoadingProgressPtr)
		{
			TSharedPtr<SNotificationItem> LoadingProcessPinned = LoadingProgressPtr->Pin();
			LoadingProcessPinned->SetText(GetTextForNotification(NotificatonType, ProgressState, BaseFilename));
			LoadingProcessPinned->SetCompletionState(SNotificationItem::CS_Fail);

			LoadingProcessPinned->ExpireAndFadeout();
			ActiveNotifications.Remove(Filename);
		}
	}
	else if (ProgressState == ELoadingProgressState::Cancelled)
	{
		const SNotificationItemWeak* LoadingProgressPtr = ActiveNotifications.Find(Filename);
		if (LoadingProgressPtr)
		{
			TSharedPtr<SNotificationItem> LoadingProcessPinned = LoadingProgressPtr->Pin();
			LoadingProcessPinned->ExpireAndFadeout();
			ActiveNotifications.Remove(Filename);
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SStartPageWindow::SendingServiceSideCapture_Cancel(const FString Filename)
{
	const SNotificationItemWeak* LoadingProgressPtr = ActiveNotifications.Find(Filename);
	if (LoadingProgressPtr)
	{
		TSharedPtr<SNotificationItem> LoadingProcessPinned = LoadingProgressPtr->Pin();
		LoadingProcessPinned->ExpireAndFadeout();
		ActiveNotifications.Remove(Filename);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SStartPageWindow::SendingServiceSideCapture_Load(const FString Filename)
{
	const SNotificationItemWeak* LoadingProgressPtr = ActiveNotifications.Find(Filename);
	if (LoadingProgressPtr)
	{
		TSharedPtr<SNotificationItem> LoadingProcessPinned = LoadingProgressPtr->Pin();
		LoadingProcessPinned->ExpireAndFadeout();
		ActiveNotifications.Remove(Filename);

		const FString PathName = FPaths::ProfilingDir() + TEXT("UnrealStats/Received/");
		const FString TraceFilepath = PathName + Filename;
		FInsightsManager::Get()->LoadTraceFile(TraceFilepath);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

EActiveTimerReturnType SStartPageWindow::UpdateActiveDuration(double InCurrentTime, float InDeltaTime)
{
	DurationActive += InDeltaTime;

	// The window will explicitly unregister this active timer when the mouse leaves.
	return EActiveTimerReturnType::Continue;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SStartPageWindow::OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	SCompoundWidget::OnMouseEnter(MyGeometry, MouseEvent);

	if (!ActiveTimerHandle.IsValid())
	{
		ActiveTimerHandle = RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateSP(this, &SStartPageWindow::UpdateActiveDuration));
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SStartPageWindow::OnMouseLeave(const FPointerEvent& MouseEvent)
{
	SCompoundWidget::OnMouseLeave(MouseEvent);

	auto PinnedActiveTimerHandle = ActiveTimerHandle.Pin();
	if (PinnedActiveTimerHandle.IsValid())
	{
		UnRegisterActiveTimer(PinnedActiveTimerHandle.ToSharedRef());
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply SStartPageWindow::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	return FReply::Unhandled();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply SStartPageWindow::OnDragOver(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	TSharedPtr<FExternalDragOperation> DragDropOp = DragDropEvent.GetOperationAs<FExternalDragOperation>();
	if (DragDropOp.IsValid())
	{
		if (DragDropOp->HasFiles())
		{
			const TArray<FString>& Files = DragDropOp->GetFiles();
			if (Files.Num() == 1)
			{
				const FString DraggedFileExtension = FPaths::GetExtension(Files[0], true);
				if (DraggedFileExtension == TEXT(".utrace"))
				{
					return FReply::Handled();
				}
			}
		}
	}

	return SCompoundWidget::OnDragOver(MyGeometry, DragDropEvent);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply SStartPageWindow::OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	TSharedPtr<FExternalDragOperation> DragDropOp = DragDropEvent.GetOperationAs<FExternalDragOperation>();
	if (DragDropOp.IsValid())
	{
		if (DragDropOp->HasFiles())
		{
			// For now, only allow a single file.
			const TArray<FString>& Files = DragDropOp->GetFiles();
			if (Files.Num() == 1)
			{
				const FString DraggedFileExtension = FPaths::GetExtension(Files[0], true);
				if (DraggedFileExtension == TEXT(".utrace"))
				{
					FInsightsManager::Get()->LoadTraceFile(Files[0]);
					return FReply::Handled();
				}
			}
		}
	}

	return SCompoundWidget::OnDrop(MyGeometry, DragDropEvent);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool SStartPageWindow::Live_IsEnabled() const
{
	return bIsAnyLiveSessionAvailable;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool SStartPageWindow::Last_IsEnabled() const
{
	return bIsAnySessionAvailable;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply SStartPageWindow::Live_OnClicked()
{
	FInsightsManager::Get()->LoadLastLiveSession();
	return FReply::Handled();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply SStartPageWindow::Last_OnClicked()
{
	FInsightsManager::Get()->LoadLastSession();
	return FReply::Handled();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply SStartPageWindow::Load_OnClicked()
{
	//const FString ProfilingDirectory(FPaths::ConvertRelativePathToFull(*FPaths::ProfilingDir()));
	TSharedRef<Trace::ISessionService> SessionService = FInsightsManager::Get()->GetSessionService();
	const FString ProfilingDirectory(FPaths::ConvertRelativePathToFull(SessionService->GetLocalSessionDirectory()));

	TArray<FString> OutFiles;
	bool bOpened = false;

	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (DesktopPlatform != nullptr)
	{
		FSlateApplication::Get().CloseToolTip();

		bOpened = DesktopPlatform->OpenFileDialog
		(
			FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
			LOCTEXT("LoadTrace_FileDesc", "Open trace file...").ToString(),
			ProfilingDirectory,
			TEXT(""),
			LOCTEXT("LoadTrace_FileFilter", "Trace files (*.utrace)|*.utrace|All files (*.*)|*.*").ToString(),
			EFileDialogFlags::None,
			OutFiles
		);
	}

	if (bOpened == true)
	{
		if (OutFiles.Num() == 1)
		{
			FInsightsManager::Get()->LoadTraceFile(OutFiles[0]);
		}
	}

	return FReply::Handled();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SStartPageWindow::LoadTraceFile(const TCHAR* TraceFile)
{
	FInsightsManager::Get()->LoadTraceFile(FString(TraceFile));
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SStartPageWindow::LoadSession(Trace::FSessionHandle SessionHandle)
{
	FInsightsManager::Get()->LoadSession(SessionHandle);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<SWidget> SStartPageWindow::MakeSessionListMenu()
{
	RefreshConnectionList();

	FMenuBuilder MenuBuilder(/*bInShouldCloseWindowAfterMenuSelection=*/true, nullptr);

	MenuBuilder.BeginSection("MostRecentlyUsedSessions", LOCTEXT("MostRecentlyUsedSessionsHeading", "Most Recently Used Sessions"));
	{
		//TODO: MRU
	}
	MenuBuilder.EndSection();

	MenuBuilder.BeginSection("AvailableSessions", LOCTEXT("AvailableSessionsHeading", "Available Sessions"));
	{
		TSharedRef<Trace::ISessionService> SessionService = FInsightsManager::Get()->GetSessionService();

		TArray<Trace::FSessionHandle> AvailableSessions;
		SessionService->GetAvailableSessions(AvailableSessions);

		// Iterate in reverse order as we want most recent sessions first.
		for (int32 SessionIndex = AvailableSessions.Num() - 1; SessionIndex >= 0; --SessionIndex)
		{
			Trace::FSessionHandle SessionHandle = AvailableSessions[SessionIndex];

			Trace::FSessionInfo SessionInfo;
			SessionService->GetSessionInfo(SessionHandle, SessionInfo);

			FText Label = FText::FromString(SessionInfo.Name);
			if (SessionInfo.bIsLive)
			{
				Label = FText::Format(LOCTEXT("LiveSessionTextFmt", "{0} (LIVE!)"), Label);
			}

			MenuBuilder.AddMenuEntry(
				Label,
				TAttribute<FText>(), // no tooltip
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateSP(this, &SStartPageWindow::LoadSession, SessionHandle)),
				NAME_None,
				EUserInterfaceActionType::Button
			);
		}
	}
	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FText SStartPageWindow::GetLocalSessionDirectory() const
{
	TSharedRef<Trace::ISessionService> SessionService = FInsightsManager::Get()->GetSessionService();
	return FText::FromString(FPaths::ConvertRelativePathToFull(SessionService->GetLocalSessionDirectory()));
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply SStartPageWindow::ExploreLocalSessionDirectory_OnClicked()
{
	TSharedRef<Trace::ISessionService> SessionService = FInsightsManager::Get()->GetSessionService();
	FString FullPath(FPaths::ConvertRelativePathToFull(SessionService->GetLocalSessionDirectory()));
	FPlatformProcess::ExploreFolder(*FullPath);
	return FReply::Handled();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FText SStartPageWindow::GetRecorderStatusText() const
{
	TSharedRef<Trace::ISessionService> SessionService = FInsightsManager::Get()->GetSessionService();
	if (SessionService->IsRecorderServerRunning())
	{
		return FText(LOCTEXT("RecorderServerRunning", "Running"));
	}
	else
	{
		return FText(LOCTEXT("RecorderServerRunning", "Stopped"));
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

EVisibility SStartPageWindow::StartTraceRecorder_Visibility() const
{
	TSharedRef<Trace::ISessionService> SessionService = FInsightsManager::Get()->GetSessionService();
	return SessionService->IsRecorderServerRunning() ? EVisibility::Collapsed : EVisibility::Visible;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

EVisibility SStartPageWindow::StopTraceRecorder_Visibility() const
{
	TSharedRef<Trace::ISessionService> SessionService = FInsightsManager::Get()->GetSessionService();
	return SessionService->IsRecorderServerRunning() ? EVisibility::Visible : EVisibility::Collapsed;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply SStartPageWindow::StartTraceRecorder_OnClicked()
{
	TSharedRef<Trace::ISessionService> SessionService = FInsightsManager::Get()->GetSessionService();
	SessionService->StartRecorderServer();
	RefreshConnectionList();
	return FReply::Handled();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply SStartPageWindow::StopTraceRecorder_OnClicked()
{
	TSharedRef<Trace::ISessionService> SessionService = FInsightsManager::Get()->GetSessionService();
	SessionService->StopRecorderServer();
	RefreshConnectionList();
	return FReply::Handled();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SStartPageWindow::OpenSettings()
{
	MainContentPanel->SetEnabled(false);
	(*OverlaySettingsSlot)
	[
		SNew(SBorder)
		.BorderImage(FEditorStyle::GetBrush("NotificationList.ItemBackground"))
		.Padding(8.0f)
		[
			SNew(SInsightsSettings)
			.OnClose(this, &SStartPageWindow::CloseSettings)
			.SettingPtr(&FInsightsManager::GetSettings())
		]
	];
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SStartPageWindow::CloseSettings()
{
	// Close the profiler settings by simply replacing widget with a null one.
	(*OverlaySettingsSlot)
	[
		SNullWidget::NullWidget
	];
	MainContentPanel->SetEnabled(true);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
