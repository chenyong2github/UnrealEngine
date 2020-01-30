// Copyright Epic Games, Inc. All Rights Reserved.

#include "CurveEditorSettings.h"

UCurveEditorSettings::UCurveEditorSettings()
{
	bAutoFrameCurveEditor = true;
	bShowCurveEditorCurveToolTips = true;
	TangentVisibility = ECurveEditorTangentVisibility::SelectedKeys;
	ZoomPosition = ECurveEditorZoomPosition::CurrentTime;
}

bool UCurveEditorSettings::GetAutoFrameCurveEditor() const
{
	return bAutoFrameCurveEditor;
}

void UCurveEditorSettings::SetAutoFrameCurveEditor(bool InbAutoFrameCurveEditor)
{
	if (bAutoFrameCurveEditor != InbAutoFrameCurveEditor)
	{
		bAutoFrameCurveEditor = InbAutoFrameCurveEditor;
		SaveConfig();
	}
}

bool UCurveEditorSettings::GetShowCurveEditorCurveToolTips() const
{
	return bShowCurveEditorCurveToolTips;
}

void UCurveEditorSettings::SetShowCurveEditorCurveToolTips(bool InbShowCurveEditorCurveToolTips)
{
	if (bShowCurveEditorCurveToolTips != InbShowCurveEditorCurveToolTips)
	{
		bShowCurveEditorCurveToolTips = InbShowCurveEditorCurveToolTips;
		SaveConfig();
	}
}

ECurveEditorTangentVisibility UCurveEditorSettings::GetTangentVisibility() const
{
	return TangentVisibility;
}

void UCurveEditorSettings::SetTangentVisibility(ECurveEditorTangentVisibility InTangentVisibility)
{
	if (TangentVisibility != InTangentVisibility)
	{
		TangentVisibility = InTangentVisibility;
		SaveConfig();
	}
}

ECurveEditorZoomPosition UCurveEditorSettings::GetZoomPosition() const
{
	return ZoomPosition;
}

void UCurveEditorSettings::SetZoomPosition(ECurveEditorZoomPosition InZoomPosition)
{
	if (ZoomPosition != InZoomPosition)
	{
		ZoomPosition = InZoomPosition;
		SaveConfig();
	}
}

