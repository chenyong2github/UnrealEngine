// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InteractiveGizmo.h"
#include "InteractiveGizmoBuilder.h"
#include "InteractiveToolObjects.h"
#include "InteractiveToolChange.h"
#include "BaseGizmos/GizmoActor.h"
#include "BaseGizmos/TransformProxy.h"
#include "TransformGizmo.generated.h"

class UInteractiveGizmoManager;
class IGizmoAxisSource;
class IGizmoTransformSource;
class IGizmoStateTarget;
class UGizmoComponentAxisSource;
class UGizmoTransformChangeStateTarget;


/**
 * ATransformGizmoActor is an Actor type intended to be used with UTransformGizmo,
 * as the in-scene visual representation of the Gizmo.
 * 
 * FTransformGizmoActorFactory returns an instance of this Actor type (or a subclass), and based on
 * which Translate and Rotate UProperties are initialized, will associate those Components
 * with UInteractiveGizmo's that implement Axis Translation, Plane Translation, and Axis Rotation.
 * 
 * If a particular sub-Gizmo is not required, simply set that FProperty to null.
 * 
 * The static factory method ::ConstructDefault3AxisGizmo() creates and initializes an 
 * Actor suitable for use in a standard 3-axis Transformation Gizmo.
 */
UCLASS(Transient)
class INTERACTIVETOOLSFRAMEWORK_API ATransformGizmoActor : public AGizmoActor
{
	GENERATED_BODY()
public:

	ATransformGizmoActor();

public:
	/** X Axis Translation Component */
	UPROPERTY()
	UPrimitiveComponent* TranslateX;

	/** Y Axis Translation Component */
	UPROPERTY()
	UPrimitiveComponent* TranslateY;

	/** Z Axis Translation Component */
	UPROPERTY()
	UPrimitiveComponent* TranslateZ;


	/** YZ Plane Translation Component */
	UPROPERTY()
	UPrimitiveComponent* TranslateYZ;

	/** XZ Plane Translation Component */
	UPROPERTY()
	UPrimitiveComponent* TranslateXZ;

	/** XY Plane Translation Component */
	UPROPERTY()
	UPrimitiveComponent* TranslateXY;


	/** X Axis Rotation Component */
	UPROPERTY()
	UPrimitiveComponent* RotateX;

	/** Y Axis Rotation Component */
	UPROPERTY()
	UPrimitiveComponent* RotateY;

	/** Z Axis Rotation Component */
	UPROPERTY()
	UPrimitiveComponent* RotateZ;


public:
	/**
	 * Create a new instance of ATransformGizmoActor and populate the various
	 * sub-components with standard GizmoXComponent instances suitable for a 3-axis transformer Gizmo
	 */
	static ATransformGizmoActor* ConstructDefault3AxisGizmo(
		UWorld* World
	);

	/**
	 * Create a new instance of ATransformGizmoActor. Populate the sub-components 
	 * specified by Elements with standard GizmoXComponent instances suitable for a 3-axis transformer Gizmo
	 */
	static ATransformGizmoActor* ConstructCustom3AxisGizmo(
		UWorld* World,
		ETransformGizmoSubElements Elements
	);
};





/**
 * FTransformGizmoActorFactory creates new instances of ATransformGizmoActor which
 * are used by UTransformGizmo to implement 3D transformation Gizmos. 
 * An instance of FTransformGizmoActorFactory is passed to UTransformGizmo
 * (by way of UTransformGizmoBuilder), which then calls CreateNewGizmoActor()
 * to spawn new Gizmo Actors.
 * 
 * By default CreateNewGizmoActor() returns a default Gizmo Actor suitable for
 * a three-axis transformation Gizmo, override this function to customize
 * the Actor sub-elements.
 */
class INTERACTIVETOOLSFRAMEWORK_API FTransformGizmoActorFactory
{
public:
	/** Only these members of the ATransformGizmoActor gizmo will be initialized */
	ETransformGizmoSubElements EnableElements =
		ETransformGizmoSubElements::TranslateAllAxes | ETransformGizmoSubElements::TranslateAllPlanes | ETransformGizmoSubElements::RotateAllAxes;

	/**
	 * @param World the UWorld to create the new Actor in
	 * @return new ATransformGizmoActor instance with members initialized with Components suitable for a transformation Gizmo
	 */
	virtual ATransformGizmoActor* CreateNewGizmoActor(UWorld* World) const;
};






UCLASS()
class INTERACTIVETOOLSFRAMEWORK_API UTransformGizmoBuilder : public UInteractiveGizmoBuilder
{
	GENERATED_BODY()

public:
	/**
	 * If set, this Actor Builder will be passed to UTransformGizmo instances.
	 * Otherwise new instances of the base FTransformGizmoActorFactory are created internally.
	 */
	TSharedPtr<FTransformGizmoActorFactory> GizmoActorBuilder;

	/**
	 * If set, this hover function will be passed to UTransformGizmo instances to use instead of the default.
	 * Hover is complicated for UTransformGizmo because all it knows about the different gizmo scene elements
	 * is that they are UPrimitiveComponent (coming from the ATransformGizmoActor). The default hover
	 * function implementation is to try casting to UGizmoBaseComponent and calling ::UpdateHoverState().
	 * If you are using different Components that do not subclass UGizmoBaseComponent, and you want hover to 
	 * work, you will need to provide a different hover update function.
	 */
	TFunction<void(UPrimitiveComponent*, bool)> UpdateHoverFunction;

