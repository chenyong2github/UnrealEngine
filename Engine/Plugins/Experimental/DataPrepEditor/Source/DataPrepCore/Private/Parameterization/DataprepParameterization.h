// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Parameterization/DataprepParameterizationUtils.h"

#include "CoreMinimal.h"
#include "Delegates/DelegateCombinations.h"
#include "Delegates/IDelegateInstance.h"
#include "Engine/UserDefinedStruct.h"
#include "Templates/SharedPointer.h"
#include "UObject/Class.h"
#include "UObject/UnrealType.h"

#include "DataprepParameterization.generated.h"

class UProperty;

USTRUCT()
struct FDataprepParameterizationBinding
{
	GENERATED_BODY()

	FDataprepParameterizationBinding()
		: ObjectBinded( nullptr )
		, PropertyChain()
		, ValueType( nullptr )
	{}

	FDataprepParameterizationBinding(UObject* InObjectBinded, TArray<FDataprepPropertyLink> InPropertyChain)
		: ObjectBinded( InObjectBinded )
		, PropertyChain( MoveTemp( InPropertyChain ) )
		, ValueType( PropertyChain.Last().CachedProperty.Get() ? PropertyChain.Last().CachedProperty.Get()->GetClass() : nullptr )
	{}

	FDataprepParameterizationBinding(FDataprepParameterizationBinding&&) = default;
	FDataprepParameterizationBinding(const FDataprepParameterizationBinding&) = default;
	FDataprepParameterizationBinding& operator=(FDataprepParameterizationBinding&&) = default;
	FDataprepParameterizationBinding& operator=(const FDataprepParameterizationBinding&) = default;

	bool operator==(const FDataprepParameterizationBinding& Other) const;

	UPROPERTY()
	UObject* ObjectBinded;

	UPROPERTY()
	TArray<FDataprepPropertyLink> PropertyChain;

	// The class of the property managing the value
	UPROPERTY()
	UClass* ValueType;
};

uint32 GetTypeHash(const FDataprepParameterizationBinding& Binding);

uint32 GetTypeHash(const TArray<FDataprepPropertyLink>& PropertyLinks);

/**
 * Count the number of time each hash was encounter
 */
USTRUCT()
struct FHashCount
{
	GENERATED_BODY()

	UPROPERTY()
	TMap<uint32, uint32> HashCount;
};

/** 
 * The DataprepParameterization contains the data for the parameterization of a pipeline
 */
UCLASS(MinimalAPI)
class UDataprepParameterization : public UObject
{
public:
	GENERATED_BODY()

	UDataprepParameterization();
	~UDataprepParameterization();

	// UObject interface
	virtual void PostInitProperties() override;
	virtual void PostLoad() override;
	virtual void Serialize(FArchive& Ar) override;
	virtual void PostEditUndo() override;
	// End of UObject interface

	void OnObjectModified(UObject* Object);


	UObject* GetDefaultObject();

	bool BindObjectProperty(UObject* Object, const TArray<FDataprepPropertyLink>& PropertyChain, FName Name);

	bool IsObjectPropertyBinded(UObject* Object, const TArray<FDataprepPropertyLink>& PropertyChain) const;

	void RemoveBindedObjectProperty(UObject* Object, const TArray<FDataprepPropertyLink>& PropertyChain);

private:

	/**
	 * Generate the Custom Container Class
	 */
	void GenerateClass();

	/**
	 * Update the Custom Container Class to a newer version
	 */
	void UpdateClass();
	
	/**
	 * Do the process of regenerating the Custom Container Class and the data of its default object from the serialized data
	 */
	void LoadParameterization();

	/**
	 * Remove the current Custom Container Class so that we can create a new one
	 */
	void PrepareCustomClassForNewClassGeneration();

	/**
	 * Do reinstancing of the objects created from the Custom Container Class
	 * @param OldClass The previous Custom Constainer Class
	 * @param bMigrateData Should we migrate the data from the old instances to the new instances
	 */
	void DoReinstancing(UClass* OldClass, bool bMigrateData = true);

