// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class SSearchBox;
class FCurveEditor;
struct FCurveEditorTreeTextFilter;

class CURVEEDITOR_API SCurveEditorTreeTextFilter : public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS(SCurveEditorTreeTextFilter){}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, TSharedPtr<FCurveEditor> CurveEditor);
	
	// SWidget Interface
	virtual FReply OnFocusReceived(const FGeometry& MyGeometry, const FFocusEvent& InFocusEvent) override;
	virtual bool SupportsKeyboardFocus() const override { return true; }
	// ~SWidget Interface
private:

	void CreateSearchBox();
	void OnTreeFilterListChanged();
	void OnFilterTextChanged(const FText& FilterText);

	TSharedPtr<SSearchBox> SearchBox;
	TSharedPtr<FCurveEditorTreeTextFilter> Filter;
	TWeakPtr<FCurveEditor> WeakCurveEditor;
};