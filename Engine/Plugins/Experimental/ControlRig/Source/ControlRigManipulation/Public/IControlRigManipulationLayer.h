// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UnrealWidget.h"

class ISequencer;
class IControlRigManipulatable;
class AControlRigGizmoActor;

/**
 * Manipulation Layer interface to create interactive interface
 * This work with IManipulatable object that provides options
 */
class CONTROLRIGMANIPULATION_API IControlRigManipulationLayer
{
public:
	IControlRigManipulationLayer();
	virtual ~IControlRigManipulationLayer() {};

	virtual void CreateLayer();
	virtual void DestroyLayer();

	// we only allow one type of class for now. This requires re-create layer. 
	virtual void AddManipulatableObject(IControlRigManipulatable* InObject);
	virtual void RemoveManipulatableObject(IControlRigManipulatable* InObject);
	virtual void TickManipulatableObjects(float DeltaTime) = 0;

	// virtual functions for child manipulation layer to write
	virtual bool CreateGizmoActors(UWorld* World, TArray<AControlRigGizmoActor*>& OutGizmoActors) = 0;
	virtual void DestroyGizmosActors() = 0;
	virtual void SetGizmoTransform(AControlRigGizmoActor* GizmoActor, const FTransform& InTransform) = 0;
	virtual void GetGizmoTransform(AControlRigGizmoActor* GizmoActor, FTransform& OutTransform) const = 0;
	virtual void MoveGizmo(AControlRigGizmoActor* GizmoActor, const bool bTranslation, FVector& InDrag,
		const bool bRotation, FRotator& InRot, const bool bScale, FVector& InScale, const FTransform& ToWorldTransform) = 0;
	virtual void TickGizmo(AControlRigGizmoActor* GizmoActor, const FTransform& ComponentTransform) = 0;
	virtual bool ModeSupportedByGizmoActor(const AControlRigGizmoActor* GizmoActor, FWidget::EWidgetMode InMode) const = 0;

protected:

	// because manipulatable is created externally often. Manipulation Layer doesn't have any ownership on these object
	// @fixme: We may want to make weak ptr? Can't be object ptr because IControlRiGManipulatable is not from UObject
	TArray<IControlRigManipulatable*> ManipulatableObjects;

private:
	// tracks whether this layer is created or not
	bool bLayerCreated;
};
