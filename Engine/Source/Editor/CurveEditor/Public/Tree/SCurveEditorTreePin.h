// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

#include "CurveEditorTypes.h"
#include "CurveEditorTreeTraits.h"

class FCurveEditor;
class ITableRow;

class CURVEEDITOR_API SCurveEditorTreePin : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SCurveEditorTreePin){}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, TWeakPtr<FCurveEditor> InCurveEditor, FCurveEditorTreeItemID InTreeItemID, const TSharedRef<ITableRow>& InTableRow);

private:

	FReply TogglePinned();
	const FSlateBrush* GetPinBrush() const;

	bool IsPinnedRecursive(FCurveEditorTreeItemID InTreeItem, FCurveEditor* CurveEditor) const;

	void PinRecursive(FCurveEditorTreeItemID InTreeItem, FCurveEditor* CurveEditor) const;

	void UnpinRecursive(FCurveEditorTreeItemID InTreeItem, FCurveEditor* CurveEditor) const;

	EVisibility GetPinVisibility() const;

private:

	TWeakPtr<ITableRow> WeakTableRow;
	TWeakPtr<FCurveEditor> WeakCurveEditor;
	FCurveEditorTreeItemID TreeItemID;
};