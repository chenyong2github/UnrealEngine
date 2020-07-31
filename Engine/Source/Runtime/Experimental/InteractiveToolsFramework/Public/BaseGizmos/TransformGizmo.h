// Copyright Epic Games, Inc. All Rights Reserved.

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
class UGizmoConstantFrameAxisSource;
class UGizmoComponentAxisSource;
class UGizmoTransformChangeStateTarget;
class UGizmoScaledTransformSource;
class FTransformGizmoTransformChange;

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
	//
	// Translation Components
	//

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

	//
	// Rotation Components
	//

	/** X Axis Rotation Component */
	UPROPERTY()
	UPrimitiveComponent* RotateX;

	/** Y Axis Rotation Component */
	UPROPERTY()
	UPrimitiveComponent* RotateY;

	/** Z Axis Rotation Component */
	UPROPERTY()
	UPrimitiveComponent* RotateZ;

	//
	// Scaling Components
	//

	/** Uniform Scale Component */
	UPROPERTY()
	UPrimitiveComponent* UniformScale;


	/** X Axis Scale Component */
	UPROPERTY()
	UPrimitiveComponent* AxisScaleX;

	/** Y Axis Scale Component */
	UPROPERTY()
	UPrimitiveComponent* AxisScaleY;

	/** Z Axis Scale Component */
	UPROPERTY()
	UPrimitiveComponent* AxisScaleZ;


	/** YZ Plane Scale Component */
	UPROPERTY()
	UPrimitiveComponent* PlaneScaleYZ;

	/** XZ Plane Scale Component */
	UPROPERTY()
	UPrimitiveComponent* PlaneScaleXZ;

	/** XY Plane Scale Component */
	UPROPERTY()
	UPrimitiveComponent* PlaneScaleXY;



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
		ETransformGizmoSubElements::TranslateAllAxes |
		ETransformGizmoSubElements::TranslateAllPlanes |
		ETransformGizmoSubElements::RotateAllAxes |
		ETransformGizmoSubElements::ScaleAllAxes |
		ETransformGizmoSubElements::ScaleAllPlanes | 
		ETransformGizmoSubElements::ScaleUniform;

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
class INTERACTIVETOOLSFRAMEWORK_API UTransformGizmo : public UInteractiveGizmo, public IToolCommandChangeSource
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
	 * by quering the ContextyQueriesAPI, otherwise the default is Local and the client can change it as necessary
	 */
	UPROPERTY()
	EToolContextCoordinateSystem CurrentCoordinateSystem = EToolContextCoordinateSystem::Local;


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

	/** List of current-active child components */
	UPROPERTY()
	TArray<UPrimitiveComponent*> ActiveComponents;

	/** 
	 * List of nonuniform scale components. Subset of of ActiveComponents. These are tracked separately so they can
	 * be hidden when Gizmo is not configured to use local axes, because UE only supports local nonuniform scaling
	 * on Components
	 */
	UPROPERTY()
	TArray<UPrimitiveComponent*> NonuniformScaleComponents;

	/** list of currently-active child gizmos */
	UPROPERTY()
	TArray<UInteractiveGizmo*> ActiveGizmos;

	/** GizmoActors will be spawned in this World */
	UWorld* World;

	/** Current active GizmoActor that was spawned by this Gizmo. Will be destroyed when Gizmo is. */
	ATransformGizmoActor* GizmoActor;

	//
	// Axis Sources
	//


	/** Axis that points towards camera, X/Y plane tangents aligned to right/up. Shared across Gizmos, and created internally during SetActiveTarget() */
	UPROPERTY()
	UGizmoConstantFrameAxisSource* CameraAxisSource;

	// internal function that updates CameraAxisSource by getting current view state from GizmoManager
	void UpdateCameraAxisSource();


	/** X-axis source is shared across Gizmos, and created internally during SetActiveTarget() */
	UPROPERTY()
	UGizmoComponentAxisSource* AxisXSource;

	/** Y-axis source is shared across Gizmos, and created internally during SetActiveTarget() */
	UPROPERTY()
	UGizmoComponentAxisSource* AxisYSource;

	/** Z-axis source is shared across Gizmos, and created internally during SetActiveTarget() */
	UPROPERTY()
	UGizmoComponentAxisSource* AxisZSource;

	//
	// Scaling support. 
	// UE Components only support scaling in local coordinates, so we have to create separate sources for that.
	//

	/** Local X-axis source (ie 1,0,0) is shared across Scale Gizmos, and created internally during SetActiveTarget() */
	UPROPERTY()
	UGizmoComponentAxisSource* UnitAxisXSource;

	/** Y-axis source (ie 0,1,0) is shared across Scale Gizmos, and created internally during SetActiveTarget() */
	UPROPERTY()
	UGizmoComponentAxisSource* UnitAxisYSource;

	/** Z-axis source (ie 0,0,1) is shared across Scale Gizmos, and created internally during SetActiveTarget() */
	UPROPERTY()
	UGizmoComponentAxisSource* UnitAxisZSource;


	//
	// Other Gizmo Components
	//


	/** 
	 * State target is shared across gizmos, and created internally during SetActiveTarget(). 
	 * Several FChange providers are registered with this StateTarget, including the UTransformGizmo
	 * itself (IToolCommandChangeSource implementation above is called)
	 */
	UPROPERTY()
	UGizmoTransformChangeStateTarget* StateTarget;


	/**
	 * This TransformSource wraps a UGizmoComponentWorldTransformSource that is on the Gizmo Actor directly.
	 * It tracks the scaling separately (SeparateChildScale is provided as the storage for the scaling).
	 * This allows the various scaling handles to update the Transform without actually scaling the Gizmo itself.
	 */
	UPROPERTY()
	UGizmoScaledTransformSource* ScaledTransformSource;

	/**
	 * Scaling on the active target UTransformProxy is stored here. Undo/Redo events update this value via an FTransformGizmoTransformChange.
	 */
	FVector SeparateChildScale = FVector(1,1,1);

	/**
	 * This function is called by FTransformGizmoTransformChange to update SeparateChildScale on Undo/Redo.
	 */
	void ExternalSetChildScale(const FVector& NewScale);

	friend class FTransformGizmoTransformChange;
	/**
	 * Ongoing transform change is tracked here, initialized in the IToolCommandChangeSource implementation.
	 * Note that currently we only handle the SeparateChildScale this way. The transform changes on the target
	 * objects are dealt with using the editor Transaction system (and on the Proxy using a FTransformProxyChangeSource attached to the StateTarget).
	 * The Transaction system is not available at runtime, we should track the entire change in the FChange and update the proxy
	 */
	TUniquePtr<FTransformGizmoTransformChange> ActiveChange;

