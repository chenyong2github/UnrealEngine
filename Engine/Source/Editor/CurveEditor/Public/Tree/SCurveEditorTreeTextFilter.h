// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class FCurveEditor;
struct FCurveEditorTreeTextFilter;

class CURVEEDITOR_API SCurveEditorTreeTextFilter : public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS(SCurveEditorTreeTextFilter){}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, TSharedPtr<FCurveEditor> CurveEditor);

private:

	void OnFilterTextChanged(const FText& FilterText);

	TSharedPtr<FCurveEditorTreeTextFilter> Filter;
	TWeakPtr<FCurveEditor> WeakCurveEditor;
};