// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

namespace Trace
{
	class ITableLayout;
	class IUntypedTable;
	class IUntypedTableReader;
}

namespace Insights
{

class FTableColumn;

////////////////////////////////////////////////////////////////////////////////////////////////////

struct FTableRowId
{
	static constexpr int32 InvalidRowIndex = -1;

	FTableRowId(int32 InRowIndex) : RowIndex(InRowIndex), Flags(0) {}

	bool HasValidIndex() const { return RowIndex >= 0; }

	union
	{
		struct
		{
			int32 RowIndex;
			uint32 Flags;
		};

		void* Data;
	};
};

////////////////////////////////////////////////////////////////////////////////////////////////////
/**
 * Table View Model.
 * View model class for the STableListView and STableTreeView widgets.
 */
class FTable : public TSharedFromThis<FTable>
{
public:
	FTable();
	virtual ~FTable();

	const FName& GetName() const { return Name; }
	FText GetDisplayName() const { return FText::FromName(Name); }
	const FText& GetDescription() const { return Description; }

	virtual void Reset();

	bool IsValid() const { return Columns.Num() > 0; }

	const TArray<TSharedRef<FTableColumn>>& GetColumns() const { return Columns; }
	void SetColumns(const TArray<TSharedRef<Insights::FTableColumn>>& InColumns);

	TSharedRef<FTableColumn> FindColumnChecked(const FName& ColumnId) const
	{
		return ColumnIdToPtrMapping.FindChecked(ColumnId);
	}

	TSharedPtr<FTableColumn> FindColumn(const FName& ColumnId) const
	{
		const TSharedRef<FTableColumn>* const ColumnRefPtr = ColumnIdToPtrMapping.Find(ColumnId);
		if (ColumnRefPtr != nullptr)
		{
			return *ColumnRefPtr;
		}
		return nullptr;
	}

	int32 GetColumnPositionIndex(const FName& ColumnId) const;

	TSharedPtr<Trace::IUntypedTable> GetSourceTable() const { return SourceTable; }
	TSharedPtr<Trace::IUntypedTableReader> GetTableReader() const { return TableReader; }

	/* Update table content. Returns true if the table layout has changed. */
	bool UpdateSourceTable(TSharedPtr<Trace::IUntypedTable> InSourceTable);

private:
	void AddColumn(TSharedRef<FTableColumn> Column);
	void CreateHierarchyColumn(int32 ColumnIndex, const TCHAR* ColumnName);
	void CreateColumns(const Trace::ITableLayout& TableLayout);

private:
	FName Name;
	FText Description;

	/** All available columns. */
	TArray<TSharedRef<FTableColumn>> Columns;

	/** Mapping between column Ids and FTableColumn shared refs. */
	TMap<FName, TSharedRef<FTableColumn>> ColumnIdToPtrMapping;

	TSharedPtr<Trace::IUntypedTable> SourceTable;
	TSharedPtr<Trace::IUntypedTableReader> TableReader;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace Insights
