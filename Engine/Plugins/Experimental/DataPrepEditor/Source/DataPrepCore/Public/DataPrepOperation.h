// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"
#include "UObject/TextProperty.h"

#include "DataprepOperationContext.h"

#include "DataPrepOperation.generated.h"


class DATAPREPCORE_API FDataprepOperationCategories
{
public:
	static FText ActorOperation;
	static FText MeshOperation;
	static FText ObjectOperation;
};

/** Experimental struct. Todo add stuct wide comment */
USTRUCT(BlueprintType)
struct FDataprepContext
{
	GENERATED_BODY()

	/**
	 * This is the objects on which an operation can operate
	 */
	UPROPERTY( BlueprintReadOnly, Category = "Dataprep")
	TArray<UObject*> Objects;
};

// Todo add class wide comment
UCLASS(Experimental, Abstract, Blueprintable)
class DATAPREPCORE_API UDataprepOperation : public UObject
{
	GENERATED_BODY()

	// User friendly interface start here ======================================================================
public:

	/**
	 * Execute the operation
	 * @param InObjects The objects that the operation will operate on
	 */
	UFUNCTION(BlueprintCallable, Category = "Execution")
	void Execute(const TArray<UObject*>& InObjects);

protected:
	
	/**
	 * This function is called when the operation is executed.
	 * If your defining your operation in Blueprint or Python this is the function to override.
	 * @param InContext The context contains the data that the operation should operate on.
	 */
	UFUNCTION(BlueprintNativeEvent)
	void OnExecution(const FDataprepContext& InContext);

	/**
	 * This function is the same has OnExcution, but it's the extension point for an operation defined in c++.
	 * It will be called on the operation execution.
	 * @param InContext The context contains the data that the operation should operate on
	 */
	virtual void OnExecution_Implementation(const FDataprepContext& InContext);

	/**
	 * Add an info to the log
	 * @param InLogText The text to add to the log
	 */
	UFUNCTION(BlueprintCallable, Category = "Log", meta = (HideSelfPin = "true"))
	void LogInfo(const FText& InLogText);

	/**
	 * Add a warning to the log
	 * @param InLogText The text to add to the log
	 */
	UFUNCTION(BlueprintCallable,  Category = "Log", meta = (HideSelfPin = "true"))
	void LogWarning(const FText& InLogText);

	/**
	 * Add Error to the log
	 * @param InLogText The text to add to the log
	 */
	UFUNCTION(BlueprintCallable,  Category = "Log", meta = (HideSelfPin = "true"))
	void LogError(const FText& InLogError);

	// User friendly interface end here ========================================================================

public:
	/**
	 * Prepare the operation for the execution and execute it.
	 * This allow the operation to report information such as log to the operation context
	 * @param InOperationContext This contains the data necessary for the setup of the operation and also the DataprepContext
	 */
	void ExecuteOperation(TSharedRef<FDataprepOperationContext>& InOperationContext);

	/** 
	* Allows to change the name of the fetcher for the ui if needed.
	*/
	UFUNCTION(BlueprintNativeEvent,  Category = "Display")
	FText GetDisplayOperationName() const;

	/**
	* Allows to change the tooltip of the fetcher for the ui if needed.
	*/
	UFUNCTION(BlueprintNativeEvent,  Category = "Display")
	FText GetTooltip() const;

	/**
	* Allows to change the tooltip of the fetcher for the ui if needed.
	*/
	UFUNCTION(BlueprintNativeEvent,  Category = "Display")
	FText GetCategory() const;

	/**
	* Allows to add more keywords for when a user is searching for the fetcher in the ui.
	*/
	UFUNCTION(BlueprintNativeEvent,  Category = "Display|Search")
	FText GetAdditionalKeyword() const;

	virtual FText GetDisplayOperationName_Implementation() const;
	virtual FText GetTooltip_Implementation() const;
	virtual FText GetCategory_Implementation() const;
	virtual FText GetAdditionalKeyword_Implementation() const;

	// Everything below is only for the Dataprep systems internal use =========================================
private:
	TSharedPtr<const FDataprepOperationContext> OperationContext;
};


// Todo add class wide comment
UCLASS(Experimental, Abstract, Blueprintable)
class DATAPREPCORE_API UDataprepAdditiveOperation : public UDataprepOperation
{
	GENERATED_BODY()

public:
	const TArray< UObject* > GetCreatedAssets() { return CreatedAssets; }

protected:
	TArray< UObject* > CreatedAssets;
};