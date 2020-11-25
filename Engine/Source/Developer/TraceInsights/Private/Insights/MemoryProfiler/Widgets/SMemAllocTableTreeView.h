// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "TraceServices/Model/AllocationsProvider.h"

// Insights
#include "Insights/MemoryProfiler/ViewModels/MemAllocTable.h"
#include "Insights/Table/Widgets/STableTreeView.h"

namespace Insights
{

class FMemoryRuleSpec;

////////////////////////////////////////////////////////////////////////////////////////////////////

class SMemAllocTableTreeView : public STableTreeView
{
public:
	/** Default constructor. */
	SMemAllocTableTreeView();

	/** Virtual destructor. */
	virtual ~SMemAllocTableTreeView();

	SLATE_BEGIN_ARGS(SMemAllocTableTreeView) {}
	SLATE_END_ARGS()

	/**
	 * Construct this widget
	 * @param InArgs - The declaration data for this widget
	 */
	void Construct(const FArguments& InArgs, TSharedPtr<FMemAllocTable> InTablePtr);

	TSharedPtr<FMemAllocTable> GetMemAllocTable() { return StaticCastSharedPtr<FMemAllocTable>(GetTable()); }
	const TSharedPtr<FMemAllocTable> GetMemAllocTable() const { return StaticCastSharedPtr<FMemAllocTable>(GetTable()); }

	//void UpdateSourceTable(TSharedPtr<TraceServices::IMemAllocTable> SourceTable);

	virtual void Reset();

	/**
	 * Ticks this widget.  Override in derived classes, but always call the parent implementation.
	 *
	 * @param  AllottedGeometry The space allotted for this widget
	 * @param  InCurrentTime  Current absolute real time
	 * @param  InDeltaTime  Real time passed since last tick
	 */
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

	/**
	 * Rebuilds the tree (if necessary).
	 * @param bResync - If true, it forces a resync even if the list did not changed since last sync.
	 */
	virtual void RebuildTree(bool bResync);

	void SetQueryParams(TSharedPtr<FMemoryRuleSpec> InRule, double TimeA = 0.0, double TimeB = 0.0, double TimeC = 0.0, double TimeD = 0.0)
	{
		Rule = InRule;
		TimeMarkers[0] = TimeA;
		TimeMarkers[1] = TimeB;
		TimeMarkers[2] = TimeC;
		TimeMarkers[3] = TimeD;
		OnQueryInvalidated();
	}

	int32 GetTabIndex() const { return TabIndex; }
	void SetTabIndex(int32 InTabIndex) { TabIndex = InTabIndex; }

protected:
	virtual void OnPreAsyncUpdate() override;
	virtual void OnPostAsyncUpdate() override;

private:
	void OnQueryInvalidated();
	void StartQuery();
	void UpdateQuery();
	void CancelQuery();

	TSharedRef<class ITableCellValueFormatter> CreateCachedLlmTagValueFormatter();

private:
	int32 TabIndex = -1;
	TSharedPtr<FMemoryRuleSpec> Rule = nullptr;
	double TimeMarkers[4];
	TraceServices::IAllocationsProvider::FQueryHandle Query = 0;

	TSharedPtr<class ITableCellValueFormatter> OriginalLlmTagValueFormatter;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace Insights
