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
	virtual ~UTransformableHandle();

	virtual void PostLoad() override;
	
	/** Sanity check to ensure the handle is safe to use. */
	virtual bool IsValid() const PURE_VIRTUAL(IsValid, return false;);
	
	/** Sets the global transform of the underlying transformable object. */
	virtual void SetGlobalTransform(const FTransform& InGlobal) const PURE_VIRTUAL(SetGlobalTransform, );
	/** Sets the local transform of the underlying transformable object in it's parent space. */
	virtual void SetLocalTransform(const FTransform& InLocal) const PURE_VIRTUAL(SetLocalTransform, );
	/** Gets the transform of the underlying transformable object. */
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

	/**
	 * Generates a hash value of the underlying transformable object.
	**/
	virtual uint32 GetHash() const PURE_VIRTUAL(GetHash, return 0;);

	/** @todo document */
	virtual TWeakObjectPtr<UObject> GetTarget() const PURE_VIRTUAL(GetTarget, return nullptr;);

#if WITH_EDITOR
	virtual FName GetName() const PURE_VIRTUAL(GetName, return NAME_None;);
#endif
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
	
	/** @todo document */
	virtual bool IsValid() const override;
	
	/** @todo document */
	virtual void SetGlobalTransform(const FTransform& InGlobal) const override;
	/** Sets the local transform of the underlying transformable object in it's parent space. */
	virtual void SetLocalTransform(const FTransform& InLocal) const override;
	/** @todo document */
	virtual FTransform GetGlobalTransform() const override;
	/** @todo document */
	virtual FTransform GetLocalTransform() const override;

	/** @todo document */
	virtual UObject* GetPrerequisiteObject() const override;
	/** @todo document */
	virtual FTickFunction* GetTickFunction() const override;

	/** @todo document */
	virtual uint32 GetHash() const override;

	/** @todo document */
	virtual TWeakObjectPtr<UObject> GetTarget() const override;

#if WITH_EDITOR
	/** @todo document */
	virtual FName GetName() const override;
#endif
	
	/** @todo document */
	UPROPERTY(BlueprintReadOnly, Category = "Object")
	TWeakObjectPtr<USceneComponent> Component;
};