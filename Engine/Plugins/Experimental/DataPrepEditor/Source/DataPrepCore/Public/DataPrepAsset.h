// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "DataPrepContentConsumer.h"
#include "DataPrepContentProducer.h"

#include "DataPrepAsset.generated.h"

class UDataprepActionAsset;
class UDataprepContentConsumer;
class UDataprepAsset;

enum class FDataprepAssetChangeType : uint8
{
	ProducerAdded,
	ProducerRemoved,
	ProducerModified,
	ConsumerModified,
	BlueprintModified,
};

USTRUCT(Experimental)
struct FDataprepAssetProducer
{
	GENERATED_BODY()

	FDataprepAssetProducer()
		: Producer(nullptr)
		, bIsEnabled( true )
		, SupersededBy( INDEX_NONE )
	{}

	FDataprepAssetProducer(UDataprepContentProducer* InProducer, bool bInEnabled )
		: Producer(InProducer)
		, bIsEnabled( bInEnabled )
		, SupersededBy( INDEX_NONE )
	{}

	UPROPERTY()
	UDataprepContentProducer* Producer;

	UPROPERTY()
	bool bIsEnabled;

	UPROPERTY()
	int32 SupersededBy;
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
		, OnOperationOrderChangedHandle()
	{}

	FDataprepAssetAction(UDataprepActionAsset* InActionAsset, bool bInEnabled, UDataprepAsset& DataprepAsset)
		: ActionAsset(nullptr)
		, bIsEnabled(bInEnabled)
		, DataprepAssetPtr(&DataprepAsset)
		, OnOperationOrderChangedHandle()
	{
		SetActionAsset(InActionAsset);
	}

	FDataprepAssetAction(const FDataprepAssetAction& Other)
		: ActionAsset(nullptr)
		, bIsEnabled(Other.bIsEnabled)
		, DataprepAssetPtr(Other.DataprepAssetPtr)
		, OnOperationOrderChangedHandle()
	{
		SetActionAsset(Other.ActionAsset);
	}

	FDataprepAssetAction(FDataprepAssetAction&& Other)
		: ActionAsset(nullptr)
		, bIsEnabled(Other.bIsEnabled)
		, DataprepAssetPtr(Other.DataprepAssetPtr)
		, OnOperationOrderChangedHandle()
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

	FDelegateHandle OnOperationOrderChangedHandle;
};


UCLASS(Experimental)
class DATAPREPCORE_API UDataprepAsset : public UObject
{
	GENERATED_BODY()

public:
	UDataprepAsset();

	virtual ~UDataprepAsset();

	// UObject interface
	virtual void Serialize( FArchive& Ar ) override;
	virtual void PostInitProperties() override;
	// End of UObject interface

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
	DECLARE_EVENT(UDataprepAsset, FOnActionsOrderChanged)
	FOnActionsOrderChanged& GetOnActionsOrderChanged()
	{
		return OnActionsOrderChanged;
	}

	/**
	 * Allow an observer to be notified when the operations order of an action changed that also include adding and removing an operation
	 * @return The delegate that will be broadcasted when the operations order of an action has changed
	 */
	DECLARE_EVENT_OneParam(UDataprepAsset, FOnActionOperationsOrderChanged, const UDataprepActionAsset* /** InActionAsset The modified action */)
	FOnActionOperationsOrderChanged& GetOnActionOperationsOrderChanged()
	{
		return OnActionOperationsOrderChanged;
	}

	/**
	 * Add a producer of a given class
	 * @param ProducerClass Class of the Producer to add
	 * @return true if addition was successful, false otherwise
	 */
	bool AddProducer( UClass* ProducerClass );

	/**
	 * Remove the producer at a given index
	 * @param IndexToRemove Index of the producer to remove
	 * @return true if remove was successful, false otherwise
	*/
	bool RemoveProducer( int32 IndexToRemove );

	/**
	 * Enable/Disable the producer at a given index
	 * @param Index Index of the producer to update
	 */
	void EnableProducer(int32 Index, bool bValue);

	/**
	 * Enable/Disable the producer at a given index
	 * @param Index Index of the producer to update
	 */
	bool EnableAllProducers(bool bValue);

