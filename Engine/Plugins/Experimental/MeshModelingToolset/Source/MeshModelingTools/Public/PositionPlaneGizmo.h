// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "InteractiveGizmo.h"
#include "InteractiveGizmoBuilder.h"
#include "PreviewMesh.h"
#include "BaseBehaviors/AnyButtonInputBehavior.h"
#include "Transforms/QuickAxisTranslater.h"
#include "PositionPlaneGizmo.generated.h"

/**
 *
 */
UCLASS()
class MESHMODELINGTOOLS_API UPositionPlaneGizmoBuilder : public UInteractiveGizmoBuilder
{
	GENERATED_BODY()

public:
	virtual UInteractiveGizmo* BuildGizmo(const FToolBuilderState& SceneState) const override;
};




/**
 * This is a simple gizmo you can use to position a 3D plane in the world, based on QuickAxisTransformer
 */
UCLASS()
class MESHMODELINGTOOLS_API UPositionPlaneGizmo : public UInteractiveGizmo
{
	GENERATED_BODY()

public:
	virtual void SetWorld(UWorld* World);

	virtual void Setup() override;
	virtual void Shutdown() override;

	virtual void Render(IToolsContextRenderAPI* RenderAPI) override;

	virtual bool HitTest(const FRay& Ray, FHitResult& OutHit);


	/**
	 * This function is called by registered InputBehaviors when the user begins a click-drag-release interaction
	 */
	virtual void OnBeginDrag(const FRay& Ray);

	/**
	 * This function is called by registered InputBehaviorseach frame that the user is in a click-drag-release interaction
	 */
	virtual void OnUpdateDrag(const FRay& Ray);

	/**
	 * This function is called by registered InputBehaviors when the user releases the button driving a click-drag-release interaction
	 */
	virtual void OnEndDrag(const FRay& Ray);


	TFunction<void(const FFrame3d&)> OnPositionUpdatedFunc = nullptr;


	virtual void ExternalUpdatePosition(const FVector& Position, const FQuat& Orientation, bool bPostUpdate);


public:
	UWorld* TargetWorld;


	UPROPERTY()
	UPreviewMesh* CenterBallShape;


	UPROPERTY()
	UMaterialInstance* CenterBallMaterial;

	UPreviewMesh* MakeSphereMesh();

	bool bInTransformDrag;
	FQuickAxisTranslater QuickTransformer;

	void PostUpdatedPosition();

};








/**
 * UMeshSurfacePointToolMouseBehavior implements mouse press-drag-release interaction behavior for Mouse devices.
 * You can configure the base UAnyButtonInputBehavior to change the mouse button in use (default = left mouse)
 */
UCLASS()
class MESHMODELINGTOOLS_API UPositionPlaneOnSceneInputBehavior : public UAnyButtonInputBehavior
{
	GENERATED_BODY()

public:
	virtual FInputCapturePriority GetPriority() override { return FInputCapturePriority(FInputCapturePriority::DEFAULT_GIZMO_PRIORITY); }

	virtual void Initialize(UPositionPlaneGizmo* Gizmo);

	virtual FInputCaptureRequest WantsCapture(const FInputDeviceState& input) override;
	virtual FInputCaptureUpdate BeginCapture(const FInputDeviceState& input, EInputCaptureSide eSide) override;
	virtual FInputCaptureUpdate UpdateCapture(const FInputDeviceState& input, const FInputCaptureData& data) override;
	virtual void ForceEndCapture(const FInputCaptureData& data) override;

protected:
	UPositionPlaneGizmo* Gizmo;
	FRay LastWorldRay;
	bool bInDragCapture;
};


