// Copyright Epic Games, Inc. All Rights Reserved.

#include "TableColumn.h"

#include "Insights/Table/ViewModels/TableCellValueFormatter.h"
#include "Insights/Table/ViewModels/TableCellValueGetter.h"
#include "Insights/Table/ViewModels/TableCellValueSorter.h"

#define LOCTEXT_NAMESPACE "TableColumn"

namespace Insights
{

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<ITableCellValueGetter> FTableColumn::GetDefaultValueGetter()
{
	return MakeShared<FTableCellValueGetter>();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

const TOptional<FTableCellValue> FTableColumn::GetValue(const FBaseTreeNode& InNode) const
{
	return ValueGetter->GetValue(*this, InNode);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<ITableCellValueFormatter> FTableColumn::GetDefaultValueFormatter()
{
	return MakeShared<FTableCellValueFormatter>();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FText FTableColumn::GetValueAsText(const FBaseTreeNode& InNode) const
{
	return ValueFormatter->FormatValue(*this, InNode);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FText FTableColumn::GetValueAsTooltipText(const FBaseTreeNode& InNode) const
{
	return ValueFormatter->FormatValueForTooltip(*this, InNode);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace Insights

#undef LOCTEXT_NAMESPACE
