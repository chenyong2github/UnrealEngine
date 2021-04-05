// Copyright Epic Games, Inc. All Rights Reserved.

#include "EditorGizmos/EditorAxisSources.h"
#include "EditorModeManager.h"

FVector UGizmoEditorAxisSource::GetOrigin() const
{
	// @todo get this from the UTransformProxy instead of global?
	// @todo - per Brooke's comment, the UTransformProxy could possibly have
	// a tool target and the toolkit host could be queried from the tool target.
	// Also refer to UTypedElementViewportInteraction, the transform proxy 
	// should work in conjunction with this for typed element support.
	FEditorModeTools& EditorModeTools = GLevelEditorModeTools();
	return EditorModeTools.GetWidgetLocation();
}

FVector UGizmoEditorAxisSource::GetDirection() const
{
	// @todo get this from the UTransformProxy instead of global?
	FEditorModeTools& EditorModeTools = GLevelEditorModeTools();
	FMatrix GizmoToWorldMatrix = EditorModeTools.GetCustomInputCoordinateSystem();

	if (bLocalAxes)
	{
		EAxis::Type Axis = (AxisIndex == 0 ? EAxis::X : AxisIndex == 1 ? EAxis::Y : EAxis::Z);
		return GizmoToWorldMatrix.GetUnitAxis(Axis);
	} 
	else
	{
		FVector Axis(0, 0, 0);
		Axis[FMath::Clamp(AxisIndex, 0, 2)] = 1.0;
		return Axis;
	}
}


