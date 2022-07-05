// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "TransformableHandle.h"
#include "Rigs/RigHierarchyDefines.h"
#include "ControlRig.h"

#include "ControlRigTransformableHandle.generated.h"

class UControlRig;
class USkeletalMeshComponent;
struct FRigBaseElement;
class URigHierarchy;
struct FRigControlElement;


/**
 * UTransformableControlHandle
 */

UCLASS(Blueprintable)
class CONTROLRIG_API UTransformableControlHandle : public UTransformableHandle 
{
	GENERATED_BODY()
	
public:
	virtual ~UTransformableControlHandle();

	virtual void PostLoad() override;
	
	/** Sanity check to ensure that ControlRig and ControlName are safe to use. */
	virtual bool IsValid() const override;

	/** Sets the global transform of the control. */
	virtual void SetGlobalTransform(const FTransform& InGlobal) const override;
	/** Sets the local transform of the control. */
	virtual void SetLocalTransform(const FTransform& InLocal) const override;
	/** Gets the global transform of the control. */
	virtual FTransform GetGlobalTransform() const  override;
	/** Sets the local transform of the control. */
	virtual FTransform GetLocalTransform() const  override;

	/** Returns the target object containing the tick function (e.i. SkeletalComponent bound to ControlRig). */
	virtual UObject* GetPrerequisiteObject() const override;
	/** Returns ths SkeletalComponent tick function. */
	virtual FTickFunction* GetTickFunction() const override;

	/** Generates a hash value based on ControlRig and ControlName. */
	virtual uint32 GetHash() const override;
	/** @todo document */
	virtual TWeakObjectPtr<UObject> GetTarget() const override;

	/** Returns the skeletal mesh bound to ControlRig. */
	USkeletalMeshComponent* GetSkeletalMesh() const;

	/** Registers/Unregisters useful delegates to track changes in the control's transform. */
	void UnregisterDelegates() const;
	void RegisterDelegates();

#if WITH_EDITOR
	/** @todo document */
	virtual FName GetName() const override;
#endif

	/** The ControlRig that this handle is pointing at. */
	UPROPERTY(BlueprintReadOnly, Category = "Object")
	TSoftObjectPtr<UControlRig> ControlRig;

	/** The ControlName of the control that this handle is pointing at. */
	UPROPERTY(BlueprintReadOnly, Category = "Object")
	FName ControlName;

	/** @todo document */
	void OnControlModified(
		UControlRig* InControlRig,
		FRigControlElement* InControl,
		const FRigControlModifiedContext& InContext);
	
private:

	/** @todo document */
	void OnHierarchyModified(
		ERigHierarchyNotification InNotif,
		URigHierarchy* InHierarchy,
		const FRigBaseElement* InElement);

#if WITH_EDITOR
	/** @todo document */
	void OnObjectsReplaced(const TMap<UObject*, UObject*>& InOldToNewInstances);
#endif

	/** @todo document */
	FRigControlElement* GetControlElement() const;
};