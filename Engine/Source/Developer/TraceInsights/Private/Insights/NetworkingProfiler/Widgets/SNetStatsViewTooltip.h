// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

// Insights
#include "Insights/InsightsManager.h"

class SToolTip;
class SGridPanel;

namespace Insights
{
	class FTable;
	class FTableColumn;
}

class FNetEventNode;

#define LOCTEXT_NAMESPACE "SNetStatsView"

/** NetStats View Tooltip */
class SNetStatsViewTooltip
{
public:
	SNetStatsViewTooltip() = delete;

	static TSharedPtr<SToolTip> GetTableTooltip(const Insights::FTable& Table);
	static TSharedPtr<SToolTip> GetColumnTooltip(const Insights::FTableColumn& Column);
	static TSharedPtr<SToolTip> GetCellTooltip(const TSharedPtr<FNetEventNode> TreeNodePtr, const TSharedPtr<Insights::FTableColumn> ColumnPtr);

private:
	static void AddStatsRow(TSharedPtr<SGridPanel> Grid, int32& Row, const FText& Name, const FText& Value1, const FText& Value2);
};

#undef LOCTEXT_NAMESPACE
