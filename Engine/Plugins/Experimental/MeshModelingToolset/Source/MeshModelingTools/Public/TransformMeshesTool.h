// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MultiSelectionTool.h"
#include "InteractiveToolBuilder.h"
#include "BaseBehaviors//BehaviorTargetInterfaces.h"
#include "FrameTypes.h"
#include "TransformMeshesTool.generated.h"

class UBaseAxisTranslationGizmo;
class UAxisAngleGizmo;
class UTransformGizmo;
class UTransformProxy;

/**
 *
 */
UCLASS()
class MESHMODELINGTOOLS_API UTransformMeshesToolBuilder : public UInteractiveToolBuilder
{
	GENERATED_BODY()

public:
	virtual bool CanBuildTool(const FToolBuilderState& SceneState) const override;
	virtual UInteractiveTool* BuildTool(const FToolBuilderState& SceneState) const override;
};



/** Mesh Transform modes */
UENUM()
enum class ETransformMeshesTransformMode : uint8
{
	/** Shared Gizmo */
	SharedGizmo = 0 UMETA(DisplayName = "Shared Gizmo"),

	/** Shared Gizmo, Local Transformations*/
	SharedGizmoLocal = 1 UMETA(DisplayName = "Shared Gizmo (Local)"),

	/** Per Object Gizmo */
	PerObjectGizmo = 2 UMETA(DisplayName = "Multi-Gizmo"),

	/** Quick Translate Transformer */
	QuickTranslate = 3 UMETA(DisplayName = "QuickTranslate")
};



/**
 * Standard properties of the Transform Meshes operation
 */
UCLASS()
class MESHMODELINGTOOLS_API UTransformMeshesToolProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()
public:

	UPROPERTY(EditAnywhere, Category = Options)
	ETransformMeshesTransformMode TransformMode = ETransformMeshesTransformMode::SharedGizmo;


	UPROPERTY(EditAnywhere, Category = Options, meta = (EditCondition = "TransformMode == ETransformMeshesTransformMode::SharedGizmo || TransformMode == ETransformMeshesTransformMode::PerObjectGizmo"))
	bool bSetPivot = false;
};


USTRUCT()
struct FTransformMeshesTarget
{
	GENERATED_BODY()

	UPROPERTY()
	UTransformProxy* TransformProxy;

	UPROPERTY()
	UTransformGizmo* TransformGizmo;
};


/**
 *
 */
UCLASS()
class MESHMODELINGTOOLS_API UTransformMeshesTool : public UMultiSelectionTool, public IClickDragBehaviorTarget
{
	GENERATED_BODY()

public:
	UTransformMeshesTool();

	virtual void SetWorld(UWorld* World, UInteractiveGizmoManager* GizmoManager);

	virtual void Setup() override;
	virtual void Shutdown(EToolShutdownType ShutdownType) override;

	virtual void Tick(float DeltaTime) override;
	virtual void Render(IToolsContextRenderAPI* RenderAPI) override;

	virtual bool HasCancel() const override { return false; }
	virtual bool HasAccept() const override { return false; }
	virtual bool CanAccept() const override { return false; }

	virtual void OnPropertyModified(UObject* PropertySet, UProperty* Property) override;


	// ICLickDragBehaviorTarget interface
	virtual FInputRayHit CanBeginClickDragSequence(const FInputDeviceRay& PressPos) override;
	virtual void OnClickPress(const FInputDeviceRay& PressPos) override;
	virtual void OnClickDrag(const FInputDeviceRay& DragPos) override;
	virtual void OnClickRelease(const FInputDeviceRay& ReleasePos) override;
	virtual void OnTerminateDragSequence() override;

protected:
	UPROPERTY()
	UTransformMeshesToolProperties* TransformProps;

protected:
	UWorld* TargetWorld;
	UInteractiveGizmoManager* GizmoManager;

	UPROPERTY()
	TArray<FTransformMeshesTarget> ActiveGizmos;


	ETransformMeshesTransformMode CurTransformMode;
	void UpdateTransformMode(ETransformMeshesTransformMode NewMode);
	void UpdateSetPivotModes(bool bEnableSetPivot);

	void SetActiveGizmos_Single(bool bLocalRotations);
	void SetActiveGizmos_PerObject();
	void ResetActiveGizmos();


	FFrame3d StartDragFrameWorld;
	FTransform StartDragTransform;


	void OnParametersUpdated();
};