public:
	// IToolCommandChangeSource implementation, used to initialize and emit ActiveChange
	virtual void BeginChange();
	virtual TUniquePtr<FToolCommandChange> EndChange();
	virtual UObject* GetChangeTarget();
	virtual FText GetChangeDescription();


protected:


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

	/** @return a new instance of the standard axis-scaling Gizmo */
	virtual UInteractiveGizmo* AddAxisScaleGizmo(
		UPrimitiveComponent* AxisComponent, USceneComponent* RootComponent,
		IGizmoAxisSource* GizmoAxisSource, IGizmoAxisSource* ParameterAxisSource,
		IGizmoTransformSource* TransformSource,
		IGizmoStateTarget* StateTarget);

	/** @return a new instance of the standard plane-scaling Gizmo */
	virtual UInteractiveGizmo* AddPlaneScaleGizmo(
		UPrimitiveComponent* AxisComponent, USceneComponent* RootComponent,
		IGizmoAxisSource* GizmoAxisSource, IGizmoAxisSource* ParameterAxisSource,
		IGizmoTransformSource* TransformSource,
		IGizmoStateTarget* StateTarget);

	/** @return a new instance of the standard plane-scaling Gizmo */
	virtual UInteractiveGizmo* AddUniformScaleGizmo(
		UPrimitiveComponent* ScaleComponent, USceneComponent* RootComponent,
		IGizmoAxisSource* GizmoAxisSource, IGizmoAxisSource* ParameterAxisSource,
		IGizmoTransformSource* TransformSource,
		IGizmoStateTarget* StateTarget);

	// Axis and Plane TransformSources use this function to execute worldgrid snap queries
	bool PositionSnapFunction(const FVector& WorldPosition, FVector& SnappedPositionOut) const;
	FQuat RotationSnapFunction(const FQuat& DeltaRotation) const;

};




/**
 * FChange for a UTransformGizmo that applies transform change.
 * Currently only handles the scaling part of a transform.
 */
class INTERACTIVETOOLSFRAMEWORK_API FTransformGizmoTransformChange : public FToolCommandChange
{
public:
	FVector ChildScaleBefore;
	FVector ChildScaleAfter;

	/** Makes the change to the object */
	virtual void Apply(UObject* Object) override;

	/** Reverts change to the object */
	virtual void Revert(UObject* Object) override;

	/** Describes this change (for debugging) */
	virtual FString ToString() const override;
};

