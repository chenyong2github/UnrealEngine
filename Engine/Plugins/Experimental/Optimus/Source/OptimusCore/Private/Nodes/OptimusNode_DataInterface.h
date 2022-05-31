// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IOptimusDataInterfaceProvider.h"
#include "OptimusComputeDataInterface.h"

#include "OptimusNode.h"
#include "Templates/SubclassOf.h"

#include "OptimusNode_DataInterface.generated.h"

/**
 * 
 */
UCLASS(Hidden)
class UOptimusNode_DataInterface :
	public UOptimusNode,
	public IOptimusDataInterfaceProvider
{
	GENERATED_BODY()

public:
	UOptimusNode_DataInterface();

	FName GetNodeCategory() const override 
	{
		return CategoryName::DataInterfaces;
	}

	// -- IOptimusDataInterfaceProvider implementations
	UOptimusComputeDataInterface *GetDataInterface(UObject *InOuter) const override;

	int32 GetDataFunctionIndexFromPin(const UOptimusNodePin* InPin) const override;

	virtual void SetDataInterfaceClass(TSubclassOf<UOptimusComputeDataInterface> InDataInterfaceClass);
	
protected:
	void PostLoad() override;
	void ConstructNode() override;
	void PostDuplicate(EDuplicateMode::Type DuplicateMode) override;

private:
	void CreatePinsFromDataInterface(UOptimusComputeDataInterface *InDataInterface);
	void CreatePinFromDefinition(
		const FOptimusCDIPinDefinition &InDefinition,
		const TMap<FString, const FShaderFunctionDefinition *>& InReadFunctionMap,
		const TMap<FString, const FShaderFunctionDefinition *>& InWriteFunctionMap
		);

protected:
	// The class of the data interface that this node represents. We call the CDO
	// to interrogate display names and pin definitions. This may change in the future once
	// data interfaces get tied closer to the objects they proxy.
	UPROPERTY()
	TObjectPtr<UClass> DataInterfaceClass; 

	// Editable copy of the DataInterface for storing properties that will customize behavior on on the data interface.
	UPROPERTY(VisibleAnywhere, Instanced, Category=DataInterface)
	TObjectPtr<UOptimusComputeDataInterface> DataInterfaceData;
};
