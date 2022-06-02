// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BaseBehaviors/BehaviorTargetInterfaces.h"
#include "BaseGizmos/GizmoElementHitTargets.h"
#include "BaseGizmos/GizmoElementStateTargets.h"
#include "BaseGizmos/TransformProxy.h"
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
class UClickDragInputBehavior;
class UGizmoConstantFrameAxisSource;
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

//
// Interface for the Transform gizmo.
//


//
// Part identifiers are used to associate transform gizmo parts with their corresponding representation 
// in the render and hit target. The render and hit target should use the default identifier for 
// any of their internal elements that do not correspond to transform gizmo parts, for example non-hittable
// visual guide elements.
//
UENUM()
enum class ETransformGizmoPartIdentifier
{
	Default,
	TranslateAll,
	TranslateXAxis,
	TranslateYAxis,
	TranslateZAxis,
	TranslateXYPlanar,
	TranslateYZPlanar,
	TranslateXZPlanar,
	TranslateScreenSpace,
	RotateAll,
	RotateXAxis,
	RotateYAxis,
	RotateZAxis,
	RotateScreenSpace,
	RotateArcball,
	RotateArcballInnerCircle,
	ScaleAll,
	ScaleXAxis,
	ScaleYAxis,
	ScaleZAxis,
	ScaleXYPlanar,
	ScaleYZPlanar,
	ScaleXZPlanar,
	ScaleUniform, 
	Max
};

/**
 * UTransformGizmo provides standard Transformation Gizmo interactions,
 * applied to a UTransformProxy target object. By default the Gizmo will be
 * a standard XYZ translate/rotate Gizmo (axis and plane translation).
 */
UCLASS()
class EDITORINTERACTIVETOOLSFRAMEWORK_API UTransformGizmo : public UInteractiveGizmo, public IHoverBehaviorTarget, public IClickDragBehaviorTarget
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

	static constexpr float ScaleAxisLength = 70.0f;
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

	// IHoverBehaviorTarget implementation
	virtual FInputRayHit BeginHoverSequenceHitTest(const FInputDeviceRay& DevicePos) override;
	virtual void OnBeginHover(const FInputDeviceRay& DevicePos) override;
	virtual bool OnUpdateHover(const FInputDeviceRay& DevicePos) override;
	virtual void OnEndHover() override;

	// IClickDragBehaviorTarget implementation
	virtual FInputRayHit CanBeginClickDragSequence(const FInputDeviceRay& PressPos) override;
	virtual void OnClickPress(const FInputDeviceRay& PressPos) override;
	virtual void OnClickDrag(const FInputDeviceRay& DragPos) override;
	virtual void OnClickRelease(const FInputDeviceRay& ReleasePos) override;
	virtual void OnTerminateDragSequence() override;

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

	/**
	 * Explicitly set the child scale. Mainly useful to "reset" the child scale to (1,1,1) when re-using Gizmo across multiple transform actions.
	 * @warning does not generate change/modify events!!
	 */
	virtual void SetNewChildScale(const FVector& NewChildScale);

	/**
	 * Set visibility for this Gizmo
	 */
	virtual void SetVisibility(bool bVisible);

public:

	/** The active target object for the Gizmo */
	UPROPERTY()
	TObjectPtr<UTransformProxy> ActiveTarget;

	/** The hit target object */
	UPROPERTY()
	TObjectPtr<UGizmoElementHitMultiTarget> HitTarget;

	/** The mouse click behavior of the gizmo is accessible so that it can be modified to use different mouse keys. */
	UPROPERTY()
	TObjectPtr<UClickDragInputBehavior> MouseBehavior;

	/** Transform Gizmo Source */
	UPROPERTY()
	TScriptInterface<ITransformGizmoSource> TransformGizmoSource;

	/** Root of renderable gizmo elements */
	UPROPERTY()
	TObjectPtr<UGizmoElementGroup> GizmoElementRoot;

	/** Whether gizmo is visible. */
	UPROPERTY()
	bool bVisible = false;

	/** Whether gizmo is interacting. */
	UPROPERTY()
	bool bInInteraction = false;

	/** If true, then when using world frame, Axis and Plane translation snap to the world grid via the ContextQueriesAPI (in PositionSnapFunction) */
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

