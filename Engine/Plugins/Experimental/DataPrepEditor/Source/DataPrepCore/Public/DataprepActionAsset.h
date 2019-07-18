// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Delegates/IDelegateInstance.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"

#include "DataprepActionAsset.generated.h"

// Forward Declarations
class UDataprepFetcher;
class UDataprepFilter;
class UDataprepOperation;

template <class T>
class TSubclassOf;

UCLASS(Experimental)
class UDataprepActionStep : public UObject
{
	GENERATED_BODY()

public:
	// The operation will only be not null if the step is a operation
	UPROPERTY()
	UDataprepOperation* Operation;

	// The Filter will only be not null if the step is a Filter/Selector
	UPROPERTY()
	UDataprepFilter* Filter;

	UPROPERTY()
	bool bIsEnabled;
};

// Delegates
DECLARE_MULTICAST_DELEGATE(FOnStepsOrderChanged)

UCLASS(Experimental)
class DATAPREPCORE_API UDataprepActionAsset : public UObject
{
	GENERATED_BODY()

public:

	UDataprepActionAsset();

	virtual ~UDataprepActionAsset();

	/**
	 * Execute the action
	 * @param Objects The objects on which the action will operate
	 */
	UFUNCTION(BlueprintCallable, Category = "Execution")
	void Execute(const TArray<UObject*>& InObjects);

	/**
	 * Add an operation to the action
	 * @param OperationClass The class of the operation
	 * @return The index of the added operation or index none if the class is 
	 */
	int32 AddOperation(const TSubclassOf<UDataprepOperation>& OperationClass);

	/**
	 * Add a filter and setup it's fetcher
	 * @param FilterClass The type of filter we want
	 * @param FetcherClass The type of fetcher that we want. 
	 * @return The index of the added filter or index none if the classes are incompatible or invalid
	 * Note that fetcher most be compatible with the filter
	 */
	int32 AddFilterWithAFetcher(const TSubclassOf<UDataprepFilter>& FilterClass, const TSubclassOf<UDataprepFetcher>& FetcherClass);

	/**
	 * Add a copy of the step to the action
	 * @param ActionStep The step we want to duplicate in the action
	 * @return The index of the added step or index none if the step is invalid
	 */
	int32 AddStep(const UDataprepActionStep* ActionStep);

	/**
	 * Access to a step of the action
	 * @param Index the index of the desired step
	 * @return A pointer to the step if it exist, otherwise nullptr
	 */
	TWeakObjectPtr<UDataprepActionStep> GetStep(int32 Index);

	/**
	 * Access to a step of the action
	 * @param Index the index of the desired step
	 * @return A const pointer to the operation if it exist, otherwise nullptr
	 */
	const TWeakObjectPtr<UDataprepActionStep> GetStep(int32 Index) const;

	/**
	 * Get the number of steps of this action 
	 * @return The number of steps
	 */
	int32 GetStepsCount() const;

	/**
	 * Get enabled status of an operation
	 * @param Index The index of the operation
	 * @return True if the operation is enabled. Always return false if the operation index is invalid
	 */
	bool IsStepEnabled(int32 Index) const;

	/**
	 * Allow to set the enabled state of a step
	 * @param Index The index of the step
	 * @param bEnable The new enabled state of the step
	 */
	void EnableStep(int32 Index, bool bEnable);

	/**
	 * Move a step to another spot in the order of steps
	 * This operation take O(n) time. Where n is the absolute value of StepIndex - DestinationIndex
	 * @param StepIndex The Index of the step to move
	 * @param DestinationIndex The index of where the step will be move to
	 * @return True if the step was move
	 */
	bool MoveStep(int32 StepIndex, int32 DestinationIndex);

	/**
	 * Remove a step from the action
	 * @param Index The index of the step to remove
	 * @return True if a step was removed
	 */
	bool RemoveStep(int32 Index);

	/**
	 * Allow an observer to be notified when the steps order changed that also include adding and removing steps
	 * @return The delegate that will be broadcasted when the steps order changed
	 */
	FOnStepsOrderChanged& GetOnStepsOrderChanged();

private:

	void OnClassesRemoved(const TArray<UClass*>& DeletedClasses);

	void RemoveInvalidOperations();

	/** Array of operations and/or filters constituting this action */
	UPROPERTY()
	TArray<UDataprepActionStep*> Steps;

	/**
	 * Array of objects the action is operating on.
	 * @remark: Temporary solution to the eventuality that an operation delete on of the UObject referred to 
	 * This array can be modified by UDataprepFilter object(s) within this action
	 */
	UPROPERTY(Transient)
	TArray<UObject*> CurrentlySelectedObjects;

	FOnStepsOrderChanged OnStepsChanged;

	FDelegateHandle OnAssetDeletedHandle;
};
