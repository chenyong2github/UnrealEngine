// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/SCompoundWidget.h"
#include "HordeBuildRowInfo.h"

class SGameSyncTab : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SGameSyncTab) {}
		SLATE_ARGUMENT(TArray<TSharedPtr<HordeBuildRowInfo>>, HordeBuilds) // Todo: remove once we start getting real horde data
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	TSharedRef<ITableRow> GenerateHordeBuildTableRow(TSharedPtr<HordeBuildRowInfo> InItem, const TSharedRef<STableViewBase>& InOwnerTable);

	TSharedPtr<SListView<TSharedPtr<HordeBuildRowInfo>>> HordeBuildsView; 
	TArray<TSharedPtr<HordeBuildRowInfo>> HordeBuilds;

	static constexpr float HordeBuildRowHorizontalPadding = 10.0f;
	static constexpr float HordeBuildRowVerticalPadding = 2.5f;
	static constexpr float HordeBuildRowExtraIconPadding = 10.0f;
};