	/**
	 * If set, this coord-system function will be passed to UTransformGizmo instances to use instead
	 * of the default UpdateCoordSystemFunction. By default the UTransformGizmo will query the external Context
	 * to ask whether it should be using world or local coordinate system. Then the default UpdateCoordSystemFunction
	 * will try casting to UGizmoBaseCmponent and passing that info on via UpdateWorldLocalState();
	 * If you are using different Components that do not subclass UGizmoBaseComponent, and you want the coord system
	 * to be configurable, you will need to provide a different update function.
	 */
	TFunction<void(UPrimitiveComponent*, EToolContextCoordinateSystem)> UpdateCoordSystemFunction;


	virtual UInteractiveGizmo* BuildGizmo(const FToolBuilderState& SceneState) const override;
};


/**
 * UTransformGizmo provides standard Transformation Gizmo interactions,
 * applied to a UTransformProxy target object. By default the Gizmo will be
 * a standard XYZ translate/rotate Gizmo (axis and plane translation).
 * 
 * The in-scene representation of the Gizmo is a ATransformGizmoActor (or subclass).
 * This Actor has FProperty members for the various sub-widgets, each as a separate Component.
 * Any particular sub-widget of the Gizmo can be disabled by setting the respective
 * Actor Component to null. 
 * 
 * So, to create non-standard variants of the Transform Gizmo, set a new GizmoActorBuilder 
 * in the UTransformGizmoBuilder registered with the GizmoManager. Return
 * a suitably-configured GizmoActor and everything else will be handled automatically.
 * 
 */
UCLASS()
class INTERACTIVETOOLSFRAMEWORK_API UTransformGizmo : public UInteractiveGizmo
{
	GENERATED_BODY()

public:

	virtual void SetWorld(UWorld* World);
	virtual void SetGizmoActorBuilder(TSharedPtr<FTransformGizmoActorFactory> Builder);
	virtual void SetUpdateHoverFunction(TFunction<void(UPrimitiveComponent*, bool)> HoverFunction);
	virtual void SetUpdateCoordSystemFunction(TFunction<void(UPrimitiveComponent*, EToolContextCoordinateSystem)> CoordSysFunction);

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
	UTransformProxy* ActiveTarget;

	/**
	 * @return the internal GizmoActor used by the Gizmo
	 */
	ATransformGizmoActor* GetGizmoActor() const { return GizmoActor; }

	/**
	 * Set a new position for the Gizmo. This is done via the same mechanisms as the sub-gizmos,
	 * so it generates the same Change/Modify() events, and hence works with Undo/Redo
	 */
	virtual void SetNewGizmoTransform(const FTransform& NewTransform);


protected:
	TSharedPtr<FTransformGizmoActorFactory> GizmoActorBuilder;

	// This function is called on each active GizmoActor Component to update it's hover state.
	// If the Component is not a UGizmoBaseCmponent, the client needs to provide a different implementation
	// of this function via the ToolBuilder
	TFunction<void(UPrimitiveComponent*, bool)> UpdateHoverFunction;

	// This function is called on each active GizmoActor Component to update it's coordinate system (eg world/local).
	// If the Component is not a UGizmoBaseCmponent, the client needs to provide a different implementation
	// of this function via the ToolBuilder
	TFunction<void(UPrimitiveComponent*, EToolContextCoordinateSystem)> UpdateCoordSystemFunction;

	/** list of current-active child components */
	UPROPERTY()
	TArray<UPrimitiveComponent*> ActiveComponents;

	/** list of currently-active child gizmos */
	UPROPERTY()
	TArray<UInteractiveGizmo*> ActiveGizmos;

	/** GizmoActors will be spawned in this World */
	UWorld* World;

	/** Current active GizmoActor that was spawned by this Gizmo. Will be destroyed when Gizmo is. */
	ATransformGizmoActor* GizmoActor;

	/** X-axis source is shared across Gizmos, and created internally during SetActiveTarget() */
	UPROPERTY()
	UGizmoComponentAxisSource* AxisXSource;

	/** Y-axis source is shared across Gizmos, and created internally during SetActiveTarget() */
	UPROPERTY()
	UGizmoComponentAxisSource* AxisYSource;

	/** Z-axis source is shared across Gizmos, and created internally during SetActiveTarget() */
	UPROPERTY()
	UGizmoComponentAxisSource* AxisZSource;

	/** State target is shared across gizmos, and created internally during SetActiveTarget() */
	UPROPERTY()
	UGizmoTransformChangeStateTarget* StateTarget;

	/** @return a new instance of the standard axis-translation Gizmo */
	virtual UInteractiveGizmo* AddAxisTranslationGizmo(
		UPrimitiveComponent* AxisComponent, USceneComponent* RootComponent,
		IGizmoAxisSource* AxisSource,
		IGizmoTransformSource* TransformSource, 
		IGizmoStateTarget* StateTarget);

	/** @return a new instance of the standard plane-translation Gizmo */
	virtual UInteractiveGizmo* AddPlaneTranslationGizmo(
		UPrimitiveComponent* AxisComponent, USceneComponent* RootComponent,
		IGizmoAxisSource* AxisSource,
		IGizmoTransformSource* TransformSource,
		IGizmoStateTarget* StateTarget);

	/** @return a new instance of the standard axis-rotation Gizmo */
	virtual UInteractiveGizmo* AddAxisRotationGizmo(
		UPrimitiveComponent* AxisComponent, USceneComponent* RootComponent,
		IGizmoAxisSource* AxisSource,
		IGizmoTransformSource* TransformSource,
		IGizmoStateTarget* StateTarget);
};

