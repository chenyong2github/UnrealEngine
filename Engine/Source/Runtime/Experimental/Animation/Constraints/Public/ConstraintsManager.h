// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Object.h"

#include "ConstraintsManager.generated.h"

class UTickableConstraint;

/** 
 * FConstraintTickFunction
 * Represents the interface of constraint as a tick function. This allows both to evaluate a constraint in the
 * UE ticking system but also to handle dependencies between parents/children and constraints between themselves
 * using the tick prerequisites system.
 **/

USTRUCT()
struct CONSTRAINTS_API FConstraintTickFunction : public FTickFunction
{
	GENERATED_USTRUCT_BODY()
public:
	FConstraintTickFunction();
	~FConstraintTickFunction();

	/* Begin FTickFunction Interface */
	virtual void ExecuteTick(
		float DeltaTime,
		ELevelTick TickType,
		ENamedThreads::Type CurrentThread,
		const FGraphEventRef& MyCompletionGraphEvent) override;
	
	virtual FString DiagnosticMessage() override;
	/* End FTickFunction Interface */

	/** Callable function that represents the actual constraint. **/
	using ConstraintFunction = TFunction<void()>;
	
	/** Register a callable function. **/
	void RegisterFunction(ConstraintFunction InConstraint);
	
	/** Register a callable function. **/
	void EvaluateFunctions() const;

	/** Weak ptr to the Constraint holding this tick function. **/
	TWeakObjectPtr<UTickableConstraint> Constraint;
	
	/** The list of the constraint functions that will be called within the tick function. **/
	TArray<ConstraintFunction> ConstraintFunctions;
};

template<>
struct TStructOpsTypeTraits<FConstraintTickFunction> : public TStructOpsTypeTraitsBase2<FConstraintTickFunction>
{
	enum
	{
		WithCopy = false
	};
};

/** 
 * UTickableConstraint
 * Represents the basic interface of constraint within the constraints manager.
 **/

UCLASS(Abstract)
class CONSTRAINTS_API UTickableConstraint : public UObject
{
	GENERATED_BODY()
	
public:
	UTickableConstraint() {}
	virtual ~UTickableConstraint() {}

	/** Returns the actual function that the tick function needs to evaluate. */
	virtual FConstraintTickFunction::ConstraintFunction GetFunction() const PURE_VIRTUAL(GetFunction, return {};);

	/** Tick function that will be registered and evaluated. */
	UPROPERTY()
	FConstraintTickFunction ConstraintTick;

	/** @todo document */
	virtual uint32 GetTargetHash() const PURE_VIRTUAL(GetTargetHash, return 0;);
	
#if WITH_EDITOR
	/** @todo document */
	virtual FName GetLabel() const;
#endif
};

/** 
 * UConstraintsManager
 * This object gathers the different constraints of the level and is held by the ConstraintsActor (unique in the level)
 **/

UCLASS()
class CONSTRAINTS_API UConstraintsManager : public UObject
{
	GENERATED_BODY()
public:

	using ConstraintPtr = TObjectPtr<UTickableConstraint>;
	
	UConstraintsManager();
	virtual ~UConstraintsManager();
	
	/** Get the existing Constraints Manager if existing or create a new one. */
	static UConstraintsManager* Get(UWorld* InWorld);

	/** Find the existing Constraints Manager. */
	static UConstraintsManager* Find(const UWorld* InWorld);

	/** @todo document */
	void Init(ULevel* InLevel);
	
	/* Set tick dependencies between two constraints. */
	void SetConstraintDependencies(
		FConstraintTickFunction* InFunctionToTickBefore,
		FConstraintTickFunction* InFunctionToTickAfter);

	/** @todo document */
	void Clear();
	
private:

	/** @todo document */
	void Dump() const;

	/** @todo document */
	UPROPERTY()
	TObjectPtr<ULevel> Level = nullptr;

	/** @todo document */
	UPROPERTY()
	TArray< TObjectPtr<UTickableConstraint> > Constraints;
	
	friend class FConstraintsManagerController;
	friend class AConstraintsActor;
};

/** 
 * FConstraintsManagerController
 * Basic controller to add / remove / get constraints
 **/

class CONSTRAINTS_API FConstraintsManagerController
{
public:
	/** @todo document */
	static FConstraintsManagerController& Get(UWorld* InWorld);

	/** @todo document */
	template< typename TConstraint >
	TConstraint* AllocateConstraintT(const FName& InBaseName) const
	{
		UConstraintsManager* Manager = GetManager();
		if (!Manager)
		{
			return nullptr;
		}

		// unique name (we may want to use another approach here to manage uniqueness)
		const FName Name = MakeUniqueObjectName(Manager, TConstraint::StaticClass(), InBaseName);

		TConstraint* NewConstraint = NewObject<TConstraint>(Manager, Name);
		AddConstraint(NewConstraint);
		
		return NewConstraint;
	}

	/** @todo document */
	void AddConstraint(UTickableConstraint* InConstraint) const;
	
	/** Get the index of the given constraint's name. */
	int32 GetConstraintIndex(const FName& InConstraintName) const;
	
	/** Remove the constraint by name. */
	void RemoveConstraint(const FName& InConstraintName) const;

	/** Remove the constraint at the given index. */
	void RemoveConstraint(const int32 InConstraintIndex) const;

	/** Get read-only access to the array of constraints. */
	const TArray< TObjectPtr<UTickableConstraint> >& GetConstrainsArray() const;
	
	/** Get parent constraints of the specified child. If bSorted is true, then the constraints will be sorted by dependency. */
	TArray< TObjectPtr<UTickableConstraint> > GetParentConstraints(
		const uint32 InTargetHash,
		const bool bSorted = false) const;

	/** Set dependencies between two constraints. */
	void SetConstraintsDependencies(
		const FName& InNameToTickBefore,
		const FName& InNameToTickAfter) const;
	
private:
	
	/** Find the existing Constraints Manager in World or create a new one. */
	UConstraintsManager* GetManager() const;
	
	/** Find the existing Constraints Manager in World. */
	UConstraintsManager* FindManager() const;

	/** Destroy the ConstraintsManager from the World. */
	void DestroyManager() const;

	/** The World that holds the ConstraintsManagerActor. */
	UWorld* World = nullptr;
};