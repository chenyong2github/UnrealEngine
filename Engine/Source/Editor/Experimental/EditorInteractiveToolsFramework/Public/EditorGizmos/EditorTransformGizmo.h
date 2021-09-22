// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BaseGizmos/TransformProxy.h"
#include "EditorGizmos/EditorAxisSources.h"
#include "EditorGizmos/GizmoArrowObject.h"
#include "EditorGizmos/GizmoBaseObject.h"
#include "EditorGizmos/GizmoGroupObject.h"
#include "EditorGizmos/GizmoObjectHitTargets.h"
#include "EditorGizmos/GizmoObjectStateTargets.h"
#include "EditorGizmos/GizmoObjectTransformSources.h"
#include "EditorInteractiveGizmoSelectionBuilder.h"
#include "InteractiveGizmo.h"
#include "InteractiveToolObjects.h"
#include "InteractiveToolChange.h"
#include "Materials/MaterialInstanceDynamic.h"

#include "EditorTransformGizmo.generated.h"

class UInteractiveGizmoManager;
class IGizmoAxisSource;
class IGizmoTransformSource;
class IGizmoStateTarget;
class UGizmoConstantFrameAxisSource;
class UGizmoTransformChangeStateTarget;
class FEditorTransformGizmoTransformChange;

UCLASS()
class EDITORINTERACTIVETOOLSFRAMEWORK_API UEditorTransformGizmoBuilder : public UInteractiveGizmoBuilder, public IEditorInteractiveGizmoSelectionBuilder
{
	GENERATED_BODY()

public:

	// UEditorInteractiveGizmoSelectionBuilder interface 
	virtual UInteractiveGizmo* BuildGizmo(const FToolBuilderState& SceneState) const override;
	virtual void UpdateGizmoForSelection(UInteractiveGizmo* Gizmo, const FToolBuilderState& SceneState) override;
};


/**
 * UEditorTransformGizmo provides standard Transformation Gizmo interactions,
 * applied to a UTransformProxy target object. By default the Gizmo will be
 * a standard XYZ translate/rotate Gizmo (axis and plane translation).
 */
UCLASS()
class EDITORINTERACTIVETOOLSFRAMEWORK_API UEditorTransformGizmo : public UInteractiveGizmo
{
	GENERATED_BODY()

public:
	constexpr static float AXIS_LENGTH = 35.0f;
	constexpr static float AXIS_RADIUS = 1.2f;
	constexpr static float AXIS_CONE_ANGLE = 15.7f;
	constexpr static float AXIS_CONE_HEIGHT = 13;
	constexpr static float AXIS_CONE_HEAD_OFFSET = 12;
	constexpr static float AXIS_CUBE_SIZE = 4;
	constexpr static float AXIS_CUBE_HEAD_OFFSET= 3;
	constexpr static float TRANSLATE_ROTATE_AXIS_CIRCLE_RADIUS = 20.0f;
	constexpr static float TWOD_AXIS_CIRCLE_RADIUS = 10.0f;
	constexpr static float INNER_AXIS_CIRCLE_RADIUS = 48.0f;
	constexpr static float OUTER_AXIS_CIRCLE_RADIUS = 56.0f;
	constexpr static float ROTATION_TEXT_RADIUS = 75.0f;
	constexpr static int32 AXIS_CIRCLE_SIDES = 24;
	constexpr static float ARCALL_RELATIVE_INNER_SIZE = 0.75f;
	constexpr static float AXIS_LENGTH_SCALE = 25.0f;
	constexpr static float AXIS_LENGTH_SCALE_OFFSET = 5.0f;

	constexpr static FLinearColor AxisColorX = FLinearColor(0.594f, 0.0197f, 0.0f);
	constexpr static FLinearColor AxisColorY = FLinearColor(0.1349f, 0.3959f, 0.0f);
	constexpr static FLinearColor AxisColorZ = FLinearColor(0.0251f, 0.207f, 0.85f);
	constexpr static FLinearColor ScreenAxisColor = FLinearColor(0.76, 0.72, 0.14f);
	constexpr static FColor PlaneColorXY = FColor(255, 255, 0); // FColor::Yellow
	constexpr static FColor ArcBallColor = FColor(128, 128, 128, 6);
	constexpr static FColor ScreenSpaceColor = FColor(196, 196, 196);
	constexpr static FColor CurrentColor = FColor(255, 255, 0); // FColor::Yellow

public:

