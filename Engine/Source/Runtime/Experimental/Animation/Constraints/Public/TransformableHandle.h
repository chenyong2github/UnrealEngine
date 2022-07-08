// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Object.h"

#include "TransformableHandle.generated.h"

struct FTickFunction;
class USceneComponent;

/**
 * UTransformableHandle
 */

UCLASS(Abstract, Blueprintable)
class CONSTRAINTS_API UTransformableHandle : public UObject 
{
	GENERATED_BODY()
	
public:

	DECLARE_EVENT_TwoParams(UTransformableHandle, FHandleModifiedEvent, UTransformableHandle*, bool);
	
	virtual ~UTransformableHandle();

	virtual void PostLoad() override;
	
	/** Sanity check to ensure the handle is safe to use. */
	virtual bool IsValid() const PURE_VIRTUAL(IsValid, return false;);
	
	/** Sets the global transform of the underlying transformable object. */
	virtual void SetGlobalTransform(const FTransform& InGlobal) const PURE_VIRTUAL(SetGlobalTransform, );
	/** Sets the local transform of the underlying transformable object in it's parent space. */
	virtual void SetLocalTransform(const FTransform& InLocal) const PURE_VIRTUAL(SetLocalTransform, );
	/** Gets the global transform of the underlying transformable object. */
	virtual FTransform GetGlobalTransform() const PURE_VIRTUAL(GetGlobalTransform, return FTransform::Identity;);
	/** Gets the local transform of the underlying transformable object in it's parent space. */
	virtual FTransform GetLocalTransform() const PURE_VIRTUAL(GetLocalTransform, return FTransform::Identity;);

	/**
	 * Returns the target object containing the tick function (returned in GetTickFunction).
	 * See FTickFunction::AddPrerequisite for details.
	 **/
	virtual UObject* GetPrerequisiteObject() const PURE_VIRTUAL(GetPrerequisiteObject, return nullptr;);
	/**
	 * Returns the tick function of the underlying transformable object.
	 * This is used to set dependencies with the constraint.
	**/
	virtual FTickFunction* GetTickFunction() const PURE_VIRTUAL(GetTickFunction, return nullptr;);

	/** Generates a hash value of the underlying transformable object. */
	virtual uint32 GetHash() const PURE_VIRTUAL(GetHash, return 0;);

	/** @todo document */
	virtual TWeakObjectPtr<UObject> GetTarget() const PURE_VIRTUAL(GetTarget, return nullptr;);

	FHandleModifiedEvent& HandleModified();

#if WITH_EDITOR
	virtual FString GetLabel() const PURE_VIRTUAL(GetLabel, return FString(););
	virtual FString GetFullLabel() const PURE_VIRTUAL(GetFullLabel, return FString(););
#endif

protected:
	FHandleModifiedEvent OnHandleModified;
};

/**
 * UTransformableComponentHandle
 */

UCLASS(Blueprintable)
class CONSTRAINTS_API UTransformableComponentHandle : public UTransformableHandle 
{
	GENERATED_BODY()
	
public:
	
	virtual ~UTransformableComponentHandle();
	
	/** Sanity check to ensure that Component. */
	virtual bool IsValid() const override;
	
	/** Sets the global transform of Component. */
	virtual void SetGlobalTransform(const FTransform& InGlobal) const override;
	/** Sets the local transform of Component in it's attachment. */
	virtual void SetLocalTransform(const FTransform& InLocal) const override;
	/** Gets the global transform of Component. */
	virtual FTransform GetGlobalTransform() const override;
	/** Gets the local transform of Component in it's attachment. */
	virtual FTransform GetLocalTransform() const override;

	/** Returns the target object containing the tick function (e.i. Component). */
	virtual UObject* GetPrerequisiteObject() const override;
	/** Returns Component's tick function. */
	virtual FTickFunction* GetTickFunction() const override;

	/** Generates a hash value of Component. */
	virtual uint32 GetHash() const override;

	/** @todo document */
	virtual TWeakObjectPtr<UObject> GetTarget() const override;

#if WITH_EDITOR
	virtual FString GetLabel() const override;
	virtual FString GetFullLabel() const override;
#endif
	
	/** The Component that this handle is pointing at. */
	UPROPERTY(BlueprintReadOnly, Category = "Object")
	TWeakObjectPtr<USceneComponent> Component;

	/** Registers/Unregisters useful delegates to track changes in the Component's transform. */
	void UnregisterDelegates() const;
	void RegisterDelegates();
	
	/** @todo document */
	void OnActorMoving(AActor* InActor);
	void OnPostPropertyChanged(UObject* InObject, FPropertyChangedEvent& InPropertyChangedEvent);
};