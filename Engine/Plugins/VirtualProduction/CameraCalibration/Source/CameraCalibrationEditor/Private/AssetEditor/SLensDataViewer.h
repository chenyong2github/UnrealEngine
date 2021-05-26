// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/SCompoundWidget.h"

#include "CameraCalibrationEditorCommon.h"
#include "SLensFilePanel.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Views/STreeView.h"
#include "UObject/StrongObjectPtr.h"

class FCurveEditor;
class SCameraCalibrationCurveEditorPanel;
class ULensFile;

/**
 * Data entry item
 * @note To be expanded
 */
class FLensDataItem : public TSharedFromThis<FLensDataItem>
{
public:
	FLensDataItem(float InFocus);

	TSharedRef<ITableRow> MakeTreeRowWidget(const TSharedRef<STableViewBase>& InOwnerTable);

	/** Focus value of this item */
	float Focus;

	/** Children of this item */
	TArray<TSharedPtr<FLensDataItem>> Children;
};

/**
 * Widget representing a data entry row
 * @note To be expanded
 */
class SLensDataItem : public STableRow<TSharedPtr<FLensDataItem>>
{
	SLATE_BEGIN_ARGS(SLensDataItem) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& OwnerTable, TSharedRef<FLensDataItem> InItemData);

private:
	FText GetLabelText() const;

private:

	/** WeakPtr to source data item */
	TWeakPtr<FLensDataItem> WeakItem;
};

/**
 * Data category item
 */
class FLensDataCategoryItem : public TSharedFromThis<FLensDataCategoryItem>
{
public:
	FLensDataCategoryItem(TWeakPtr<FLensDataCategoryItem> Parent, EDataCategories InCategory, FName InLabel);

	TSharedRef<ITableRow> MakeTreeRowWidget(const TSharedRef<STableViewBase>& InOwnerTable);

public:

	/** Category this item is associated with */
	EDataCategories Category;

	/** Label of this category */
	FName Label;

	/** WeakPtr to parent of this item */
	TWeakPtr<FLensDataCategoryItem> Parent;

	/** Children of this category */
	TArray<TSharedPtr<FLensDataCategoryItem>> Children;
};

/**
 * Data category row widget
 */
class SLensDataCategoryItem : public STableRow<TSharedPtr<FLensDataCategoryItem>>
{
	SLATE_BEGIN_ARGS(SLensDataCategoryItem) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& OwnerTable, TSharedRef<FLensDataCategoryItem> InItemData);

private:

	/** Returns the label of this row */
	FText GetLabelText() const;

private:

	/** WeakPtr to source data item */
	TWeakPtr<FLensDataCategoryItem> WeakItem;
};

/** Widget used to display data from the LensFile */
class SLensDataViewer : public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS(SLensDataViewer)
	{}

		/** FIZ data */
		SLATE_ATTRIBUTE(FCachedFIZData, CachedFIZData)

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, ULensFile* InLensFile);

	/** Get the currently selected data category. */
	TSharedPtr<FLensDataCategoryItem> GetDataCategorySelection() const;

	/** Generates one data category row */
	TSharedRef<ITableRow> OnGenerateDataCategoryRow(TSharedPtr<FLensDataCategoryItem> Item, const TSharedRef<STableViewBase>& OwnerTable);

	/** Used to get the children of a data category item */
	void OnGetDataCategoryItemChildren(TSharedPtr<FLensDataCategoryItem> Item, TArray<TSharedPtr<FLensDataCategoryItem>>& OutChildren);

	/** Triggered when data category selection has changed */
	void OnDataCategorySelectionChanged(TSharedPtr<FLensDataCategoryItem> Item, ESelectInfo::Type SelectInfo);

	/** Get the currently selected data entry */
	TSharedPtr<FLensDataItem> GetSelectedDataEntry() const;

	/** Generates one data entry row */
	TSharedRef<ITableRow> OnGenerateDataEntryRow(TSharedPtr<FLensDataItem> Node, const TSharedRef<STableViewBase>& OwnerTable);

	/** Used to get the children of a data entry item */
	void OnGetDataEntryChildren(TSharedPtr<FLensDataItem> Node, TArray<TSharedPtr<FLensDataItem>>& OutNodes);

	/** Triggered when data entry selection has changed */
	void OnDataEntrySelectionChanged(TSharedPtr<FLensDataItem> Node, ESelectInfo::Type SelectInfo);

private:

	/** Makes the widget showing lens data and curve editor */
	TSharedRef<SWidget> MakeLensDataWidget();

	/** Makes the Toolbar with data manipulation buttons */
	TSharedRef<SWidget> MakeToolbarWidget(TSharedRef<SCameraCalibrationCurveEditorPanel> InEditorPanel);

	/** Called when user clicks on AddPoint button */
	FReply OnAddDataPointClicked();

	/** Called when DataMode is changed */
	void OnDataModeChanged();

	/** Refreshes the widget's trees */
	void Refresh();

	/** Refreshes data categories tree */
	void RefreshDataCategoriesTree();

	/** Refreshes data entries tree */
	void RefreshDataEntriesTree();

	/** Refreshes curve editor */
	void RefreshCurve();

	/** Callbacked when user clicks AddPoint from the dialog */
	void OnLensDataPointAdded();

private:
	
	/** Data category TreeView */
	TSharedPtr<STreeView<TSharedPtr<FLensDataCategoryItem>>> TreeView;

	/** List of data category items */
	TArray<TSharedPtr<FLensDataCategoryItem>> DataCategories;

	/** Data items associated with selected data category TreeView */
	TSharedPtr<STreeView<TSharedPtr<FLensDataItem>>> DataEntriesTree;

	/** List of data items for the selected data category */
	TArray<TSharedPtr<FLensDataItem>> DataEntries;

	/** Data entries title */
	TSharedPtr<STextBlock> DataEntryNameWidget;

	/** LensFile being edited */
	TStrongObjectPtr<ULensFile> LensFile;

	/** Curve editor manager and panel to display */
	TSharedPtr<FCurveEditor> CurveEditor;

	/** Child class of curve editor panel */
	TSharedPtr<SCameraCalibrationCurveEditorPanel> CurvePanel;

	/** Evaluated FIZ for the current frame */
	TAttribute<FCachedFIZData> CachedFIZ;
};