// Copyright Epic Games, Inc. All Rights Reserved.

#include "SConcertLiveSessionTabView.h"

#include "Framework/Docking/TabManager.h"
#include "Session/History/SSessionHistory.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"

#define LOCTEXT_NAMESPACE "SConcertSessionInspector"

const FName SConcertLiveSessionTabView::HistoryTabId("HistoryTabId");
const FName SConcertLiveSessionTabView::SessionContentTabId("SessionContentTabId");
const FName SConcertLiveSessionTabView::ConnectionMonitorTabId("ConnectionMonitorTabId");

void SConcertLiveSessionTabView::Construct(const FArguments& InArgs, const FRequiredWidgets& InRequiredArgs, FName StatusBarId)
{
	SConcertTabViewWithManagerBase::Construct(
		SConcertTabViewWithManagerBase::FArguments()
		.ConstructUnderWindow(InRequiredArgs.ConstructUnderWindow)
		.ConstructUnderMajorTab(InRequiredArgs.ConstructUnderMajorTab)
		.CreateTabs(FCreateTabs::CreateLambda([this, &InRequiredArgs](const TSharedRef<FTabManager>& InTabManager, const TSharedRef<FTabManager::FLayout>& InLayout)
		{
			CreateTabs(InTabManager, InLayout, InRequiredArgs);
		}))
		.LayoutName("ConcertSessionInspector_v0.1"),
		StatusBarId
		);
}

TSharedRef<SWidget> SConcertLiveSessionTabView::CreateTabs(const TSharedRef<FTabManager>& InTabManager, const TSharedRef<FTabManager::FLayout>& InLayout, const FRequiredWidgets& InRequiredArgs)
{
	InTabManager->RegisterTabSpawner(HistoryTabId, FOnSpawnTab::CreateSP(this, &SConcertLiveSessionTabView::SpawnActivityHistory, InRequiredArgs.SessionHistory))
		.SetDisplayName(LOCTEXT("ActivityHistoryLabel", "History"));
	
	InTabManager->RegisterTabSpawner(SessionContentTabId, FOnSpawnTab::CreateSP(this, &SConcertLiveSessionTabView::SpawnSessionContent, InRequiredArgs.PackageViewer))
		.SetDisplayName(LOCTEXT("SessionContentLabel", "Session Content"));
	
	InTabManager->RegisterTabSpawner(ConnectionMonitorTabId, FOnSpawnTab::CreateSP(this, &SConcertLiveSessionTabView::SpawnConnectionMonitor))
		.SetDisplayName(LOCTEXT("ConnectionMonitorLabel", "Connection Monitor"));

	InLayout->AddArea
	(
		FTabManager::NewPrimaryArea()
			->SetOrientation(Orient_Horizontal)
			->Split
			(
				FTabManager::NewStack()
				->SetSizeCoefficient(0.5f)
				->AddTab(HistoryTabId, ETabState::OpenedTab)
			)
			->Split
			(
				FTabManager::NewSplitter()
				->SetSizeCoefficient(0.5f)
				->SetOrientation(Orient_Vertical)
				->Split
				(
					FTabManager::NewStack()
					->SetSizeCoefficient(0.5f)
					->AddTab(SessionContentTabId, ETabState::OpenedTab)
				)
				->Split
				(
					FTabManager::NewStack()
					->SetSizeCoefficient(0.5f)
					->AddTab(ConnectionMonitorTabId, ETabState::OpenedTab)
				)
			)
	);

	return InTabManager->RestoreFrom(InLayout, InRequiredArgs.ConstructUnderWindow).ToSharedRef();
}

TSharedRef<SDockTab> SConcertLiveSessionTabView::SpawnActivityHistory(const FSpawnTabArgs& Args, TSharedRef<SSessionHistory> SessionHistory)
{
	return SNew(SDockTab)
		.Label(LOCTEXT("ActivityHistoryLabel", "History"))
		.TabRole(PanelTab)
		[
			SessionHistory
		];
}

TSharedRef<SDockTab> SConcertLiveSessionTabView::SpawnSessionContent(const FSpawnTabArgs& Args, TSharedRef<SConcertSessionPackageViewer> PackageViewer)
{
	return SNew(SDockTab)
		.Label(LOCTEXT("SessionContentLabel", "Session Content"))
		.TabRole(PanelTab)
		[
			PackageViewer
		];
}

TSharedRef<SDockTab> SConcertLiveSessionTabView::SpawnConnectionMonitor(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.Label(LOCTEXT("ConnectionMonitorLabel", "Connection Monitor"))
		.TabRole(PanelTab)
		[
			SNullWidget::NullWidget
		];
}

#undef LOCTEXT_NAMESPACE
