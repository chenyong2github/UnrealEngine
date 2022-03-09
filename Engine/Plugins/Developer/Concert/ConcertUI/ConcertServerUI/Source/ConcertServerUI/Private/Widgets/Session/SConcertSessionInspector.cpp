// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/Session/SConcertSessionInspector.h"

#include "Framework/Docking/TabManager.h"
#include "SessionHistory/SSessionHistory.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/SWindow.h"

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

			// Toolbar
			+SVerticalBox::Slot()
			.AutoHeight()
			[
				SNullWidget::NullWidget
			]

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
	
	TabManager->RegisterTabSpawner(HistoryTabId, FOnSpawnTab::CreateSP(this, &SConcertSessionInspector::SpawnActivityHistory, RequiredArgs.SessionHistoryController))
		.SetDisplayName(LOCTEXT("ActivityHistoryLabel", "History"));
	
	TabManager->RegisterTabSpawner(SessionContentTabId, FOnSpawnTab::CreateSP(this, &SConcertSessionInspector::SpawnSessionContent))
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

TSharedRef<SDockTab> SConcertSessionInspector::SpawnActivityHistory(const FSpawnTabArgs& Args, TSharedRef<FServerSessionHistoryController> SessionHistoryController)
{
	return SNew(SDockTab)
		.Label(LOCTEXT("ActivityHistoryLabel", "History"))
		.TabRole(PanelTab)
		[
			SessionHistoryController->GetSessionHistory()
		];
}

TSharedRef<SDockTab> SConcertSessionInspector::SpawnSessionContent(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.Label(LOCTEXT("SessionContentLabel", "Session Content"))
		.TabRole(PanelTab)
		[
			SNullWidget::NullWidget
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