	virtual void SetWorld(UWorld* World);
	virtual void SetElements(ETransformGizmoSubElements InEnableElements);

	/**
	 * By default, non-uniform scaling handles appear (assuming they exist in the gizmo to begin with), 
	 * when CurrentCoordinateSystem == EToolContextCoordinateSystem::Local, since components can only be
	 * locally scaled. However, this can be changed to a custom check here, perhaps to hide them in extra
	 * conditions or to always show them (if the gizmo is not scaling a component).
	 */
	virtual void SetIsNonUniformScaleAllowedFunction(
		TUniqueFunction<bool()>&& IsNonUniformScaleAllowed
	);

	/**
	 * By default, the nonuniform scale components can scale negatively. However, they can be made to clamp
	 * to zero instead by passing true here. This is useful for using the gizmo to flatten geometry.
	 *
	 * TODO: Should this affect uniform scaling too?
	 */
	virtual void SetDisallowNegativeScaling(bool bDisallow);

	// UInteractiveGizmo overrides
	virtual void Setup() override;
	virtual void Shutdown() override;
	virtual void Tick(float DeltaTime) override;


	/**
	 * Set the active target object for the Gizmo
	 * @param Target active target
	 * @param TransactionProvider optional IToolContextTransactionProvider implementation to use - by default uses GizmoManager
	 */
	virtual void SetActiveTarget(UTransformProxy* Target, IToolContextTransactionProvider* TransactionProvider = nullptr);

	/**
	 * Clear the active target object for the Gizmo
	 */
	virtual void ClearActiveTarget();

	/** The active target object for the Gizmo */
	UPROPERTY()
	TObjectPtr<UTransformProxy> ActiveTarget;

	/**
	 * Repositions the gizmo without issuing undo/redo changes, triggering callbacks, 
	 * or moving any components. Useful for resetting the gizmo to a new location without
	 * it being viewed as a gizmo manipulation.
	 */
	void ReinitializeGizmoTransform(const FTransform& NewTransform);

	/**
	 * Set a new position for the Gizmo. This is done via the same mechanisms as the sub-gizmos,
	 * so it generates the same Change/Modify() events, and hence works with Undo/Redo
	 */
	virtual void SetNewGizmoTransform(const FTransform& NewTransform);

	/**
	 * Explicitly set the child scale. Mainly useful to "reset" the child scale to (1,1,1) when re-using Gizmo across multiple transform actions.
	 * @warning does not generate change/modify events!!
	 */
	virtual void SetNewChildScale(const FVector& NewChildScale);

	/**
	 * Set visibility for this Gizmo
	 */
	virtual void SetVisibility(bool bVisible);

	/**
	 * If true, then when using world frame, Axis and Plane translation snap to the world grid via the ContextQueriesAPI (in PositionSnapFunction)
	 */
	UPROPERTY()
	bool bSnapToWorldGrid = false;

	/**
	 * Optional grid size which overrides the Context Grid
	 */
	UPROPERTY()
	bool bGridSizeIsExplicit = false;
	UPROPERTY()
	FVector ExplicitGridSize;

	/**
	 * Optional grid size which overrides the Context Rotation Grid
	 */
	UPROPERTY()
	bool bRotationGridSizeIsExplicit = false;
	UPROPERTY()
	FRotator ExplicitRotationGridSize;

	/**
	 * If true, then when using world frame, Axis and Plane translation snap to the world grid via the ContextQueriesAPI (in RotationSnapFunction)
	 */
	UPROPERTY()
	bool bSnapToWorldRotGrid = false;

	/**
	 * Whether to use the World/Local coordinate system provided by the context via the ContextyQueriesAPI.
	 */
	UPROPERTY()
	bool bUseContextCoordinateSystem = true;

