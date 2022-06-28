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

	/** @todo document */
	void Setup();
	/** @todo document */
	virtual void PostLoad() override;
	/** @todo document */
	virtual void PostDuplicate(bool bDuplicateForPIE) override;

	/** @todo document */
	virtual uint32 GetTargetHash() const override;
	/** @todo document */
	virtual bool ReferencesObject(TWeakObjectPtr<UObject> InObject) const override;
	
	/** @todo document */
	UPROPERTY(BlueprintReadWrite, Category = "Handle")
	TObjectPtr<UTransformableHandle> ParentTRSHandle;
	/** @todo document */
	UPROPERTY(BlueprintReadWrite, Category = "Handle")
	TObjectPtr<UTransformableHandle> ChildTRSHandle;

	/** @todo document */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Offset")
	bool bMaintainOffset = true;

	/** @todo document */
	// UPROPERTY(EditAnywhere, BlueprintReadWrite,Category="Weight", meta = (Input, ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	//@benoit when not EditAnywhere?
	UPROPERTY(meta = (Input, ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float Weight = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Offset")
	bool bDynamicOffset = false;

	/** @todo document. */
	int64 GetType() const;

	/** @todo document */
	FTransform GetChildGlobalTransform() const;
	/** @todo document */
	FTransform GetChildLocalTransform() const;
		
protected:
	/** @todo document */
	virtual void ComputeOffset() PURE_VIRTUAL(ComputeOffset, return;);
	
	/** @todo document */
	void SetupDependencies();
	
	/** @todo document */
	void SetChildGlobalTransform(const FTransform& InGlobal) const;
	/** @todo document */
	void SetChildLocalTransform(const FTransform& InLocal) const;

	/** @todo document */
	FTransform GetParentGlobalTransform() const;
	/** @todo document */
	FTransform GetParentLocalTransform() const;

	/** @todo document */
	UPROPERTY()
	ETransformConstraintType Type = ETransformConstraintType::Parent;

#if WITH_EDITOR
public:
	/** @todo document */
	virtual FName GetLabel() const override;

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

	/** @todo document */
	virtual FConstraintTickFunction::ConstraintFunction GetFunction() const override;

protected:
	/** @todo document */
	virtual void ComputeOffset() override;

	/** @todo document */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Offset")
	FVector OffsetTranslation = FVector::ZeroVector;
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

	/** @todo document */
	virtual FConstraintTickFunction::ConstraintFunction GetFunction() const override;

protected:
	/** @todo document */
	virtual void ComputeOffset() override;
	
	/** @todo document */
	UPROPERTY(EditAnywhere, BlueprintReadWrite,Category="Offset")
	FQuat OffsetRotation = FQuat::Identity;
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
	
	/** @todo document */
	virtual FConstraintTickFunction::ConstraintFunction GetFunction() const override;

protected:
	/** @todo document */
	virtual void ComputeOffset() override;

	/** @todo document */
	UPROPERTY(EditAnywhere, BlueprintReadWrite,Category="Offset")
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
	
	/** @todo document */
	virtual FConstraintTickFunction::ConstraintFunction GetFunction() const override;

protected:
	/** @todo document */
	virtual void ComputeOffset() override;

	/** @todo document */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Offset")
	FTransform OffsetTransform = FTransform::Identity;

	/** @todo document */
	struct FDynamicCache
	{
		FTransform LastGlobalSet = FTransform::Identity;
		FTransform LastLocalSet = FTransform::Identity;
		uint32 CachedInputHash = 0;
	};
	mutable FDynamicCache Cache;

	/** @todo document */
	uint32 CalculateDependenciesHash() const;
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
	
	/** @todo document */
	virtual FConstraintTickFunction::ConstraintFunction GetFunction() const override;

protected:
	/** @todo document */
	virtual void ComputeOffset() override;

	/** @todo document */
	UPROPERTY(EditAnywhere, BlueprintReadWrite,Category="Offset")
	FTransform OffsetTransform = FTransform::Identity;
};

/** 
 * TransformConstraintUtils
 **/

struct CONSTRAINTS_API FTransformConstraintUtils
{

	/** @todo document */
	static void GetParentConstraints(
		UWorld* World,
		const AActor* InChild,
		TArray< TObjectPtr<UTickableConstraint> >& OutConstraints);
		
	/** Create a handle for the scene component.*/
	static UTransformableComponentHandle* CreateHandleForSceneComponent(
		USceneComponent* InSceneComponent, 
		UObject* Outer);

	/** @todo document */
	static UTickableTransformConstraint* CreateFromType(
		UWorld* InWorld,
		const ETransformConstraintType InType);

	/** @todo document */
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
};
