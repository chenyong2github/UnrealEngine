// Copyright Epic Games, Inc. All Rights Reserved.

#include "STimingProfilerWindow.h"

#include "EditorStyleSet.h"
#include "Framework/Docking/LayoutExtender.h"
#include "Framework/Docking/WorkspaceItem.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Docking/LayoutService.h"
#include "SlateOptMacros.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Modules/ModuleManager.h"

#if WITH_EDITOR
	#include "EngineAnalytics.h"
	#include "Runtime/Analytics/Analytics/Public/AnalyticsEventAttribute.h"
	#include "Runtime/Analytics/Analytics/Public/Interfaces/IAnalyticsProvider.h"
#endif // WITH_EDITOR

// Insights
#include "Insights/Common/InsightsMenuBuilder.h"
#include "Insights/InsightsManager.h"
#include "Insights/InsightsStyle.h"
#include "Insights/TimingProfilerManager.h"
#include "Insights/TraceInsightsModule.h"
#include "Insights/Version.h"
#include "Insights/Widgets/SFrameTrack.h"
#include "Insights/Widgets/SInsightsSettings.h"
#include "Insights/Widgets/SLogView.h"
#include "Insights/Widgets/SStatsView.h"
#include "Insights/Widgets/STimersView.h"
#include "Insights/Widgets/STimerTreeView.h"
#include "Insights/Widgets/STimingProfilerToolbar.h"
#include "Insights/Widgets/STimingView.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

#define LOCTEXT_NAMESPACE "STimingProfilerWindow"

////////////////////////////////////////////////////////////////////////////////////////////////////

const FName FTimingProfilerTabs::ToolbarID(TEXT("Toolbar"));
const FName FTimingProfilerTabs::FramesTrackID(TEXT("Frames"));
const FName FTimingProfilerTabs::TimingViewID(TEXT("TimingView"));
const FName FTimingProfilerTabs::TimersID(TEXT("Timers"));
const FName FTimingProfilerTabs::CallersID(TEXT("Callers"));
const FName FTimingProfilerTabs::CalleesID(TEXT("Callees"));
const FName FTimingProfilerTabs::StatsCountersID(TEXT("StasCounters"));
const FName FTimingProfilerTabs::LogViewID(TEXT("LogView"));

////////////////////////////////////////////////////////////////////////////////////////////////////

STimingProfilerWindow::STimingProfilerWindow()
	: DurationActive(0.0f)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

