// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Table.h"
#include "TraceServices/Containers/Tables.h"

// Insights
#include "Insights/Table/ViewModels/TableColumn.h"

#define LOCTEXT_NAMESPACE "Insights_Table"

namespace Insights
{

////////////////////////////////////////////////////////////////////////////////////////////////////

FTable::FTable()
	: Name()
	, Description()
	, SourceTable()
	, TableReader()
	, Columns()
	, VisibleColumns()
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
	SourceTable.Reset();
	TableReader.Reset();

	Columns.Reset();
	VisibleColumns.Reset();
	ColumnIdToPtrMapping.Reset();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTable::Init(TSharedPtr<Trace::IUntypedTable> InSourceTable)
{
	Reset();

	SourceTable = InSourceTable;

	if (SourceTable)
	{
		TableReader = MakeShareable(SourceTable->CreateReader());
		CreateColumns();
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool AreTableLayoutsEqual(const Trace::ITableLayout& TableLayoutA, const Trace::ITableLayout& TableLayoutB)
{
	if (TableLayoutA.GetColumnCount() != TableLayoutB.GetColumnCount())
	{
		return false;
	}

	int32 ColumnCount = TableLayoutA.GetColumnCount();
	for (int32 ColumnIndex = 0; ColumnIndex < ColumnCount; ++ColumnIndex)
	{
		if (TableLayoutA.GetColumnType(ColumnIndex) != TableLayoutB.GetColumnType(ColumnIndex))
		{
			return false;
		}
		if (FCString::Strcmp(TableLayoutA.GetColumnName(ColumnIndex), TableLayoutB.GetColumnName(ColumnIndex)) != 0)
		{
			return false;
		}
	}

	return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTable::UpdateSourceTable(TSharedPtr<Trace::IUntypedTable> InSourceTable)
{
	check(InSourceTable.IsValid() && SourceTable.IsValid());
	check(AreTableLayoutsEqual(InSourceTable->GetLayout(), SourceTable->GetLayout()));
	SourceTable = InSourceTable;
	TableReader = MakeShareable(SourceTable->CreateReader());
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTable::CreateHierarchyColumn(const TCHAR* ColumnName)
{
	const FString ColumnNameStr(ColumnName);
	const FText ColumnNameText = FText::FromString(ColumnNameStr);

	constexpr EHorizontalAlignment HorizontalAlignment = HAlign_Left;
	constexpr ETableColumnFlags ColumnFlags = ETableColumnFlags::ShouldBeVisible | ETableColumnFlags::CanBeSorted | ETableColumnFlags::CanBeFiltered | ETableColumnFlags::IsHierarchy;

	constexpr float InitialColumnWidth = 200.0f;
	constexpr float MinColumnWidth = 0.0f;
	constexpr float MaxColumnWidth = FLT_MAX;

	FTableColumn::FGetValueAsTextFunction GetValueAsTextFn = [](const FTable& Table, const FTableColumn& Column, const FTableRowId& RowId)->FText
	{
		return FText();
	};

	TSharedPtr<FTableColumn> ColumnPtr = MakeShareable(new FTableColumn
	(
		-1, // Order
		0,
		FName(ColumnName), // Id
		ColumnNameText, // Short Name
		ColumnNameText, // Title Name
		FText::GetEmpty(), // Description
		ColumnFlags,
		HorizontalAlignment,
		InitialColumnWidth,
		MinColumnWidth,
		MaxColumnWidth,
		GetValueAsTextFn
	));

	AddColumn(ColumnPtr);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTable::AddColumn(TSharedPtr<FTableColumn> ColumnPtr)
{
	ColumnPtr->SetParentTable(SharedThis(this));

	Columns.Add(ColumnPtr);

	if (ColumnPtr->IsVisible())
	{
		VisibleColumns.Add(ColumnPtr);
	}

	ColumnIdToPtrMapping.Add(ColumnPtr->GetId(), ColumnPtr);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTable::CreateColumns()
{
	ensure(TableReader.IsValid());
	ensure(Columns.Num() == 0);

	const TCHAR* HierarchyColumnName = TEXT("Name");

	CreateHierarchyColumn(HierarchyColumnName);

	const Trace::ITableLayout& TableLayout = SourceTable->GetLayout();
	const int32 ColumnCount = TableLayout.GetColumnCount();

	for (int32 ColumnIndex = 0; ColumnIndex < ColumnCount; ++ColumnIndex)
	{
		Trace::ETableColumnType ColumnType = TableLayout.GetColumnType(ColumnIndex);
		const TCHAR* ColumnName = TableLayout.GetColumnName(ColumnIndex);

		if (ColumnType == Trace::TableColumnType_CString && FCString::Stricmp(ColumnName, HierarchyColumnName) == 0)
		{
			// Skip the "Name" column. It is already created as the name / hierarchy column.
			continue;
		}

		const FString ColumnNameStr(ColumnName);
		const FText ColumnNameText = FText::FromString(ColumnNameStr);

		EHorizontalAlignment HorizontalAlignment = HAlign_Left;

		const ETableColumnFlags ColumnFlags = ETableColumnFlags::ShouldBeVisible | ETableColumnFlags::CanBeSorted | ETableColumnFlags::CanBeFiltered | ETableColumnFlags::CanBeHidden;

		float InitialColumnWidth = 60.0f;
		float MinColumnWidth = 0.0f;
		float MaxColumnWidth = FLT_MAX;

		FTableColumn::FGetValueAsTextFunction GetUnknownValueAsTextFn = [](const FTable& Table, const FTableColumn& Column, const FTableRowId& RowId) -> FText
		{
			return LOCTEXT("UnknownValue", "!?");
		};

		FTableColumn::FGetValueAsTextFunction GetBoolValueAsTextFn = [](const FTable& Table, const FTableColumn& Column, const FTableRowId& RowId) -> FText
		{
			TSharedPtr<Trace::IUntypedTableReader> Reader = Table.GetTableReader();
			if (Reader.IsValid() && RowId.HasValidIndex())
			{
				Reader->SetRowIndex(RowId.RowIndex);
				bool Value = Reader->GetValueBool(Column.GetIndex());
				return FText::FromString(Value ? TEXT("True") : TEXT("False"));
			}
			return FText::GetEmpty();
		};

		FTableColumn::FGetValueAsTextFunction GetIntValueAsTextFn = [](const FTable& Table, const FTableColumn& Column, const FTableRowId& RowId) -> FText
		{
			TSharedPtr<Trace::IUntypedTableReader> Reader = Table.GetTableReader();
			if (Reader.IsValid() && RowId.HasValidIndex())
			{
				Reader->SetRowIndex(RowId.RowIndex);
				int64 Value = Reader->GetValueInt(Column.GetIndex());
				return FText::AsNumber(Value);
			}
			return FText::GetEmpty();
		};

		FTableColumn::FGetValueAsTextFunction GetFloatValueAsTextFn = [](const FTable& Table, const FTableColumn& Column, const FTableRowId& RowId) -> FText
		{
			TSharedPtr<Trace::IUntypedTableReader> Reader = Table.GetTableReader();
			if (Reader.IsValid() && RowId.HasValidIndex())
			{
				Reader->SetRowIndex(RowId.RowIndex);
				float Value = Reader->GetValueFloat(Column.GetIndex());
				return FText::AsNumber(Value);
			}
			return FText::GetEmpty();
		};

		FTableColumn::FGetValueAsTextFunction GetDoubleValueAsTextFn = [](const FTable& Table, const FTableColumn& Column, const FTableRowId& RowId) -> FText
		{
			TSharedPtr<Trace::IUntypedTableReader> Reader = Table.GetTableReader();
			if (Reader.IsValid() && RowId.HasValidIndex())
			{
				Reader->SetRowIndex(RowId.RowIndex);
				double Value = Reader->GetValueDouble(Column.GetIndex());
				return FText::AsNumber(Value);
			}
			return FText::GetEmpty();
		};

		FTableColumn::FGetValueAsTextFunction GetStringValueAsTextFn = [](const FTable& Table, const FTableColumn& Column, const FTableRowId& RowId) -> FText
		{
			TSharedPtr<Trace::IUntypedTableReader> Reader = Table.GetTableReader();
			if (Reader.IsValid() && RowId.HasValidIndex())
			{
				Reader->SetRowIndex(RowId.RowIndex);
				const TCHAR* Value = Reader->GetValueCString(Column.GetIndex());
				return FText::FromString(Value);
			}
			return FText::GetEmpty();
		};

		FTableColumn::FGetValueAsTextFunction GetValueAsTextFn = GetUnknownValueAsTextFn;

		switch (ColumnType)
		{
		case Trace::TableColumnType_Bool:
			HorizontalAlignment = HAlign_Right;
			InitialColumnWidth = 40.0f;
			GetValueAsTextFn = GetBoolValueAsTextFn;
			break;
		case Trace::TableColumnType_Int:
			HorizontalAlignment = HAlign_Right;
			InitialColumnWidth = 60.0f;
			GetValueAsTextFn = GetIntValueAsTextFn;
			break;
		case Trace::TableColumnType_Float:
			HorizontalAlignment = HAlign_Right;
			InitialColumnWidth = 60.0f;
			GetValueAsTextFn = GetFloatValueAsTextFn;
			break;
		case Trace::TableColumnType_Double:
			HorizontalAlignment = HAlign_Right;
			InitialColumnWidth = 80.0f;
			GetValueAsTextFn = GetDoubleValueAsTextFn;
			break;
		case Trace::TableColumnType_CString:
			HorizontalAlignment = HAlign_Left;
			InitialColumnWidth = FMath::Max(120.0f, 6.0f * ColumnNameStr.Len());
			GetValueAsTextFn = GetStringValueAsTextFn;
			break;
		}

		TSharedPtr<FTableColumn> ColumnPtr = MakeShareable(new FTableColumn
		(
			ColumnIndex, // Order
			ColumnIndex,
			FName(ColumnName), // Id
			ColumnNameText, // Short Name
			ColumnNameText, // Title Name
			FText::GetEmpty(), // Description
			ColumnFlags,
			HorizontalAlignment,
			InitialColumnWidth,
			MinColumnWidth,
			MaxColumnWidth,
			GetValueAsTextFn
		));

		AddColumn(ColumnPtr);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace Insights

#undef LOCTEXT_NAMESPACE
