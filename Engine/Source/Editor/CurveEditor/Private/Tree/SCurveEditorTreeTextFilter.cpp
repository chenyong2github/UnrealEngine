// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Tree/SCurveEditorTreeTextFilter.h"
#include "Tree/CurveEditorTreeFilter.h"
#include "Tree/CurveEditorTree.h"
#include "CurveEditor.h"

#include "Widgets/Input/SSearchBox.h"


void SCurveEditorTreeTextFilter::Construct(const FArguments& InArgs, TSharedPtr<FCurveEditor> CurveEditor)
{
	WeakCurveEditor = CurveEditor;

	ChildSlot
	[
		SNew(SSearchBox)
		.HintText(NSLOCTEXT("CurveEditor", "TextFilterHint", "Filter"))
		.OnTextChanged(this, &SCurveEditorTreeTextFilter::OnFilterTextChanged)
	];
}

void SCurveEditorTreeTextFilter::OnFilterTextChanged( const FText& FilterText )
{
	TSharedPtr<FCurveEditor> CurveEditor = WeakCurveEditor.Pin();
	if (CurveEditor)
	{
		if (!Filter)
		{
			Filter = MakeShared<FCurveEditorTreeTextFilter>();
		}

		Filter->FilterTerms.Reset();

		static const bool bCullEmpty = true;
		FilterText.ToString().ParseIntoArray(Filter->FilterTerms, TEXT(" "), bCullEmpty);

		if (Filter->FilterTerms.Num() > 0)
		{
			CurveEditor->GetTree()->AddFilter(Filter);
		}
		else
		{
			CurveEditor->GetTree()->RemoveFilter(Filter);
		}

		CurveEditor->GetTree()->RunFilters();
	}
}