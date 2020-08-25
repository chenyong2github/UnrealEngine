// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InteractiveGizmo.h"
#include "InteractiveGizmoBuilder.h"
#include "TransformProxy.h"
#include "GizmoActor.h"
#include "BaseBehaviors/BehaviorTargetInterfaces.h"
#include "BaseBehaviors/AnyButtonInputBehavior.h"

#include "ScalableSphereGizmo.generated.h"

UCLASS()
class INTERACTIVETOOLSFRAMEWORK_API UScalableSphereGizmoBuilder : public UInteractiveGizmoBuilder
{
	GENERATED_BODY()

public:
	virtual UInteractiveGizmo* BuildGizmo(const FToolBuilderState& SceneState) const override;
};

UCLASS()
class INTERACTIVETOOLSFRAMEWORK_API AScalableSphereGizmoActor : public AGizmoActor
{
	GENERATED_BODY()

	public:

	AScalableSphereGizmoActor();

	// +X Axis Handle
	UPROPERTY()
	UPrimitiveComponent* XPositive;

	// -X Axis Handle
	UPROPERTY()
	UPrimitiveComponent* XNegative;

	// +Y Axis Handle
	UPROPERTY()
	UPrimitiveComponent* YPositive;

	// -Y Axis Handle
	UPROPERTY()
	UPrimitiveComponent* YNegative;

	// +Z Axis Handle
	UPROPERTY()
	UPrimitiveComponent* ZPositive;

	// -Z Axis Handle
	UPROPERTY()
	UPrimitiveComponent* ZNegative;
};

/**
 * UScalableSphereGizmo provides a sphere that can be scaled in all directions by dragging it's handles
 *
 * The in-scene representation of the Gizmo is a AScalableSphereGizmoActor (or subclass).
 * This Actor has FProperty members for the various sub-widgets, each as a separate Component.
 * Any particular sub-widget of the Gizmo can be disabled by setting the respective
 * Actor Component to null.
 *
 */
UCLASS()
class INTERACTIVETOOLSFRAMEWORK_API UScalableSphereGizmo : public UInteractiveGizmo, public IHoverBehaviorTarget
{
	GENERATED_BODY()

public:
	// UInteractiveGizmo interface

	virtual void Setup() override;

	virtual void Render(IToolsContextRenderAPI* RenderAPI) override;

	virtual void Shutdown() override;

	// IHoverBehaviorTarget interface
	// TODO: Add HoverBehavior
	virtual FInputRayHit BeginHoverSequenceHitTest(const FInputDeviceRay& PressPos) override { return FInputRayHit(); }
	virtual void OnBeginHover(const FInputDeviceRay& DevicePos) override {}
	virtual bool OnUpdateHover(const FInputDeviceRay& DevicePos) override { return true; }
	virtual void OnEndHover() override {}
	
	/**
	 * Set the Target to which the gizmo will be attached
	 */
	virtual void SetTarget(UTransformProxy* InTarget);

	/* Set the World in which the GizmoActor will be spawned*/
	virtual void SetWorld(UWorld* InWorld);

	virtual void OnBeginDrag(const FInputDeviceRay& Ray);
	virtual void OnUpdateDrag(const FInputDeviceRay& Ray);

	/** Check if the input Ray hit any of the components of the internal actor */
	bool HitTest(const FRay& Ray, FHitResult& OutHit, FVector& OutAxis, FTransform& OutTransform);

	/* Set the Radius of the Sphere*/
	void SetRadius(float InRadius);

	/** Called when the radius is chaged (by dragging or setting). Sends new radius as parameter. */
	TFunction<void(const float)> UpdateRadiusFunc = nullptr;
private:

	/**
	 * Create the GizmoActor and the handles
	 */
	void CreateGizmoHandles();

	/**
	 * Update the handles to the right position when the radius changes
	 */
	void UpdateGizmoHandles();

	/**
	 * Callback for when the ActiveTarget's transform is changed
	 */
	void OnTransformChanged(UTransformProxy*, FTransform);

	// The radius of the sphere
	UPROPERTY()
	float Radius;

	UPROPERTY()
	UWorld* World;

	UPROPERTY()
	UTransformProxy* ActiveTarget;

	UPROPERTY()
	AScalableSphereGizmoActor* GizmoActor;

	// The current axis that is being dragged along
	UPROPERTY()
	FVector ActiveAxis;

	// The position the drag was started on
	UPROPERTY()
	FVector DragStartWorldPosition;

	// The initial parameter along the drag axist
	UPROPERTY()
	float InteractionStartParameter;
};


/**
 * A behavior that forwards clicking and dragging to the gizmo.
 */
UCLASS()
class INTERACTIVETOOLSFRAMEWORK_API UScalableSphereGizmoInputBehavior : public UAnyButtonInputBehavior
{
	GENERATED_BODY()

public:
	virtual FInputCapturePriority GetPriority() override { return FInputCapturePriority(FInputCapturePriority::DEFAULT_GIZMO_PRIORITY); }

	virtual void Initialize(UScalableSphereGizmo* Gizmo);

	virtual FInputCaptureRequest WantsCapture(const FInputDeviceState& input) override;
	virtual FInputCaptureUpdate BeginCapture(const FInputDeviceState& input, EInputCaptureSide eSide) override;
	virtual FInputCaptureUpdate UpdateCapture(const FInputDeviceState& input, const FInputCaptureData& data) override;
	virtual void ForceEndCapture(const FInputCaptureData& data) override;

protected:
	UScalableSphereGizmo* Gizmo;
	FRay LastWorldRay;
	FVector2D LastScreenPosition;
	bool bInputDragCaptured;
};