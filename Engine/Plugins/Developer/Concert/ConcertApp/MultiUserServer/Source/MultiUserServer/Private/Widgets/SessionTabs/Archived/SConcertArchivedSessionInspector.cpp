// Copyright Epic Games, Inc. All Rights Reserved.

#include "SConcertArchivedSessionInspector.h"

#include "Framework/Docking/TabManager.h"
#include "Session/History/SSessionHistory.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "Styling/AppStyle.h"

#define LOCTEXT_NAMESPACE "SConcertArchivedSessionInspector"

const FName SConcertArchivedSessionInspector::HistoryTabId("HistoryTabId");

void SConcertArchivedSessionInspector::Construct(const FArguments& InArgs)
{
	check(InArgs._ConstructUnderWindow && InArgs._ConstructUnderMajorTab && InArgs._MakeSessionHistory.IsBound() && InArgs._CanDeleteActivity.IsBound());
	
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
					CreateTabs(InArgs)
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

TSharedRef<SWidget> SConcertArchivedSessionInspector::CreateTabs(const FArguments& InArgs)
{
	TabManager = FGlobalTabmanager::Get()->NewTabManager(InArgs._ConstructUnderMajorTab.ToSharedRef());
	
	TabManager->RegisterTabSpawner(HistoryTabId, FOnSpawnTab::CreateSP(this, &SConcertArchivedSessionInspector::SpawnActivityHistory, InArgs._MakeSessionHistory, InArgs._CanDeleteActivity, InArgs._DeleteActivity))
		.SetDisplayName(LOCTEXT("ActivityHistoryLabel", "History"));

	const TSharedRef<FTabManager::FLayout> Layout = FTabManager::NewLayout("ConcertArchivedSessionLayout_v0.1")
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
		);

	return TabManager->RestoreFrom(Layout, InArgs._ConstructUnderWindow).ToSharedRef();
}

TSharedRef<SDockTab> SConcertArchivedSessionInspector::SpawnActivityHistory(
	const FSpawnTabArgs& Args,
	SEditableSessionHistory::FMakeSessionHistory MakeSessionHistory,
	SEditableSessionHistory::FCanDeleteActivities CanDeleteActivity,
	SEditableSessionHistory::FRequestDeleteActivities DeleteActivity)
{
	SessionHistory = SNew(SEditableSessionHistory)
		.MakeSessionHistory(MoveTemp(MakeSessionHistory))
		.CanDeleteActivity(MoveTemp(CanDeleteActivity))
		.DeleteActivity(MoveTemp(DeleteActivity));
	return SNew(SDockTab)
		.Label(LOCTEXT("ActivityHistoryLabel", "History"))
		.TabRole(PanelTab)
		[
			SessionHistory.ToSharedRef()
		]; 
}

#undef LOCTEXT_NAMESPACE
