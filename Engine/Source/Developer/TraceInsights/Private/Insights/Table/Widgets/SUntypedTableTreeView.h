// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

// Insights
#include "Insights/Table/ViewModels/UntypedTable.h"
#include "Insights/Table/Widgets/STableTreeView.h"

namespace Insights
{

////////////////////////////////////////////////////////////////////////////////////////////////////

class SUntypedTableTreeView : public STableTreeView
{
public:
	/** Default constructor. */
	SUntypedTableTreeView();

	/** Virtual destructor. */
	virtual ~SUntypedTableTreeView();

	SLATE_BEGIN_ARGS(SUntypedTableTreeView) {}
	SLATE_END_ARGS()

	/**
	 * Construct this widget
	 * @param InArgs - The declaration data for this widget
	 */
	void Construct(const FArguments& InArgs, TSharedPtr<Insights::FUntypedTable> InTablePtr);

	TSharedPtr<Insights::FUntypedTable> GetUntypedTable() const { return StaticCastSharedPtr<Insights::FUntypedTable>(GetTable()); }

	void UpdateSourceTable(TSharedPtr<Trace::IUntypedTable> SourceTable);

	virtual void Reset();

	/**
	 * Rebuilds the tree (if necessary).
	 * @param bResync - If true, it forces a resync even if the list did not changed since last sync.
	 */
	virtual void RebuildTree(bool bResync);

private:
};

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace Insights
