// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BaseGizmos/GizmoElementHitTargets.h"
#include "BaseGizmos/GizmoElementStateTargets.h"
#include "BaseGizmos/TransformProxy.h"
#include "EditorGizmos/EditorAxisSources.h"
#include "EditorGizmos/TransformGizmoInterfaces.h"
#include "InteractiveGizmo.h"
#include "InteractiveToolObjects.h"
#include "InteractiveToolChange.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Math/Axis.h"
#include "TransformGizmo.generated.h"

class UInteractiveGizmoManager;
class IGizmoAxisSource;
class IGizmoTransformSource;
class IGizmoStateTarget;
class FEditorTransformGizmoTransformChange;
class UGizmoConstantFrameAxisSource;
class UGizmoTransformChangeStateTarget;
class UGizmoElementArrow;
class UGizmoElementBase;
class UGizmoElementBox;
class UGizmoElementCircle;
class UGizmoElementCone;
class UGizmoElementCylinder;
class UGizmoElementGroup;
class UGizmoElementRectangle;
class UGizmoElementRoot;
class UGizmoElementTorus;

/**
 * UTransformGizmo provides standard Transformation Gizmo interactions,
 * applied to a UTransformProxy target object. By default the Gizmo will be
 * a standard XYZ translate/rotate Gizmo (axis and plane translation).
 */
UCLASS()
class EDITORINTERACTIVETOOLSFRAMEWORK_API UTransformGizmo : public UInteractiveGizmo
{
	GENERATED_BODY()

public:

	static constexpr float AxisRadius = 1.5f;
	static constexpr float AxisLengthOffset = 20.0f;

	static constexpr float TranslateAxisLength = 70.0f;
	static constexpr float TranslateAxisConeAngle = 16.0f;
	static constexpr float TranslateAxisConeHeight = 22.0f;
	static constexpr float TranslateAxisConeRadius = 7.0f;
	static constexpr float TranslateScreenSpaceHandleSize = 14.0f;

	// Rotate constants
	static constexpr float RotateArcballInnerRadius = 8.0f;
	static constexpr float RotateArcballOuterRadius = 10.0f;
	static constexpr float RotateArcballSphereRadius = 70.0f;
	static constexpr float RotateAxisOuterRadius = 73.0f;
	static constexpr float RotateAxisInnerRadius = 1.25f;
	static constexpr int32 RotateAxisOuterSegments = 64;
	static constexpr int32 RotateAxisInnerSlices = 8;
	static constexpr float RotateOuterCircleRadius = 73.0f;
	static constexpr float RotateScreenSpaceRadius = 83.0f;

	static constexpr float ScaleAxisLength = 35.0f;
	static constexpr float ScaleAxisCubeSize = 3.0f;
	static constexpr float ScaleAxisCubeDim = 12.0f;

	static constexpr float PlanarHandleOffset = 55.0f;
	static constexpr float PlanarHandleSize = 15.0f;

	static constexpr float AxisTransp = 0.8f;
	static constexpr FLinearColor AxisColorX = FLinearColor(0.594f, 0.0197f, 0.0f);
	static constexpr FLinearColor AxisColorY = FLinearColor(0.1349f, 0.3959f, 0.0f);
	static constexpr FLinearColor AxisColorZ = FLinearColor(0.0251f, 0.207f, 0.85f);
	static constexpr FLinearColor ScreenAxisColor = FLinearColor(0.76, 0.72, 0.14f);
	static constexpr FColor PlaneColorXY = FColor(255, 255, 0); // FColor::Yellow
	static constexpr FColor ArcBallColor = FColor(128, 128, 128, 6);
	static constexpr FColor ScreenSpaceColor = FColor(196, 196, 196);
	static constexpr FColor CurrentColor = FColor(255, 255, 0); // FColor::Yellow

	static constexpr FLinearColor GreyColor = FLinearColor(0.50f, 0.50f, 0.50f);
	static constexpr FLinearColor WhiteColor = FLinearColor(1.0f, 1.0f, 1.0f);

