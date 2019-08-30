// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "CurveEditorContextMenu.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Commands/GenericCommands.h"
#include "CurveEditor.h"
#include "CurveEditorCommands.h"

#define LOCTEXT_NAMESPACE "CurveEditorContextMenu"

void FCurveEditorContextMenu::BuildMenu(FMenuBuilder& MenuBuilder, TSharedRef<FCurveEditor> CurveEditor, TOptional<FCurvePointHandle> ClickedPoint, TOptional<FCurveModelID> HoveredCurveID)
{
	int32 NumSelectedKeys = CurveEditor->GetSelection().Count();

	TSharedPtr<FCurveEditor> LocalCurveEditor = CurveEditor->AsShared();

	// We change the name to reflect the current number of curves selected.
	TAttribute<FText> ApplyBufferedCurvesText = TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateLambda([LocalCurveEditor] {
		return FText::Format(LOCTEXT("ApplyStoredCurvesContextMenu", "Apply {0} Stored Curves"), LocalCurveEditor->GetNumBufferedCurves());
	}));

	// We prioritize key selections over curve selections to reduce the pixel-perfectness needed
	// to edit the keys (which is more common than curves). Right clicking on a key or an empty space
	// should show the key menu, otherwise we show the curve menu (ie: right clicking on a curve, not 
	// directly over a key).
	if (NumSelectedKeys > 0 && (!HoveredCurveID.IsSet() || ClickedPoint.IsSet()))
	{
		MenuBuilder.BeginSection("CurveEditorKeySection", FText::Format(LOCTEXT("CurveEditorKeySection", "{0} Selected {0}|plural(one=Key,other=Keys)"), NumSelectedKeys));
		{
			// Modify Data
			MenuBuilder.AddMenuEntry(FGenericCommands::Get().Delete);
			
			MenuBuilder.AddMenuEntry(FCurveEditorCommands::Get().FlattenTangents);
			MenuBuilder.AddMenuEntry(FCurveEditorCommands::Get().StraightenTangents);

			MenuBuilder.AddMenuSeparator();

			// Tangent Types
			MenuBuilder.AddMenuEntry(FCurveEditorCommands::Get().InterpolationCubicAuto);
			MenuBuilder.AddMenuEntry(FCurveEditorCommands::Get().InterpolationCubicUser);
			MenuBuilder.AddMenuEntry(FCurveEditorCommands::Get().InterpolationCubicBreak);
			MenuBuilder.AddMenuEntry(FCurveEditorCommands::Get().InterpolationLinear);
			MenuBuilder.AddMenuEntry(FCurveEditorCommands::Get().InterpolationConstant);
			MenuBuilder.AddMenuEntry(FCurveEditorCommands::Get().InterpolationToggleWeighted);

			MenuBuilder.AddMenuSeparator();

			// Filters
			MenuBuilder.AddMenuEntry(FCurveEditorCommands::Get().OpenUserImplementableFilterWindow);
		}
		MenuBuilder.EndSection();
	}
	else
	{
		const FCurveModel* HoveredCurve = HoveredCurveID.IsSet() ? CurveEditor->FindCurve(HoveredCurveID.GetValue()) : nullptr;
		if (HoveredCurve)
		{
			MenuBuilder.BeginSection("CurveEditorCurveSection", FText::Format(LOCTEXT("CurveNameFormat", "Curve '{0}'"), HoveredCurve->GetLongDisplayName()));
			{
				// Buffer Curves
				MenuBuilder.AddMenuEntry(FCurveEditorCommands::Get().BufferVisibleCurves);
				MenuBuilder.AddMenuEntry(FCurveEditorCommands::Get().ApplyBufferedCurves, NAME_None, ApplyBufferedCurvesText);
				MenuBuilder.AddMenuSeparator();

				// Modify Curve
				MenuBuilder.AddMenuEntry(FCurveEditorCommands::Get().AddKeyHovered);

				MenuBuilder.AddSubMenu(LOCTEXT("PreExtrapText", "Pre-Extrap"), FText(), FNewMenuDelegate::CreateLambda(
					[](FMenuBuilder& SubMenu)
					{
						SubMenu.AddMenuEntry(FCurveEditorCommands::Get().SetPreInfinityExtrapCycle);
						SubMenu.AddMenuEntry(FCurveEditorCommands::Get().SetPreInfinityExtrapCycleWithOffset);
						SubMenu.AddMenuEntry(FCurveEditorCommands::Get().SetPreInfinityExtrapOscillate);
						SubMenu.AddMenuEntry(FCurveEditorCommands::Get().SetPreInfinityExtrapLinear);
						SubMenu.AddMenuEntry(FCurveEditorCommands::Get().SetPreInfinityExtrapConstant);
					})
				);

				MenuBuilder.AddSubMenu(LOCTEXT("PostExtrapText", "Post-Extrap"), FText(), FNewMenuDelegate::CreateLambda(
					[](FMenuBuilder& SubMenu)
					{
						SubMenu.AddMenuEntry(FCurveEditorCommands::Get().SetPostInfinityExtrapCycle);
						SubMenu.AddMenuEntry(FCurveEditorCommands::Get().SetPostInfinityExtrapCycleWithOffset);
						SubMenu.AddMenuEntry(FCurveEditorCommands::Get().SetPostInfinityExtrapOscillate);
						SubMenu.AddMenuEntry(FCurveEditorCommands::Get().SetPostInfinityExtrapLinear);
						SubMenu.AddMenuEntry(FCurveEditorCommands::Get().SetPostInfinityExtrapConstant);
					})
				);

				MenuBuilder.AddMenuSeparator();

				// Filters
				MenuBuilder.AddMenuEntry(FCurveEditorCommands::Get().OpenUserImplementableFilterWindow);

			}
			MenuBuilder.EndSection();
		}
		else
		{
			MenuBuilder.BeginSection("CurveEditorAllCurveSections", LOCTEXT("CurveEditorAllCurveSections", "All Curves"));
			{
				// Buffer Curves
				MenuBuilder.AddMenuEntry(FCurveEditorCommands::Get().BufferVisibleCurves);
				MenuBuilder.AddMenuEntry(FCurveEditorCommands::Get().ApplyBufferedCurves, NAME_None, ApplyBufferedCurvesText);
				MenuBuilder.AddMenuSeparator();
				
				// Modify Curves
				MenuBuilder.AddMenuEntry(FCurveEditorCommands::Get().AddKeyToAllCurves);
				MenuBuilder.AddMenuSeparator();

				// Filters
				MenuBuilder.AddMenuEntry(FCurveEditorCommands::Get().OpenUserImplementableFilterWindow);
			}
			MenuBuilder.EndSection();
		}
	}
}

#undef LOCTEXT_NAMESPACE