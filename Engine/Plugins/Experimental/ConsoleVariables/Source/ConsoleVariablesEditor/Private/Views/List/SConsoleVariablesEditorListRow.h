// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SConsoleVariablesEditorList.h"

#include "CoreMinimal.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/SCompoundWidget.h"

class SConsoleVariablesEditorListValueInput;
class SConsoleVariablesEditorListRowHoverWidgets;

class SConsoleVariablesEditorListRow : public SCompoundWidget
{
public:
	
	SLATE_BEGIN_ARGS(SConsoleVariablesEditorListRow)
	{}

	SLATE_END_ARGS()
	
	void Construct(const FArguments& InArgs, const TWeakPtr<FConsoleVariablesEditorListRow> InRow, const FConsoleVariablesEditorListSplitterManagerPtr& InSplitterManagerPtr);

	void FlashRow() const;
	
	virtual void OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;

	virtual void OnMouseLeave(const FPointerEvent& MouseEvent) override;

	virtual ~SConsoleVariablesEditorListRow() override;

	float GetNameColumnSize() const;

	float CalculateAndReturnNestedColumnSize();

	float GetSourceColumnSize() const;

	float GetValueColumnSize() const;

	void SetNestedColumnSize(const float InWidth) const;

	void SetSourceColumnSize(const float InWidth) const;

private:
	
	TSharedPtr<SSplitter> OuterSplitterPtr;
	TSharedPtr<SSplitter> NestedSplitterPtr;
	
	TWeakPtr<FConsoleVariablesEditorListRow> Item;

	TSharedPtr<SBorder> BorderPtr;

	TSharedPtr<SConsoleVariablesEditorListValueInput> ValueChildInputWidget;

	/* For splitter sync */

	/* To sync up splitter location in tree view items, we need to account for the tree view's indentation.
	 * Instead of calculating the coefficient twice each frame (for left and right splitter slots), we do it once and cache it here. */
	float CachedNestedColumnWidthAdjusted = 0.f;
	
	FConsoleVariablesEditorListSplitterManagerPtr SplitterManagerPtr;

	TSharedPtr<SConsoleVariablesEditorListRowHoverWidgets> HoverableWidgetsPtr;
};

class SConsoleVariablesEditorListRowHoverWidgets : public SCompoundWidget
{
public:
	
	SLATE_BEGIN_ARGS(SConsoleVariablesEditorListRowHoverWidgets)
	{}

	SLATE_END_ARGS()
	
	void Construct(const FArguments& InArgs, const TWeakPtr<FConsoleVariablesEditorListRow> InRow);

	virtual void OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;

	virtual void OnMouseLeave(const FPointerEvent& MouseEvent) override;

	virtual ~SConsoleVariablesEditorListRowHoverWidgets() override;

private:
	
	TWeakPtr<FConsoleVariablesEditorListRow> Item;
	
	TSharedPtr<SButton> RemoveButtonPtr;
};
