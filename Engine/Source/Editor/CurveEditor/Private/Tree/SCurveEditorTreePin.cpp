// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tree/SCurveEditorTreePin.h"
#include "CurveEditor.h"
#include "Algo/AllOf.h"

#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Views/STableRow.h"

#include "EditorStyleSet.h"


void SCurveEditorTreePin::Construct(const FArguments& InArgs, TWeakPtr<FCurveEditor> InCurveEditor, FCurveEditorTreeItemID InTreeItemID, const TSharedRef<ITableRow>& InTableRow)
{
	WeakCurveEditor = InCurveEditor;
	WeakTableRow = InTableRow;
	TreeItemID = InTreeItemID;

	ChildSlot
	[
		SNew(SButton)
		.ButtonStyle(FEditorStyle::Get(), "HoverHintOnly")
		.Visibility(this, &SCurveEditorTreePin::GetPinVisibility)
		.OnClicked(this, &SCurveEditorTreePin::TogglePinned)
		[
			SNew(SImage)
			.Image(this, &SCurveEditorTreePin::GetPinBrush)
		]
	];
}

FReply SCurveEditorTreePin::TogglePinned()
{
	TSharedPtr<FCurveEditor> CurveEditor = WeakCurveEditor.Pin();
	if (CurveEditor)
	{
		if (IsPinnedRecursive(TreeItemID, CurveEditor.Get()))
		{
			UnpinRecursive(TreeItemID, CurveEditor.Get());
		}
		else
		{
			PinRecursive(TreeItemID, CurveEditor.Get());
		}
	}
	return FReply::Handled();
}

void SCurveEditorTreePin::PinRecursive(FCurveEditorTreeItemID InTreeItem, FCurveEditor* CurveEditor) const
{
	FCurveEditorTreeItem& Item = CurveEditor->GetTreeItem(InTreeItem);

	for (FCurveModelID CurveID : Item.GetOrCreateCurves(CurveEditor))
	{
		CurveEditor->PinCurve(CurveID);
	}

	for (FCurveEditorTreeItemID Child : Item.GetChildren())
	{
		PinRecursive(Child, CurveEditor);
	}
}

void SCurveEditorTreePin::UnpinRecursive(FCurveEditorTreeItemID InTreeItem, FCurveEditor* CurveEditor) const
{
	const bool bIsSelected = CurveEditor->GetTreeSelectionState(InTreeItem) == ECurveEditorTreeSelectionState::Explicit;

	FCurveEditorTreeItem& Item = CurveEditor->GetTreeItem(InTreeItem);
	for (FCurveModelID CurveID : Item.GetCurves())
	{
		if (bIsSelected)
		{
			CurveEditor->UnpinCurve(CurveID);
		}
		else
		{
			Item.DestroyCurves(CurveEditor);
		}
	}

	for (FCurveEditorTreeItemID Child : Item.GetChildren())
	{
		UnpinRecursive(Child, CurveEditor);
	}
}

bool SCurveEditorTreePin::IsPinnedRecursive(FCurveEditorTreeItemID InTreeItem, FCurveEditor* CurveEditor) const
{
	const FCurveEditorTreeItem& Item = CurveEditor->GetTreeItem(InTreeItem);

	TArrayView<const FCurveModelID>          Curves   = Item.GetCurves();
	TArrayView<const FCurveEditorTreeItemID> Children = Item.GetChildren();

	const bool bAllChildren = Algo::AllOf(Children, [this, CurveEditor](FCurveEditorTreeItemID In){ return this->IsPinnedRecursive(In, CurveEditor); });

	if (Curves.Num() == 0)
	{
		return Children.Num() > 0 && bAllChildren;
	}

	const bool bAllCurves = Algo::AllOf(Curves, [CurveEditor](FCurveModelID In) { return CurveEditor->IsCurvePinned(In); });
	return bAllCurves && bAllChildren;
}

EVisibility SCurveEditorTreePin::GetPinVisibility() const
{
	TSharedPtr<FCurveEditor> CurveEditor = WeakCurveEditor.Pin();
	TSharedPtr<ITableRow> Row = WeakTableRow.Pin();
	TSharedPtr<SWidget> RowWidget = Row ? TSharedPtr<SWidget>(Row->AsWidget()) : nullptr;

	if (RowWidget && RowWidget->IsHovered())
	{
		return EVisibility::Visible;
	}
	else if (CurveEditor && IsPinnedRecursive(TreeItemID, CurveEditor.Get()))
	{
		return EVisibility::Visible;
	}
	return EVisibility::Collapsed;
}

const FSlateBrush* SCurveEditorTreePin::GetPinBrush() const
{
	TSharedPtr<FCurveEditor> CurveEditor = WeakCurveEditor.Pin();
	if (CurveEditor)
	{
		if (IsPinnedRecursive(TreeItemID, CurveEditor.Get()))
		{
			return FEditorStyle::GetBrush("GenericCurveEditor.Pin_Active");
		}
	}

	return FEditorStyle::GetBrush("GenericCurveEditor.Pin_Inactive");
}