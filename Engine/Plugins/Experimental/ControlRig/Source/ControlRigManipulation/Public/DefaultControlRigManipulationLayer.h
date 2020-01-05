// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IControlRigManipulationLayer.h"
#include "ControlRigGizmoActor.h"
#include "Units/RigUnitContext.h"
#include "DefaultControlRigManipulationLayer.generated.h"

class IControlRigObjectBinding;
typedef uint32 FControlID;

// control ID, I should use ID for it
struct FControlData
{
	IControlRigManipulatable* ManipObject;
	FName ControlName;

	friend bool operator==(const FControlData& Lhs, const FControlData& Rhs) { return &Lhs.ManipObject == &Rhs.ManipObject && Lhs.ControlName == Rhs.ControlName; }
};

/**
 * Default control rig manipulation layer
 *
 * This is default manipulation layer that supports editor functionality
 * This can support multiple control rigs and all control values by types
 * For now, it only supports 3D spacial types - vector/rotation/transform and one controlrig
 */
UCLASS()
class CONTROLRIGMANIPULATION_API UDefaultControlRigManipulationLayer : public UObject, public IControlRigManipulationLayer
{
	GENERATED_UCLASS_BODY()

	virtual ~UDefaultControlRigManipulationLayer() {}

public:
	// IControlRigManipulationLayer START
	virtual bool CreateGizmoActors(UWorld* World, TArray<AControlRigGizmoActor*>& OutGizmoActors);
	virtual void DestroyGizmosActors();

	virtual void CreateLayer() override;
	virtual void DestroyLayer() override;

	virtual void AddManipulatableObject(IControlRigManipulatable* InObject) override;
	virtual void RemoveManipulatableObject(IControlRigManipulatable* InObject) override;
	virtual void TickManipulatableObjects(float DeltaTime) override;

	virtual void SetGizmoTransform(AControlRigGizmoActor* GizmoActor, const FTransform& InTransform) override;
	virtual void GetGizmoTransform(AControlRigGizmoActor* GizmoActor, FTransform& OutTransform) const override;
	virtual void MoveGizmo(AControlRigGizmoActor* GizmoActor, const bool bTranslation, FVector& InDrag, 
		const bool bRotation, FRotator& InRot, const bool bScale, FVector& InScale, const FTransform& ToWorldTransform) override;
	virtual void TickGizmo(AControlRigGizmoActor* GizmoActor, const FTransform& ComponentTransform) override;
	virtual bool ModeSupportedByGizmoActor(const AControlRigGizmoActor* GizmoActor, FWidget::EWidgetMode InMode) const override;
	// IControlRigManipulationLayer END

	// Object binding
	/** Setup bindings to a runtime object (or clear by passing in nullptr). */
	void SetObjectBinding(TSharedPtr<IControlRigObjectBinding> InObjectBinding);
	/** Get bindings to a runtime object */
	TSharedPtr<IControlRigObjectBinding> GetObjectBinding() const;

	USkeletalMeshComponent* GetSkeletalMeshComponent() const;
	FTransform	GetSkeletalMeshComponentTransform() const;

	// Utility functions
	void BeginTransaction();
	void EndTransaction();

	// in this layer, we only care one to one
	const FControlData* GetControlDataFromGizmo(const AControlRigGizmoActor* GizmoActor) const;
	// this is slow, and it only finds the first one, and there is no guarantee it will always find the same name if you run this multiple sessions.
	// for example, if you have 2 control rigs (A and B for example) with same ControlName, it may find A or B
	AControlRigGizmoActor* GetGizmoFromControlName(const FName& ControlName) const;
	bool GetGlobalTransform(AControlRigGizmoActor* GizmoActor, const FName& ControlName, FTransform& OutTransform) const;

private:
	// GizmoActor* to ControlData index
	TMap<AControlRigGizmoActor*, FControlID> GizmoToControlMap;
	// the index is used for FControlID, so ensure the change of order is applied to GizmoToControlMap
	TArray<FControlData> ControlData;

	// link gizmo actor to (manipulatable object, control name). 
	void AddToControlData(AControlRigGizmoActor* GizmoActor, IControlRigManipulatable* InManipulatableObject, const FName& InControlName);

	// control data related
	virtual void ResetControlData();

	// Post pose update handler
	UFUNCTION()
	virtual void PostPoseUpdate();

	void OnControlModified(IControlRigManipulatable* InManipulatable, const FRigControl& InControl);
	TArray<FDelegateHandle> ControlModifiedDelegateHandles;

	void OnControlRigAdded(UControlRig* InControlRig);
	void OnControlRigRemoved(UControlRig* InControlRig);

	void GetGizmoCreationParams(TArray<FGizmoActorCreationParam>& OutCreationParams);
	// world clean up handlers
	FDelegateHandle OnWorldCleanupHandle;
	void OnWorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources);
	UWorld* WorldPtr = nullptr;
};
