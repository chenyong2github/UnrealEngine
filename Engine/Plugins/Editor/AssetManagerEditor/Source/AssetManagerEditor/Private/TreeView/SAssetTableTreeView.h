// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

#include "TreeView/AssetTable.h"
#include "Insights/Table/Widgets/STableTreeView.h"

class FAssetTreeNode;

class SAssetTableTreeView : public UE::Insights::STableTreeView
{
public:
	/** Default constructor. */
	SAssetTableTreeView();

	/** Virtual destructor. */
	virtual ~SAssetTableTreeView();

	SLATE_BEGIN_ARGS(SAssetTableTreeView) {}
	SLATE_END_ARGS()

	/**
	 * Construct this widget
	 * @param InArgs - The declaration data for this widget
	 */
	void Construct(const FArguments& InArgs, TSharedPtr<FAssetTable> InTablePtr);

	virtual TSharedPtr<SWidget> ConstructToolbar() override;
	virtual TSharedPtr<SWidget> ConstructFooter() override;

	TSharedPtr<FAssetTable> GetAssetTable() { return StaticCastSharedPtr<FAssetTable>(GetTable()); }
	const TSharedPtr<FAssetTable> GetAssetTable() const { return StaticCastSharedPtr<FAssetTable>(GetTable()); }

	//void UpdateSourceTable(TSharedPtr<TraceServices::IAssetTable> SourceTable);

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
	virtual void RebuildTreeAsync() { bNeedsToRebuild = true; }

	////////////////////////////////////////////////////////////////////////////////////////////////////
	// IAsyncOperationStatusProvider implementation

	virtual bool IsRunning() const override;
	virtual double GetAllOperationsDuration() override;
	virtual FText GetCurrentOperationName() const override;

	////////////////////////////////////////////////////////////////////////////////////////////////////

protected:
	virtual void InternalCreateGroupings() override;

	TSharedPtr<FAssetTreeNode> GetSingleSelectedAssetNode() const;

	virtual void ExtendMenu(FMenuBuilder& MenuBuilder) override;

private:
	bool virtual ApplyCustomAdvancedFilters(const UE::Insights::FTableTreeNodePtr& NodePtr) override;
	virtual void AddCustomAdvancedFilters() override;

	void InitAvailableViewPresets();

private:
	bool bNeedsToRebuild = false;
};
