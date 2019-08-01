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

namespace Insights
{

class FTable;
class FTableColumn;
class FTableTreeNode;

/** Tooltip for STableTreeView widget. */
class STableTreeViewTooltip
{
public:
	STableTreeViewTooltip() = delete;

	static TSharedPtr<SToolTip> GetTableTooltip(const FTable& Table);
	static TSharedPtr<SToolTip> GetColumnTooltip(const FTableColumn& Column);
	static TSharedPtr<SToolTip> GetCellTooltip(const TSharedPtr<FTableTreeNode> TreeNodePtr, const TSharedPtr<FTableColumn> ColumnPtr);
};

} // namespace Insights
