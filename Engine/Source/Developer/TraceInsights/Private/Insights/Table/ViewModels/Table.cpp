// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Table.h"
#include "TraceServices/Containers/Tables.h"

// Insights
#include "Insights/Common/TimeUtils.h"
#include "Insights/Table/ViewModels/TableColumn.h"

#define LOCTEXT_NAMESPACE "Insights_Table"

namespace Insights
{

////////////////////////////////////////////////////////////////////////////////////////////////////
// FTableCellValueFormatter
////////////////////////////////////////////////////////////////////////////////////////////////////

class FBoolValueFormatter : public FTableCellValueFormatter
{
public:
	virtual FText FormatValue(const TOptional<FTableCellValue>& InValue) override
	{
		if (InValue.IsSet())
		{
			return FormatBoolValue(InValue.GetValue().Bool);
		}
		return FText::GetEmpty();
	}

	virtual FText FormatValue(const FTable& Table, const FTableColumn& Column, const FTableRowId& RowId) override
	{
		TSharedPtr<Trace::IUntypedTableReader> Reader = Table.GetTableReader();
		if (Reader.IsValid() && RowId.HasValidIndex())
		{
			Reader->SetRowIndex(RowId.RowIndex);
			const bool Value = Reader->GetValueBool(Column.GetIndex());
			return FormatBoolValue(Value);
		}
		return FText::GetEmpty();
	}

private:
	FText FormatBoolValue(bool Value)
	{
		return FText::FromString(Value ? TEXT("True") : TEXT("False"));
	}
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class FInt64ValueFormatter : public FTableCellValueFormatter
{
public:
	virtual FText FormatValue(const TOptional<FTableCellValue>& InValue) override
	{
		if (InValue.IsSet())
		{
			return FormatInt64Value(InValue.GetValue().Int64);
		}
		return FText::GetEmpty();
	}

	virtual FText FormatValue(const FTable& Table, const FTableColumn& Column, const FTableRowId& RowId) override
	{
		TSharedPtr<Trace::IUntypedTableReader> Reader = Table.GetTableReader();
		if (Reader.IsValid() && RowId.HasValidIndex())
		{
			Reader->SetRowIndex(RowId.RowIndex);
			const int64 Value = Reader->GetValueInt(Column.GetIndex());
			return FormatInt64Value(Value);
		}
		return FText::GetEmpty();
	}

private:
	FText FormatInt64Value(int64 Value)
	{
		return FText::AsNumber(Value);
	}
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class FFloatValueFormatter : public FTableCellValueFormatter
{
public:
	virtual FText FormatValue(const TOptional<FTableCellValue>& InValue) override
	{
		if (InValue.IsSet())
		{
			return FormatFloatValue(InValue.GetValue().Float);
		}
		return FText::GetEmpty();
	}

	virtual FText FormatValue(const FTable& Table, const FTableColumn& Column, const FTableRowId& RowId) override
	{
		TSharedPtr<Trace::IUntypedTableReader> Reader = Table.GetTableReader();
		if (Reader.IsValid() && RowId.HasValidIndex())
		{
			Reader->SetRowIndex(RowId.RowIndex);
			const float Value = Reader->GetValueFloat(Column.GetIndex());
			return FormatFloatValue(Value);
		}
		return FText::GetEmpty();
	}

private:
	FText FormatFloatValue(float Value)
	{
		if (Value == 0.0f)
		{
			return FText::FromString(TEXT("0"));
		}
		else
		{
			return FText::FromString(FString::Printf(TEXT("%f"), Value));
		}
	}
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class FFloatTimeValueFormatter : public FTableCellValueFormatter
{
public:
	virtual FText FormatValue(const TOptional<FTableCellValue>& InValue) override
	{
		if (InValue.IsSet())
		{
			return FormatFloatValue(InValue.GetValue().Float);
		}
		return FText::GetEmpty();
	}

	virtual FText FormatValue(const FTable& Table, const FTableColumn& Column, const FTableRowId& RowId) override
	{
		TSharedPtr<Trace::IUntypedTableReader> Reader = Table.GetTableReader();
		if (Reader.IsValid() && RowId.HasValidIndex())
		{
			Reader->SetRowIndex(RowId.RowIndex);
			const float Value = Reader->GetValueFloat(Column.GetIndex());
			return FormatFloatValue(Value);
		}
		return FText::GetEmpty();
	}

	virtual FText FormatValueForTooltip(const TOptional<FTableCellValue>& InValue) override
	{
		if (InValue.IsSet())
		{
			return FormatFloatValueForTooltip(InValue.GetValue().Float);
		}
		return FText::GetEmpty();
	}

	virtual FText FormatValueForTooltip(const FTable& Table, const FTableColumn& Column, const FTableRowId& RowId) override
	{
		TSharedPtr<Trace::IUntypedTableReader> Reader = Table.GetTableReader();
		if (Reader.IsValid() && RowId.HasValidIndex())
		{
			Reader->SetRowIndex(RowId.RowIndex);
			const float Value = Reader->GetValueFloat(Column.GetIndex());
			return FormatFloatValueForTooltip(Value);
		}
		return FText::GetEmpty();
	}

private:
	FText FormatFloatValue(float Value)
	{
		return FText::FromString(TimeUtils::FormatTimeAuto(static_cast<double>(Value)));
	}

	FText FormatFloatValueForTooltip(float Value)
	{
		if (Value == 0.0f)
		{
			return FText::FromString(TEXT("0"));
		}
		else
		{
			return FText::FromString(FString::Printf(TEXT("%f (%s)"), Value, *TimeUtils::FormatTimeAuto(static_cast<double>(Value))));
		}
	}
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class FDoubleValueFormatter : public FTableCellValueFormatter
{
public:
	virtual FText FormatValue(const TOptional<FTableCellValue>& InValue) override
	{
		if (InValue.IsSet())
		{
			return FormatDoubleValue(InValue.GetValue().Double);
		}
		return FText::GetEmpty();
	}

	virtual FText FormatValue(const FTable& Table, const FTableColumn& Column, const FTableRowId& RowId) override
	{
		TSharedPtr<Trace::IUntypedTableReader> Reader = Table.GetTableReader();
		if (Reader.IsValid() && RowId.HasValidIndex())
		{
			Reader->SetRowIndex(RowId.RowIndex);
			const double Value = Reader->GetValueDouble(Column.GetIndex());
			return FormatDoubleValue(Value);
		}
		return FText::GetEmpty();
	}

private:
	FText FormatDoubleValue(double Value)
	{
		if (Value == 0.0)
		{
			return FText::FromString(TEXT("0"));
		}
		else
		{
			return FText::FromString(FString::Printf(TEXT("%f"), Value));
		}
	}
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class FDoubleTimeValueFormatter : public FTableCellValueFormatter
{
public:
	virtual FText FormatValue(const TOptional<FTableCellValue>& InValue) override
	{
		if (InValue.IsSet())
		{
			return FormatDoubleValue(InValue.GetValue().Double);
		}
		return FText::GetEmpty();
	}

	virtual FText FormatValue(const FTable& Table, const FTableColumn& Column, const FTableRowId& RowId) override
	{
		TSharedPtr<Trace::IUntypedTableReader> Reader = Table.GetTableReader();
		if (Reader.IsValid() && RowId.HasValidIndex())
		{
			Reader->SetRowIndex(RowId.RowIndex);
			const double Value = Reader->GetValueDouble(Column.GetIndex());
			return FormatDoubleValue(Value);
		}
		return FText::GetEmpty();
	}

	virtual FText FormatValueForTooltip(const TOptional<FTableCellValue>& InValue) override
	{
		if (InValue.IsSet())
		{
			return FormatDoubleValueForTooltip(InValue.GetValue().Double);
		}
		return FText::GetEmpty();
	}

	virtual FText FormatValueForTooltip(const FTable& Table, const FTableColumn& Column, const FTableRowId& RowId) override
	{
		TSharedPtr<Trace::IUntypedTableReader> Reader = Table.GetTableReader();
		if (Reader.IsValid() && RowId.HasValidIndex())
		{
			Reader->SetRowIndex(RowId.RowIndex);
			const double Value = Reader->GetValueDouble(Column.GetIndex());
			return FormatDoubleValueForTooltip(Value);
		}
		return FText::GetEmpty();
	}

private:
	FText FormatDoubleValue(double Value)
	{
		return FText::FromString(TimeUtils::FormatTimeAuto(Value));
	}

	FText FormatDoubleValueForTooltip(double Value)
	{
		if (Value == 0.0)
		{
			return FText::FromString(TEXT("0"));
		}
		else
		{
			return FText::FromString(FString::Printf(TEXT("%f (%s)"), Value, *TimeUtils::FormatTimeAuto(Value)));
		}
	}
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class FCStringValueFormatter : public FTableCellValueFormatter
{
public:
	virtual FText FormatValue(const TOptional<FTableCellValue>& InValue) override
	{
		if (InValue.IsSet())
		{
			return FormatCStringValue(InValue.GetValue().CString);
		}
		return FText::GetEmpty();
	}

	virtual FText FormatValue(const FTable& Table, const FTableColumn& Column, const FTableRowId& RowId) override
	{
		TSharedPtr<Trace::IUntypedTableReader> Reader = Table.GetTableReader();
		if (Reader.IsValid() && RowId.HasValidIndex())
		{
			Reader->SetRowIndex(RowId.RowIndex);
			const TCHAR* Value = Reader->GetValueCString(Column.GetIndex());
			return FormatCStringValue(Value);
		}
		return FText::GetEmpty();
	}

private:
	FText FormatCStringValue(const TCHAR* Value)
	{
		return FText::FromString(Value);
	}
};

////////////////////////////////////////////////////////////////////////////////////////////////////
// FTable
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

void FTable::CreateHierarchyColumn(int32 ColumnIndex, const TCHAR* ColumnName)
{
	const FString ColumnNameStr = ColumnName ? FString::Printf(TEXT("Hierarchy (%s)"), ColumnName) : TEXT("Hierarchy");
	const FText ColumnNameText = FText::FromString(ColumnNameStr);

	constexpr EHorizontalAlignment HorizontalAlignment = HAlign_Left;
	constexpr ETableColumnFlags ColumnFlags = ETableColumnFlags::ShouldBeVisible | ETableColumnFlags::CanBeSorted | ETableColumnFlags::CanBeFiltered | ETableColumnFlags::IsHierarchy;

	constexpr ETableCellDataType DataType = ETableCellDataType::CString;
	constexpr ETableColumnAggregation Aggregation = ETableColumnAggregation::None;

	constexpr float InitialColumnWidth = 90.0f;
	constexpr float MinColumnWidth = 0.0f;
	constexpr float MaxColumnWidth = FLT_MAX;

	TSharedPtr<FTableCellValueFormatter> FormatterPtr = MakeShareable(new FTableCellValueFormatter());

	TSharedPtr<FTableColumn> ColumnPtr = MakeShareable(new FTableColumn
	(
		-1, // Order
		ColumnIndex,
		FName(TEXT("_Hierarchy")), // Id
		ColumnNameText, // Short Name
		ColumnNameText, // Title Name
		FText::GetEmpty(), // Description
		ColumnFlags,
		DataType,
		Aggregation,
		HorizontalAlignment,
		InitialColumnWidth,
		MinColumnWidth,
		MaxColumnWidth,
		FormatterPtr.ToSharedRef()
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

	const Trace::ITableLayout& TableLayout = SourceTable->GetLayout();
	const int32 ColumnCount = TableLayout.GetColumnCount();

	//////////////////////////////////////////////////
	// Hierarchy Column

	int32 HierarchyColumnIndex = -1;
	const TCHAR* HierarchyColumnName = nullptr;

	// Look for first string column.
	//for (int32 ColumnIndex = 0; ColumnIndex < ColumnCount; ++ColumnIndex)
	//{
	//	Trace::ETableColumnType ColumnType = TableLayout.GetColumnType(ColumnIndex);
	//	if (ColumnType == Trace::TableColumnType_CString)
	//	{
	//		HierarchyColumnIndex = ColumnIndex;
	//		HierarchyColumnName = TableLayout.GetColumnName(ColumnIndex);
	//		break;
	//	}
	//}

	CreateHierarchyColumn(HierarchyColumnIndex, HierarchyColumnName);

	//////////////////////////////////////////////////

	for (int32 ColumnIndex = 0; ColumnIndex < ColumnCount; ++ColumnIndex)
	{
		Trace::ETableColumnType ColumnType = TableLayout.GetColumnType(ColumnIndex);
		const TCHAR* ColumnName = TableLayout.GetColumnName(ColumnIndex);

		const FString ColumnNameStr(ColumnName);
		const FText ColumnNameText = FText::FromString(ColumnNameStr);

		EHorizontalAlignment HorizontalAlignment = HAlign_Left;

		ETableColumnFlags ColumnFlags = ETableColumnFlags::CanBeSorted | ETableColumnFlags::CanBeFiltered | ETableColumnFlags::CanBeHidden;
		if (ColumnIndex != HierarchyColumnIndex)
		{
			ColumnFlags |= ETableColumnFlags::ShouldBeVisible;
		}

		ETableCellDataType DataType = ETableCellDataType::Unknown;
		ETableColumnAggregation Aggregation = ETableColumnAggregation::None;

		float InitialColumnWidth = 60.0f;
		float MinColumnWidth = 0.0f;
		float MaxColumnWidth = FLT_MAX;

		TSharedPtr<FTableCellValueFormatter> FormatterPtr;

		switch (ColumnType)
		{
		case Trace::TableColumnType_Bool:
			DataType = ETableCellDataType::Bool;
			HorizontalAlignment = HAlign_Right;
			InitialColumnWidth = 40.0f;
			FormatterPtr = MakeShareable(new FBoolValueFormatter());
			break;
		case Trace::TableColumnType_Int:
			DataType = ETableCellDataType::Int64;
			Aggregation = ETableColumnAggregation::Sum;
			HorizontalAlignment = HAlign_Right;
			InitialColumnWidth = 60.0f;
			FormatterPtr = MakeShareable(new FInt64ValueFormatter());
			break;
		case Trace::TableColumnType_Float:
			DataType = ETableCellDataType::Float;
			Aggregation = ETableColumnAggregation::Sum;
			HorizontalAlignment = HAlign_Right;
			InitialColumnWidth = 60.0f;
			FormatterPtr = MakeShareable(new FFloatTimeValueFormatter());
			break;
		case Trace::TableColumnType_Double:
			DataType = ETableCellDataType::Double;
			Aggregation = ETableColumnAggregation::Sum;
			HorizontalAlignment = HAlign_Right;
			InitialColumnWidth = 80.0f;
			FormatterPtr = MakeShareable(new FDoubleTimeValueFormatter());
			break;
		case Trace::TableColumnType_CString:
			DataType = ETableCellDataType::CString;
			HorizontalAlignment = HAlign_Left;
			InitialColumnWidth = FMath::Max(120.0f, 6.0f * ColumnNameStr.Len());
			FormatterPtr = MakeShareable(new FCStringValueFormatter());
			break;
		}

		if (!FormatterPtr.IsValid())
		{
			FormatterPtr = MakeShareable(new FTableCellValueFormatter());
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
			DataType,
			Aggregation,
			HorizontalAlignment,
			InitialColumnWidth,
			MinColumnWidth,
			MaxColumnWidth,
			FormatterPtr.ToSharedRef()
		));

		AddColumn(ColumnPtr);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace Insights

#undef LOCTEXT_NAMESPACE
