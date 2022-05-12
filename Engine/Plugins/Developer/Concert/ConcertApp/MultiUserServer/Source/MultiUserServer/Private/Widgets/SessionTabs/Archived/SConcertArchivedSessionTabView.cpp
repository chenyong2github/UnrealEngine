// Copyright Epic Games, Inc. All Rights Reserved.

#include "SConcertArchivedSessionTabView.h"

#include "Framework/Docking/TabManager.h"
#include "Session/History/SSessionHistory.h"
#include "Widgets/Docking/SDockTab.h"

#define LOCTEXT_NAMESPACE "SConcertArchivedSessionInspector"

const FName SConcertArchivedSessionTabView::HistoryTabId("HistoryTabId");

void SConcertArchivedSessionTabView::Construct(const FArguments& InArgs, FName InStatusBarID)
{
	check(InArgs._MakeSessionHistory.IsBound() && InArgs._CanDeleteActivity.IsBound());
	SConcertTabViewWithManagerBase::Construct(
		SConcertTabViewWithManagerBase::FArguments()
		.ConstructUnderWindow(InArgs._ConstructUnderWindow)
		.ConstructUnderMajorTab(InArgs._ConstructUnderMajorTab)
		.CreateTabs(FCreateTabs::CreateLambda([this, &InArgs](const TSharedRef<FTabManager>& InTabManager, const TSharedRef<FTabManager::FLayout>& InLayout)
		{
			CreateTabs(InTabManager, InLayout, InArgs);
		}))
		.LayoutName("ConcertArchivedSessionInspector_v0.1"),
		InStatusBarID
	);
}

void SConcertArchivedSessionTabView::CreateTabs(const TSharedRef<FTabManager>& InTabManager, const TSharedRef<FTabManager::FLayout>& InLayout, const FArguments& InArgs)
{
	InTabManager->RegisterTabSpawner(HistoryTabId, FOnSpawnTab::CreateSP(this, &SConcertArchivedSessionTabView::SpawnActivityHistory, InArgs._MakeSessionHistory, InArgs._CanDeleteActivity, InArgs._DeleteActivity))
		.SetDisplayName(LOCTEXT("ActivityHistoryLabel", "History"));
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
		);
}

TSharedRef<SDockTab> SConcertArchivedSessionTabView::SpawnActivityHistory(
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
