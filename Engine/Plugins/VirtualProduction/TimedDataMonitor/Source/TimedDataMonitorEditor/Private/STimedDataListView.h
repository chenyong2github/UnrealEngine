// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "SlateFwd.h"
#include "SlateOptMacros.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

#include "Widgets/Views/STreeView.h"
#include "Widgets/Views/STableRow.h"


class STimedDataInputListView;
class STimedDataInputTableRow;
struct FTimedDataInputTableRowData;


/**
 * 
 */
using FTimedDataInputTableRowDataPtr = TSharedPtr<FTimedDataInputTableRowData>;


/**
 *
 */
class STimedDataInputTableRow : public SMultiColumnTableRow<FTimedDataInputTableRowDataPtr>
{
	using Super = SMultiColumnTableRow<FTimedDataInputTableRowDataPtr>;

public:
	SLATE_BEGIN_ARGS(STimedDataInputTableRow) { }
		SLATE_ARGUMENT(FTimedDataInputTableRowDataPtr, Item)
	SLATE_END_ARGS()

	void Construct(const FArguments& Args, const TSharedRef<STableViewBase>& OwerTableView, const TSharedRef<STimedDataInputListView>& OwnerTreeView);

private:
	virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& ColumnName) override;

	ECheckBoxState GetEnabledCheckState() const;
	void OnEnabledCheckStateChanged(ECheckBoxState NewState);

	FText GetStateGlyphs() const;
	FSlateColor GetStateColorAndOpacity() const;

	FText GetDescription() const;
	FText GetEvaluationOffsetText() const;
	TOptional<int32> GetBufferSize() const;
	FText GetBufferSizeText() const;
	void SetBufferSize(int32 NewValue, ETextCommit::Type CommitType);
	bool CanEditBufferSize() const;

	/** Queries about buffer stats to display */
	FText GetBufferUnderflowCount() const;
	FText GetBufferOverflowCount() const;
	FText GetFrameDroppedCount() const;

private:
	FTimedDataInputTableRowDataPtr Item;
	TSharedPtr<STimedDataInputListView> OwnerTreeView;
};


/**
 *
 */
class STimedDataInputListView : public STreeView<FTimedDataInputTableRowDataPtr>
{
	using Super = STreeView<FTimedDataInputTableRowDataPtr>;

public:
	SLATE_BEGIN_ARGS(STimedDataInputListView) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~STimedDataInputListView();

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

	void RequestRefresh() { bRefreshRequested = true; }

private:
	void RequestRebuildSources();
	void RebuildSources();

	ECheckBoxState GetAllEnabledCheckState() const;
	void OnToggleAllEnabledCheckState(ECheckBoxState CheckBoxState);
	TSharedRef<ITableRow> OnGenerateRow(FTimedDataInputTableRowDataPtr Item, const TSharedRef<STableViewBase>& OwnerTable);
	void GetChildrenForInfo(FTimedDataInputTableRowDataPtr InItem, TArray<FTimedDataInputTableRowDataPtr>& OutChildren);
	void OnSelectionChanged(FTimedDataInputTableRowDataPtr InItem, ESelectInfo::Type SelectInfo);
	bool OnIsSelectableOrNavigable(FTimedDataInputTableRowDataPtr InItem) const;

private:
	TArray<FTimedDataInputTableRowDataPtr> ListItemsSource;
	bool bRebuildListRequested = true;
	bool bRefreshRequested = true;
	double LastCachedValueUpdateTime = 0.0;
};
