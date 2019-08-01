// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "TableTreeNode.h"

#define LOCTEXT_NAMESPACE "Insights_TableTreeNode"

namespace Insights
{

////////////////////////////////////////////////////////////////////////////////////////////////////

bool FTableTreeNode::GetValueBool(int32 InColumnIndex) const
{
	bool Value = false;

	TSharedPtr<FTable> Table = ParentTable.Pin();
	if (Table.IsValid())
	{
		TSharedPtr<Trace::IUntypedTableReader> Reader = Table->GetTableReader();
		if (Reader.IsValid() && RowId.HasValidIndex())
		{
			Reader->SetRowIndex(RowId.RowIndex);
			Value = Reader->GetValueBool(InColumnIndex);
		}
	}

	return Value;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

int64 FTableTreeNode::GetValueInt(int32 InColumnIndex) const
{
	int64 Value = 0;

	TSharedPtr<FTable> Table = ParentTable.Pin();
	if (Table.IsValid())
	{
		TSharedPtr<Trace::IUntypedTableReader> Reader = Table->GetTableReader();
		if (Reader.IsValid() && RowId.HasValidIndex())
		{
			Reader->SetRowIndex(RowId.RowIndex);
			Value = Reader->GetValueInt(InColumnIndex);
		}
	}

	return Value;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

float FTableTreeNode::GetValueFloat(int32 InColumnIndex) const
{
	float Value = 0.0f;

	TSharedPtr<FTable> Table = ParentTable.Pin();
	if (Table.IsValid())
	{
		TSharedPtr<Trace::IUntypedTableReader> Reader = Table->GetTableReader();
		if (Reader.IsValid() && RowId.HasValidIndex())
		{
			Reader->SetRowIndex(RowId.RowIndex);
			Value = Reader->GetValueFloat(InColumnIndex);
		}
	}

	return Value;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

double FTableTreeNode::GetValueDouble(int32 InColumnIndex) const
{
	double Value = 0.0;

	TSharedPtr<FTable> Table = ParentTable.Pin();
	if (Table.IsValid())
	{
		TSharedPtr<Trace::IUntypedTableReader> Reader = Table->GetTableReader();
		if (Reader.IsValid() && RowId.HasValidIndex())
		{
			Reader->SetRowIndex(RowId.RowIndex);
			Value = Reader->GetValueDouble(InColumnIndex);
		}
	}

	return Value;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

const TCHAR* FTableTreeNode::GetValueCString(int32 InColumnIndex) const
{
	const TCHAR* Value = nullptr;

	TSharedPtr<FTable> Table = ParentTable.Pin();
	if (Table.IsValid())
	{
		TSharedPtr<Trace::IUntypedTableReader> Reader = Table->GetTableReader();
		if (Reader.IsValid() && RowId.HasValidIndex())
		{
			Reader->SetRowIndex(RowId.RowIndex);
			Value = Reader->GetValueCString(InColumnIndex);
		}
	}

	return Value;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace Insights

#undef LOCTEXT_NAMESPACE
