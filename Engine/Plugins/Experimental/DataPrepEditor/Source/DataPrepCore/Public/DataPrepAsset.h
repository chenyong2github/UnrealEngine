// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "DataprepActionAsset.h"
#include "DataprepAssetInterface.h"
#include "DataprepAssetProducers.h"

#include "DataPrepAsset.generated.h"

class UBlueprint;
class UDataprepActionAsset;
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
	// End of UObject interface

	// UDataprepAssetInterface interface
	virtual void ExecuteRecipe( const TSharedPtr<FDataprepActionContext>& InActionsContext ) override;
	virtual bool HasActions() const override { return ActionAssets.Num() > 0; }
private:
	virtual TArray<UDataprepActionAsset*> GetCopyOfActions(TMap<UObject*,UObject*>& OutOriginalToCopy) const override;
	// End of UDataprepAssetInterface interface

public:
	// Temp code for the nodes development
	bool CreateBlueprint();

	bool CreateParameterization();

	/** @return pointer on the recipe */
	const UBlueprint* GetRecipeBP() const
	{ 
		return DataprepRecipeBP;
	}

	UBlueprint* GetRecipeBP()
	{ 
		return DataprepRecipeBP;
	}

	// struct to restrict the access scope
	struct FDataprepBlueprintChangeNotifier
	{
	private:
		friend class FDataprepEditorUtils;

		static void NotifyDataprepBlueprintChange(UDataprepAsset& DataprepAsset, UObject* ModifiedObject)
		{
			// The asset is not complete yet. Skip this change
			if(!DataprepAsset.HasAnyFlags(RF_NeedLoad | RF_NeedPostLoad | RF_NeedPostLoadSubobjects))
			{
				if(UDataprepActionAsset* ActionAsset = Cast<UDataprepActionAsset>(ModifiedObject))
				{
					DataprepAsset.UpdateActions();
				}
				DataprepAsset.OnBlueprintChanged.Broadcast( ModifiedObject );
			}
		}
	};

	/**
	* Allow an observer to be notified of an change in the pipeline
	* return The event that will be broadcasted when a object has receive a modification that might change the result of the pipeline
	*/
	DECLARE_EVENT_OneParam(UDataprepAsset, FOnDataprepBlueprintChange, UObject* /*The object that was modified*/)
	FOnDataprepBlueprintChange& GetOnBlueprintChanged() { return OnBlueprintChanged; }
	// end of temp code for nodes development

public:
	// Functions specific to the parametrization of the dataprep asset

	/**
	 * Event to notify the ui that a dataprep parametrization was modified
	 * This necessary as the ui for the parameterization is only updated by manual event (the ui don't pull new values each frame)
	 * Note on the objects param: The parameterized objects that should refresh their ui. If nullptr all widgets that can display some info on the parameterization should be refreshed
	 */
	DECLARE_EVENT_OneParam(UDataprepParameterization, FDataprepParameterizationStatusForObjectsChanged, const TSet<UObject*>* /** Objects */)
	FDataprepParameterizationStatusForObjectsChanged OnParameterizedObjectsChanged;

	virtual UObject* GetParameterizationObject() override;

	void BindObjectPropertyToParameterization(UDataprepParameterizableObject* Object, const TArray<struct FDataprepPropertyLink>& InPropertyChain,const FName& Name);

	bool IsObjectPropertyBinded(UDataprepParameterizableObject* Object, const TArray<struct FDataprepPropertyLink>& InPropertyChain) const;

	FName GetNameOfParameterForObjectProperty(UDataprepParameterizableObject* Object, const TArray<struct FDataprepPropertyLink>& InPropertyChain) const;

	void RemoveObjectPropertyFromParameterization(UDataprepParameterizableObject* Object, const TArray<struct FDataprepPropertyLink>& InPropertyChain);

	void GetExistingParameterNamesForType(UProperty* Property, bool bIsDescribingFullProperty, TSet<FString>& OutValidExistingNames, TSet<FString>& OutInvalidNames) const;

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
	void UpdateActions();

private:
	UPROPERTY()
	UEdGraphNode* StartNode;

	UPROPERTY()
	UDataprepParameterization* Parameterization;

	UPROPERTY()
	TArray<UDataprepActionAsset*> ActionAssets;

	/** Event broadcasted when object in the pipeline was modified (Only broadcasted on changes that can affect the result of execution) */
	FOnDataprepBlueprintChange OnBlueprintChanged;
	// end of temp code for nodes development
};