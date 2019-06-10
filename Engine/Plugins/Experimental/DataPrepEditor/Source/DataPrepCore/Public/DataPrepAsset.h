// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "DataPrepContentProducer.h"

#include "DataPrepAsset.generated.h"

class UDataprepActionAsset;
class UDataprepContentConsumer;
class UDataprepContentProducer;
class UDataprepAsset;

// Delegates
DECLARE_MULTICAST_DELEGATE(FOnActionsOrderChanged)
DECLARE_MULTICAST_DELEGATE_OneParam(FOnActionOperationsOrderChanged, const UDataprepActionAsset* /** InActionAsset The modified action */)

USTRUCT(Experimental)
struct FDataprepAssetProducer
{
	GENERATED_BODY()

	FDataprepAssetProducer()
		: Producer(nullptr)
		, bIsEnabled( true )
	{}

	FDataprepAssetProducer(UDataprepContentProducer* InProducer, bool bInEnabled )
		: Producer(InProducer)
		, bIsEnabled( bInEnabled )
	{}

	UPROPERTY()
	UDataprepContentProducer* Producer;
	
	UPROPERTY()
	bool bIsEnabled;
};

/**
 * Used for the internals of the DataprepAsset only.
 * FDataprepAssetAction manage the state of an action for the DataprepAsset.
 * Also, it report the notification from the action to Dataprep asset.
 */
USTRUCT(Experimental)
struct FDataprepAssetAction
{
	GENERATED_BODY()

	FDataprepAssetAction()
		: ActionAsset(nullptr)
		, bIsEnabled(true)
		, DataprepAssetPtr(nullptr)
		, OnOperationOrderChandedHandle()
	{}

	FDataprepAssetAction(UDataprepActionAsset* InActionAsset, bool bInEnabled, UDataprepAsset& DataprepAsset)
		: ActionAsset(nullptr)
		, bIsEnabled(bInEnabled)
		, DataprepAssetPtr(&DataprepAsset)
		, OnOperationOrderChandedHandle()
	{
		SetActionAsset(InActionAsset);
	}

	FDataprepAssetAction(const FDataprepAssetAction& Other)
		: ActionAsset(nullptr)
		, bIsEnabled(Other.bIsEnabled)
		, DataprepAssetPtr(Other.DataprepAssetPtr)
		, OnOperationOrderChandedHandle()
	{
		SetActionAsset(Other.ActionAsset);
	}

	FDataprepAssetAction(FDataprepAssetAction&& Other)
		: ActionAsset(nullptr)
		, bIsEnabled(Other.bIsEnabled)
		, DataprepAssetPtr(Other.DataprepAssetPtr)
		, OnOperationOrderChandedHandle()
	{
		SetActionAsset(Other.ActionAsset);
		Other.SetActionAsset( nullptr );
	}

	virtual ~FDataprepAssetAction();

	FDataprepAssetAction& operator=(const FDataprepAssetAction& Other);

	FDataprepAssetAction& operator=(FDataprepAssetAction&& Other);

	UDataprepActionAsset* GetActionAsset() { return ActionAsset; }

	const UDataprepActionAsset* GetActionAsset() const { return ActionAsset; }

	void Enable(bool bInIsEnable) { bIsEnabled = bInIsEnable; }

	bool IsEnabled() const { return bIsEnabled; }

private:

	void SetActionAsset(UDataprepActionAsset* InActionAsset);

	void BindDataprepAssetToAction();

	void UnbindDataprepAssetFromAction();

	void OnActionOperationsOrderChanged();

	UPROPERTY()
	UDataprepActionAsset* ActionAsset;

	UPROPERTY()
	bool bIsEnabled;

	TWeakObjectPtr<UDataprepAsset> DataprepAssetPtr;

	FDelegateHandle OnOperationOrderChandedHandle;
};


UCLASS(Experimental)
class DATAPREPCORE_API UDataprepAsset : public UObject
{
	GENERATED_BODY()

public:
	UDataprepAsset();

	virtual ~UDataprepAsset();

	/**
	 * Create an action at the end of actions array
	 * @return The index of the added action
	 */
	int32 AddAction();

	/**
	 * Access to an action
	 * @param Index the index of the desired action
	 * @return A pointer to the action if it exist, otherwise nullptr
	 */
	inline UDataprepActionAsset* GetAction(int32 Index);

	/**
	 * Access to an action
	 * @param Index the index of the desired action
	 * @return A pointer to the action if it exist, otherwise nullptr
	 */
	inline const UDataprepActionAsset* GetAction(int32 Index) const;

	/**
	 * Get the number of action of this Dataprep
	 * @return The number of action
	 */
	inline int32 GetActionsCount() const;

	/**
	 * Get enabled status of an action
	 * @param Index The index of the action
	 * @return True if the action is enabled. Always return false if the action index is invalid
	 */
	inline bool IsActionEnabled(int32 Index) const;

	/**
	 * Allow to set the enabled state of an action
	 * @param Index The index of the action
	 * @param bEnable The new enabled state of the action
	 */
	void EnableAction(int32 Index, bool bEnable);

	/**
	 * Move an action to another spot in the order of actions
	 * This operation take O(n) time. Where n is the absolute value of ActionIndex - DestinationIndex
	 * @param OperationIndex The Index of the operation to move
	 * @param DestinationIndex The index of where the action will be move to
	 * @return True if the operation was move
	 */
	bool MoveAction(int32 ActionIndex, int32 DestinationIndex);

	/**
	 * Remove an action from the Dataprep
	 * @param Index The index of the action to remove
	 * @return True if an action was removed
	 */
	bool RemoveAction(int32 Index);

	/**
	 * Allow an observer to be notified when the actions order changed that also include adding and removing an action
	 * @return The delegate that will be broadcasted when the actions order changed
	 */
	FOnActionsOrderChanged& GetOnActionsOrderChanged();

	/**
	 * Allow an observer to be notified when the operations order of an action changed that also include adding and removing an operation
	 * @return The delegate that will be broadcasted when the operations order of an action has changed
	 */
	FOnActionOperationsOrderChanged& GetOnActionOperationsOrderChanged();

	void RunProducers( const UDataprepContentProducer::ProducerContext& InContext, TArray< TWeakObjectPtr< UObject > >& OutAssets );

#if WITH_EDITORONLY_DATA
	/** List of producers referenced by the asset */
	UPROPERTY()
	TArray< FDataprepAssetProducer > Producers;

	/** Consumer referenced by the asset */
	UPROPERTY()
	UDataprepContentConsumer* Consumer;
#endif

	// Temp code for the nodes development
	/** Temporary: Pointer to data preparation pipeline blueprint used to process input data */
	UPROPERTY()
	class UBlueprint* DataprepRecipeBP;
	// end of temp code for nodes development

private:

	void RemoveInvalidActions();

	/** Ordered array of the actions */
	UPROPERTY()
	TArray<FDataprepAssetAction> Actions;

	//Delegate broadcasted when the actions order changed
	FOnActionsOrderChanged OnActionsOrderChanged;

	//Delegate broadcasted when the order of the operations of an action changed
	FOnActionOperationsOrderChanged OnActionOperationsOrderChanged;

	FDelegateHandle OnAssetDeletedHandle;
};