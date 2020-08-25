// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InteractiveGizmo.h"
#include "InteractiveGizmoBuilder.h"
#include "BaseGizmos/TransformProxy.h"
#include "BaseGizmos/GizmoActor.h"
#include "BaseBehaviors/BehaviorTargetInterfaces.h"
#include "BaseBehaviors/AnyButtonInputBehavior.h"

#include "ScalableConeGizmo.generated.h"

UCLASS()
class LIGHTGIZMOS_API UScalableConeGizmoBuilder : public UInteractiveGizmoBuilder
{
	GENERATED_BODY()

public:
	virtual UInteractiveGizmo* BuildGizmo(const FToolBuilderState& SceneState) const override;
};

UCLASS()
class LIGHTGIZMOS_API AScalableConeGizmoActor : public AGizmoActor
{
	GENERATED_BODY()

public:

	AScalableConeGizmoActor();

	UPrimitiveComponent* ScaleHandleYPlus;
	UPrimitiveComponent* ScaleHandleYMinus;
	UPrimitiveComponent* ScaleHandleZPlus;
	UPrimitiveComponent* ScaleHandleZMinus;

	UPrimitiveComponent* LengthHandle;
};

/**
 * UScalableConeGizmo provides a cone that can be scaled (changing its angle)
 * The in-scene representation of the Gizmo is a AScalableConeGizmoActor (or subclass).
 */
UCLASS()
class LIGHTGIZMOS_API UScalableConeGizmo : public UInteractiveGizmo, public IHoverBehaviorTarget
{
	GENERATED_BODY()

public:
	// UInteractiveGizmo interface

	virtual void Setup() override;

	virtual void Render(IToolsContextRenderAPI* RenderAPI) override;

	virtual void Shutdown() override;

	// Set the target to attach the gizmo to
	virtual void SetTarget(UTransformProxy* InTarget);
	virtual void SetWorld(UWorld* InWorld);

	// Gettors and Settors for the Angle and Length
	void SetAngleDegrees(float InAngle);
	void SetLength(float InLength);
	float GetLength();
	float GetAngleDegrees();

	// IHoverBehaviorTarget interface
	virtual FInputRayHit BeginHoverSequenceHitTest(const FInputDeviceRay& PressPos) override { return FInputRayHit(); }
	virtual void OnBeginHover(const FInputDeviceRay& DevicePos) override {}
	virtual bool OnUpdateHover(const FInputDeviceRay& DevicePos) override { return true; }
	virtual void OnEndHover() override {}

	virtual void OnBeginDrag(const FInputDeviceRay& Ray);
	virtual void OnUpdateDrag(const FInputDeviceRay& Ray);
	bool HitTest(const FRay& Ray, FHitResult& OutHit, FVector& OutAxis, FTransform& OutTransform);

	// The maximum angle the cone can be stretched to
	UPROPERTY()
	float MaxAngle;

	// The minimum angle the cone can be stretched to
	UPROPERTY()
	float MinAngle;

	// The color of the cone
	UPROPERTY()
	FColor ConeColor;

	/** Called when the Angle of the cone is changed. Sends new angle as parameter. */
	TFunction<void(const float)> UpdateAngleFunc = nullptr;

private:

	void CreateGizmoHandles();
	void UpdateGizmoHandles();
	void OnTransformChanged(UTransformProxy*, FTransform);

	// The ConeLength
	UPROPERTY()
	float Length;

	UPROPERTY()
	float Angle;

	UPROPERTY()
	UTransformProxy* ActiveTarget;

	UPROPERTY()
	AScalableConeGizmoActor* GizmoActor;

	UPROPERTY()
	UWorld* World;

	/** Used for calculations when moving the handles*/

	UPROPERTY()
	FVector DragStartWorldPosition;

	UPROPERTY()
	FVector InteractionStartPoint;

	UPROPERTY()
	float InteractionStartParameter;

	UPROPERTY()
	FVector HitAxis;

	UPROPERTY()
	FVector RotationPlaneX;

	UPROPERTY()
	FVector RotationPlaneY;
};

/**
 * A behavior that forwards clicking and dragging to the gizmo.
 */
UCLASS()
class LIGHTGIZMOS_API UScalableConeGizmoInputBehavior : public UAnyButtonInputBehavior
{
	GENERATED_BODY()

public:
	virtual FInputCapturePriority GetPriority() override { return FInputCapturePriority(FInputCapturePriority::DEFAULT_GIZMO_PRIORITY); }

	virtual void Initialize(UScalableConeGizmo* Gizmo);

	virtual FInputCaptureRequest WantsCapture(const FInputDeviceState& input) override;
	virtual FInputCaptureUpdate BeginCapture(const FInputDeviceState& input, EInputCaptureSide eSide) override;
	virtual FInputCaptureUpdate UpdateCapture(const FInputDeviceState& input, const FInputCaptureData& data) override;
	virtual void ForceEndCapture(const FInputCaptureData& data) override;

protected:
	UScalableConeGizmo* Gizmo;
	FRay LastWorldRay;
	FVector2D LastScreenPosition;
	bool bInputDragCaptured;

};