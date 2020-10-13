// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Layout/SSplitter.h"

class FDMXEditor;
class IPropertyHandle;
class IPropertyHandleArray;
class FUICommandList;
class FDMXFixtureFunctionItem;
class FDMXFixtureTypeSharedData;
class SPopupErrorText;
class IDMXNamedType;
class SInlineEditableTextBlock;

using SDMXFunctionItemListView = SListView<TSharedPtr<FDMXFixtureFunctionItem>>;

struct FDMXCellAttributeItem
{
	FDMXCellAttributeItem(const TSharedPtr<IPropertyHandle> InCellFunctionHandle);

	TSharedPtr<IPropertyHandle> CellFunctionHandle;

	FText GetAttributeName() const;
};

using SDMXCellAttributeItemListView = SListView<TSharedPtr<FDMXCellAttributeItem>>;

struct FDMXFunctionDetailColumnSizeData
{
	TAttribute<float> LeftColumnWidth;
	TAttribute<float> RightColumnWidth;
	SSplitter::FOnSlotResized OnWidthChanged;

	void SetColumnWidth(float InWidth) { OnWidthChanged.ExecuteIfBound(InWidth); }
};

/**
 * An widget item for function row
 */
class SDMXFunctionTableRow
	: public STableRow<TSharedPtr<FDMXFixtureFunctionItem>>
{
public:
	SLATE_BEGIN_ARGS(SDMXFunctionTableRow)
		: _ColumnSizeData()
		, _AttributeMinWidth(120.f)
		, _AttributeMaxWidth(120.f)
	{}

	SLATE_ARGUMENT(FDMXFunctionDetailColumnSizeData, ColumnSizeData)

	SLATE_ARGUMENT(FText, NameEmptyError)

	SLATE_ARGUMENT(FText, NameDuplicateError)

	SLATE_ARGUMENT(float, AttributeMinWidth)

	SLATE_ARGUMENT(float, AttributeMaxWidth)

	SLATE_END_ARGS()

	/** Constructs the widget */
	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& OwnerTable, TSharedPtr<FDMXFixtureFunctionItem> InItem, TSharedPtr<IPropertyHandle> InCurrentModeHandle);

	/** Returns the item */
	TSharedPtr<FDMXFixtureFunctionItem> GetItem() const { return StaticCastSharedPtr<FDMXFixtureFunctionItem>(Item); }

	/** Enters editing  */
	void EnterEditingMode();

	EVisibility CheckCellChannelsOverlap() const;

private:
	//~ Begin SWidget interface
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	virtual FReply OnMouseButtonDoubleClick(const FGeometry& InMyGeometry, const FPointerEvent& InMouseEvent) override;
	//~End of SWidget interface

	/** Called when text changed */
	bool OnVerifyFunctionTextChanged(const FText& InNewText, FText& OutErrorMessage);

	/** Called when text was comitted */
	void OnFunctionTextCommitted(const FText& InNewText, ETextCommit::Type InTextCommit);

	void SetAttributeValue(FName NewValue);

	FName GetAttributeValue() const;

	bool HasMultipleAttributeValues() const;

	bool HideAttributeWarningIcon() const;

	void OnLeftColumnResized(float InNewWidth);

	FText GetFunctionChannelText() const;

protected:
	/**  item shown in the row */
	TSharedPtr<IDMXNamedType> Item;

	/** Store handle for attribute propery */
	TSharedPtr<IPropertyHandle> AttributeHandle;

	/** Store handle for ChannelOffset propery */
	TSharedPtr<IPropertyHandle> ChannelOffsetHandle;

	/** Error displayed when the text box is empty */
	FText NameEmptyError;

	/** Error displayed when the the  name entered already exists */
	FText NameDuplicateError;

	/** Text box editing the Name */
	TSharedPtr<SInlineEditableTextBlock> InlineEditableTextBlock;

	/** Error reporting widget, required as SInlineEditableTextBlock error can't be cleared on commit */
	TSharedPtr<SPopupErrorText> ErrorTextWidget;

	/** Resizing column configuration */
	FDMXFunctionDetailColumnSizeData ColumnSizeData;

	/** Handle to the currently selected Mode */
	TSharedPtr<IPropertyHandle> CurrentModeHandle;
};


