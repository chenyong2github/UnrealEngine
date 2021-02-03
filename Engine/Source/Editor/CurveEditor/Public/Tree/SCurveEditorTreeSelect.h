// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

#include "CurveEditorTypes.h"
#include "CurveEditorTreeTraits.h"

class FCurveEditor;
class ITableRow;

class CURVEEDITOR_API SCurveEditorTreeSelect : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SCurveEditorTreeSelect){}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, TWeakPtr<FCurveEditor> InCurveEditor, FCurveEditorTreeItemID InTreeItemID, const TSharedRef<ITableRow>& InTableRow);

private:

	FReply SelectAll();

	const FSlateBrush* GetSelectBrush() const;

	EVisibility GetSelectVisibility() const;

private:

	TWeakPtr<ITableRow> WeakTableRow;
	TWeakPtr<FCurveEditor> WeakCurveEditor;
	FCurveEditorTreeItemID TreeItemID;
};