// Copyright Epic Games, Inc. All Rights Reserved.

#include "Table.h"

#include "Logging/MessageLog.h"

// Insights
#include "Insights/Table/ViewModels/TableCellValueFormatter.h"
#include "Insights/Table/ViewModels/TableCellValueGetter.h"
#include "Insights/Table/ViewModels/TableCellValueSorter.h"
#include "Insights/Table/ViewModels/TableColumn.h"

#define LOCTEXT_NAMESPACE "Insights::FTable"

namespace Insights
{

////////////////////////////////////////////////////////////////////////////////////////////////////
// FTable
////////////////////////////////////////////////////////////////////////////////////////////////////

FTable::FTable()
	: Name()
	, Description()
	, Columns()
	, ColumnIdToPtrMapping()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FTable::~FTable()
{
	Reset();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTable::Reset()
{
	Columns.Reset();
	ColumnIdToPtrMapping.Reset();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

int32 FTable::GetColumnPositionIndex(const FName& ColumnId) const
{
	return Columns.IndexOfByPredicate([&ColumnId](const TSharedRef<FTableColumn>& ColumnRef) -> bool { return ColumnRef->GetId() == ColumnId; });
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTable::SetColumns(const TArray<TSharedRef<Insights::FTableColumn>>& InColumns)
{
	Columns.Reset(InColumns.Num());
	ColumnIdToPtrMapping.Reset();
	for (TSharedRef<Insights::FTableColumn> ColumnRef : InColumns)
	{
		AddColumn(ColumnRef);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTable::AddColumn(TSharedRef<FTableColumn> ColumnRef)
{
	ColumnRef->SetParentTable(SharedThis(this));
	Columns.Add(ColumnRef);
	ColumnIdToPtrMapping.Add(ColumnRef->GetId(), ColumnRef);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

const FName FTable::GetHierarchyColumnId()
{
	static const FName HierarchyColumnId(TEXT("_Hierarchy"));
	return HierarchyColumnId;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTable::AddHierarchyColumn(int32 ColumnIndex, const TCHAR* ColumnName)
{
	TSharedRef<FTableColumn> ColumnRef = MakeShared<FTableColumn>(GetHierarchyColumnId());
	FTableColumn& Column = *ColumnRef;

	Column.SetIndex(ColumnIndex);

	const FString ColumnNameStr = ColumnName ? FString::Printf(TEXT("Hierarchy (%s)"), ColumnName) : TEXT("Hierarchy");
	const FText ColumnNameText = FText::FromString(ColumnNameStr);

	Column.SetShortName(ColumnNameText);
	Column.SetTitleName(ColumnNameText);
	//TODO: Column.SetDescription(...);

	Column.SetFlags(ETableColumnFlags::ShouldBeVisible | ETableColumnFlags::CanBeFiltered | ETableColumnFlags::IsHierarchy);

	Column.SetHorizontalAlignment(HAlign_Left);
	Column.SetInitialWidth(90.0f);

	Column.SetDataType(ETableCellDataType::CString);

	TSharedRef<ITableCellValueGetter> Getter = MakeShared<FDisplayNameValueGetter>();
	Column.SetValueGetter(Getter);

	TSharedRef<ITableCellValueFormatter> Formatter = MakeShared<FTextValueFormatter>();
	Column.SetValueFormatter(Formatter);

	TSharedRef<ITableCellValueSorter> Sorter = MakeShared<FSorterByName>(ColumnRef);
	Column.SetValueSorter(Sorter);

	AddColumn(ColumnRef);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTable::GetVisibleColumns(TArray<TSharedRef<FTableColumn>>& InArray) const
{
	for (const TSharedRef<FTableColumn>& Column : Columns)
	{
		if (Column->IsVisible())
		{
			InArray.Add(Column);
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTable::GetVisibleColumnsData(const TArray<Insights::FBaseTreeNodePtr>& InNodes, const FName& LogListingName, FString& OutData) const
{
	constexpr TCHAR Separator = TEXT('\t');

	TArray<TSharedRef<Insights::FTableColumn>> VisibleColumns;
	GetVisibleColumns(VisibleColumns);

	// Table headers
	for (const TSharedRef<Insights::FTableColumn>& ColumnRef : VisibleColumns)
	{
		OutData += ColumnRef->GetShortName().ToString().ReplaceCharWithEscapedChar() + Separator;
	}

	if (OutData.Len() > 0)
	{
		OutData.RemoveAt(OutData.Len() - 1, 1, false);
		OutData.AppendChar(TEXT('\n'));
	}

	static int32 Max_Rows = 100000;
	int32 NumItems = InNodes.Num();
	if (NumItems > Max_Rows)
	{
		NumItems = Max_Rows;

		FMessageLog ReportMessageLog((LogListingName != NAME_None) ? LogListingName : TEXT("Other"));
		ReportMessageLog.Warning(FText::Format(LOCTEXT("TooManyRows", "Too many rows selected. Only the first {0} will be copied."), NumItems));
		ReportMessageLog.Notify();
	}

	// Selected items
	for (int Index = 0; Index < NumItems; Index++)
	{
		const Insights::FBaseTreeNodePtr& Node = InNodes[Index];
		for (const TSharedRef<Insights::FTableColumn>& ColumnRef : VisibleColumns)
		{
			FText NodeText = ColumnRef->GetValueAsText(*Node);
			OutData += NodeText.ToString().ReplaceCharWithEscapedChar() + Separator;
		}

		if (OutData.Len() > 0)
		{
			OutData.RemoveAt(OutData.Len() - 1, 1, false);
			OutData.AppendChar(TEXT('\n'));
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace Insights

#undef LOCTEXT_NAMESPACE
