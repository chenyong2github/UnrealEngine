// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "DataprepActionAsset.h"
#include "DataprepAssetInterface.h"
#include "DataprepAssetProducers.h"

#include "DataprepAsset.generated.h"

#ifndef NO_BLUEPRINT
class UBlueprint;
#endif
class UDataprepActionAsset;
class UDataprepActionStep;
class UDataprepParameterizableObject;
class UDataprepParameterization;
class UDataprepProducers;
class UEdGraphNode;

struct FDataprepActionContext;
struct FDataprepConsumerContext;
struct FDataprepProducerContext;

/**
 * A DataprepAsset is an implementation of the DataprepAssetInterface using
 * a Blueprint as the recipe pipeline. The Blueprint is composed of DataprepAction
 * nodes linearly connected.
 */
UCLASS(Experimental, BlueprintType)
class DATAPREPCORE_API UDataprepAsset : public UDataprepAssetInterface
{
	GENERATED_BODY()

public:
	UDataprepAsset();

	virtual ~UDataprepAsset() = default;

	// UObject interface
	virtual void PostLoad() override;
	virtual bool Rename(const TCHAR* NewName/* =nullptr */, UObject* NewOuter/* =nullptr */, ERenameFlags Flags/* =REN_None */) override;
	virtual void PostEditUndo() override;
	// End of UObject interface

	// UDataprepAssetInterface interface
	virtual void ExecuteRecipe( const TSharedPtr<FDataprepActionContext>& InActionsContext ) override;
	virtual bool HasActions() const override { return ActionAssets.Num() > 0; }
	virtual const TArray<UDataprepActionAsset*>& GetActions() const override { return ActionAssets; }
private:
	virtual TArray<UDataprepActionAsset*> GetCopyOfActions(TMap<UObject*,UObject*>& OutOriginalToCopy) const override;
	// End of UDataprepAssetInterface interface

public:
	int32 GetActionCount() const { return ActionAssets.Num(); }

	UDataprepActionAsset* GetAction(int32 Index)
	{
		return const_cast<UDataprepActionAsset*>( static_cast<const UDataprepAsset*>(this)->GetAction(Index) );
	}

	const UDataprepActionAsset* GetAction(int32 Index) const;

	int32 GetActionIndex(UDataprepActionAsset* ActionAsset) const
	{
		return ActionAssets.Find(ActionAsset);
	}

	/**
	 * Add a copy of the action to the Dataprep asset
	 * @param Action The action we want to duplicate in the Dataprep asset. Parameter can be null
	 * @return The index of the added action or index none if the action is invalid
	 * @remark If action is nullptr, a new DataprepActionAsset is simply created
	 */
	int32 AddAction(const UDataprepActionAsset* Action);

	/**
	 * Add the actions to the Dataprep asset
	 * @param Actions The array of action to add
	 * @param bCreateOne Indicates if one or more action assets should be created. By default one is created
	 * @return The index of the last added action or index none if the action is invalid
	 */
	int32 AddActions(const TArray<const UDataprepActionAsset*>& Actions);

	/**
	 * Creates an action from the array of action steps or one action per action steps
	 * then add the action(s) to the Dataprep asset
	 * @param ActionSteps The array of action steps to process
	 * @param bCreateOne Indicates if one or more action assets should be created. By default one is created
	 * @return The index of the last added action or index none if the action is invalid
	 */
	int32 AddActions(const TArray<const UDataprepActionStep*>& ActionSteps, bool bCreateOne = true);

	/**
	 * Insert a copy of the action to the Dataprep asset at the requested index
	 * @param Action The action we want to duplicate in the Dataprep asset
	 * @param Index The index at which the insertion must happen
	 * @return True if the insertion is successful, false if the action or the index are invalid
	 */
	bool InsertAction(const UDataprepActionAsset* InAction, int32 Index);

	/**
	 * Insert a copy of each action into the Dataprep asset at the requested index
	 * @param Actions The array of actions to insert
	 * @param Index The index at which the insertion must happen
	 * @return True if the insertion is successful, false if the actions or the index are invalid
	 */
	bool InsertActions(const TArray<const UDataprepActionAsset*>& InActions, int32 Index);

	/**
	 * Creates an action from the array of action steps or one action per action steps
	 * then insert the action(s) to the Dataprep asset at the requested index
	 * @param ActionSteps The array of action steps to process
	 * @param Index The index at which the insertion must happen
	 * @return True if the insertion is successful, false if the action steps or the index are invalid
	 */
	bool InsertActions(const TArray<const UDataprepActionStep*>& InActionSteps, int32 Index, bool bCreateOne = true);

