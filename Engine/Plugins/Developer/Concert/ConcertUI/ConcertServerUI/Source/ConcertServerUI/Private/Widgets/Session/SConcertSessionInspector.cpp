// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/Session/SConcertSessionInspector.h"

#include "Framework/Docking/TabManager.h"
#include "Session/History/SSessionHistory.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"

#define LOCTEXT_NAMESPACE "SConcertSessionInspector"

const FName SConcertSessionInspector::HistoryTabId("HistoryTabId");
const FName SConcertSessionInspector::SessionContentTabId("SessionContentTabId");
const FName SConcertSessionInspector::ConnectionMonitorTabId("ConnectionMonitorTabId");

void SConcertSessionInspector::Construct(const FArguments& InArgs, const FRequiredArgs& RequiredArgs)
{
	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
		.Padding(FMargin(1.0f, 2.0f))
		[
			SNew(SVerticalBox)

			// Content
			+SVerticalBox::Slot()
			.FillHeight(1.f)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::Get().GetBrush("Brushes.Title"))
				.Padding(FMargin(0.f, 0.f, 0.f, 5.0f)) // Visually separate status bar from tabs
				[
					CreateTabs(InArgs, RequiredArgs)
				]
			]

			// Status bar
			+SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Fill)
			[
				InArgs._StatusBar.Widget
			]
		]
	];
}

TSharedRef<SWidget> SConcertSessionInspector::CreateTabs(const FArguments& InArgs, const FRequiredArgs& RequiredArgs)
{
	TabManager = FGlobalTabmanager::Get()->NewTabManager(RequiredArgs.ConstructUnderMajorTab);
	
	TabManager->RegisterTabSpawner(HistoryTabId, FOnSpawnTab::CreateSP(this, &SConcertSessionInspector::SpawnActivityHistory, RequiredArgs.SessionHistory))
		.SetDisplayName(LOCTEXT("ActivityHistoryLabel", "History"));
	
	TabManager->RegisterTabSpawner(SessionContentTabId, FOnSpawnTab::CreateSP(this, &SConcertSessionInspector::SpawnSessionContent, RequiredArgs.PackageViewer))
		.SetDisplayName(LOCTEXT("SessionContentLabel", "Session Content"));
	
	TabManager->RegisterTabSpawner(ConnectionMonitorTabId, FOnSpawnTab::CreateSP(this, &SConcertSessionInspector::SpawnConnectionMonitor))
		.SetDisplayName(LOCTEXT("ConnectionMonitorLabel", "Connection Monitor"));

	const TSharedRef<FTabManager::FLayout> Layout = FTabManager::NewLayout("ConcertSessionLayout_v0.3")
		->AddArea
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

	return TabManager->RestoreFrom(Layout, RequiredArgs.ConstructUnderWindow).ToSharedRef();
}

TSharedRef<SDockTab> SConcertSessionInspector::SpawnActivityHistory(const FSpawnTabArgs& Args, TSharedRef<SSessionHistory> SessionHistory)
{
	return SNew(SDockTab)
		.Label(LOCTEXT("ActivityHistoryLabel", "History"))
		.TabRole(PanelTab)
		[
			SessionHistory
		];
}

TSharedRef<SDockTab> SConcertSessionInspector::SpawnSessionContent(const FSpawnTabArgs& Args, TSharedRef<SConcertSessionPackageViewer> PackageViewer)
{
	return SNew(SDockTab)
		.Label(LOCTEXT("SessionContentLabel", "Session Content"))
		.TabRole(PanelTab)
		[
			PackageViewer
		];
}

TSharedRef<SDockTab> SConcertSessionInspector::SpawnConnectionMonitor(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.Label(LOCTEXT("ConnectionMonitorLabel", "Connection Monitor"))
		.TabRole(PanelTab)
		[
			SNullWidget::NullWidget
		];
}

#undef LOCTEXT_NAMESPACE
