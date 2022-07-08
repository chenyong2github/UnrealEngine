// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/SCompoundWidget.h"
#include "HordeBuildRowInfo.h"

class UGSTab;
class SLogWidget;

class SGameSyncTab : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SGameSyncTab) {}
		SLATE_ARGUMENT(TArray<TSharedPtr<HordeBuildRowInfo>>, HordeBuilds) // Todo: remove once we start getting real horde data
		SLATE_ARGUMENT(UGSTab*, Tab)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	// We need access to the SyncLog when creating the Workspace
	// TODO: think of a better way to do this
	TSharedPtr<SLogWidget> GetSyncLog() const;
	void SetSyncLogLocation(const FString& LogFileName);

private:
	TSharedRef<ITableRow> GenerateHordeBuildTableRow(TSharedPtr<HordeBuildRowInfo> InItem, const TSharedRef<STableViewBase>& InOwnerTable);

	// Button callbacks
	TSharedRef<SWidget> MakeSyncButtonDropdown();

	TSharedPtr<SListView<TSharedPtr<HordeBuildRowInfo>>> HordeBuildsView;
	TArray<TSharedPtr<HordeBuildRowInfo>> HordeBuilds;

	TSharedPtr<SLogWidget> SyncLog;

	static constexpr float HordeBuildRowHorizontalPadding = 10.0f;
	static constexpr float HordeBuildRowVerticalPadding = 2.5f;
	static constexpr float HordeBuildRowExtraIconPadding = 10.0f;

	UGSTab* Tab;
};
