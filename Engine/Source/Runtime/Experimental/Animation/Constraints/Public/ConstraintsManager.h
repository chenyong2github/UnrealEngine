// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Object.h"
#include "Engine/EngineBaseTypes.h"

#include "ConstraintsManager.generated.h"

class UTickableConstraint;
class ULevel;

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

UCLASS(Abstract, Blueprintable)
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

	/** Sets the Active value and enable/disable the tick function. */
	void SetActive(const bool bIsActive);
	
	/** Evaluates the constraint in a context where it's mot done thru the ConstraintTick's tick function. */
	void Evaluate() const;
	
	/** @todo document */
	virtual uint32 GetTargetHash() const PURE_VIRTUAL(GetTargetHash, return 0;);
	/** @todo document */
	virtual bool ReferencesObject(TWeakObjectPtr<UObject> InObject) const PURE_VIRTUAL(ReferencesObject, return false;);

#if WITH_EDITOR
	/** Returns the constraint's label used for UI. */
	virtual FString GetLabel() const;
	virtual FString GetFullLabel() const;

	/** Returns the constraint's type label used for UI. */
	virtual FString GetTypeLabel() const;

	// UObject interface
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	// End of UObject interface
#endif
	/** @todo documentation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, DisplayName="Active State", Category="Constraint")
	bool Active = true;
};


/** 
 * UConstraintsManager
 * This object gathers the different constraints of the level and is held by the ConstraintsActor (unique in the level)
 **/

UCLASS(BLUEPRINTABLE)
class CONSTRAINTS_API UConstraintsManager : public UObject
{
	GENERATED_BODY()
public:

	using ConstraintPtr = TObjectPtr<UTickableConstraint>;

	/** Dynamic blueprintable delegates for knowing when a constraints are added or deleted*/
	DECLARE_DYNAMIC_MULTICAST_SPARSE_DELEGATE_TwoParams(FOnConstraintAdded, UConstraintsManager, OnConstraintAdded_BP, UConstraintsManager*, Mananger, UTickableConstraint*, Constraint);
	DECLARE_DYNAMIC_MULTICAST_SPARSE_DELEGATE_TwoParams(FOnConstraintRemoved, UConstraintsManager, OnConstraintRemoved_BP, UConstraintsManager*, Mananger, UTickableConstraint*, Constraint);

	
	UConstraintsManager();
	virtual ~UConstraintsManager();
	
	//UObjects
	virtual void PostLoad() override;
	/** Get the existing Constraints Manager if existing or create a new one. */
	static UConstraintsManager* Get(UWorld* InWorld);

	/** Find the existing Constraints Manager. */
	static UConstraintsManager* Find(const UWorld* InWorld);

	/** @todo document */
	void Init(UWorld* InWorld);
	
	/* Set tick dependencies between two constraints. */
	void SetConstraintDependencies(
		FConstraintTickFunction* InFunctionToTickBefore,
		FConstraintTickFunction* InFunctionToTickAfter);

	/** @todo document */
	void Clear(UWorld* World);


	/** BP Delegate fired when constraints are added*/
	UPROPERTY(BlueprintAssignable, Category = Constraints, meta = (DisplayName = "OnConstraintAdded"))
	FOnConstraintAdded OnConstraintAdded_BP;
	
	/** BP Delegate fired when constraints are removed*/
	UPROPERTY(BlueprintAssignable, Category = Constraints, meta = (DisplayName = "OnConstraintRemoved"))
	FOnConstraintAdded OnConstraintRemoved_BP;

private:

	/** @todo document */
	FDelegateHandle OnActorDestroyedHandle;
	
	void OnActorDestroyed(AActor* InActor);

	void RegisterDelegates(UWorld* World);
	void UnregisterDelegates(UWorld* World);
	
	/** @todo document */
	void Dump() const;

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
		return NewConstraint;
	}

	/** @todo document */
	bool AddConstraint(UTickableConstraint* InConstraint) const;
	
	/** Get the index of the given constraint's name. */
	int32 GetConstraintIndex(const FName& InConstraintName) const;
	
	/** Remove the constraint by name. */
	bool RemoveConstraint(const FName& InConstraintName) const;

	/** Remove the constraint at the given index. */
	bool RemoveConstraint(const int32 InConstraintIndex) const;

	/** @todo document */
	UTickableConstraint* GetConstraint(const FName& InConstraintName) const;

	/** @todo document */
	UTickableConstraint* GetConstraint(const int32 InConstraintIndex) const;
	
	/** Get read-only access to the array of constraints. */
	const TArray< TObjectPtr<UTickableConstraint> >& GetConstraintsArray() const;

	/** Get parent constraints of the specified child. If bSorted is true, then the constraints will be sorted by dependency. */
	TArray< TObjectPtr<UTickableConstraint> > GetParentConstraints(
		const uint32 InTargetHash,
		const bool bSorted = false) const;

	/** Set dependencies between two constraints. */
	void SetConstraintsDependencies(
		const FName& InNameToTickBefore,
		const FName& InNameToTickAfter) const;
	
private:
	/** Delegeate that's fired when a scene component is constrained, this is needed to make sure things like gizmo's get updated after the constraint tick happens*/
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnSceneComponentConstrained, USceneComponent* /*InSceneComponent*/);
	FOnSceneComponentConstrained SceneComponentConstrained;

	/** @todo document */
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnConstraintRemoved, FName /*InConstraintName*/);
	FOnConstraintRemoved ConstraintRemoved;
	
	/** Find the existing Constraints Manager in World or create a new one. */
	UConstraintsManager* GetManager() const;
	
	/** Find the existing Constraints Manager in World. */
	UConstraintsManager* FindManager() const;

	/** Destroy the ConstraintsManager from the World. */
	void DestroyManager() const;

	/** The World that holds the ConstraintsManagerActor. */
	UWorld* World = nullptr;

public:
	/** Delegate that's fired when a scene component is constrained, this is needed to make sure things like gizmo's get updated after the constraint tick happens*/
	FOnSceneComponentConstrained& OnSceneComponentConstrained() { return SceneComponentConstrained; }
	/** @todo document */
	FOnConstraintRemoved& OnConstraintRemoved(){ return ConstraintRemoved; };
};