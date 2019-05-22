// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "STimingProfilerWindow.h"

#include "EditorStyleSet.h"
#include "Framework/Docking/WorkspaceItem.h"
#include "SlateOptMacros.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
//#include "WorkspaceMenuStructure.h"
//#include "WorkspaceMenuStructureModule.h"

#if WITH_EDITOR
	#include "EngineAnalytics.h"
	#include "Runtime/Analytics/Analytics/Public/AnalyticsEventAttribute.h"
	#include "Runtime/Analytics/Analytics/Public/Interfaces/IAnalyticsProvider.h"
#endif // WITH_EDITOR

// Insights
#include "Insights/InsightsManager.h"
#include "Insights/InsightsStyle.h"
#include "Insights/TimingProfilerManager.h"
#include "Insights/Version.h"
#include "Insights/Widgets/SFrameTrack.h"
#include "Insights/Widgets/SGraphTrack.h"
#include "Insights/Widgets/SInsightsSettings.h"
#include "Insights/Widgets/SLogView.h"
#include "Insights/Widgets/SStatsView.h"
#include "Insights/Widgets/STimersView.h"
#include "Insights/Widgets/STimingProfilerToolbar.h"
#include "Insights/Widgets/STimingView.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

#define LOCTEXT_NAMESPACE "STimingProfilerWindow"

////////////////////////////////////////////////////////////////////////////////////////////////////

const FName FTimingProfilerTabs::ToolbarID(TEXT("Toolbar"));
const FName FTimingProfilerTabs::FramesTrackID(TEXT("Frames"));
const FName FTimingProfilerTabs::GraphTrackID(TEXT("Graph"));
const FName FTimingProfilerTabs::TimingViewID(TEXT("TimingView"));
const FName FTimingProfilerTabs::TimersID(TEXT("Timers"));
const FName FTimingProfilerTabs::StatsCountersID(TEXT("StasCounters"));
const FName FTimingProfilerTabs::LogViewID(TEXT("LogView"));

////////////////////////////////////////////////////////////////////////////////////////////////////

// TODO: move this function to InsightsManager or in TraceSession.h
static FText Timing_GetTextForNotification(const EInsightsNotificationType NotificatonType, const ELoadingProgressState ProgressState, const FString& Filename, const float ProgressPercent = 0.0f)
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

STimingProfilerWindow::STimingProfilerWindow()
	: DurationActive(0.0f)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

STimingProfilerWindow::~STimingProfilerWindow()
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

