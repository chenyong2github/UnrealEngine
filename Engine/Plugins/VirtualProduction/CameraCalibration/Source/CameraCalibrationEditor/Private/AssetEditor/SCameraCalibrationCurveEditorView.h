// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Views/SCurveEditorViewAbsolute.h"

class IMenu;

/**
 * A Camera Calibration curve view supporting custom context menu
 */
class SCameraCalibrationCurveEditorView : public SCurveEditorViewAbsolute
{
	using Super = SCurveEditorViewAbsolute;
	
public:
	void Construct(const FArguments& InArgs, TWeakPtr<FCurveEditor> InCurveEditor);

private:
	//~ Begin SInteractiveCurveEditorView interface
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	//~ End SInteractiveCurveEditorView interface

	/** Create context menu for Editor View widget */
	void CreateContextMenu(const FGeometry& InMyGeometry, const FPointerEvent& InMouseEvent);

private:
	/** Track if we have a context menu active. Used to suppress hover updates as it causes flickers in the CanExecute bindings. */
	TWeakPtr<IMenu> ActiveContextMenu;
};