	/**
	 * Toggle the producer at a given index
	 * @param Index Index of the producer to update
	 */
	void ToggleProducer( int32 Index )
	{
		EnableProducer( Index, !IsProducerEnabled( Index ) );
	}

	/**
	 * Replace the current consumer with one of a given class
	 * @param NewConsumerClass Class of the consumer to replace the current one with
	 * @return true if replacement was successful, false otherwise
	 */
	bool ReplaceConsumer( UClass* NewConsumerClass );

	const UDataprepContentConsumer* GetConsumer() const { return Consumer; }

	UDataprepContentConsumer* GetConsumer() { return Consumer; }

	int32 GetProducersCount() const { return Producers.Num(); }

	/** @return pointer on producer at Index-th position in Producers array. nullptr if Index is invalid */
	const UDataprepContentProducer* GetProducer(int32 Index) const
	{ 
		return Producers.IsValidIndex(Index) ? Producers[Index].Producer : nullptr;
	}

	UDataprepContentProducer* GetProducer(int32 Index)
	{ 
		return Producers.IsValidIndex(Index) ? Producers[Index].Producer : nullptr;
	}

	/** @return True if producer at Index-th position is enabled. Returns false if disabled or Index is invalid */
	bool IsProducerEnabled(int32 Index) const
	{ 
		return Producers.IsValidIndex(Index) ? Producers[Index].bIsEnabled : false;
	}

	/** @return True if producer at Index-th position is superseded by an enabled producer. Returns false if not superseded or superseder is disabled or Index is invalid */
	bool IsProducerSuperseded(int32 Index) const
	{ 
		return Producers.IsValidIndex(Index) ? Producers[Index].SupersededBy != INDEX_NONE && Producers.IsValidIndex(Producers[Index].SupersededBy) && Producers[Producers[Index].SupersededBy].bIsEnabled : false;
	}

	/**
	 * Allow an observer to be notified when when the consumer or one of the producer has changed
	 * @return The delegate that will be broadcasted when when the consumer or one of the producer has changed
	 */
	DECLARE_EVENT_TwoParams(UDataprepAsset, FOnDataprepAssetChanged, FDataprepAssetChangeType, int32 )
	FOnDataprepAssetChanged& GetOnChanged() { return OnChanged; }

	void RunProducers( const UDataprepContentProducer::ProducerContext& InContext, TArray< TWeakObjectPtr< UObject > >& OutAssets );

	bool RunConsumer( const UDataprepContentConsumer::ConsumerContext& InContext, FString& OutReason );

#if WITH_EDITORONLY_DATA

	// Temp code for the nodes development
	/** Temporary: Pointer to data preparation pipeline blueprint used to process input data */
	UPROPERTY()
	class UBlueprint* DataprepRecipeBP;
	// end of temp code for nodes development

protected:
	/** List of producers referenced by the asset */
	UPROPERTY()
	TArray< FDataprepAssetProducer > Producers;

	/** Consumer referenced by the asset */
	UPROPERTY()
	UDataprepContentConsumer* Consumer;
#endif

protected:

	/**
	 * Answer notification that the consumer has changed
	 * @param InProducer Producer which has changed
	 */
	void OnProducerChanged( const UDataprepContentProducer* InProducer );

	/** Answer notification that the consumer has changed */
	void OnConsumerChanged();

	// Temp code for the nodes development
	void OnBlueprintChanged( class UBlueprint* InBlueprint );
	// end of temp code for nodes development

private:
	/**
	  * Helper to check superseding on all producers after changes made on one producer
	  * @param Index Index of the producer to validate against
	  * @param bChangeAll Set to true if changes on input producer implied changes on other producers
	  */
	void ValidateProducerChanges( int32 Index, bool &bChangeAll );

	void RemoveInvalidActions();

	/** Ordered array of the actions */
	UPROPERTY()
	TArray<FDataprepAssetAction> Actions;

	/** Delegate broadcasted when the actions order changed */
	FOnActionsOrderChanged OnActionsOrderChanged;

	/** Delegate broadcasted when the order of the operations of an action changed */
	FOnActionOperationsOrderChanged OnActionOperationsOrderChanged;

	/** Delegate broadcasted when the consumer or one of the producers has changed */
	FOnDataprepAssetChanged OnChanged;

	FDelegateHandle OnAssetDeletedHandle;
};