	/**
	 * Current coordinate system in use. If bUseContextCoordinateSystem is true, this value will be updated internally every Tick()
	 * by querying the ContextyQueriesAPI, otherwise the default is Local and the client can change it as necessary
	 */
	UPROPERTY()
	EToolContextCoordinateSystem CurrentCoordinateSystem = EToolContextCoordinateSystem::Local;


protected:
	// TSharedPtr<FEditorTransformGizmoActorFactory> GizmoEditorActorBuilder;
	/** Only these parts of the UEditorTransformGizmo will be initialized */
	ETransformGizmoSubElements EnableElements =
		ETransformGizmoSubElements::TranslateAllAxes |
		ETransformGizmoSubElements::TranslateAllPlanes |
		ETransformGizmoSubElements::RotateAllAxes |
		ETransformGizmoSubElements::ScaleAllAxes |
		ETransformGizmoSubElements::ScaleAllPlanes |
		ETransformGizmoSubElements::ScaleUniform;

	/** List of current-active gizmo objects */
	UPROPERTY()
	TArray<TObjectPtr<UGizmoBaseObject>> ActiveObjects;

	/**
	 * List of nonuniform scale objects. Subset of of ActiveObjects. These are tracked separately so they can
	 * be hidden when Gizmo is not configured to use local axes, because UE only supports local nonuniform scaling
	 * on Components
	 */
	UPROPERTY()
	TArray<TObjectPtr<UGizmoBaseObject>> NonuniformScaleObjects;

	/** list of currently-active child gizmos */
	UPROPERTY()
	TArray<TObjectPtr<UInteractiveGizmo>> ActiveGizmos;

	/** GizmoActors will be spawned in this World */
	UWorld* World;

	//
	// Axis Sources
	//
	/** Axis that points towards camera, X/Y plane tangents aligned to right/up. Shared across Gizmos, and created internally during SetActiveTarget() */
	UPROPERTY()
	TObjectPtr<UGizmoConstantFrameAxisSource> CameraAxisSource;

	// internal function that updates CameraAxisSource by getting current view state from GizmoManager
	void UpdateCameraAxisSource();

	/** Gizmo group object contains all gizmos created internally during SetActiveTarget() */
	UPROPERTY()
	TObjectPtr<UGizmoGroupObject> GizmoGroupObject;

	// @todo - these properties can probably be removed since they are contained within the group now.
	/** X-axis source is shared across Gizmos, and created internally during SetActiveTarget() */
	UPROPERTY()
	TObjectPtr<UGizmoEditorAxisSource> AxisXSource;

	/** Y-axis source is shared across Gizmos, and created internally during SetActiveTarget() */
	UPROPERTY()
	TObjectPtr<UGizmoEditorAxisSource> AxisYSource;

	/** Z-axis source is shared across Gizmos, and created internally during SetActiveTarget() */
	UPROPERTY()
	TObjectPtr<UGizmoEditorAxisSource> AxisZSource;

	/** X-axis arrow object is shared across Gizmos, and created internally during SetActiveTarget() */
	UPROPERTY()
	TObjectPtr<UGizmoArrowObject> AxisXObject;

	/** Y-axis arrow object is shared across Gizmos, and created internally during SetActiveTarget() */
	UPROPERTY()
	TObjectPtr<UGizmoArrowObject> AxisYObject;

	/** Z-axis arrow object is shared across Gizmos, and created internally during SetActiveTarget() */
	UPROPERTY()
	TObjectPtr<UGizmoArrowObject> AxisZObject;
	//
	// Scaling support. 
	// UE Components only support scaling in local coordinates, so we have to create separate sources for that.
	//

	/** Local X-axis source (ie 1,0,0) is shared across Scale Gizmos, and created internally during SetActiveTarget() */
	UPROPERTY()
	TObjectPtr<UGizmoEditorAxisSource> UnitAxisXSource;

	/** Y-axis source (ie 0,1,0) is shared across Scale Gizmos, and created internally during SetActiveTarget() */
	UPROPERTY()
	TObjectPtr<UGizmoEditorAxisSource> UnitAxisYSource;

	/** Z-axis source (ie 0,0,1) is shared across Scale Gizmos, and created internally during SetActiveTarget() */
	UPROPERTY()
	TObjectPtr<UGizmoEditorAxisSource> UnitAxisZSource;

