// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/SCompoundWidget.h"
#include "HordeBuildRowInfo.h"

class SUnrealGameSyncWindow : public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS( SUnrealGameSyncWindow ) {}
		SLATE_ARGUMENT(TArray<TSharedPtr<HordeBuildRowInfo>>, HordeBuilds)
	SLATE_END_ARGS()

	/**
	 * Constructs the widget.
	 */
	void Construct(const FArguments& InArgs);

private:

	TSharedRef<ITableRow> GenerateHordeBuildTableRow(TSharedPtr<HordeBuildRowInfo> InItem, const TSharedRef<STableViewBase>& InOwnerTable);

	TSharedPtr<SListView<TSharedPtr<HordeBuildRowInfo>>> SHordeBuildsView; 
	TArray<TSharedPtr<HordeBuildRowInfo>> SHordeBuilds;

	static constexpr float HordeBuildRowHorizontalPadding = 10.0f;
	static constexpr float HordeBuildRowVerticalPadding = 2.5f;
	static constexpr float HordeBuildRowExtraIconPadding = 10.0f;
};
