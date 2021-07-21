// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "ComputeFramework/ComputeDataInterface.h"

#include "OptimusComputeDataInterface.generated.h"


struct FOptimusCDIPinDefinition
{
	// Singleton value read/write
	FOptimusCDIPinDefinition(
		FName InPinName,
		FString InDataFunctionName,
		FName InContextName
		) :
		PinName(InPinName),
		DataFunctionName(InDataFunctionName),
		ContextName(InContextName)
	{ }

	FOptimusCDIPinDefinition(
		FName InPinName,
		FString InDataFunctionName,
		FString InCountFunctionName,
		FName InContextName
		) :
		PinName(InPinName),
		DataFunctionName(InDataFunctionName),
		CountFunctionNames{{InCountFunctionName}},
		ContextName(InContextName)
	{ }

	FOptimusCDIPinDefinition(
		FName InPinName,
		FString InDataFunctionName,
		TArray<FString> InCountFunctionNames,
		FName InContextName
		) :
		PinName(InPinName),
		DataFunctionName(InDataFunctionName),
		CountFunctionNames(InCountFunctionNames),
		ContextName(InContextName)
	{ }

	
	// The name of the pin as seen by the user.
	FName PinName;

	// The name of the function that underlies the data access by the pin. The data functions
	// are used to either read or write to data interfaces, whether explicit or implicit.
	// The read functions take zero to N uint indices, determined by the number of count 
	// functions below, and return a value. The write functions take zero to N uint indices,
	// followed by the value, with no return value.
	FString DataFunctionName;

	// The function to calls to get the item count for the data. If there is no count function
	// name then the data is assumed to be a singleton and will be shown as a value pin rather
	// than a resource pin. Otherwise, the number of count functions defines the dimensionality
	// of the lookup. The first count function returns the count required for the context and
	// should accept no arguments. The second count function takes as index any number between
	// zero and the result of the first count function. E.g:
	// uint GetFirstDimCount();
	// uint GetSecondDimCount(uint Index);
	// These two results then bound the indices used to call the data function.
	TArray<FString> CountFunctionNames;

	// The data context for the primary dimension. Connections of different contexts cannot be
	// made, nor connections of the 
	FName ContextName;
};


UCLASS(Abstract, Const)
class OPTIMUSCORE_API UOptimusComputeDataInterface : public UComputeDataInterface
{
	GENERATED_BODY()
	
public:
	static TArray<UClass*> GetAllComputeDataInterfaceClasses();
	
	/// Returns the name to show on the node that will proxy this interface in the graph view.
	virtual FString GetDisplayName() const PURE_VIRTUAL(UOptimusComputeDataInterface::GetDisplayName, return {};)
	
	/// Returns the list of pins that will map to the shader functions provided by this data interface.
	virtual TArray<FOptimusCDIPinDefinition> GetPinDefinitions() const PURE_VIRTUAL(UOptimusComputeDataInterface::GetDisplayName, return {};)
	
	virtual bool IsVisible() const
	{
		return true;
	}

private:
	static TArray<UClass*> CachedClasses;
};


UCLASS(Blueprintable, Const)
class OPTIMUSCORE_API UOptimusDataInterfaceHelpers : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Create and auto initialize the set of data providers for a graph.
	 * Initialization is very hard coded.
	 * FIXME: Better to have some kind of factory pattern here. Some providers will need custom set up on the caller side.
	 */ 
	UFUNCTION(BlueprintCallable, Category = OptimusNodeGraph)
	static void InitDataProviders(UComputeGraphComponent* ComputeGraphComponent, USkeletalMeshComponent* SkeletalMeshComponent);
};
