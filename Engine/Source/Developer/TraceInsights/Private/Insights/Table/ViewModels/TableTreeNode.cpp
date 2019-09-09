// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "TableTreeNode.h"

// Insights
#include "Insights/Table/ViewModels/TableColumn.h"

#define LOCTEXT_NAMESPACE "Insights_TableTreeNode"

namespace Insights
{

////////////////////////////////////////////////////////////////////////////////////////////////////

FTableCellValue FTableTreeNode::GetValue(const FTableColumn& Column) const
{
	switch (Column.GetDataType())
	{
	case ETableCellDataType::Bool:    return FTableCellValue(GetValueBool(Column));
	case ETableCellDataType::Int64:   return FTableCellValue(GetValueInt64(Column));
	case ETableCellDataType::Float:   return FTableCellValue(GetValueFloat(Column));
	case ETableCellDataType::Double:  return FTableCellValue(GetValueDouble(Column));
	case ETableCellDataType::CString: return FTableCellValue(GetValueCString(Column));
	default:                          return FTableCellValue();
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool FTableTreeNode::GetValueBool(const FTableColumn& Column) const
{
	bool Value = false;

	if (!IsGroup())
	{
		TSharedPtr<FTable> Table = ParentTable.Pin();
		if (Table.IsValid())
		{
			TSharedPtr<Trace::IUntypedTableReader> Reader = Table->GetTableReader();
			if (Reader.IsValid() && RowId.HasValidIndex())
			{
				Reader->SetRowIndex(RowId.RowIndex);
				Value = Reader->GetValueBool(Column.GetIndex());
			}
		}
	}
	else
	{
		const FTableCellValue* ValuePtr = AggregatedValues.Find(Column.GetId());
		if (ValuePtr != nullptr)
		{
			Value = ValuePtr->Bool;
		}
	}

	return Value;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

int64 FTableTreeNode::GetValueInt64(const FTableColumn& Column) const
{
	int64 Value = 0;

	if (!IsGroup())
	{
		TSharedPtr<FTable> Table = ParentTable.Pin();
		if (Table.IsValid())
		{
			TSharedPtr<Trace::IUntypedTableReader> Reader = Table->GetTableReader();
			if (Reader.IsValid() && RowId.HasValidIndex())
			{
				Reader->SetRowIndex(RowId.RowIndex);
				Value = Reader->GetValueInt(Column.GetIndex());
			}
		}
	}
	else
	{
		const FTableCellValue* ValuePtr = AggregatedValues.Find(Column.GetId());
		if (ValuePtr != nullptr)
		{
			Value = ValuePtr->Int64;
		}
	}

	return Value;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

float FTableTreeNode::GetValueFloat(const FTableColumn& Column) const
{
	float Value = 0.0f;

	if (!IsGroup())
	{
		TSharedPtr<FTable> Table = ParentTable.Pin();
		if (Table.IsValid())
		{
			TSharedPtr<Trace::IUntypedTableReader> Reader = Table->GetTableReader();
			if (Reader.IsValid() && RowId.HasValidIndex())
			{
				Reader->SetRowIndex(RowId.RowIndex);
				Value = Reader->GetValueFloat(Column.GetIndex());
			}
		}
	}
	else
	{
		const FTableCellValue* ValuePtr = AggregatedValues.Find(Column.GetId());
		if (ValuePtr != nullptr)
		{
			Value = ValuePtr->Float;
		}
	}

	return Value;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

double FTableTreeNode::GetValueDouble(const FTableColumn& Column) const
{
	double Value = 0.0;

	if (!IsGroup())
	{
		TSharedPtr<FTable> Table = ParentTable.Pin();
		if (Table.IsValid())
		{
			TSharedPtr<Trace::IUntypedTableReader> Reader = Table->GetTableReader();
			if (Reader.IsValid() && RowId.HasValidIndex())
			{
				Reader->SetRowIndex(RowId.RowIndex);
				Value = Reader->GetValueDouble(Column.GetIndex());
			}
		}
	}
	{
		const FTableCellValue* ValuePtr = AggregatedValues.Find(Column.GetId());
		if (ValuePtr != nullptr)
		{
			Value = ValuePtr->Double;
		}
	}

	return Value;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

const TCHAR* FTableTreeNode::GetValueCString(const FTableColumn& Column) const
{
	const TCHAR* Value = nullptr;

	if (!IsGroup())
	{
		TSharedPtr<FTable> Table = ParentTable.Pin();
		if (Table.IsValid())
		{
			TSharedPtr<Trace::IUntypedTableReader> Reader = Table->GetTableReader();
			if (Reader.IsValid() && RowId.HasValidIndex())
			{
				Reader->SetRowIndex(RowId.RowIndex);
				Value = Reader->GetValueCString(Column.GetIndex());
			}
		}
	}
	else
	{
		const FTableCellValue* ValuePtr = AggregatedValues.Find(Column.GetId());
		if (ValuePtr != nullptr)
		{
			Value = ValuePtr->CString;
		}
	}

	return Value;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace Insights

#undef LOCTEXT_NAMESPACE
