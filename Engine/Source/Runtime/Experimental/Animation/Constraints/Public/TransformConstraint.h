// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Constraint.h"
#include "ConstraintsManager.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "TransformConstraint.generated.h"

class UTransformableHandle;
class UTransformableComponentHandle;
class USceneComponent;

using SetTransformFunc = TFunction<void(const FTransform&)>;
using GetTransformFunc = TFunction<FTransform()>;

/** 
 * UTickableTransformConstraint
 **/

UCLASS(Abstract, Blueprintable)
class CONSTRAINTS_API UTickableTransformConstraint : public UTickableConstraint
{
	GENERATED_BODY()

public:

	/** Sets up the constraint so that the initial offset is set and dependencies and handles managed. */
	void Setup();
	
	/** UObjects overrides. */
	virtual void PostLoad() override;
	virtual void PostDuplicate(bool bDuplicateForPIE) override;

	/** Returns the target hash value (i.e. the child handle's hash). */
	virtual uint32 GetTargetHash() const override;
	
	/** Test whether an InObject is referenced by that constraint. (i.e. is it's parent or child). */
	virtual bool ReferencesObject(TWeakObjectPtr<UObject> InObject) const override;
	
	/** The transformable handle representing the parent of that constraint. */
	UPROPERTY(BlueprintReadWrite, Category = "Handle")
	TObjectPtr<UTransformableHandle> ParentTRSHandle;
	
	/** The transformable handle representing the child of that constraint. */
	UPROPERTY(BlueprintReadWrite, Category = "Handle")
	TObjectPtr<UTransformableHandle> ChildTRSHandle;

	/** Should that constraint maintain the default offset.  */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Offset")
	bool bMaintainOffset = true;

