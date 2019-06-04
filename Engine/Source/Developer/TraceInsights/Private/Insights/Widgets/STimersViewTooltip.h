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
class FTimerNode;
class FTimersViewColumn;

#define LOCTEXT_NAMESPACE "STimersView"

/** Timers View Tooltip */
class STimersViewTooltip
{
	const uint32 TimerId;
	TSharedPtr<const Trace::IAnalysisSession> Session;

public:
	STimersViewTooltip(const uint32 InTimerId)
		: TimerId(InTimerId)
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
	static TSharedPtr<SToolTip> GetColumnTooltip(const FTimersViewColumn& Column);
	static TSharedPtr<SToolTip> GetTableCellTooltip(const TSharedPtr<FTimerNode> TimerNodePtr);

protected:
	static void AddStatsRow(TSharedPtr<SGridPanel> Grid, int32& Row, const FText& Name, const FText& Value1, const FText& Value2);
};

#undef LOCTEXT_NAMESPACE