	/**
	 * Try adding a binded property to the parameterization class
	 * @return false if the binding is no more valid
	 */
	UProperty* AddPropertyToClass(FName ParameterisationPropertyName, UProperty& Property);

	UPROPERTY()
	TMap<FDataprepParameterizationBinding, FName> BindingsFromPipeline;

	UPROPERTY(Transient, NonTransactional)
	TMap<FName, UProperty*> NameToParameterizationProperty; // Just a cache for the CustomFindProperty

	/** Track the name usage for parameters */
	TMap<FName, TSet<FDataprepParameterizationBinding>> NameUsage;

	UPROPERTY(Transient, NonTransactional)
	UClass* CustomContainerClass;

	UPROPERTY(Transient, NonTransactional)
	UObject* DefaultParameterisation;

	/** 
	 * This is used only to store a serialization of the values of the parameterization since we can't save our custom container class
	 */
	UPROPERTY()
	TArray<uint8> ParameterizationStorage;

	DECLARE_EVENT(UDataprepParameterization, FOnCustomClassAboutToBeUpdated);
	FOnCustomClassAboutToBeUpdated OnCustomClassAboutToBeUpdated;

	using FMapOldToNewObjects = TMap<UObject*, UObject*>;
	DECLARE_EVENT_OneParam(UDataprepParameterization, FOnCustomClassWasUpdated, const FMapOldToNewObjects& /** OldToNew */);
	FOnCustomClassWasUpdated OnCustomClassWasUpdated;

	DECLARE_EVENT(UDataprepParameterization,FOnTellInstancesToReloadTheirSerializedData);
	FOnTellInstancesToReloadTheirSerializedData OnTellInstancesToReloadTheirSerializedData;

	// the dataprep instance need some special access to the dataprep parameterization
	friend class UDataprepParameterizationInstance;

	FDelegateHandle OnObjectModifiedHandle;
};


UCLASS(MinimalAPI)
class UDataprepParameterizationInstance : public UObject
{
public:
	GENERATED_BODY()

	UDataprepParameterizationInstance();
	~UDataprepParameterizationInstance();

	// UObject interface
	virtual void PostLoad() override;
	virtual void Serialize(FArchive& Ar) override;
	virtual void PostEditUndo() override;
	// End of UObject interface

	void OnObjectModified(UObject* Object);

	// Apply the parameterization to a copy of the source pipeline
	void ApplyParameterization(const TMap<UObject*, UObject*>& SourceToCopy);

	void SetParameterizationSource(UDataprepParameterization& Parameterization);

	UObject* GetParameterizationInstance() { return ParameterizationInstance; }

private:

	void CustomClassAboutToBeUpdated();

	/**
	 * Used as call back for event coming from the source parameterization
	 * Change the parametrization instance to the new object after a reinstancing
	 */
	void CustomClassWasUpdated(const TMap<UObject*, UObject*>& OldToNew);

	/**
	 * Load the parameterization data on the instance from the ParameterizationInstanceStorage
	 */
	void LoadParameterization();

	/**
	 * Setup the parameterization instance so that we can react to event coming from the source parameterization
	 */
	void SetupCallbacksFromSourceParameterisation();

	/**
	 * Clean the parameterization instance so that we can bind to a new source parameterization
	 */
	void UndoSetupForCallbacksFromParameterization();

	// The parameterization from which this instance was constructed
	UPROPERTY()
	UDataprepParameterization* SourceParameterization;

	// The actual object on which the parameterization data is stored
	UPROPERTY(Transient, NonTransactional)
	UObject* ParameterizationInstance;

	// This is used only to store a serialization of the values of the parameterization since we can't save the custom class
	UPROPERTY()
	TArray<uint8> ParameterizationInstanceStorage;

	FDelegateHandle OnObjectModifiedHandle;
};

