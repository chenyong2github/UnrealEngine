// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "OptimusComputeDataInterface.h"

#include "OptimusNode.h"

#include "OptimusNode_DataInterface.generated.h"

/**
 * 
 */
UCLASS(NotPlaceable)
class UOptimusNode_DataInterface
	: public UOptimusNode
{
	GENERATED_BODY()

public:
	UOptimusNode_DataInterface();

	FName GetNodeCategory() const override 
	{
		return CategoryName::Deformers;
	}

	void SetDataInterfaceClass(TSubclassOf<UOptimusComputeDataInterface> InDataInterfaceClass);

	UClass *GetDataInterfaceClass() const
	{
		return DataInterfaceClass;
	}

protected:
	void CreatePins() override;

private:
	void CreatePinsFromDataInterface(UOptimusComputeDataInterface *InDataInterface);
	void CreatePinFromDefinition(
		const FOptimusCDIPinDefinition &InDefinition,
		const TMap<FString, const FShaderFunctionDefinition *>& InReadFunctionMap,
		const TMap<FString, const FShaderFunctionDefinition *>& InWriteFunctionMap
		);
	
	// The class of the data interface that this node represents. We call the CDO
	// to interrogate display names and pin definitions. This may change in the future once
	// data interfaces get tied closer to the objects they proxy.
	UPROPERTY()
	TObjectPtr<UClass> DataInterfaceClass; 
};
