// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SStartPageWindow.h"

#include "EditorStyleSet.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/WorkspaceItem.h"
#include "SlateOptMacros.h"
#include "TraceServices/SessionService.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
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

SStartPageWindow::SStartPageWindow()
	: DurationActive(0.0f)
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

////////////////////////////////////////////////////////////////////////////////////////////////////

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
						[
							SNew(SBorder)
							.Visibility(this, &SStartPageWindow::IsSessionOverlayVisible)
							.BorderImage(FEditorStyle::GetBrush("NotificationList.ItemBackground"))
							.Padding(8.0f)
							[
								SNew(STextBlock)
								.Text(LOCTEXT("SelectTraceOverlayText", "Please select a trace."))
							]
						]

					+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(SBox)
							.HeightOverride(6.0f)
						]

					+ SVerticalBox::Slot()
						.AutoHeight()
						.HAlign(HAlign_Center)
						[
							SNew(SHorizontalBox)

							+ SHorizontalBox::Slot()
								.AutoWidth()
								[
									SNew(SButton)
									.OnClicked(this, &SStartPageWindow::Live_OnClicked)
									.ToolTipText(LOCTEXT("LiveButtonTooltip", "Start a live session or load the last trace file from the Local Session Directory."))
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
								[
									SNew(SBox)
									.WidthOverride(6.0f)
								]

							+ SHorizontalBox::Slot()
								.AutoWidth()
								[
									SNew(SButton)
									.OnClicked(this, &SStartPageWindow::Load_OnClicked)
									.ToolTipText(LOCTEXT("LoadButtonTooltip", "Load a trace file."))
									.ContentPadding(8.0f)
									.Content()
									[
										SNew(SHorizontalBox)

										+SHorizontalBox::Slot()
											.AutoWidth()
											[
												SNew(SImage)
												.Image(FInsightsStyle::GetBrush("Load.Icon.Large"))
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
													.Text(LOCTEXT("LoadButtonText", "Load..."))
												]
											]
									]
								]
						]

					+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(SBox)
							.HeightOverride(6.0f)
						]

					+ SVerticalBox::Slot()
						.AutoHeight()
						.HAlign(HAlign_Center)
						[
							SNew(SBorder)
							.Visibility(this, &SStartPageWindow::IsSessionOverlayVisible)
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

								+SVerticalBox::Slot()
									.AutoHeight()
									.HAlign(HAlign_Center)
									.Padding(0.0, 2.0f)
									[
										SNew(STextBlock)
										.Text(this, &SStartPageWindow::GetLocalSessionDirectory)
									]
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

END_SLATE_FUNCTION_BUILD_OPTIMIZATION

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
					// Enqueue load operation.
					FInsightsManager::Get()->LoadTraceFile(Files[0]);
					return FReply::Handled();
				}
			}
		}
	}

	return SCompoundWidget::OnDrop(MyGeometry, DragDropEvent);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SStartPageWindow::SpawnAndActivateTabs()
{
	//TODO: expose those in FInsightsManager; see also TraceInsightsModule.cpp
	static const FName StartPageTabName(TEXT("StartPage"));
	static const FName TimingProfilerTabName(TEXT("TimingProfiler"));
	static const FName IoProfilerTabName(TEXT("IoProfiler"));

	// Open tabs for profilers.
	if (FGlobalTabmanager::Get()->CanSpawnTab(TimingProfilerTabName))
	{
		FGlobalTabmanager::Get()->InvokeTab(TimingProfilerTabName);
	}
	if (FGlobalTabmanager::Get()->CanSpawnTab(IoProfilerTabName))
	{
		FGlobalTabmanager::Get()->InvokeTab(IoProfilerTabName);
	}

	// Ensure Timing Insights / Timing View is the active tab.
	if (TSharedPtr<SDockTab> TimingInsightsTab = FGlobalTabmanager::Get()->FindExistingLiveTab(TimingProfilerTabName))
	{
		//FGlobalTabmanager::Get()->SetActiveTab(TimingProfilerTabName); // doesn't really work :(
		FGlobalTabmanager::Get()->InvokeTab(TimingProfilerTabName); // this activates the tab (brings the tab in foreground)

		TSharedPtr<class STimingProfilerWindow> Wnd = FTimingProfilerManager::Get()->GetProfilerWindow();
		if (Wnd)
		{
			TSharedPtr<FTabManager> TabManager = Wnd->GetTabManager();
			if (TSharedPtr<SDockTab> TimingViewTab = TabManager->FindExistingLiveTab(FTimingProfilerTabs::TimingViewID))
			{
				TabManager->DrawAttention(TimingViewTab.ToSharedRef());
				TimingViewTab->ActivateInParent(ETabActivationCause::SetDirectly);
				FSlateApplication::Get().SetKeyboardFocus(TimingViewTab->GetContent());
			}
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FText SStartPageWindow::GetLocalSessionDirectory() const
{
	TSharedRef<Trace::ISessionService> SessionService = FInsightsManager::Get()->GetSessionService();
	return FText::FromString(FPaths::ConvertRelativePathToFull(SessionService->GetLocalSessionDirectory()));
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply SStartPageWindow::Live_OnClicked()
{
	SpawnAndActivateTabs();
	FInsightsManager::Get()->GetCommandList()->ExecuteAction(FInsightsManager::GetCommands().InsightsManager_Live.ToSharedRef());
	return FReply::Handled();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply SStartPageWindow::Load_OnClicked()
{
	SpawnAndActivateTabs();
	FInsightsManager::Get()->GetCommandList()->ExecuteAction(FInsightsManager::GetCommands().InsightsManager_Load.ToSharedRef());
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
