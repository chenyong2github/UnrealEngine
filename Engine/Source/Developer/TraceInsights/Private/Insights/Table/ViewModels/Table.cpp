// Copyright Epic Games, Inc. All Rights Reserved.

#include "Table.h"

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

void FTable::AddHierarchyColumn(int32 ColumnIndex, const TCHAR* ColumnName)
{
	const FName HierarchyColumnId(TEXT("_Hierarchy"));

	TSharedRef<FTableColumn> ColumnRef = MakeShared<FTableColumn>(HierarchyColumnId);
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

} // namespace Insights

#undef LOCTEXT_NAMESPACE
