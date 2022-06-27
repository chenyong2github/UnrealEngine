// Copyright Epic Games, Inc. All Rights Reserved.

#include "SActivityDependencyView.h"

#include "HistoryEdition/HistoryAnalysis.h"

#define LOCTEXT_NAMESPACE "UnrealMultiUserUI"

namespace UE::MultiUserServer
{
	const FName CustomActivityColumnID("CustomActivityColumn");
	
	static FActivityColumn CustomDependencyColumn(const SActivityDependencyView::FCreateActivityColumnWidget& CreateActivityColumnWidget, const FText& CustomActivityColumnLabel)
	{
		return FActivityColumn(
			SHeaderRow::Column(CustomActivityColumnID)
				.DefaultLabel(CustomActivityColumnLabel)
				.FixedWidth(20)
				.ShouldGenerateWidget(true)
			)
			.ColumnSortOrder(static_cast<int32>(ConcertSharedSlate::ActivityColumn::EPredefinedColumnOrder::AvatarColor))
			.GenerateColumnWidget(FActivityColumn::FGenerateColumnWidget::CreateLambda([CreateActivityColumnWidget](const TSharedRef<SConcertSessionActivities>& Owner, const TSharedRef<FConcertSessionActivity>& Activity, SOverlay::FScopedWidgetSlotArguments& Slot)
			{
				Slot
				.VAlign(VAlign_Center)
				[
					CreateActivityColumnWidget.Execute(Activity->Activity.ActivityId)
				];
			}));
	}
}

void SActivityDependencyView::Construct(const FArguments& InArgs, const UE::ConcertSyncCore::FHistoryAnalysisResult& DeletionRequirements)
{
	check(InArgs._CreateSessionHistory.IsBound());

	TSharedPtr<FConcertSessionActivitiesOptions> ViewOptions = MakeShared<FConcertSessionActivitiesOptions>();
	ViewOptions->bEnableConnectionActivityFiltering = false;
	ViewOptions->bEnableLockActivityFiltering = false;
	ViewOptions->bEnableIgnoredActivityFiltering = false;

	TArray<FActivityColumn> Columns { UE::ConcertSharedSlate::ActivityColumn::Operation() };
	if (InArgs._CreateActivityColumnWidget.IsBound())
	{
		Columns.Add(UE::MultiUserServer::CustomDependencyColumn(InArgs._CreateActivityColumnWidget, InArgs._CustomActivityColumnLabel));
	}
	
	ChildSlot
	[
		InArgs._CreateSessionHistory.Execute(
			SSessionHistory::FArguments()
				.AllowActivity_Lambda([DeletionRequirements](const FConcertSyncActivity& Activity, const TStructOnScope<FConcertSyncActivitySummary>&)
				{
					return DeletionRequirements.HardDependencies.Contains(Activity.ActivityId) || DeletionRequirements.PossibleDependencies.Contains(Activity.ActivityId);
				})
				.Columns(Columns)
				.ViewOptions(ViewOptions)
				.DetailsAreaVisibility(EVisibility::Collapsed)
			)
	];
}

#undef LOCTEXT_NAMESPACE 