	/** Defines how much the constraint will be applied. */
	// UPROPERTY(EditAnywhere, BlueprintReadWrite,Category="Weight", meta = (Input, ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	//@benoit when not EditAnywhere?
	UPROPERTY(meta = (Input, ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float Weight = 1.f;

	/** Should the child be able to change it's offset dynamically. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Offset")
	bool bDynamicOffset = false;

	/** Returns the constraint type (Position, Parent, Aim...). */
	int64 GetType() const;

	/** Get the current child's global transform. */
	FTransform GetChildGlobalTransform() const;
	
	/** Get the current child's local transform. */
	FTransform GetChildLocalTransform() const;

	/** Get the current parent's global transform. */
	FTransform GetParentGlobalTransform() const;
	
	/** Get the current parent's local transform. */
	FTransform GetParentLocalTransform() const;
	
protected:

	/** Registers/Unregisters useful delegates for both child and parent handles. */
	void UnregisterDelegates() const;
	void RegisterDelegates();
	
	/**
	 * Manages changes on the child/parent transformable handle. This can be used to update internal data (i.e. offset)
	 * when transform changes outside of that system and need to trigger changes within the constraint itself.   
	*/
	virtual void OnHandleModified(UTransformableHandle* InHandle, bool bUpdate);
	
	/**
	 * Computes the initial offset that is needed to keep the child's global transform unchanged when creating the
	 * constraint. This can be called whenever necessary to update the current offset.
	*/
	virtual void ComputeOffset() PURE_VIRTUAL(ComputeOffset, return;);
	
	/**
	 * Sets up dependencies between the parent, the constraint and the child using their respective tick functions.
	 * It creates a dependency graph between them so that they tick in the right order when evaluated.   
	*/
	void SetupDependencies();
	
	/** Set the current child's global transform. */
	void SetChildGlobalTransform(const FTransform& InGlobal) const;
	
	/** Set the current child's local transform. */
	void SetChildLocalTransform(const FTransform& InLocal) const;

	/** Defines the constraint's type (Position, Parent, Aim...). */
	UPROPERTY()
	ETransformConstraintType Type = ETransformConstraintType::Parent;

#if WITH_EDITOR
public:
	/** Returns the constraint's label used for UI. */
	virtual FString GetLabel() const override;
	virtual FString GetFullLabel() const override;

	/** Returns the constraint's type label used for UI. */
	virtual FString GetTypeLabel() const override;

	// UObject interface
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	// End of UObject interface
#endif
};

/** 
 * UTickableTranslationConstraint
 **/

UCLASS(Blueprintable)
class CONSTRAINTS_API UTickableTranslationConstraint : public UTickableTransformConstraint
{
	GENERATED_BODY()

public:
	UTickableTranslationConstraint();

	/** Returns the position constraint function that the tick function will evaluate. */
	virtual FConstraintTickFunction::ConstraintFunction GetFunction() const override;

protected:

	/** Cache data structure to store last child local/global location. */
	struct FDynamicCache
	{
		uint32 CachedInputHash = 0;
	};
	mutable FDynamicCache Cache;
	uint32 CalculateInputHash() const;

	/** Updates the dynamic offset based on external child's transform changes. */
	virtual void OnHandleModified(UTransformableHandle* InHandle, bool bUpdate) override;
	
	/** Computes the child's local translation offset in the parent space. */
	virtual void ComputeOffset() override;

	/** Defines the local child's translation offset in the parent space. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Offset")
	FVector OffsetTranslation = FVector::ZeroVector;

	/** Defines the local child's translation dynamic offset in the parent space. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Offset")
	FVector DynamicOffsetTranslation = FVector::ZeroVector;

#if WITH_EDITOR
public:
	// UObject interface
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	// End of UObject interface
#endif
};

/** 
 * UTickableRotationConstraint
 **/

UCLASS(Blueprintable)
class CONSTRAINTS_API UTickableRotationConstraint : public UTickableTransformConstraint
{
	GENERATED_BODY()

public:
	UTickableRotationConstraint();

	/** Returns the rotation constraint function that the tick function will evaluate. */
	virtual FConstraintTickFunction::ConstraintFunction GetFunction() const override;

protected:

	/** Cache data structure to store last child local/global rotation. */
	struct FDynamicCache
	{
		uint32 CachedInputHash = 0;
	};
	mutable FDynamicCache Cache;
	uint32 CalculateInputHash() const;
	
	/** Computes the child's local rotation offset in the parent space. */
	virtual void ComputeOffset() override;

	/** Updates the dynamic offset based on external child's transform changes. */
	virtual void OnHandleModified(UTransformableHandle* InHandle, bool bUpdate) override;
	
	/** Defines the local child's rotation offset in the parent space. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Offset")
	FQuat OffsetRotation = FQuat::Identity;

	/** Defines the local child's rotation dynamic offset in the parent space. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Offset")
	FQuat DynamicOffsetRotation = FQuat::Identity;

#if WITH_EDITOR
public:
	// UObject interface
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	// End of UObject interface
#endif
};

/** 
 * UTickableScaleConstraint
 **/

UCLASS(Blueprintable)
class CONSTRAINTS_API UTickableScaleConstraint : public UTickableTransformConstraint
{
	GENERATED_BODY()

public:
	UTickableScaleConstraint();
	
	/** Returns the scale constraint function that the tick function will evaluate. */
	virtual FConstraintTickFunction::ConstraintFunction GetFunction() const override;

protected:
	/** Computes the child's local scale offset in the parent space. */
	virtual void ComputeOffset() override;

	/** Defines the local child's scale offset in the parent space. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Offset")
	FVector OffsetScale = FVector::OneVector;
};

/** 
 * UTickableParentConstraint
 **/

UCLASS(Blueprintable)
class CONSTRAINTS_API UTickableParentConstraint : public UTickableTransformConstraint
{
	GENERATED_BODY()

public:
	UTickableParentConstraint();
	
	/** Returns the transform constraint function that the tick function will evaluate. */
	virtual FConstraintTickFunction::ConstraintFunction GetFunction() const override;

protected:
	/** Cache data structure to store last child local/global transform. */
	struct FDynamicCache
	{
		uint32 CachedInputHash = 0;
	};
	mutable FDynamicCache Cache;
	uint32 CalculateInputHash() const;

	/** Updates the dynamic offset based on external child's transform changes. */
	virtual void OnHandleModified(UTransformableHandle* InHandle, bool bUpdate) override;
	
	/** Computes the child's local transform offset in the parent space. */
	virtual void ComputeOffset() override;

	/** Defines the local child's transform offset in the parent space. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Offset")
	FTransform OffsetTransform = FTransform::Identity;

	/** Defines the local child's dynamic transform offset in the parent space. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Offset")
	FTransform DynamicOffsetTransform = FTransform::Identity;

#if WITH_EDITOR
public:
	// UObject interface
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	// End of UObject interface
#endif
};

/** 
 * UTickableLookAtConstraint
 **/

UCLASS(Blueprintable)
class CONSTRAINTS_API UTickableLookAtConstraint : public UTickableTransformConstraint
{
	GENERATED_BODY()

public:
	UTickableLookAtConstraint();
	
	/** Returns the look at constraint function that the tick function will evaluate. */
	virtual FConstraintTickFunction::ConstraintFunction GetFunction() const override;

protected:
	/** @todo document */
	virtual void ComputeOffset() override;

	/** Defines the aiming axis. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Axis")
	FVector Axis = FVector::XAxisVector;

private:

	/** Computes the shortest quaternion between A and B. */
	static FQuat FindQuatBetweenNormals(const FVector& A, const FVector& B);
};

/** 
 * TransformConstraintUtils
 **/

struct CONSTRAINTS_API FTransformConstraintUtils
{
	/** Fills a sorted constraint array that InChild actor is the child of. */
	static void GetParentConstraints(
		UWorld* World,
		const AActor* InChild,
		TArray< TObjectPtr<UTickableConstraint> >& OutConstraints);
		
	/** Create a handle for the scene component.*/
	static UTransformableComponentHandle* CreateHandleForSceneComponent(
		USceneComponent* InSceneComponent, 
		UObject* Outer);

	/** Creates a new transform constraint based on the InType. */
	static UTickableTransformConstraint* CreateFromType(
		UWorld* InWorld,
		const ETransformConstraintType InType);

	/** Creates respective handles and creates a new InType transform constraint. */	
	static UTickableTransformConstraint* CreateAndAddFromActors(
		UWorld* InWorld,
		AActor* InParent,
		AActor* InChild,
		const ETransformConstraintType InType,
		const bool bMaintainOffset = true);

	/** Registers a new transform constraint within the constraints manager. */	
	static bool AddConstraint(
		UWorld* InWorld,
		UTransformableHandle* InParentHandle,
		UTransformableHandle* InChildHandle,
		UTickableTransformConstraint* Constraint,
		const bool bMaintainOffset = true);

	/** Computes the relative transform between both transform based on the constraint's InType. */
	static FTransform ComputeRelativeTransform(
		const FTransform& InChildLocal,
		const FTransform& InChildWorld,
		const FTransform& InSpaceWorld,
		const ETransformConstraintType InType);
};