protected:

	//
	// Gizmo Objects, used for rendering and hit testing
	//

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

	/** Translate planar XY handle */
	UPROPERTY()
	TObjectPtr<UGizmoElementRectangle> TranslatePlanarXYElement;

	/** Translate planar YZ handle */
	UPROPERTY()
	TObjectPtr<UGizmoElementRectangle> TranslatePlanarYZElement;

	/** Translate planar XZ handle */
	UPROPERTY()
	TObjectPtr<UGizmoElementRectangle> TranslatePlanarXZElement;

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

	/** Scale planar XY handle */
	UPROPERTY()
	TObjectPtr<UGizmoElementRectangle> ScalePlanarXYElement;

	/** Scale planar YZ handle */
	UPROPERTY()
	TObjectPtr<UGizmoElementRectangle> ScalePlanarYZElement;

	/** Scale planar XZ handle */
	UPROPERTY()
	TObjectPtr<UGizmoElementRectangle> ScalePlanarXZElement;

	/** Uniform scale object */
	UPROPERTY()
	TObjectPtr<UGizmoElementBox> ScaleUniformElement;

	/** Axis that points towards camera, X/Y plane tangents aligned to right/up. Shared across Gizmos, and created internally during SetActiveTarget() */
	UPROPERTY()
	TObjectPtr<UGizmoConstantFrameAxisSource> CameraAxisSource;

	// internal function that updates CameraAxisSource by getting current view state from GizmoManager
	void UpdateCameraAxisSource();

	/** The state target is created internally during SetActiveTarget() */
	UPROPERTY()
	TObjectPtr<IGizmoStateTarget> StateTarget;

	/**
	 * These are used to let the translation subgizmos use raycasts into the scene to align the gizmo with scene geometry.
	 * See comment for SetWorldAlignmentFunctions().
	 */
	TUniqueFunction<bool()> ShouldAlignDestination = []() { return false; };
	TUniqueFunction<bool(const FRay&, FVector&)> DestinationAlignmentRayCaster = [](const FRay&, FVector&) {return false; };

	bool bDisallowNegativeScaling = false;

protected:

	/** Setup behaviors */
	virtual void SetupBehaviors();

	/** Setup materials */
	virtual void SetupMaterials();

	/** Update current gizmo mode based on transform source */
	virtual void UpdateMode();

	/** Enable the given mode with the specified axes, EAxisList::Type::None will hide objects associated with mode */
	virtual void EnableMode(EGizmoTransformMode InGizmoMode, EAxisList::Type InAxisListToDraw);

	/** Enable translate using specified axis list */
	virtual void EnableTranslate(EAxisList::Type InAxisListToDraw);

	/** Enable rotate using specified axis list */
	virtual void EnableRotate(EAxisList::Type InAxisListToDraw);

	/** Enable scale using specified axis list */
	virtual void EnableScale(EAxisList::Type InAxisListToDraw);

	/** Enable planar handles used by translate and scale */
	virtual void EnablePlanarObjects(bool bTranslate, bool bEnableX, bool bEnableY, bool bEnableZ);

	/** Construct translate axis handle */
	virtual UGizmoElementArrow* MakeTranslateAxis(ETransformGizmoPartIdentifier InPartId, const FVector& InAxisDir, const FVector& InSideDir, UMaterialInterface* InMaterial);

	/** Construct scale axis handle */
	virtual UGizmoElementArrow* MakeScaleAxis(ETransformGizmoPartIdentifier InPartId, const FVector& InAxisDir, const FVector& InSideDir, UMaterialInterface* InMaterial);

	/** Construct rotate axis handle */
	virtual UGizmoElementTorus* MakeRotateAxis(ETransformGizmoPartIdentifier InPartId, const FVector& Normal, const FVector& TorusAxis0, const FVector& TorusAxis1,
		UMaterialInterface* InMaterial, UMaterialInterface* InCurrentMaterial);

	/** Construct uniform scale handle */
	virtual UGizmoElementBox* MakeUniformScaleHandle();

	/** Construct planar axis handle */
	virtual UGizmoElementRectangle* MakePlanarHandle(ETransformGizmoPartIdentifier InPartId, const FVector& InUpDirection, const FVector& InSideDirection, const FVector& InPlaneNormal,
		UMaterialInterface* InMaterial, const FLinearColor& InVertexColor);

	/** Construct translate screen space handle */
	virtual UGizmoElementRectangle* MakeTranslateScreenSpaceHandle();

	/** Construct rotate screen space handle */
	virtual UGizmoElementCircle* MakeRotateCircleHandle(ETransformGizmoPartIdentifier InPartId, float InRadius, const FLinearColor& InColor, float bFill);

	/** Get gizmo transform based on cached current transform. */
	virtual FTransform GetGizmoTransform() const;

	/** Determine hit part and update hover state based on current input ray */
	virtual FInputRayHit UpdateHoveredPart(const FInputDeviceRay& DevicePos);

	/** Get current interaction axis */
	virtual FVector GetWorldAxis(const FVector& InAxis);

	/** Handle click press for translate and scale axes */
	virtual void OnClickPressAxis(const FInputDeviceRay& InPressPos);

	/** Handle click drag for translate and scale axes */
	virtual void OnClickDragAxis(const FInputDeviceRay& PressPos);

	/** Handle click release for translate and scale axes */
	virtual void OnClickReleaseAxis(const FInputDeviceRay& PressPos);

	/** Handle click press for translate and scale planar */
	virtual void OnClickPressPlanar(const FInputDeviceRay& InPressPos);

	/** Handle click drag for translate and scale planar */
	virtual void OnClickDragPlanar(const FInputDeviceRay& PressPos);

	/** Handle click release for translate and scale planar */
	virtual void OnClickReleasePlanar(const FInputDeviceRay& PressPos);

	/** Apply translate delta to transform proxy */
	virtual void ApplyTranslateDelta(const FVector& InTranslateDelta);

	/** Apply scale delta to transform proxy */
	virtual void ApplyScaleDelta(const FVector& InScaleDelta);

	// Axis and Plane TransformSources use this function to execute worldgrid snap queries
	bool PositionSnapFunction(const FVector& WorldPosition, FVector& SnappedPositionOut) const;
	FQuat RotationSnapFunction(const FQuat& DeltaRotation) const;

	// Get max part identifier.
	virtual uint32 GetMaxPartIdentifier() const;

	// Verify part identifier is within recognized range of transform gizmo part ids
	virtual bool VerifyPartIdentifier(uint32 InPartIdentifier) const;

