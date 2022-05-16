// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Constraint.h"
#include "ConstraintsManager.h"

#include "TransformConstraint.generated.h"

class UTransformableHandle;

using SetTransformFunc = TFunction<void(const FTransform&)>;
using GetTransformFunc = TFunction<FTransform()>;

/** 
 * UTickableTransformConstraint
 **/

UCLASS(Abstract)
class CONSTRAINTS_API UTickableTransformConstraint : public UTickableConstraint
{
	GENERATED_BODY()

public:

	/** @todo document */
	void Setup();
	/** @todo document */
	virtual void PostLoad() override;
	/** @todo document */
	virtual void PostDuplicate(bool bDuplicateForPIE) override;

	/** @todo document */
	virtual uint32 GetTargetHash() const override;
	
	/** @todo document */
	UPROPERTY()
	TObjectPtr<UTransformableHandle> ParentTRSHandle;
	/** @todo document */
	UPROPERTY()
	TObjectPtr<UTransformableHandle> ChildTRSHandle;

	/** @todo document */
	UPROPERTY()
	bool bMaintainOffset = true;

	/** @todo document */
	UPROPERTY(meta = (Input, ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float Weight = 1.f;
	
	/** @todo document. */
	int64 GetType() const;
		
protected:
	/** @todo document */
	virtual void ComputeOffset() PURE_VIRTUAL(ComputeOffset, return;);
	
	/** @todo document */
	void SetupDependencies();
	
	/** @todo document */
	void SetChildGlobalTransform(const FTransform& InGlobal) const;

	/** @todo document */
	FTransform GetChildGlobalTransform() const;

	/** @todo document */
	FTransform GetParentGlobalTransform() const;

	/** @todo document */
	UPROPERTY()
	ETransformConstraintType Type = ETransformConstraintType::Parent;

#if WITH_EDITOR
public:
	/** @todo document */
	virtual FName GetLabel() const override;
#endif
};

/** 
 * UTickableTranslationConstraint
 **/

UCLASS()
class CONSTRAINTS_API UTickableTranslationConstraint : public UTickableTransformConstraint
{
	GENERATED_BODY()

public:
	UTickableTranslationConstraint();

	/** @todo document */
	virtual FConstraintTickFunction::ConstraintFunction GetFunction() const override;

protected:
	/** @todo document */
	virtual void ComputeOffset() override;

	/** @todo document */
	UPROPERTY()
	FVector OffsetTranslation = FVector::ZeroVector;
};

/** 
 * UTickableRotationConstraint
 **/

UCLASS()
class CONSTRAINTS_API UTickableRotationConstraint : public UTickableTransformConstraint
{
	GENERATED_BODY()

public:
	UTickableRotationConstraint();

	/** @todo document */
	virtual FConstraintTickFunction::ConstraintFunction GetFunction() const override;

protected:
	/** @todo document */
	virtual void ComputeOffset() override;
	
	/** @todo document */
	UPROPERTY()
	FQuat OffsetRotation = FQuat::Identity;
};

/** 
 * UTickableScaleConstraint
 **/

UCLASS()
class CONSTRAINTS_API UTickableScaleConstraint : public UTickableTransformConstraint
{
	GENERATED_BODY()

public:
	UTickableScaleConstraint();
	
	/** @todo document */
	virtual FConstraintTickFunction::ConstraintFunction GetFunction() const override;

protected:
	/** @todo document */
	virtual void ComputeOffset() override;

	/** @todo document */
	UPROPERTY()
	FVector OffsetScale = FVector::OneVector;
};

/** 
 * UTickableParentConstraint
 **/

UCLASS()
class CONSTRAINTS_API UTickableParentConstraint : public UTickableTransformConstraint
{
	GENERATED_BODY()

public:
	UTickableParentConstraint();
	
	/** @todo document */
	virtual FConstraintTickFunction::ConstraintFunction GetFunction() const override;

protected:
	/** @todo document */
	virtual void ComputeOffset() override;

	/** @todo document */
	UPROPERTY()
	FTransform OffsetTransform = FTransform::Identity;
};

/** 
 * UTickableLookAtConstraint
 **/

UCLASS()
class CONSTRAINTS_API UTickableLookAtConstraint : public UTickableTransformConstraint
{
	GENERATED_BODY()

public:
	UTickableLookAtConstraint();
	
	/** @todo document */
	virtual FConstraintTickFunction::ConstraintFunction GetFunction() const override;

protected:
	/** @todo document */
	virtual void ComputeOffset() override;

	/** @todo document */
	UPROPERTY()
	FTransform OffsetTransform = FTransform::Identity;
};

/** 
 * TransformConstraintUtils
 **/

struct CONSTRAINTS_API TransformConstraintUtils
{
	/** @todo document */
	static bool Create(
		UWorld* World,
		const AActor* InParent,
		const AActor* InChild,
		const ETransformConstraintType InType = ETransformConstraintType::Parent,
		const bool bMaintainOffset = true);

	/** @todo document */
	static void GetParentConstraints(
		UWorld* World,
		const AActor* InChild,
		TArray< TObjectPtr<UTickableConstraint> >& OutConstraints);
	
private:
	
	/** Registers a new transform constraint within the constraints manager. */	
	static bool AddConst(
		UWorld* InWorld,
		UTransformableHandle* InParentHandle,
		UTransformableHandle* InChildHandle,
		const ETransformConstraintType InType,
		const bool bMaintainOffset = true);
};
