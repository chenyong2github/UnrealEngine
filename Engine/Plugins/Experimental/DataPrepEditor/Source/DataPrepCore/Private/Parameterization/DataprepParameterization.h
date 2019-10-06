// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Parameterization/DataprepParameterizationUtils.h"

#include "CoreMinimal.h"
#include "Delegates/DelegateCombinations.h"
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
 * The DataprepParameterization contains the data for the parameterization of a pipeline
 */
UCLASS(MinimalAPI)
class UDataprepParameterization : public UObject
{
public:
	GENERATED_BODY()

	// UObject interface
	virtual void PostInitProperties() override;
	virtual void PostLoad() override;
	virtual void Serialize(FArchive& Ar) override;
	// End of UObject interface

	// Temporary function to help debug and test the feature
	UObject* GetDefaultObject();

	bool BindObjectProperty(UObject* Object, const TArray<FDataprepPropertyLink>& PropertyChain, FName Name);

private:

	void GenerateClass();

	void UpdateClass();

	void RegeneratedBindingAfterLoad();

	/**
	 * Try adding a binded property to the parameterization class
	 * @return false if the binding is no more valid
	 */
	UProperty* AddPropertyToClass(FName ParameterisationPropertyName, UProperty& Property);

	UPROPERTY()
	TMap<FDataprepParameterizationBinding, FName> BindingsFromPipeline;

	UPROPERTY(Transient)
	TMap<FName, UProperty*> NameToParameterizationProperty; // Just a cache for the CustomFindProperty

	/** Track the name usage for parameters */
	TMap<FName, TArray<FDataprepParameterizationBinding>> NameUsage;

	UPROPERTY(Transient)
	UClass* CustomContainerClass;

	UPROPERTY(Transient)
	UObject* DefaultParameterisation;

	/** 
	 * This is used only to store a serialization of the values of the parameterization since we can't save our custom class
	 */
	UPROPERTY()
	TArray<uint8> ParameterizationStorage;

	using FMapOldToNewObjects = TMap<UObject*, UObject*>;
	DECLARE_EVENT_OneParam(UDataprepParameterization, FCustomClassWasUpdated, const FMapOldToNewObjects& /** OldToNew */);
	FCustomClassWasUpdated OnClassUpdate;

	// the dataprep instance need some special access to the dataprep parameterization
	friend class UDataprepParameterizationInstance;
};


UCLASS(MinimalAPI)
class UDataprepParameterizationInstance : public UObject
{
public:
	GENERATED_BODY()

	// UObject interface
	virtual void PostLoad() override;
	virtual void Serialize(FArchive& Ar) override;
	// End of UObject interface

	// Apply the parameterization to a copy of the source pipeline
	void ApplyParameterization(const TMap<UObject*, UObject*>& SourceToCopy);

	void SetParameterizationSource(UDataprepParameterization& Parameterization);

	UObject* GetParameterizationInstance() { return ParameterizationInstance; }

private:

	void OnCustomClassUpdate(const TMap<UObject*, UObject*>& OldToNew);

	void LoadParameterization();

	UPROPERTY()
	UDataprepParameterization* SourceParameterization;

	UPROPERTY(Transient)
	UObject* ParameterizationInstance;

	/** 
	 * This is used only to store a serialization of the values of the parameterization since we can't save the custom class
	 */
	UPROPERTY()
	TArray<uint8> ParameterizationInstanceStorage;
};