protected:

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

	/** Scale delta is multiplied by this amount */
	UPROPERTY()
	double ScaleMultiplier = 0.05;

	/** Current transform */
	UPROPERTY()
	FTransform CurrentTransform = FTransform::Identity;

	/** Currently rendered transform mode */
	UPROPERTY()
	EGizmoTransformMode CurrentMode = EGizmoTransformMode::None;

	/** Currently rendered axis list */
	UPROPERTY()
	TEnumAsByte<EAxisList::Type> CurrentAxisToDraw = EAxisList::None;

	/** Last hit part */
	UPROPERTY()
	ETransformGizmoPartIdentifier LastHitPart = ETransformGizmoPartIdentifier::Default;

	//
	// The values below are used in the context of a single click-drag interaction, ie if bInInteraction = true
	// They otherwise should be considered uninitialized
	//

	/** Active world space axis origin (only valid between state target BeginModify/EndModify) */
	UPROPERTY()
	FVector InteractionAxisOrigin;

	/** Active world space axis (only valid between state target BeginModify/EndModify) */
	UPROPERTY()
	FVector InteractionAxis;

	/** Active world space normal used for planar (only valid between state target BeginModify/EndModify) */
	UPROPERTY()
	FVector InteractionNormal;

	/** Active world space axis X used for planar (only valid between state target BeginModify/EndModify) */
	UPROPERTY()
	FVector InteractionAxisX;

	/** Active world space axis Y used for planar (only valid between state target BeginModify/EndModify) */
	UPROPERTY()
	FVector InteractionAxisY;

	/** Active axis type (only valid between state target BeginModify/EndModify) */
	UPROPERTY()
	TEnumAsByte<EAxisList::Type> InteractionAxisType;

	/** Active interaction start point (only valid between state target BeginModify/EndModify) */
	UPROPERTY()
	FVector InteractionStartPoint;

	/** Active interaction current point (only valid between state target BeginModify/EndModify) */
	UPROPERTY()
	FVector InteractionCurrPoint;

	/** Active interaction start point planar (only valid between state target BeginModify/EndModify) */
	UPROPERTY()
	FVector2D InteractionStartPoint2D;

	/** Active interaction current point planar (only valid between state target BeginModify/EndModify) */
	UPROPERTY()
	FVector2D InteractionCurrPoint2D;
};