class SDMXFunctionItemListViewBox
	: public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SDMXFunctionItemListViewBox) 
		: _ColumnWidth(0.65f)
	{ }
		SLATE_ARGUMENT(float, ColumnWidth)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedPtr<FDMXEditor>& InDMXEditor, const TSharedPtr<IPropertyHandleArray>& InModesHandleArray);

protected:
	// Begin SWidget
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	// End SWidget

private:
	/** Generates a Table Row of Function names */
	TSharedRef<ITableRow> GenerateFunctionNameRow(TSharedPtr<FDMXFixtureFunctionItem> InItem, const TSharedRef<STableViewBase>& OwnerTable);

	/** Generates a Table Row of Cell Function names */
	TSharedRef<ITableRow> GenerateCellAttributeNameRow(TSharedPtr<FDMXCellAttributeItem> InItem, const TSharedRef<STableViewBase>& OwnerTable);

	/** Callback for when the a function is selected in the list view */
	void OnListSelectionChanged(TSharedPtr<FDMXFixtureFunctionItem>, ESelectInfo::Type SelectInfo);

	/** Called when a context menu should be displayed on a list item */
	TSharedPtr<SWidget> OnContextMenuOpening();

	/** Rebuilds the list from Functions Arrays */
	void RebuildList(bool bUpdateSelection = true);

	/** Called when num modes changed */
	void OnNumFunctionsChanged();

	/** Called when modes were selected */
	void OnModesSelected();

	/** Registers commands for handling actions, such as copy/paste */
	void RegisterCommands();

	/** Cut selected Item(s) */
	void OnCutSelectedItems();
	bool CanCutItems() const;

	/** Copy selected Item(s) */
	void OnCopySelectedItems();
	bool CanCopyItems() const;

	/** Pastes previously copied Item(s) */
	void OnPasteItems();
	bool CanPasteItems() const;

	/** Duplicates the selected Item */
	bool CanDuplicateItems() const;
	void OnDuplicateItems();

	/** Removes existing selected component Items */
	void OnDeleteItems();
	bool CanDeleteItems() const;

	/** Requests a rename on the selected Entity. */
	void OnRenameItem();
	bool CanRenameItem() const;

	/** Column width accessibility */
	float OnGetLeftColumnWidth() const { return 1.0f - ColumnWidth; }
	float OnGetRightColumnWidth() const { return ColumnWidth; }
	void OnSetColumnWidth(float InWidth) { ColumnWidth = InWidth; }

	EVisibility GetFixtureMatrixVisibility() const;
	FText GetCellAttributesHeader() const;

	/** Returns warning text if properties will not produce meaningful results for fixture matrices */
	FText GetFixtureMatrixWarning() const;

	FText GetCellChannelsStartChannel() const;

private:
	/** The list widget where the user can select function names */
	TSharedPtr<SDMXFunctionItemListView> ListView;

	/** The list widget where the user can select function names */
	TSharedPtr<SDMXCellAttributeItemListView> CellAttributeListView;

	/** Source for the list view as an array of Function Items */
	TArray<TSharedPtr<FDMXFixtureFunctionItem>> ListSource;

	/** Source for the list view as an array of Cell Function Items */
	TArray<TSharedPtr<FDMXCellAttributeItem>> CellAttributeListSource;

	/** Table rows displayed in the list view */
	TArray<TSharedPtr<SDMXFunctionTableRow>> TableRows;

	/** Shared data for fixture types */
	TSharedPtr<FDMXFixtureTypeSharedData> SharedData;

	/** Command list for handling actions such as copy/paste */
	TSharedPtr<FUICommandList> CommandList;

	/** Array handle array of the Fixture Types' Modes Array */
	TSharedPtr<IPropertyHandleArray> ModesHandleArray;

	/** Request list update flag */
	bool bRefreshRequested;

	/** The actual width of the right column.  The left column is 1-ColumnWidth */
	float ColumnWidth;

	/** Container for passing around column size data to rows in the tree (each row has a splitter which can affect the column size)*/
	FDMXFunctionDetailColumnSizeData ColumnSizeData;

	/** Handle to the currently selected Mode */
	TSharedPtr<IPropertyHandle> CurrentModeHandle;
};