STimingProfilerWindow::~STimingProfilerWindow()
{
	if (LogView)
	{
		HideTab(FTimingProfilerTabs::LogViewID);
		check(LogView == nullptr);
	}

	if (StatsView)
	{
		HideTab(FTimingProfilerTabs::StatsCountersID);
		check(StatsView == nullptr);
	}

	if (CalleesTreeView)
	{
		HideTab(FTimingProfilerTabs::CalleesID);
		check(CalleesTreeView == nullptr);
	}

	if (CallersTreeView)
	{
		HideTab(FTimingProfilerTabs::CallersID);
		check(CallersTreeView == nullptr);
	}

	if (TimersView)
	{
		HideTab(FTimingProfilerTabs::TimersID);
		check(TimersView == nullptr);
	}

	if (TimingView)
	{
		HideTab(FTimingProfilerTabs::TimingViewID);
		check(TimingView == nullptr);
	}

	if (FrameTrack)
	{
		HideTab(FTimingProfilerTabs::FramesTrackID);
		check(FrameTrack == nullptr);
	}

	HideTab(FTimingProfilerTabs::ToolbarID);

#if WITH_EDITOR
	if (DurationActive > 0.0f && FEngineAnalytics::IsAvailable())
	{
		FEngineAnalytics::GetProvider().RecordEvent(TEXT("Insights.Usage.TimingProfiler"), FAnalyticsEventAttribute(TEXT("Duration"), DurationActive));
	}
#endif // WITH_EDITOR
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STimingProfilerWindow::Reset()
{
	if (FrameTrack)
	{
		FrameTrack->Reset();
	}

	if (TimingView)
	{
		TimingView->Reset();
	}

	if (TimersView)
	{
		TimersView->Reset();
	}

	if (CallersTreeView)
	{
		CallersTreeView->Reset();
	}

	if (CalleesTreeView)
	{
		CalleesTreeView->Reset();
	}

	if (StatsView)
	{
		StatsView->Reset();
	}

	if (LogView)
	{
		LogView->Reset();
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION

TSharedRef<SDockTab> STimingProfilerWindow::SpawnTab_Toolbar(const FSpawnTabArgs& Args)
{
	const TSharedRef<SDockTab> DockTab = SNew(SDockTab)
		.ShouldAutosize(true)
		.TabRole(ETabRole::PanelTab)
		[
			SNew(STimingProfilerToolbar)
		];

	DockTab->SetOnTabClosed(SDockTab::FOnTabClosedCallback::CreateRaw(this, &STimingProfilerWindow::OnToolbarTabClosed));

	return DockTab;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STimingProfilerWindow::OnToolbarTabClosed(TSharedRef<SDockTab> TabBeingClosed)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<SDockTab> STimingProfilerWindow::SpawnTab_FramesTrack(const FSpawnTabArgs& Args)
{
	FTimingProfilerManager::Get()->SetFramesTrackVisible(true);

	const TSharedRef<SDockTab> DockTab = SNew(SDockTab)
		.ShouldAutosize(false)
		.TabRole(ETabRole::PanelTab)
		[
			SAssignNew(FrameTrack, SFrameTrack)
		];

	DockTab->SetOnTabClosed(SDockTab::FOnTabClosedCallback::CreateRaw(this, &STimingProfilerWindow::OnFramesTrackTabClosed));

	return DockTab;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STimingProfilerWindow::OnFramesTrackTabClosed(TSharedRef<SDockTab> TabBeingClosed)
{
	FTimingProfilerManager::Get()->SetFramesTrackVisible(false);
	FrameTrack = nullptr;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<SDockTab> STimingProfilerWindow::SpawnTab_TimingView(const FSpawnTabArgs& Args)
{
	FTimingProfilerManager::Get()->SetTimingViewVisible(true);

	const TSharedRef<SDockTab> DockTab = SNew(SDockTab)
		.ShouldAutosize(false)
		.TabRole(ETabRole::PanelTab)
		[
			SAssignNew(TimingView, STimingView)
		];

	TimingView->Reset(true);

	DockTab->SetOnTabClosed(SDockTab::FOnTabClosedCallback::CreateRaw(this, &STimingProfilerWindow::OnTimingViewTabClosed));

	return DockTab;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STimingProfilerWindow::OnTimingViewTabClosed(TSharedRef<SDockTab> TabBeingClosed)
{
	FTimingProfilerManager::Get()->SetTimingViewVisible(false);
	TimingView = nullptr;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<SDockTab> STimingProfilerWindow::SpawnTab_Timers(const FSpawnTabArgs& Args)
{
	FTimingProfilerManager::Get()->SetTimersViewVisible(true);

	const TSharedRef<SDockTab> DockTab = SNew(SDockTab)
		.ShouldAutosize(false)
		.TabRole(ETabRole::PanelTab)
		[
			SAssignNew(TimersView, STimersView)
		];

	DockTab->SetOnTabClosed(SDockTab::FOnTabClosedCallback::CreateRaw(this, &STimingProfilerWindow::OnTimersTabClosed));

	return DockTab;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STimingProfilerWindow::OnTimersTabClosed(TSharedRef<SDockTab> TabBeingClosed)
{
	FTimingProfilerManager::Get()->SetTimersViewVisible(false);
	TimersView = nullptr;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<SDockTab> STimingProfilerWindow::SpawnTab_Callers(const FSpawnTabArgs& Args)
{
	FTimingProfilerManager::Get()->SetCallersTreeViewVisible(true);

	const TSharedRef<SDockTab> DockTab = SNew(SDockTab)
		.ShouldAutosize(false)
		.TabRole(ETabRole::PanelTab)
		[
			SAssignNew(CallersTreeView, STimerTreeView, LOCTEXT("CallersTreeViewName", "Callers"))
		];

	DockTab->SetOnTabClosed(SDockTab::FOnTabClosedCallback::CreateRaw(this, &STimingProfilerWindow::OnCallersTabClosed));

	return DockTab;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STimingProfilerWindow::OnCallersTabClosed(TSharedRef<SDockTab> TabBeingClosed)
{
	FTimingProfilerManager::Get()->SetCallersTreeViewVisible(false);
	CallersTreeView = nullptr;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<SDockTab> STimingProfilerWindow::SpawnTab_Callees(const FSpawnTabArgs& Args)
{
	FTimingProfilerManager::Get()->SetCalleesTreeViewVisible(true);

	const TSharedRef<SDockTab> DockTab = SNew(SDockTab)
		.ShouldAutosize(false)
		.TabRole(ETabRole::PanelTab)
		[
			SAssignNew(CalleesTreeView, STimerTreeView, LOCTEXT("CalleesTreeViewName", "Callees"))
		];

	DockTab->SetOnTabClosed(SDockTab::FOnTabClosedCallback::CreateRaw(this, &STimingProfilerWindow::OnCalleesTabClosed));

	return DockTab;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STimingProfilerWindow::OnCalleesTabClosed(TSharedRef<SDockTab> TabBeingClosed)
{
	FTimingProfilerManager::Get()->SetCalleesTreeViewVisible(false);
	CalleesTreeView = nullptr;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<SDockTab> STimingProfilerWindow::SpawnTab_StatsCounters(const FSpawnTabArgs& Args)
{
	FTimingProfilerManager::Get()->SetStatsCountersViewVisible(true);

	const TSharedRef<SDockTab> DockTab = SNew(SDockTab)
		.ShouldAutosize(false)
		.TabRole(ETabRole::PanelTab)
		[
			SAssignNew(StatsView, SStatsView)
		];

	DockTab->SetOnTabClosed(SDockTab::FOnTabClosedCallback::CreateRaw(this, &STimingProfilerWindow::OnStatsCountersTabClosed));

	return DockTab;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STimingProfilerWindow::OnStatsCountersTabClosed(TSharedRef<SDockTab> TabBeingClosed)
{
	FTimingProfilerManager::Get()->SetStatsCountersViewVisible(false);
	StatsView = nullptr;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<SDockTab> STimingProfilerWindow::SpawnTab_LogView(const FSpawnTabArgs& Args)
{
	FTimingProfilerManager::Get()->SetLogViewVisible(true);

	const TSharedRef<SDockTab> DockTab = SNew(SDockTab)
		.ShouldAutosize(false)
		.TabRole(ETabRole::PanelTab)
		[
			SAssignNew(LogView, SLogView)
		];

	DockTab->SetOnTabClosed(SDockTab::FOnTabClosedCallback::CreateRaw(this, &STimingProfilerWindow::OnLogViewTabClosed));

	return DockTab;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STimingProfilerWindow::OnLogViewTabClosed(TSharedRef<SDockTab> TabBeingClosed)
{
	FTimingProfilerManager::Get()->SetLogViewVisible(false);
	LogView = nullptr;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STimingProfilerWindow::Construct(const FArguments& InArgs, const TSharedRef<SDockTab>& ConstructUnderMajorTab, const TSharedPtr<SWindow>& ConstructUnderWindow)
{
	// Create & initialize tab manager.
	TabManager = FGlobalTabmanager::Get()->NewTabManager(ConstructUnderMajorTab);
	const auto& PersistLayout = [](const TSharedRef<FTabManager::FLayout>& LayoutToSave)
	{
		FLayoutSaveRestore::SaveToConfig(FTraceInsightsModule::GetUnrealInsightsLayoutIni(), LayoutToSave);
	};
	TabManager->SetOnPersistLayout(FTabManager::FOnPersistLayout::CreateLambda(PersistLayout));

	TSharedRef<FWorkspaceItem> AppMenuGroup = TabManager->AddLocalWorkspaceMenuCategory(LOCTEXT("TimingProfilerMenuGroupName", "Timing Insights"));

	Extension = MakeShared<FInsightsMajorTabExtender>(TabManager);

	TabManager->RegisterTabSpawner(FTimingProfilerTabs::ToolbarID, FOnSpawnTab::CreateRaw(this, &STimingProfilerWindow::SpawnTab_Toolbar))
		.SetDisplayName(LOCTEXT("DeviceToolbarTabTitle", "Toolbar"))
		.SetIcon(FSlateIcon(FInsightsStyle::GetStyleSetName(), "Toolbar.Icon.Small"))
		.SetGroup(AppMenuGroup);

	TabManager->RegisterTabSpawner(FTimingProfilerTabs::FramesTrackID, FOnSpawnTab::CreateRaw(this, &STimingProfilerWindow::SpawnTab_FramesTrack))
		.SetDisplayName(LOCTEXT("FramesTrackTabTitle", "Frames"))
		.SetIcon(FSlateIcon(FInsightsStyle::GetStyleSetName(), "FramesTrack.Icon.Small"))
		.SetGroup(AppMenuGroup);

	TabManager->RegisterTabSpawner(FTimingProfilerTabs::TimingViewID, FOnSpawnTab::CreateRaw(this, &STimingProfilerWindow::SpawnTab_TimingView))
		.SetDisplayName(LOCTEXT("TimingViewTabTitle", "Timing View"))
		.SetIcon(FSlateIcon(FInsightsStyle::GetStyleSetName(), "TimingView.Icon.Small"))
		.SetGroup(AppMenuGroup);

	TabManager->RegisterTabSpawner(FTimingProfilerTabs::TimersID, FOnSpawnTab::CreateRaw(this, &STimingProfilerWindow::SpawnTab_Timers))
		.SetDisplayName(LOCTEXT("TimersTabTitle", "Timers"))
		.SetIcon(FSlateIcon(FInsightsStyle::GetStyleSetName(), "TimersView.Icon.Small"))
		.SetGroup(AppMenuGroup);

	TabManager->RegisterTabSpawner(FTimingProfilerTabs::CallersID, FOnSpawnTab::CreateRaw(this, &STimingProfilerWindow::SpawnTab_Callers))
		.SetDisplayName(LOCTEXT("CallersTabTitle", "Callers"))
		.SetIcon(FSlateIcon(FInsightsStyle::GetStyleSetName(), "TimersView.Icon.Small")) // TODO
		.SetGroup(AppMenuGroup);

	TabManager->RegisterTabSpawner(FTimingProfilerTabs::CalleesID, FOnSpawnTab::CreateRaw(this, &STimingProfilerWindow::SpawnTab_Callees))
		.SetDisplayName(LOCTEXT("CalleesTabTitle", "Callees"))
		.SetIcon(FSlateIcon(FInsightsStyle::GetStyleSetName(), "TimersView.Icon.Small")) // TODO
		.SetGroup(AppMenuGroup);

	TabManager->RegisterTabSpawner(FTimingProfilerTabs::StatsCountersID, FOnSpawnTab::CreateRaw(this, &STimingProfilerWindow::SpawnTab_StatsCounters))
		.SetDisplayName(LOCTEXT("StatsCountersTabTitle", "Counters"))
		.SetIcon(FSlateIcon(FInsightsStyle::GetStyleSetName(), "StatsCountersView.Icon.Small"))
		.SetGroup(AppMenuGroup);

	TabManager->RegisterTabSpawner(FTimingProfilerTabs::LogViewID, FOnSpawnTab::CreateRaw(this, &STimingProfilerWindow::SpawnTab_LogView))
		.SetDisplayName(LOCTEXT("LogViewTabTitle", "Log View"))
		.SetIcon(FSlateIcon(FInsightsStyle::GetStyleSetName(), "LogView.Icon.Small"))
		.SetGroup(AppMenuGroup);

	TSharedPtr<FTimingProfilerManager> TimingProfilerManager = FTimingProfilerManager::Get();
	ensure(TimingProfilerManager.IsValid());

	// Check for layout overrides.
	FTraceInsightsModule& TraceInsightsModule = FModuleManager::GetModuleChecked<FTraceInsightsModule>("TraceInsights");
	FInsightsMajorTabConfig TabConfig = TraceInsightsModule.FindMajorTabConfig(FInsightsManagerTabs::TimingProfilerTabId);

	const FOnRegisterMajorTabExtensions* ExtensionDelegate = TraceInsightsModule.FindMajorTabLayoutExtension(FInsightsManagerTabs::TimingProfilerTabId);
	if (ExtensionDelegate)
	{
		ExtensionDelegate->Broadcast(*Extension);
	}

	// Register any new minor tabs.
	for (const FInsightsMinorTabConfig& MinorTabConfig : Extension->GetMinorTabs())
	{
		FTabSpawnerEntry& TabSpawnerEntry = TabManager->RegisterTabSpawner(MinorTabConfig.TabId, MinorTabConfig.OnSpawnTab);

		TabSpawnerEntry
		.SetDisplayName(MinorTabConfig.TabLabel)
		.SetTooltipText(MinorTabConfig.TabTooltip)
		.SetIcon(MinorTabConfig.TabIcon)
		.SetReuseTabMethod(MinorTabConfig.OnFindTabToReuse);

		if (MinorTabConfig.WorkspaceGroup.IsValid())
		{
			TabSpawnerEntry.SetGroup(MinorTabConfig.WorkspaceGroup.ToSharedRef());
		}
	}

	TSharedRef<FTabManager::FLayout> Layout = [&TabConfig]() -> TSharedRef<FTabManager::FLayout>
	{
		if (TabConfig.Layout.IsValid())
		{
			return TabConfig.Layout.ToSharedRef();
		}
		else
		{
			// Create tab layout.
			return FTabManager::NewLayout("InsightsTimingProfilerLayout_v1.1")
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
								->AddTab(FTimingProfilerTabs::FramesTrackID, ETabState::OpenedTab)
							)
							->Split
							(
								FTabManager::NewStack()
								->SetSizeCoefficient(0.5f)
								->SetHideTabWell(true)
								->AddTab(FTimingProfilerTabs::TimingViewID, ETabState::OpenedTab)
							)
							->Split
							(
								FTabManager::NewStack()
								->SetSizeCoefficient(0.2f)
								->SetHideTabWell(true)
								->AddTab(FTimingProfilerTabs::LogViewID, ETabState::OpenedTab)
							)
						)
						->Split
						(
							FTabManager::NewSplitter()
							->SetOrientation(Orient_Vertical)
							->SetSizeCoefficient(0.35f)
							->Split
							(
								FTabManager::NewStack()
								->SetSizeCoefficient(0.67f)
								->AddTab(FTimingProfilerTabs::TimersID, ETabState::OpenedTab)
								->AddTab(FTimingProfilerTabs::StatsCountersID, ETabState::OpenedTab)
								->SetForegroundTab(FTimingProfilerTabs::TimersID)
							)
							->Split
							(
								FTabManager::NewStack()
								->SetSizeCoefficient(0.165f)
								->SetHideTabWell(true)
								->AddTab(FTimingProfilerTabs::CallersID, ETabState::OpenedTab)
							)
							->Split
							(
								FTabManager::NewStack()
								->SetSizeCoefficient(0.165f)
								->SetHideTabWell(true)
								->AddTab(FTimingProfilerTabs::CalleesID, ETabState::OpenedTab)
							)
						)
					)
				);
		}
	}();
	
	Layout->ProcessExtensions(Extension->GetLayoutExtender());
	Layout = FLayoutSaveRestore::LoadFromConfig(FTraceInsightsModule::GetUnrealInsightsLayoutIni(), Layout);

	// Create & initialize main menu.
	FMenuBarBuilder MenuBarBuilder = FMenuBarBuilder(TSharedPtr<FUICommandList>(), Extension->GetMenuExtender());

	MenuBarBuilder.AddPullDownMenu(
		LOCTEXT("MenuLabel", "Menu"),
		FText::GetEmpty(),
		FNewMenuDelegate::CreateStatic(&STimingProfilerWindow::FillMenu, TabManager),
		FName(TEXT("Menu"))
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

			// Overlay slot for the main window area
			+ SOverlay::Slot()
				.HAlign(HAlign_Fill)
				.VAlign(VAlign_Fill)
				[
					SNew(SVerticalBox)

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
		];

	// Tell tab-manager about the global menu bar.
	TabManager->SetMenuMultiBox(MenuBarBuilder.GetMultiBox());

	// Tell clients about creation
	TraceInsightsModule.OnMajorTabCreated().Broadcast(FInsightsManagerTabs::TimingProfilerTabId, TabManager.ToSharedRef());
}

END_SLATE_FUNCTION_BUILD_OPTIMIZATION

////////////////////////////////////////////////////////////////////////////////////////////////////

void STimingProfilerWindow::FillMenu(FMenuBuilder& MenuBuilder, const TSharedPtr<FTabManager> TabManager)
{
	if (!TabManager.IsValid())
	{
		return;
	}

	FInsightsManager::Get()->GetInsightsMenuBuilder()->PopulateMenu(MenuBuilder);

	TabManager->PopulateLocalTabSpawnerMenu(MenuBuilder);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STimingProfilerWindow::ShowTab(const FName& TabID)
{
	if (TabManager->HasTabSpawner(TabID))
	{
		TabManager->TryInvokeTab(TabID);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STimingProfilerWindow::HideTab(const FName& TabID)
{
	TSharedPtr<SDockTab> Tab = TabManager->FindExistingLiveTab(TabID);
	if (Tab.IsValid())
	{
		Tab->RequestCloseTab();
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

EVisibility STimingProfilerWindow::IsSessionOverlayVisible() const
{
	if (FInsightsManager::Get()->GetSession().IsValid())
	{
		return EVisibility::Hidden;
	}
	else
	{
		return EVisibility::Visible;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool STimingProfilerWindow::IsProfilerEnabled() const
{
	return FInsightsManager::Get()->GetSession().IsValid();
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

#undef LOCTEXT_NAMESPACE