	static constexpr FLinearColor RotateScreenSpaceCircleColor = WhiteColor;
	static constexpr FLinearColor RotateOuterCircleColor = GreyColor;
	static constexpr FLinearColor RotateArcballCircleColor = WhiteColor;

	static constexpr uint8 LargeInnerAlpha = 0x3f;
	static constexpr uint8 SmallInnerAlpha = 0x0f;
	static constexpr uint8 LargeOuterAlpha = 0x7f;
	static constexpr uint8 SmallOuterAlpha = 0x0f;


public:

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
	virtual void Render(IToolsContextRenderAPI* RenderAPI);
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
	 * Whether gizmo is visible.
	 */
	UPROPERTY()
	bool bVisible = false;

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

	//
	// Transform Source
	//
	UPROPERTY()
	TScriptInterface<ITransformGizmoSource> TransformSource;

protected:

	//
	// Gizmo Objects, used for rendering and hit testing
	//

	/** Root of renderable gizmo elements */
	UPROPERTY()
	TObjectPtr<UGizmoElementGroup> GizmoElementRoot;

	/** Translate X Axis */
	UPROPERTY()
	TObjectPtr<UGizmoElementArrow> TranslateXAxisElement;

	/** Translate Y Axis */
	UPROPERTY()
	TObjectPtr<UGizmoElementArrow> TranslateYAxisElement;

	/** Translate Z Axis */
	UPROPERTY()
	TObjectPtr<UGizmoElementArrow> TranslateZAxisElement;

	/** Translate screen-space */
	UPROPERTY()
	TObjectPtr<UGizmoElementRectangle> TranslateScreenSpaceElement;

	/** Planar XY handle */
	UPROPERTY()
	TObjectPtr<UGizmoElementRectangle> PlanarXYElement;

	/** Planar YZ handle */
	UPROPERTY()
	TObjectPtr<UGizmoElementRectangle> PlanarYZElement;

	/** Planar XZ handle */
	UPROPERTY()
	TObjectPtr<UGizmoElementRectangle> PlanarXZElement;

	/** Rotate X Axis */
	UPROPERTY()
	TObjectPtr<UGizmoElementTorus> RotateXAxisElement;

	/** Rotate Y Axis */
	UPROPERTY()
	TObjectPtr<UGizmoElementTorus> RotateYAxisElement;

	/** Rotate Z Axis */
	UPROPERTY()
	TObjectPtr<UGizmoElementTorus> RotateZAxisElement;

	/** Rotate outer circle */
	UPROPERTY()
	TObjectPtr<UGizmoElementCircle> RotateOuterCircleElement;

	/** Rotate arcball outer circle */
	UPROPERTY()
	TObjectPtr<UGizmoElementCircle> RotateArcballOuterElement;

	/** Rotate arcball inner circle */
	UPROPERTY()
	TObjectPtr<UGizmoElementCircle> RotateArcballInnerElement;

	/** Rotate screen space circle */
	UPROPERTY()
	TObjectPtr<UGizmoElementCircle> RotateScreenSpaceElement;

	/** Scale X Axis object */
	UPROPERTY()
	TObjectPtr<UGizmoElementArrow> ScaleXAxisElement;

	/** Scale Y Axis object */
	UPROPERTY()
	TObjectPtr<UGizmoElementArrow> ScaleYAxisElement;

	/** Scale Z Axis object */
	UPROPERTY()
	TObjectPtr<UGizmoElementArrow> ScaleZAxisElement;

	/** Uniform scale object */
	UPROPERTY()
	TObjectPtr<UGizmoElementBox> ScaleUniformElement;

	//
	// Axis Sources
	//
	/** Axis that points towards camera, X/Y plane tangents aligned to right/up. Shared across Gizmos, and created internally during SetActiveTarget() */
	UPROPERTY()
	TObjectPtr<UGizmoConstantFrameAxisSource> CameraAxisSource;

