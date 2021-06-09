// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "ComputeFramework/ComputeDataInterface.h"

#include "OptimusComputeDataInterface.generated.h"


struct FOptimusCDIPinDefinition
{
	// The name of the pin.
	FName PinName;

	// The name of the function that underlies the data access by the pin
	FString DataFunctionName;

	// The function to call to get the item count for the data. If there is no count function
	// name then the data is assumed to be a singleton and will be shown as a value pin rather
	// than a resource pin.
	FString CountFunctionName;

	// The data context. Connections of different contexts cannot be made.
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
	
	virtual bool IsTerminal() const
	{
		return true;
	}

private:
	static TArray<UClass*> CachedClasses;
};