	/**
	 * Move an action to another spot in the order of actions
	 * This operation take O(n) time. Where n is the absolute value of SourceIndex - DestinationIndex
	 * @param SourceIndex The Index of the action to move
	 * @param DestinationIndex The index of where the action will be move to
	 * @return True if the action was move
	*/
	bool MoveAction(int32 SourceIndex, int32 DestinationIndex);

	/**
	 * Remove an action from the Dataprep asset
	 * @param Index The index of the action to remove
	 * @return True if the action was removed
	 */
	bool RemoveAction(int32 Index);

	/**
	 * Remove a set of actions from the Dataprep asset
	 * @param Index The index of the action to remove
	 * @return True if the action was removed
	 */
	bool RemoveActions(const TArray<int32>& Indices);

	/**
	 * Allow an observer to be notified of an change in the pipeline
	 * return The event that will be broadcasted when a object has receive a modification that might change the result of the pipeline
	 */
	DECLARE_EVENT_TwoParams(UDataprepAsset, FOnDataprepActionAssetChange, UObject* /*The object that was modified*/, FDataprepAssetChangeType)
	FOnDataprepActionAssetChange& GetOnActionChanged() { return OnActionChanged; }

	bool CreateParameterization();

#ifndef NO_BLUEPRINT
	// Temp code for the nodes development
	bool CreateBlueprint();

	/** @return pointer on the recipe */
	const UBlueprint* GetRecipeBP() const
	{ 
		return DataprepRecipeBP;
	}

	UBlueprint* GetRecipeBP()
	{ 
		return DataprepRecipeBP;
	}

	//Todo Change the signature of this function when the new graph is ready (Hack to avoid a refactoring)
	UDataprepActionAsset* AddActionUsingBP(class UEdGraphNode* NewActionNode);

	void SwapActionsUsingBP(int32 FirstActionIndex, int32 SecondActionIndex);

	void RemoveActionUsingBP(int32 Index);

	// end of temp code for nodes development
#endif

public:
	// Functions specific to the parametrization of the Dataprep asset

	/**
	 * Event to notify the ui that a dataprep parametrization was modified
	 * This necessary as the ui for the parameterization is only updated by manual event (the ui don't pull new values each frame)
	 * Note on the objects param: The parameterized objects that should refresh their ui. If nullptr all widgets that can display some info on the parameterization should be refreshed
	 */
	DECLARE_EVENT_OneParam(UDataprepParameterization, FDataprepParameterizationStatusForObjectsChanged, const TSet<UObject*>* /** Objects */)
	FDataprepParameterizationStatusForObjectsChanged OnParameterizedObjectsStatusChanged;

	// Internal Use only (the current implementation might be subject to change)
	virtual UObject* GetParameterizationObject() override;

	void BindObjectPropertyToParameterization(UDataprepParameterizableObject* Object, const TArray<struct FDataprepPropertyLink>& InPropertyChain,const FName& Name);

	bool IsObjectPropertyBinded(UDataprepParameterizableObject* Object, const TArray<struct FDataprepPropertyLink>& InPropertyChain) const;

	FName GetNameOfParameterForObjectProperty(UDataprepParameterizableObject* Object, const TArray<struct FDataprepPropertyLink>& InPropertyChain) const;

	void RemoveObjectPropertyFromParameterization(UDataprepParameterizableObject* Object, const TArray<struct FDataprepPropertyLink>& InPropertyChain);

	void GetExistingParameterNamesForType(FProperty* Property, bool bIsDescribingFullProperty, TSet<FString>& OutValidExistingNames, TSet<FString>& OutInvalidNames) const;

	// Internal only for now
	UDataprepParameterization* GetDataprepParameterization() { return Parameterization; }

protected:
#if WITH_EDITORONLY_DATA
	// Temp code for the nodes development
	/** Temporary: Pointer to data preparation pipeline blueprint used to process input data */
	UPROPERTY()
	UBlueprint* DataprepRecipeBP;
	// end of temp code for nodes development

	/** DEPRECATED: List of producers referenced by the asset */
	UPROPERTY()
	TArray< FDataprepAssetProducer > Producers_DEPRECATED;

	/** DEPRECATED: COnsumer referenced by the asset */
	UPROPERTY()
	UDataprepContentConsumer* Consumer_DEPRECATED;
#endif

	// Temp code for the nodes development
	void OnDataprepBlueprintChanged( class UBlueprint* InBlueprint );
	// end of temp code for nodes development

	// Temp code for the nodes development
private:
	void UpdateActions(bool bNotify = true);

private:
	UPROPERTY()
	UEdGraphNode* StartNode;

	UPROPERTY()
	UDataprepParameterization* Parameterization;

	UPROPERTY()
	TArray<UDataprepActionAsset*> ActionAssets;

	FOnDataprepActionAssetChange OnActionChanged;

	int32 CachedActionCount;

};