	// internal function that updates CameraAxisSource by getting current view state from GizmoManager
	void UpdateCameraAxisSource();

	/** 
	 * State target is shared across gizmos, and created internally during SetActiveTarget(). 
	 * Several FChange providers are registered with this StateTarget, including the UTransformGizmo
	 * itself (IToolCommandChangeSource implementation above is called)
	 */
	UPROPERTY()
	TObjectPtr<UGizmoDependentTransformChangeStateTarget> StateTarget;

	/**
	 * These are used to let the translation subgizmos use raycasts into the scene to align the gizmo with scene geometry.
	 * See comment for SetWorldAlignmentFunctions().
	 */
	TUniqueFunction<bool()> ShouldAlignDestination = []() { return false; };
	TUniqueFunction<bool(const FRay&, FVector&)> DestinationAlignmentRayCaster = [](const FRay&, FVector&) {return false; };

	bool bDisallowNegativeScaling = false;
protected:

	/** Update current gizmo mode based on transform source */
	void UpdateMode();

	/** Enable the given mode with the specified axes, EAxisList::Type::None will hide objects associated with mode */
	void EnableMode(EGizmoTransformMode InGizmoMode, EAxisList::Type InAxisListToDraw);

	/** Enable translate using specified axis list */
	void EnableTranslate(EAxisList::Type InAxisListToDraw);

	/** Enable rotate using specified axis list */
	void EnableRotate(EAxisList::Type InAxisListToDraw);

	/** Enable scale using specified axis list */
	void EnableScale(EAxisList::Type InAxisListToDraw);

	/** Enable planar handles used by translate and scale */
	void EnablePlanarObjects(bool bEnableX, bool bEnableY, bool bEnableZ);


	UPROPERTY()
	EGizmoTransformMode CurrentMode = EGizmoTransformMode::None;

	UPROPERTY()
	TEnumAsByte<EAxisList::Type> CurrentAxisToDraw = EAxisList::None;

	/** Construct translate axis handle */
	virtual UGizmoElementArrow* MakeTranslateAxis(const FVector& InAxisDir, const FVector& InSideDir, UMaterialInterface* InMaterial);

	/** Construct scale axis handle */
	virtual UGizmoElementArrow* MakeScaleAxis(const FVector& InAxisDir, const FVector& InSideDir, UMaterialInterface* InMaterial);

	/** Construct rotate axis handle */
	virtual UGizmoElementTorus* MakeRotateAxis(const FVector& Normal, const FVector& TorusAxis0, const FVector& TorusAxis1, 
		UMaterialInterface* InMaterial, UMaterialInterface* InCurrentMaterial);

	/** Construct uniform scale handle */
	virtual UGizmoElementBox* MakeUniformScaleHandle();

	/** Construct planar axis handle */
	virtual UGizmoElementRectangle* MakePlanarHandle(const FVector& InUpDirection, const FVector& InSideDirection, const FVector& InPlaneNormal,
		UMaterialInterface* InMaterial, const FLinearColor& InVertexColor);

	/** Construct translate screen space handle */
	virtual UGizmoElementRectangle* MakeTranslateScreenSpaceHandle();

	/** Construct rotate screen space handle */
	virtual UGizmoElementCircle* MakeRotateCircleHandle(float InRadius, const FLinearColor& InColor, float bFill);

	// Axis and Plane TransformSources use this function to execute worldgrid snap queries
	bool PositionSnapFunction(const FVector& WorldPosition, FVector& SnappedPositionOut) const;
	FQuat RotationSnapFunction(const FQuat& DeltaRotation) const;

	/** Materials and colors to be used when drawing the items for each axis */
	UPROPERTY()
	TObjectPtr<UMaterialInterface> TransparentVertexColorMaterial;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> GridMaterial;
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> AxisMaterialX;
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> AxisMaterialY;
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> AxisMaterialZ;
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> CurrentAxisMaterial;
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> GreyMaterial;
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> WhiteMaterial;
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> OpaquePlaneMaterialXY;
};
