// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/IToolTip.h"
#include "Widgets/SToolTip.h"

// Insights
#include "Insights/InsightsManager.h"

class SGridPanel;

namespace Insights
{
	//TODO: class FTable;
	//TODO: class FTableColumn;
}

class FStatsNode;
class FStatsViewColumn;

#define LOCTEXT_NAMESPACE "SStatsView"

////////////////////////////////////////////////////////////////////////////////////////////////////

/** Stats Counters View Tooltip */
class SStatsViewTooltip
{
public:
	SStatsViewTooltip() = delete;

	//TODO: static TSharedPtr<SToolTip> GetTableTooltip(const Insights::FTable& Table);
	//TODO: static TSharedPtr<SToolTip> GetColumnTooltip(const Insights::FTableColumn& Column);
	static TSharedPtr<SToolTip> GetColumnTooltip(const FStatsViewColumn& Column);
	static TSharedPtr<SToolTip> GetRowTooltip(const TSharedPtr<FStatsNode> StatsNodePtr);

private:
	static void AddAggregatedStatsRow(TSharedPtr<SGridPanel> Grid, int32& Row, const FText& Name, const FText& Value);
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class SStatsCounterTableRowToolTip : public IToolTip
{
public:
	SStatsCounterTableRowToolTip(const TSharedPtr<FStatsNode> InTreeNodePtr) : TreeNodePtr(InTreeNodePtr) {}
	virtual ~SStatsCounterTableRowToolTip() { }

	virtual TSharedRef<class SWidget> AsWidget()
	{
		CreateToolTipWidget();
		return ToolTipWidget.ToSharedRef();
	}

	virtual TSharedRef<SWidget> GetContentWidget()
	{
		CreateToolTipWidget();
		return ToolTipWidget->GetContentWidget();
	}

	virtual void SetContentWidget(const TSharedRef<SWidget>& InContentWidget)
	{
		CreateToolTipWidget();
		ToolTipWidget->SetContentWidget(InContentWidget);
	}

	void InvalidateWidget()
	{
		ToolTipWidget.Reset();
	}

	virtual bool IsEmpty() const { return false; }
	virtual bool IsInteractive() const { return false; }
	virtual void OnOpening() {}
	virtual void OnClosed() {}

private:
	void CreateToolTipWidget()
	{
		if (!ToolTipWidget.IsValid())
		{
			ToolTipWidget = SStatsViewTooltip::GetRowTooltip(TreeNodePtr);
		}
	}

private:
	TSharedPtr<SToolTip> ToolTipWidget;
	const TSharedPtr<FStatsNode> TreeNodePtr;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