	/** 
	 * State target is shared across gizmos, and created internally during SetActiveTarget(). 
	 * Several FChange providers are registered with this StateTarget, including the UEditorTransformGizmo
	 * itself (IToolCommandChangeSource implementation above is called)
	 */
	UPROPERTY()
	TObjectPtr<UGizmoObjectTransformChangeStateTarget> StateTarget;

	/**
	 * These are used to let the translation subgizmos use raycasts into the scene to align the gizmo with scene geometry.
	 * See comment for SetWorldAlignmentFunctions().
	 */
	TUniqueFunction<bool()> ShouldAlignDestination = []() { return false; };
	TUniqueFunction<bool(const FRay&, FVector&)> DestinationAlignmentRayCaster = [](const FRay&, FVector&) {return false; };

	TUniqueFunction<bool()> IsNonUniformScaleAllowed = [this]() { return CurrentCoordinateSystem == EToolContextCoordinateSystem::Local; };

	bool bDisallowNegativeScaling = false;
protected:


	/** @return a new instance of the standard axis-translation Gizmo */
	virtual UInteractiveGizmo* AddAxisTranslationGizmo(
		UGizmoArrowObject* InArrowObject,
		IGizmoAxisSource* InAxisSource,
		IGizmoTransformSource* InTransformSource,
		IGizmoStateTarget* InStateTarget,
		EAxisList::Type InAxisType,
		const FLinearColor InAxisColor);

	/** @return a new instance of the standard plane-translation Gizmo */
	virtual UInteractiveGizmo* AddPlaneTranslationGizmo(
		IGizmoAxisSource* InAxisSource,
		IGizmoTransformSource* InTransformSource,
		IGizmoStateTarget* InStateTarget);

	/** @return a new instance of the standard axis-rotation Gizmo */
	virtual UInteractiveGizmo* AddAxisRotationGizmo(
		IGizmoAxisSource* InAxisSource,
		IGizmoTransformSource* InTransformSource,
		IGizmoStateTarget* InStateTarget,
		EAxisList::Type InAxisType,
		const FLinearColor InAxisColor);

	/** @return a new instance of the standard axis-scaling Gizmo */
	virtual UInteractiveGizmo* AddAxisScaleGizmo(
		UGizmoArrowObject* InArrowObject,
		IGizmoAxisSource* InGizmoAxisSource, IGizmoAxisSource* InParameterAxisSource,
		IGizmoTransformSource* InTransformSource,
		IGizmoStateTarget* InStateTarget,
		EAxisList::Type InAxisType,
		const FLinearColor InAxisColor);

	/** @return a new instance of the standard plane-scaling Gizmo */
	virtual UInteractiveGizmo* AddPlaneScaleGizmo(
		IGizmoAxisSource* InGizmoAxisSource, IGizmoAxisSource* InParameterAxisSource,
		IGizmoTransformSource* InTransformSource,
		IGizmoStateTarget* InStateTarget);

	/** @return a new instance of the standard plane-scaling Gizmo */
	virtual UInteractiveGizmo* AddUniformScaleGizmo(
		IGizmoAxisSource* InGizmoAxisSource, IGizmoAxisSource* InParameterAxisSource,
		IGizmoTransformSource* InTransformSource,
		IGizmoStateTarget* InStateTarget);

	// Axis and Plane TransformSources use this function to execute worldgrid snap queries
	bool PositionSnapFunction(const FVector& WorldPosition, FVector& SnappedPositionOut) const;
	FQuat RotationSnapFunction(const FQuat& DeltaRotation) const;

	/** Materials and colors to be used when drawing the items for each axis */
	TObjectPtr<UMaterialInterface> TransparentPlaneMaterialXY;
	TObjectPtr<UMaterialInterface> GridMaterial;

	TObjectPtr<UMaterialInstanceDynamic> AxisMaterialX;
	TObjectPtr<UMaterialInstanceDynamic> AxisMaterialY;
	TObjectPtr<UMaterialInstanceDynamic> AxisMaterialZ;
	TObjectPtr<UMaterialInstanceDynamic> CurrentAxisMaterial;
	TObjectPtr<UMaterialInstanceDynamic> OpaquePlaneMaterialXY;
};
