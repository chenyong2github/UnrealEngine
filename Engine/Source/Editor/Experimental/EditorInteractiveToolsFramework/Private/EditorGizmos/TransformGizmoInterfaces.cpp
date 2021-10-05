// Copyright Epic Games, Inc. All Rights Reserved.

#include "EditorGizmos/TransformGizmoInterfaces.h"
#include "EditorModeManager.h"
#include "EditorViewportClient.h"

EGizmoTransformMode FTransformGizmoUtil::GetGizmoMode(UE::Widget::EWidgetMode InWidgetMode)
{
	switch (InWidgetMode)
	{
		case UE::Widget::EWidgetMode::WM_Translate: return EGizmoTransformMode::Translate;
		case UE::Widget::EWidgetMode::WM_Rotate: return EGizmoTransformMode::Rotate;
		case UE::Widget::EWidgetMode::WM_Scale: return EGizmoTransformMode::Scale;
	}
	return EGizmoTransformMode::None;
}

UE::Widget::EWidgetMode FTransformGizmoUtil::GetWidgetMode(EGizmoTransformMode InGizmoMode)
{
	switch (InGizmoMode)
	{
		case EGizmoTransformMode::Translate: return UE::Widget::EWidgetMode::WM_Translate;
		case EGizmoTransformMode::Rotate: return UE::Widget::EWidgetMode::WM_Rotate;
		case EGizmoTransformMode::Scale: return UE::Widget::EWidgetMode::WM_Scale;
	}
	return UE::Widget::EWidgetMode::WM_None;
}
