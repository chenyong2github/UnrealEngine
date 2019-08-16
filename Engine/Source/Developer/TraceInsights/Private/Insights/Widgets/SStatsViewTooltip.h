// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EditorStyleSet.h"
#include "SlateOptMacros.h"
#include "TraceServices/AnalysisService.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/SToolTip.h"
#include "Widgets/Text/STextBlock.h"

// Insights
#include "Insights/InsightsManager.h"

// Insights
class FStatsNode;
class FStatsViewColumn;

#define LOCTEXT_NAMESPACE "SStatsView"

/** Stats View Tooltip */
class SStatsViewTooltip
{
	const uint32 StatsId;
	TSharedPtr<const Trace::IAnalysisSession> Session;

public:
	SStatsViewTooltip(const uint32 InStatsId)
		: StatsId(InStatsId)
	{
		Session = FInsightsManager::Get()->GetSession();
	}

	TSharedRef<SToolTip> GetTooltip();

protected:
	void AddNoDataInformation(const TSharedRef<SGridPanel>& Grid, int32& RowPos);
	void AddHeader(const TSharedRef<SGridPanel>& Grid, int32& RowPos);
	void AddDescription(const TSharedRef<SGridPanel>& Grid, int32& RowPos);
	void AddSeparator(const TSharedRef<SGridPanel>& Grid, int32& RowPos);

public:
	static TSharedPtr<SToolTip> GetColumnTooltip(const FStatsViewColumn& Column);
	static TSharedPtr<SToolTip> GetTableCellTooltip(const TSharedPtr<FStatsNode> StatsNodePtr);

protected:
	static void AddAggregatedStatsRow(TSharedPtr<SGridPanel> Grid, int32& Row, const FText& Name, const FText& Value);
};

#undef LOCTEXT_NAMESPACE
