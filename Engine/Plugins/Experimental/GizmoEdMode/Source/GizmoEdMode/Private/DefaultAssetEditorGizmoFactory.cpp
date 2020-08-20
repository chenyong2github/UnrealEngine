// Copyright Epic Games, Inc. All Rights Reserved.

#include "DefaultAssetEditorGizmoFactory.h"
#include "EditorModeManager.h"
#include "Engine/Selection.h"
#include "BaseGizmos/TransformGizmo.h"
#include "BaseGizmos/TransformProxy.h"
#include "UnrealWidget.h"

bool UDefaultAssetEditorGizmoFactory::CanBuildGizmoForSelection(FEditorModeTools* ModeTools) const
{
	return true;
}

TArray<UInteractiveGizmo*> UDefaultAssetEditorGizmoFactory::BuildGizmoForSelection(FEditorModeTools* ModeTools, UInteractiveGizmoManager* GizmoManager) const
{
	ETransformGizmoSubElements Elements  = ETransformGizmoSubElements::None;
	bool bUseContextCoordinateSystem = true;
	FWidget::EWidgetMode WidgetMode = ModeTools->GetWidgetMode();
	switch ( WidgetMode )
	{
	case FWidget::EWidgetMode::WM_Translate:
		Elements = ETransformGizmoSubElements::TranslateAllAxes | ETransformGizmoSubElements::TranslateAllPlanes;
		break;
	case FWidget::EWidgetMode::WM_Rotate:
		Elements = ETransformGizmoSubElements::RotateAllAxes;
		break;
	case FWidget::EWidgetMode::WM_Scale:
		Elements = ETransformGizmoSubElements::ScaleAllAxes | ETransformGizmoSubElements::ScaleAllPlanes;
		bUseContextCoordinateSystem = false;
		break;
	case FWidget::EWidgetMode::WM_2D:
		Elements = ETransformGizmoSubElements::RotateAxisY | ETransformGizmoSubElements::TranslatePlaneXZ;
		break;
	default:
		Elements = ETransformGizmoSubElements::FullTranslateRotateScale;
		break;
	}
	UTransformGizmo* TransformGizmo = GizmoManager->CreateCustomTransformGizmo(Elements);
	TransformGizmo->bUseContextCoordinateSystem = bUseContextCoordinateSystem;

	TArray<AActor*> SelectedActors;
	ModeTools->GetSelectedActors()->GetSelectedObjects<AActor>(SelectedActors);

	UTransformProxy* TransformProxy = NewObject<UTransformProxy>();
	for (auto Actor : SelectedActors)
	{
		USceneComponent* SceneComponent = Actor->GetRootComponent();
		TransformProxy->AddComponent(SceneComponent);
	}
	TransformGizmo->SetActiveTarget( TransformProxy );
	TransformGizmo->SetVisibility(SelectedActors.Num() > 0);

	TArray<UInteractiveGizmo*> Gizmos;

	Gizmos.Add(TransformGizmo);

	return Gizmos;
}

void UDefaultAssetEditorGizmoFactory::ConfigureGridSnapping(bool bGridEnabled, bool bRotGridEnabled, const TArray<UInteractiveGizmo*>& Gizmos) const
{
	for (auto& Gizmo : Gizmos)
	{
		UTransformGizmo* TransformGizmo = Cast<UTransformGizmo>(Gizmo);

		if (TransformGizmo)
		{
			TransformGizmo->bSnapToWorldGrid = bGridEnabled;
			TransformGizmo->bSnapToWorldRotGrid = bRotGridEnabled;
		}
	}

}
