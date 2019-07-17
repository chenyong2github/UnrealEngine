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


UCLASS(Experimental)
class DATAPREPCORE_API UDataprepAsset : public UObject
{
	GENERATED_BODY()

public:
	UDataprepAsset();

	virtual ~UDataprepAsset() = default;

	// UObject interface
	virtual void Serialize( FArchive& Ar ) override;
	virtual void PostInitProperties() override;
	// End of UObject interface

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
	 * Allow an observer to be notified of an change in the pipeline
	 * return The event that will be broadcasted when a object has receive a modification that might change the result of the pipeline
	 */
	DECLARE_EVENT_OneParam(UDataprepAsset, FOnDataprepPipelineChange, UObject* /*The object that was modified*/)
	FOnDataprepPipelineChange& GetOnPipelineChange() { return OnPipelineChange; }

	// struct to restrict the access scope
	struct FDataprepPipelineChangeNotifier
	{
	private:
		friend class FDataprepEditorUtils;

		static void NotifyDataprepOfPipelineChange(UDataprepAsset& DataprepAsset, UObject* ModifiedObject)
		{
			DataprepAsset.OnPipelineChange.Broadcast( ModifiedObject );
		}
	};

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


	/** Delegate broadcasted when the consumer or one of the producers has changed */
	FOnDataprepAssetChanged OnChanged;

	/** Event broadcasted when object in the pipeline was modified (Only broadcasted on changes that can affect the result of execution) */
	FOnDataprepPipelineChange OnPipelineChange;
};