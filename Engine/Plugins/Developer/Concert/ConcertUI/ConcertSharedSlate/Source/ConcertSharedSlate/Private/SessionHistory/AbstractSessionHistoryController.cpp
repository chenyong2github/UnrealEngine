// Copyright Epic Games, Inc. All Rights Reserved.

#include "SessionHistory/AbstractSessionHistoryController.h"

#include "SessionHistory/SSessionHistory.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

FAbstractSessionHistoryController::FAbstractSessionHistoryController(FName PackageFilter)
	: SessionHistory(MakeSessionHistory(PackageFilter))
{}

void FAbstractSessionHistoryController::ReloadActivities()
{
	constexpr int64 MaximumNumberOfActivities = SSessionHistory::MaximumNumberOfActivities;
	TMap<FGuid, FConcertClientInfo> EndpointClientInfoMap;
	TArray<FConcertSessionActivity> FetchedActivities;
	GetActivities(MaximumNumberOfActivities, EndpointClientInfoMap, FetchedActivities);

	// MoveTemp strictly not needed - but it will be faster on Debug builds
	GetSessionHistory()->ReloadActivities(
		MoveTemp(EndpointClientInfoMap),
		MoveTemp(FetchedActivities)
		);
}

TSharedRef<SSessionHistory> FAbstractSessionHistoryController::MakeSessionHistory(FName PackageFilter) const
{
	return SNew(SSessionHistory)
		.PackageFilter(PackageFilter)
		.GetPackageEvent([this](const FConcertSessionActivity& Activity, FConcertSyncPackageEventMetaData& OutPackageEvent) { return GetPackageEvent(Activity, OutPackageEvent); })
		.GetTransactionEvent([this](const FConcertSessionActivity& Activity) { return GetTransactionEvent(Activity); });
}
