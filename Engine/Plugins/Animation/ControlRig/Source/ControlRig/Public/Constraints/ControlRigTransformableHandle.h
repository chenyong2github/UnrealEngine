// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "TransformableHandle.h"
#include "Rigs/RigHierarchyDefines.h"

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
	
	/** @todo document */
	virtual bool IsValid() const override;

	/** @todo document */
	virtual void SetGlobalTransform(const FTransform& InGlobal) const override;
	/** @todo document */
	virtual void SetLocalTransform(const FTransform& InLocal) const override;
	/** @todo document */
	virtual FTransform GetGlobalTransform() const  override;
	/** @todo document */
	virtual FTransform GetLocalTransform() const  override;

	/** @todo document */
	virtual UObject* GetPrerequisiteObject() const override;
	/** @todo document */
	virtual FTickFunction* GetTickFunction() const override;

	/** @todo document */
	virtual uint32 GetHash() const override;
	/** @todo document */
	virtual TWeakObjectPtr<UObject> GetTarget() const override;
	/** @todo document */
	USkeletalMeshComponent* GetSkeletalMesh() const;

	/** @todo document */
	void UnregisterDelegates() const;
	void RegisterDelegates();

#if WITH_EDITOR
	/** @todo document */
	virtual FName GetName() const override;
#endif

	/** @todo document */
	UPROPERTY(BlueprintReadOnly, Category = "Object")
	TSoftObjectPtr<UControlRig> ControlRig;

	/** @todo document */
	UPROPERTY(BlueprintReadOnly, Category = "Object")
	FName ControlName;
	
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