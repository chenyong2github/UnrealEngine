// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Insights/Table/ViewModels/Table.h"

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

class FUntypedTable : public FTable
{
public:
	FUntypedTable();
	virtual ~FUntypedTable();

	virtual void Reset();

	TSharedPtr<Trace::IUntypedTable> GetSourceTable() const { return SourceTable; }
	TSharedPtr<Trace::IUntypedTableReader> GetTableReader() const { return TableReader; }

	/* Update table content. Returns true if the table layout has changed. */
	bool UpdateSourceTable(TSharedPtr<Trace::IUntypedTable> InSourceTable);

private:
	void CreateColumns(const Trace::ITableLayout& TableLayout);

private:
	TSharedPtr<Trace::IUntypedTable> SourceTable;
	TSharedPtr<Trace::IUntypedTableReader> TableReader;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace Insights
