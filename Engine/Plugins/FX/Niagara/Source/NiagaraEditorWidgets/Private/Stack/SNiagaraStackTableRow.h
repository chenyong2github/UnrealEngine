// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/Views/STableRow.h"
#include "ViewModels/Stack/NiagaraStackEntry.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Widgets/Views/STreeView.h"
#include "Styling/SlateTypes.h"
#include "Layout/Visibility.h"

class UNiagaraStackViewModel;
class UNiagaraStackEntry;
class FNiagaraStackCommandContext;

class SNiagaraStackTableRow: public STableRow<UNiagaraStackEntry*>
{
public:
	DECLARE_DELEGATE_OneParam(FOnColumnWidthChanged, float);
	DECLARE_DELEGATE_OneParam(FOnFillRowContextMenu, FMenuBuilder&);

public:
	SLATE_BEGIN_ARGS(SNiagaraStackTableRow)
		: _ContentPadding(FMargin(2, 0, 2, 0))
		, _ItemBackgroundColor(FLinearColor::Transparent)
		, _IsCategoryIconHighlighted(false)
		, _ShowExecutionCategoryIcon(false)
		, _RowPadding(FMargin(0, 0, 0, 0))
	{}
	SLATE_ARGUMENT(FMargin, ContentPadding)
		SLATE_ARGUMENT(FLinearColor, ItemBackgroundColor)
		SLATE_ARGUMENT(FLinearColor, ItemForegroundColor)
		SLATE_ARGUMENT(bool, IsCategoryIconHighlighted)
		SLATE_ARGUMENT(bool, ShowExecutionCategoryIcon)
		SLATE_ATTRIBUTE(float, NameColumnWidth)
		SLATE_ATTRIBUTE(float, ValueColumnWidth)
		SLATE_ATTRIBUTE(EVisibility, IssueIconVisibility)
		SLATE_ATTRIBUTE(FMargin, RowPadding)
		SLATE_EVENT(FOnColumnWidthChanged, OnNameColumnWidthChanged)
		SLATE_EVENT(FOnColumnWidthChanged, OnValueColumnWidthChanged)
		SLATE_EVENT(FOnDragDetected, OnDragDetected)
		SLATE_EVENT(FOnTableRowDragLeave, OnDragLeave)
		SLATE_EVENT(FOnCanAcceptDrop, OnCanAcceptDrop)
		SLATE_EVENT(FOnAcceptDrop, OnAcceptDrop);
	SLATE_END_ARGS();

	void Construct(const FArguments& InArgs, UNiagaraStackViewModel* InStackViewModel, UNiagaraStackEntry* InStackEntry, TSharedRef<FNiagaraStackCommandContext> InStackCommandContext, const TSharedRef<STreeView<UNiagaraStackEntry*>>& InOwnerTree);

	void SetOverrideNameWidth(TOptional<float> InMinWidth, TOptional<float> InMaxWidth);

	void SetOverrideNameAlignment(EHorizontalAlignment InHAlign, EVerticalAlignment InVAlign);

	void SetOverrideValueWidth(TOptional<float> InMinWidth, TOptional<float> InMaxWidth);

	void SetOverrideValueAlignment(EHorizontalAlignment InHAlign, EVerticalAlignment InVAlign);

	FMargin GetContentPadding() const;

	void SetContentPadding(FMargin InContentPadding);

	void SetNameAndValueContent(TSharedRef<SWidget> InNameWidget, TSharedPtr<SWidget> InValueWidget);

	void AddFillRowContextMenuHandler(FOnFillRowContextMenu FillRowContextMenuHandler);

	virtual FReply OnMouseButtonDoubleClick(const FGeometry& InMyGeometry, const FPointerEvent& InMouseEvent) override;

	FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent);

	virtual const FSlateBrush* GetBorder() const override;

private:
	void CollapseChildren();

	void ExpandChildren();

	EVisibility GetRowVisibility() const;

	EVisibility GetExecutionCategoryIconVisibility() const;

	FOptionalSize GetIndentSize() const;

	EVisibility GetExpanderVisibility() const;

	FReply ExpandButtonClicked();

	const FSlateBrush* GetExpandButtonImage() const;

	void OnNameColumnWidthChanged(float Width);

	void OnValueColumnWidthChanged(float Width);

	FSlateColor GetItemBackgroundColor() const;

	const FSlateBrush* GetSelectionBorderBrush() const;

	const FSlateBrush* GetSearchResultBorderBrush() const;

	void NavigateTo(UNiagaraStackEntry* Item);

private:
	UNiagaraStackViewModel* StackViewModel;
	UNiagaraStackEntry* StackEntry;
	TSharedPtr<STreeView<UNiagaraStackEntry*>> OwnerTree;

	TAttribute<float> NameColumnWidth;
	TAttribute<float> ValueColumnWidth;
	FOnColumnWidthChanged NameColumnWidthChanged;
	FOnColumnWidthChanged ValueColumnWidthChanged;

	TAttribute<EVisibility> IssueIconVisibility;
	TAttribute<FMargin> RowPadding;

	const FSlateBrush* ExpandedImage;
	const FSlateBrush* CollapsedImage;

	FLinearColor ItemBackgroundColor;
	FLinearColor DisabledItemBackgroundColor;
	FLinearColor ForegroundColor;

	FText ExecutionCategoryToolTipText;

	FMargin ContentPadding;

	EHorizontalAlignment NameHorizontalAlignment;
	EVerticalAlignment NameVerticalAlignment;

	TOptional<float> NameMinWidth;
	TOptional<float> NameMaxWidth;

	EHorizontalAlignment ValueHorizontalAlignment;
	EVerticalAlignment ValueVerticalAlignment;

	TOptional<float> ValueMinWidth;
	TOptional<float> ValueMaxWidth;

	bool bIsCategoryIconHighlighted;
	bool bShowExecutionCategoryIcon;

	TArray<FOnFillRowContextMenu> OnFillRowContextMenuHanders;

	TSharedPtr<FNiagaraStackCommandContext> StackCommandContext;
};