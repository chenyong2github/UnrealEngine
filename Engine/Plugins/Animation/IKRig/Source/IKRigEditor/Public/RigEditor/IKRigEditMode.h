// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "IKRigDefinition.h"
#include "IPersonaEditMode.h"

class FIKRigEditorController;
class UIKRigEffectorGoal;
class FIKRigEditorToolkit;
class FIKRigPreviewScene;

struct GoalGizmo
{
	GoalGizmo()
	{
		// wireframe cube lines (point pairs)
		BoxPoints.Add(FVector(0.5f, 0.5f, 0.5f));
		BoxPoints.Add(FVector(0.5f, -0.5f, 0.5f));
		BoxPoints.Add(FVector(0.5f, -0.5f, 0.5f));
		BoxPoints.Add(FVector(-0.5f, -0.5f, 0.5f));
		BoxPoints.Add(FVector(-0.5f, -0.5f, 0.5f));
		BoxPoints.Add(FVector(-0.5f, 0.5f, 0.5f));
		BoxPoints.Add(FVector(-0.5f, 0.5f, 0.5f));
		BoxPoints.Add(FVector(0.5f, 0.5f, 0.5f));

		BoxPoints.Add(FVector(0.5f, 0.5f, -0.5f));
		BoxPoints.Add(FVector(0.5f, -0.5f, -0.5f));
		BoxPoints.Add(FVector(0.5f, -0.5f, -0.5f));
		BoxPoints.Add(FVector(-0.5f, -0.5f, -0.5f));
		BoxPoints.Add(FVector(-0.5f, -0.5f, -0.5f));
		BoxPoints.Add(FVector(-0.5f, 0.5f, -0.5f));
		BoxPoints.Add(FVector(-0.5f, 0.5f, -0.5f));
		BoxPoints.Add(FVector(0.5f, 0.5f, -0.5f));

		BoxPoints.Add(FVector(0.5f, 0.5f, 0.5f));
		BoxPoints.Add(FVector(0.5f, 0.5f, -0.5f));
		BoxPoints.Add(FVector(0.5f, -0.5f, 0.5f));
		BoxPoints.Add(FVector(0.5f, -0.5f, -0.5f));
		BoxPoints.Add(FVector(-0.5f, -0.5f, 0.5f));
		BoxPoints.Add(FVector(-0.5f, -0.5f, -0.5f));
		BoxPoints.Add(FVector(-0.5f, 0.5f, 0.5f));
		BoxPoints.Add(FVector(-0.5f, 0.5f, -0.5f));
	}

	void DrawGoal(FPrimitiveDrawInterface* PDI, const UIKRigEffectorGoal* Goal, bool bIsSelected) const
	{
		const FLinearColor Color = bIsSelected ? FLinearColor::Green : FLinearColor::Yellow;
		const float Thickness = Goal->GizmoThickness;
		const float Scale = FMath::Clamp(Goal->GizmoSize, 0.1f, 1000.0f);
		const FTransform Transform = Goal->CurrentTransform;
		for (int32 PointIndex = 0; PointIndex < BoxPoints.Num() - 1; PointIndex += 2)
		{
			PDI->DrawLine(Transform.TransformPosition(BoxPoints[PointIndex] * Scale), Transform.TransformPosition(BoxPoints[PointIndex + 1] * Scale), Color, SDPG_Foreground, Thickness);
		}
	}

	TArray<FVector> BoxPoints;
};

class FIKRigEditMode : public IPersonaEditMode
{
public:
	static FName ModeName;
	
	FIKRigEditMode();

	/** glue for all the editor parts to communicate */
	void SetEditorController(const TSharedPtr<FIKRigEditorController> InEditorController) { EditorController = InEditorController; };

	/** IPersonaEditMode interface */
	virtual bool GetCameraTarget(FSphere& OutTarget) const override;
	virtual class IPersonaPreviewScene& GetAnimPreviewScene() const override;
	virtual void GetOnScreenDebugInfo(TArray<FText>& OutDebugInfo) const override;
	/** END IPersonaEditMode interface */

	/** FEdMode interface */
	virtual void Tick(FEditorViewportClient* ViewportClient, float DeltaTime) override;
	virtual void Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI) override;
	virtual void DrawHUD(FEditorViewportClient* ViewportClient, FViewport* Viewport, const FSceneView* View, FCanvas* Canvas) override;
	virtual bool IsCompatibleWith(FEditorModeID OtherModeID) const override { return true; }
	virtual bool AllowWidgetMove() override;
	virtual bool ShouldDrawWidget() const override;
	virtual bool UsesTransformWidget() const override;
	virtual bool UsesTransformWidget(UE::Widget::EWidgetMode CheckMode) const override;
	virtual FVector GetWidgetLocation() const override;
	virtual bool HandleClick(FEditorViewportClient* InViewportClient, HHitProxy* HitProxy, const FViewportClick& Click) override;
	virtual bool StartTracking(FEditorViewportClient* InViewportClient, FViewport* InViewport) override;
	virtual bool EndTracking(FEditorViewportClient* InViewportClient, FViewport* InViewport) override;
	virtual bool InputDelta(FEditorViewportClient* InViewportClient, FViewport* InViewport, FVector& InDrag, FRotator& InRot, FVector& InScale) override;
	virtual bool GetCustomDrawingCoordinateSystem(FMatrix& InMatrix, void* InData) override;
	virtual bool GetCustomInputCoordinateSystem(FMatrix& InMatrix, void* InData) override;
	/** END FEdMode interface */

private:
	/** The hosting app */
	TWeakPtr<FIKRigEditorController> EditorController;

	/** draws goals in viewport */
	GoalGizmo GoalDrawer;
};