TSharedRef<SDockTab> STimingProfilerWindow::SpawnTab_Toolbar(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.ShouldAutosize(true)
		.TabRole(ETabRole::PanelTab)
		[
			SNew(STimingProfilerToolbar)
		];
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<SDockTab> STimingProfilerWindow::SpawnTab_FramesTrack(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.ShouldAutosize(false)
		.TabRole(ETabRole::PanelTab)
		[
			SAssignNew(FrameTrack, SFrameTrack)
			//.IsEnabled(this, &STimingProfilerWindow::IsProfilerEnabled)
			//.Visibility(this, &STimingProfilerWindow::IsFramesTrackVisible)
		];
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<SDockTab> STimingProfilerWindow::SpawnTab_GraphTrack(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.ShouldAutosize(false)
		.TabRole(ETabRole::PanelTab)
		[
			SAssignNew(GraphTrack, SGraphTrack)
			//.IsEnabled(this, &STimingProfilerWindow::IsProfilerEnabled)
			//.Visibility(this, &STimingProfilerWindow::IsGraphTrackVisible)
		];
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<SDockTab> STimingProfilerWindow::SpawnTab_TimingView(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.ShouldAutosize(false)
		.TabRole(ETabRole::PanelTab)
		[
			SAssignNew(TimingView, STimingView)
			//.IsEnabled(this, &STimingProfilerWindow::IsProfilerEnabled)
			//.Visibility(this, &STimingProfilerWindow::IsTimersViewVisible)
		];
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<SDockTab> STimingProfilerWindow::SpawnTab_Timers(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.ShouldAutosize(false)
		.TabRole(ETabRole::PanelTab)
		[
		
			SAssignNew(TimersView, STimersView)
			//.IsEnabled(this, &STimingProfilerWindow::IsProfilerEnabled)
			//.Visibility(this, &STimingProfilerWindow::IsTimersViewVisible)
		];
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<SDockTab> STimingProfilerWindow::SpawnTab_StatsCounters(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.ShouldAutosize(false)
		.TabRole(ETabRole::PanelTab)
		[
			SAssignNew(StatsView, SStatsView)
			//.IsEnabled(this, &STimingProfilerWindow::IsProfilerEnabled)
			//.Visibility(this, &STimingProfilerWindow::IsStatsCountersViewVisible)
			
		];
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<SDockTab> STimingProfilerWindow::SpawnTab_LogView(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.ShouldAutosize(false)
		.TabRole(ETabRole::PanelTab)
		[
			SAssignNew(LogView, SLogView)
			//.IsEnabled(this, &STimingProfilerWindow::IsProfilerEnabled)
			//.Visibility(this, &STimingProfilerWindow::IsLogViewVisible)
		];
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STimingProfilerWindow::Construct(const FArguments& InArgs, const TSharedRef<SDockTab>& ConstructUnderMajorTab, const TSharedPtr<SWindow>& ConstructUnderWindow)
{
	// Create & initialize tab manager.
	TabManager = FGlobalTabmanager::Get()->NewTabManager(ConstructUnderMajorTab);
	TSharedRef<FWorkspaceItem> AppMenuGroup = TabManager->AddLocalWorkspaceMenuCategory(LOCTEXT("TimingProfilerMenuGroupName", "Timing Insights"));

	TabManager->RegisterTabSpawner(FTimingProfilerTabs::ToolbarID, FOnSpawnTab::CreateRaw(this, &STimingProfilerWindow::SpawnTab_Toolbar))
		.SetDisplayName(LOCTEXT("DeviceToolbarTabTitle", "Toolbar"))
		.SetIcon(FSlateIcon(FInsightsStyle::GetStyleSetName(), "Toolbar.Icon.Small"))
		.SetGroup(AppMenuGroup);

	TabManager->RegisterTabSpawner(FTimingProfilerTabs::FramesTrackID, FOnSpawnTab::CreateRaw(this, &STimingProfilerWindow::SpawnTab_FramesTrack))
		.SetDisplayName(LOCTEXT("FramesTrackTabTitle", "Frames"))
		.SetIcon(FSlateIcon(FInsightsStyle::GetStyleSetName(), "FramesTrack.Icon.Small"))
		.SetGroup(AppMenuGroup);

	TabManager->RegisterTabSpawner(FTimingProfilerTabs::GraphTrackID, FOnSpawnTab::CreateRaw(this, &STimingProfilerWindow::SpawnTab_GraphTrack))
		.SetDisplayName(LOCTEXT("GraphTrackTabTitle", "Graph"))
		.SetIcon(FSlateIcon(FInsightsStyle::GetStyleSetName(), "GraphTrack.Icon.Small"))
		.SetGroup(AppMenuGroup);

	TabManager->RegisterTabSpawner(FTimingProfilerTabs::TimingViewID, FOnSpawnTab::CreateRaw(this, &STimingProfilerWindow::SpawnTab_TimingView))
		.SetDisplayName(LOCTEXT("TimingViewTabTitle", "Timing View"))
		.SetIcon(FSlateIcon(FInsightsStyle::GetStyleSetName(), "TimingView.Icon.Small"))
		.SetGroup(AppMenuGroup);

	TabManager->RegisterTabSpawner(FTimingProfilerTabs::TimersID, FOnSpawnTab::CreateRaw(this, &STimingProfilerWindow::SpawnTab_Timers))
		.SetDisplayName(LOCTEXT("TimersTabTitle", "Timers"))
		.SetIcon(FSlateIcon(FInsightsStyle::GetStyleSetName(), "TimersView.Icon.Small"))
		.SetGroup(AppMenuGroup);

	TabManager->RegisterTabSpawner(FTimingProfilerTabs::StatsCountersID, FOnSpawnTab::CreateRaw(this, &STimingProfilerWindow::SpawnTab_StatsCounters))
		.SetDisplayName(LOCTEXT("StatsCountersTabTitle", "Stats Counters"))
		.SetIcon(FSlateIcon(FInsightsStyle::GetStyleSetName(), "StatsCountersView.Icon.Small"))
		.SetGroup(AppMenuGroup);

	TabManager->RegisterTabSpawner(FTimingProfilerTabs::LogViewID, FOnSpawnTab::CreateRaw(this, &STimingProfilerWindow::SpawnTab_LogView))
		.SetDisplayName(LOCTEXT("LogViewTabTitle", "Log View"))
		.SetIcon(FSlateIcon(FInsightsStyle::GetStyleSetName(), "LogView.Icon.Small"))
		.SetGroup(AppMenuGroup);

	// Create tab layout.
	const TSharedRef<FTabManager::FLayout> Layout = FTabManager::NewLayout("InsightsTimingProfilerLayout_v1.0")
		->AddArea
		(
			FTabManager::NewPrimaryArea()
			->SetOrientation(Orient_Vertical)
			->Split
			(
				FTabManager::NewStack()
				->AddTab(FTimingProfilerTabs::ToolbarID, ETabState::OpenedTab)
				->SetHideTabWell(true)
			)
			->Split
			(
				FTabManager::NewSplitter()
				->SetOrientation(Orient_Horizontal)
				->SetSizeCoefficient(1.0f)
				->Split
				(
					FTabManager::NewSplitter()
					->SetOrientation(Orient_Vertical)
					->SetSizeCoefficient(0.65f)
					->Split
					(
						FTabManager::NewStack()
						->SetSizeCoefficient(0.1f)
						->SetHideTabWell(true)
						->AddTab(FTimingProfilerTabs::FramesTrackID, FTimingProfilerManager::Get()->IsFramesTrackVisible() ? ETabState::OpenedTab : ETabState::ClosedTab)
					)
					->Split
					(
						FTabManager::NewStack()
						->SetSizeCoefficient(0.2f)
						->SetHideTabWell(true)
						->AddTab(FTimingProfilerTabs::GraphTrackID, FTimingProfilerManager::Get()->IsGraphTrackVisible() ? ETabState::OpenedTab : ETabState::ClosedTab)
					)
					->Split
					(
						FTabManager::NewStack()
						->SetSizeCoefficient(0.5f)
						->SetHideTabWell(true)
						->AddTab(FTimingProfilerTabs::TimingViewID, FTimingProfilerManager::Get()->IsTimingViewVisible() ? ETabState::OpenedTab : ETabState::ClosedTab)
					)
					->Split
					(
						FTabManager::NewStack()
						->SetSizeCoefficient(0.2f)
						->SetHideTabWell(true)
						->AddTab(FTimingProfilerTabs::LogViewID, FTimingProfilerManager::Get()->IsLogViewVisible() ? ETabState::OpenedTab : ETabState::ClosedTab)
					)
				)
				->Split
				(
					FTabManager::NewStack()
					->SetSizeCoefficient(0.35f)
					->AddTab(FTimingProfilerTabs::TimersID, FTimingProfilerManager::Get()->IsTimersViewVisible() ? ETabState::OpenedTab : ETabState::ClosedTab)
					->AddTab(FTimingProfilerTabs::StatsCountersID, FTimingProfilerManager::Get()->IsStatsCountersViewVisible() ? ETabState::OpenedTab : ETabState::ClosedTab)
				)
			)
		);

	// Create & initialize main menu.
	FMenuBarBuilder MenuBarBuilder = FMenuBarBuilder(TSharedPtr<FUICommandList>());
	
	MenuBarBuilder.AddPullDownMenu(
		LOCTEXT("MenuLabel", "MENU"),
		FText::GetEmpty(),
		FNewMenuDelegate::CreateStatic(&STimingProfilerWindow::FillMenu, TabManager),
		FName(TEXT("MENU"))
	);

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

			// Overlay slot for the main profiler window area
			+ SOverlay::Slot()
				.HAlign(HAlign_Fill)
				.VAlign(VAlign_Fill)
				[
					SAssignNew(MainContentPanel, SVerticalBox)

					+ SVerticalBox::Slot()
						.AutoHeight()
						[
							MenuBarBuilder.MakeWidget()
						]

					+ SVerticalBox::Slot()
						.FillHeight(1.0f)
						[
							TabManager->RestoreFrom(Layout, ConstructUnderWindow).ToSharedRef()
						]
				]

			// Session hint overlay
			+ SOverlay::Slot()
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				[
					SNew(SBorder)
						.Visibility(this, &STimingProfilerWindow::IsSessionOverlayVisible)
						.BorderImage(FEditorStyle::GetBrush("NotificationList.ItemBackground"))
						.Padding(8.0f)
						[
							SNew(STextBlock)
								.Text(LOCTEXT("SelectTraceOverlayText", "Please select a trace."))
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

	// Tell tab-manager about the global menu bar.
	TabManager->SetMenuMultiBox(MenuBarBuilder.GetMultiBox());
}

END_SLATE_FUNCTION_BUILD_OPTIMIZATION

////////////////////////////////////////////////////////////////////////////////////////////////////

void STimingProfilerWindow::FillMenu(FMenuBuilder& MenuBuilder, const TSharedPtr<FTabManager> TabManager)
{
	if (!TabManager.IsValid())
	{
		return;
	}

#if !WITH_EDITOR
	//TODO: FGlobalTabmanager::Get()->PopulateTabSpawnerMenu(MenuBuilder, WorkspaceMenu::GetMenuStructure().GetStructureRoot());
#endif //!WITH_EDITOR

	TabManager->PopulateLocalTabSpawnerMenu(MenuBuilder);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

EVisibility STimingProfilerWindow::IsFramesTrackVisible() const
{
	if (FTimingProfilerManager::Get()->IsFramesTrackVisible() &&
		FInsightsManager::Get()->GetSession().IsValid())
	{
		return EVisibility::Visible;
	}

	return EVisibility::Collapsed;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

EVisibility STimingProfilerWindow::IsGraphTrackVisible() const
{
	if (FTimingProfilerManager::Get()->IsGraphTrackVisible() &&
		FInsightsManager::Get()->GetSession().IsValid())
	{
		return EVisibility::Visible;
	}

	return EVisibility::Collapsed;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

EVisibility STimingProfilerWindow::IsTimingViewVisible() const
{
	if (FTimingProfilerManager::Get()->IsTimingViewVisible() &&
		FInsightsManager::Get()->GetSession().IsValid())
	{
		return EVisibility::Visible;
	}

	return EVisibility::Collapsed;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

EVisibility STimingProfilerWindow::IsTimersViewVisible() const
{
	if (FTimingProfilerManager::Get()->IsTimersViewVisible() &&
		FInsightsManager::Get()->GetSession().IsValid())
	{
		return EVisibility::Visible;
	}

	return EVisibility::Collapsed;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

EVisibility STimingProfilerWindow::IsLogViewVisible() const
{
	if (FTimingProfilerManager::Get()->IsLogViewVisible() &&
		FInsightsManager::Get()->GetSession().IsValid())
	{
		return EVisibility::Visible;
	}

	return EVisibility::Collapsed;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STimingProfilerWindow::ShowHideTab(const FName& TabID, bool bIsVisible)
{
	if (!bIsVisible)
	{
		TSharedPtr<SDockTab> Tab = TabManager->FindExistingLiveTab(TabID);
		if (Tab.IsValid())
		{
			Tab->RequestCloseTab();
		}
	}
	else
	{
		if (TabManager->CanSpawnTab(TabID))
		{
			TabManager->InvokeTab(TabID);
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

EVisibility STimingProfilerWindow::IsSessionOverlayVisible() const
{
	if (FInsightsManager::Get()->GetSession().IsValid())
	{
		return EVisibility::Hidden;
	}

	return EVisibility::Visible;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool STimingProfilerWindow::IsProfilerEnabled() const
{
	const bool bIsActive = FInsightsManager::Get()->GetSession().IsValid();
	return bIsActive;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STimingProfilerWindow::ManageLoadingProgressNotificationState(const FString& Filename, const EInsightsNotificationType NotificatonType, const ELoadingProgressState ProgressState, const float LoadingProgress)
{
	const FString BaseFilename = FPaths::GetBaseFilename(Filename);

	if (ProgressState == ELoadingProgressState::Started)
	{
		const bool bContains = ActiveNotifications.Contains(Filename);
		if (!bContains)
		{
			FNotificationInfo NotificationInfo(Timing_GetTextForNotification(NotificatonType, ProgressState, BaseFilename));
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
					FSimpleDelegate::CreateSP(this, &STimingProfilerWindow::SendingServiceSideCapture_Cancel, Filename), SNotificationItem::CS_Success));
				NotificationInfo.ButtonDetails.Add(FNotificationButtonInfo(LoadButtonText, LoadButtonTT,
					FSimpleDelegate::CreateSP(this, &STimingProfilerWindow::SendingServiceSideCapture_Load, Filename), SNotificationItem::CS_Success));
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
			LoadingProcessPinned->SetText(Timing_GetTextForNotification(NotificatonType, ProgressState, BaseFilename, LoadingProgress));
			LoadingProcessPinned->SetCompletionState(SNotificationItem::CS_Pending);
		}
	}
	else if (ProgressState == ELoadingProgressState::Loaded)
	{
		const SNotificationItemWeak* LoadingProgressPtr = ActiveNotifications.Find(Filename);
		if (LoadingProgressPtr)
		{
			TSharedPtr<SNotificationItem> LoadingProcessPinned = LoadingProgressPtr->Pin();
			LoadingProcessPinned->SetText(Timing_GetTextForNotification(NotificatonType, ProgressState, BaseFilename));
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
			LoadingProcessPinned->SetText(Timing_GetTextForNotification(NotificatonType, ProgressState, BaseFilename));
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

void STimingProfilerWindow::SendingServiceSideCapture_Cancel(const FString Filename)
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

void STimingProfilerWindow::SendingServiceSideCapture_Load(const FString Filename)
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

EActiveTimerReturnType STimingProfilerWindow::UpdateActiveDuration(double InCurrentTime, float InDeltaTime)
{
	DurationActive += InDeltaTime;

	// The profiler window will explicitly unregister this active timer when the mouse leaves.
	return EActiveTimerReturnType::Continue;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STimingProfilerWindow::OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	SCompoundWidget::OnMouseEnter(MyGeometry, MouseEvent);

	if (!ActiveTimerHandle.IsValid())
	{
		ActiveTimerHandle = RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateSP(this, &STimingProfilerWindow::UpdateActiveDuration));
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STimingProfilerWindow::OnMouseLeave(const FPointerEvent& MouseEvent)
{
	SCompoundWidget::OnMouseLeave(MouseEvent);

	auto PinnedActiveTimerHandle = ActiveTimerHandle.Pin();
	if (PinnedActiveTimerHandle.IsValid())
	{
		UnRegisterActiveTimer(PinnedActiveTimerHandle.ToSharedRef());
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply STimingProfilerWindow::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	return FTimingProfilerManager::Get()->GetCommandList()->ProcessCommandBindings(InKeyEvent) ? FReply::Handled() : FReply::Unhandled();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply STimingProfilerWindow::OnDragOver(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
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

	return SCompoundWidget::OnDragOver(MyGeometry,DragDropEvent);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply STimingProfilerWindow::OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
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

	return SCompoundWidget::OnDrop(MyGeometry,DragDropEvent);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STimingProfilerWindow::OpenProfilerSettings()
{
	MainContentPanel->SetEnabled(false);
	(*OverlaySettingsSlot)
	[
		SNew(SBorder)
		.BorderImage(FEditorStyle::GetBrush("NotificationList.ItemBackground"))
		.Padding(8.0f)
		[
			SNew(SInsightsSettings)
			.OnClose(this, &STimingProfilerWindow::CloseProfilerSettings)
			.SettingPtr(&FInsightsManager::GetSettings())
		]
	];
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STimingProfilerWindow::CloseProfilerSettings()
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
