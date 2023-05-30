// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Delegates/DelegateCombinations.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

#include "TreeView/AssetTable.h"
#include "Insights/Table/Widgets/STableTreeView.h"

class FAssetTreeNode;

class SAssetTableTreeView : public UE::Insights::STableTreeView
{
public:
	DECLARE_DELEGATE_OneParam(FOnSelectionChanged, const TArray<TSharedPtr<UE::Insights::FTableTreeNode>>)

public:
	/** Default constructor. */
	SAssetTableTreeView();

	/** Virtual destructor. */
	virtual ~SAssetTableTreeView();

	SLATE_BEGIN_ARGS(SAssetTableTreeView)
		: _OnSelectionChanged()
	{
	}
	SLATE_EVENT(FOnSelectionChanged, OnSelectionChanged)
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

	TSharedPtr<FAssetTreeNode> GetSingleSelectedAssetNode() const { return SelectedAssetNode; }

protected:
	virtual void InternalCreateGroupings() override;

	virtual void ExtendMenu(FMenuBuilder& MenuBuilder) override;

	FText GetFooterLeftText() const;
	FText GetFooterCenterText1() const;
	FText GetFooterCenterText2() const;
	FText GetFooterRightText1() const;

	virtual void TreeView_OnSelectionChanged(UE::Insights::FTableTreeNodePtr SelectedItem, ESelectInfo::Type SelectInfo) override;

private:
	void InitAvailableViewPresets();

	void ExportDependencyData() const;

	TArray<FAssetData> GetAssetDataForSelection() const;

private:
	bool bNeedsToRebuild = false;

	FText FooterLeftText;
	FText FooterCenterText1;
	FText FooterCenterText2;
	FText FooterRightText1;

	/** Delegate to invoke when selection changes. */
	FOnSelectionChanged OnSelectionChanged;
	TSharedPtr<FAssetTreeNode> SelectedAssetNode;
	TSet<int32> SelectedIndices;
};
