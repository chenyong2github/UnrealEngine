// Copyright Epic Games, Inc. All Rights Reserved.

#include "SCameraCalibrationCurveEditorView.h"

#include "CameraCalibrationCurveEditor.h"
#include "CurveEditorCommands.h"
#include "CurveEditorTypes.h"
#include "Framework/Commands/GenericCommands.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Misc/Optional.h"
#include "SCurveEditorPanel.h"

#define LOCTEXT_NAMESPACE "SCameraCalibrationCurveEditorView"


/**
 * Mostly copy of CurveEditorContextMenu.h
 * Building custom context menu with custom Camera Calibration buttons and handlers
 */
struct FCurveEditorContextMenu
{
	static void BuildMenu(FMenuBuilder& MenuBuilder, const TSharedRef<FCameraCalibrationCurveEditor>& CurveEditor, TOptional<FCurveModelID> HoveredCurveID)
	{
		int32 NumSelectedKeys = CurveEditor->GetSelection().Count();

		TSharedPtr<FCameraCalibrationCurveEditor> LocalCurveEditor = CurveEditor;

		const FCurveModel* HoveredCurve = HoveredCurveID.IsSet() ? CurveEditor->FindCurve(HoveredCurveID.GetValue()) : nullptr;

		// Add curve button action
		const FUIAction AddButtonAction
		(
			FExecuteAction::CreateLambda([LocalCurveEditor]()
			{
				LocalCurveEditor->OnAddDataPointDelegate.ExecuteIfBound();
			})
		);

		// We prioritize key selections over curve selections to reduce the pixel-perfectness needed
		// to edit the keys (which is more common than curves). Right clicking on a key or an empty space
		// should show the key menu, otherwise we show the curve menu (ie: right clicking on a curve, not 
		// directly over a key).
		if (NumSelectedKeys > 0 && !HoveredCurveID.IsSet())
		{
			MenuBuilder.BeginSection("CurveEditorKeySection", FText::Format(LOCTEXT("CurveEditorKeySection", "{0} Selected {0}|plural(one=Key,other=Keys)"), NumSelectedKeys));
			{
				bool bIsReadOnly = false;
				if (HoveredCurve)
				{
					bIsReadOnly = HoveredCurve->IsReadOnly();
				}

				if (!bIsReadOnly)
				{
					MenuBuilder.AddMenuEntry(FCurveEditorCommands::Get().FlattenTangents);
					MenuBuilder.AddMenuEntry(FCurveEditorCommands::Get().StraightenTangents);

					MenuBuilder.AddMenuSeparator();

					// Tangent Types
					MenuBuilder.AddMenuEntry(FCurveEditorCommands::Get().InterpolationCubicAuto);
					MenuBuilder.AddMenuEntry(FCurveEditorCommands::Get().InterpolationCubicUser);
					MenuBuilder.AddMenuEntry(FCurveEditorCommands::Get().InterpolationCubicBreak);
					MenuBuilder.AddMenuEntry(FCurveEditorCommands::Get().InterpolationLinear);
					MenuBuilder.AddMenuEntry(FCurveEditorCommands::Get().InterpolationConstant);

					MenuBuilder.AddMenuSeparator();
				}

				// View
				MenuBuilder.AddMenuEntry(FCurveEditorCommands::Get().ZoomToFit);
			}
			MenuBuilder.EndSection();
		}
		else
		{
			auto CreateUnselectedKeysMenu = [&MenuBuilder, &AddButtonAction](FName InExtensionHook, const TAttribute< FText >& InHeadingText, const bool bInIsCurveReadOnly)
			{
				MenuBuilder.BeginSection(InExtensionHook, InHeadingText);
				if (!bInIsCurveReadOnly)
				{
					// Add Curve Point
					MenuBuilder.AddMenuEntry(
						LOCTEXT("AddDataPointLabel", "Add Data Point"),
						LOCTEXT("AddDataPointTooltip", "Add a new key to all curves at the current time"),
						FSlateIcon(),
						AddButtonAction);

					MenuBuilder.AddMenuSeparator();

					// View
					MenuBuilder.AddMenuEntry(FCurveEditorCommands::Get().ZoomToFit);
				}

				MenuBuilder.EndSection();
			};
			
			if (HoveredCurve)
			{
				CreateUnselectedKeysMenu(
					TEXT("CurveEditorCurveSection"),
					FText::Format(LOCTEXT("CurveNameFormat", "Curve '{0}'"), HoveredCurve->GetLongDisplayName()),
					HoveredCurve->IsReadOnly());
			}
			else
			{
				// Test if at least one curve is editable
				bool bIsReadOnly = true;
				TSet<FCurveModelID> CurvesToAddTo;
				for(const FCurveModelID& CurveModelID : CurveEditor->GetEditedCurves())
				{
					if (const FCurveModel* CurveModel = CurveEditor->FindCurve(CurveModelID))
					{
						if (!CurveModel->IsReadOnly())
						{
							bIsReadOnly = false;
							break;
						}
					}
				}

				CreateUnselectedKeysMenu(
					TEXT("CurveEditorAllCurveSections"),
					LOCTEXT("CurveEditorAllCurveSections", "All Curves"),
					bIsReadOnly);
			}
		}
	}
};

void SCameraCalibrationCurveEditorView::Construct(const FArguments& InArgs, TWeakPtr<FCurveEditor> InCurveEditor)
{
	FArguments ParentArguments;
	ParentArguments.AutoSize(false);
	
	SCurveEditorViewAbsolute::Construct(ParentArguments, InCurveEditor);
}

FReply SCameraCalibrationCurveEditorView::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		CreateContextMenu(MyGeometry, MouseEvent);	
		return FReply::Handled();
	}

	return Super::OnMouseButtonUp(MyGeometry, MouseEvent);
}

FReply SCameraCalibrationCurveEditorView::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	// Don't handle updating if we have a context menu open.
	if (ActiveContextMenu.Pin())
	{
		return FReply::Unhandled();
	}
	
	return Super::OnMouseMove(MyGeometry, MouseEvent);
}

void SCameraCalibrationCurveEditorView::CreateContextMenu(const FGeometry& InMyGeometry, const FPointerEvent& InMouseEvent)
{
	const TSharedPtr<FCurveEditor> CurveEditor = WeakCurveEditor.Pin();
	const TSharedPtr<SCurveEditorPanel> EditorPanel = CurveEditor.IsValid() ? CurveEditor->GetPanel() : nullptr;
	if (!CurveEditor || !EditorPanel)
	{
		return;
	}

	// Cast to Camera Calibration Curve Editor
	const TSharedPtr<FCameraCalibrationCurveEditor> CameraCalibrationCurveEditor = StaticCastSharedPtr<FCameraCalibrationCurveEditor>(CurveEditor);

	constexpr  bool bCloseAfterSelection = true;
	FMenuBuilder MenuBuilder(bCloseAfterSelection, EditorPanel->GetCommands());

	FCurveEditorContextMenu::BuildMenu(MenuBuilder, CameraCalibrationCurveEditor.ToSharedRef(), GetHoveredCurve());

	// Push the context menu
	const FWidgetPath WidgetPath = InMouseEvent.GetEventPath() != nullptr ? *InMouseEvent.GetEventPath() : FWidgetPath();
	ActiveContextMenu = FSlateApplication::Get().PushMenu(AsShared(), WidgetPath, MenuBuilder.MakeWidget(),
	                                                      FSlateApplication::Get().GetCursorPos(),
	                                                      FPopupTransitionEffect(FPopupTransitionEffect::ContextMenu));
}

#undef LOCTEXT_NAMESPACE
