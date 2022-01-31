// Copyright Epic Games, Inc. All Rights Reserved.

#include "Nodes/OptimusNode_DataInterface.h"

#include "OptimusCoreModule.h"
#include "OptimusNodePin.h"
#include "OptimusDataTypeRegistry.h"

#include "ComputeFramework/ShaderParamTypeDefinition.h"


UOptimusNode_DataInterface::UOptimusNode_DataInterface()
{
}


void UOptimusNode_DataInterface::SetDataInterfaceClass(
	TSubclassOf<UOptimusComputeDataInterface> InDataInterfaceClass
	)
{
	DataInterfaceClass = InDataInterfaceClass;
}


void UOptimusNode_DataInterface::ConstructNode()
{
	if (ensure(!DataInterfaceClass.IsNull()))
	{
		UOptimusComputeDataInterface *DataInterfaceCDO = Cast<UOptimusComputeDataInterface>(DataInterfaceClass->GetDefaultObject());

		if (ensure(DataInterfaceCDO))
		{
			SetDisplayName(FText::FromString(DataInterfaceCDO->GetDisplayName()));
			CreatePinsFromDataInterface(DataInterfaceCDO);
		}
	}
}


void UOptimusNode_DataInterface::CreatePinsFromDataInterface(
	UOptimusComputeDataInterface* InDataInterface
	)
{
	// A data interface provides read and write functions. A data interface node exposes
	// the read functions as output pins to be fed into kernel nodes (or into other interface
	// nodes' write functions). Conversely all write functions are exposed as input pins,
	// since the data is being written to.
	const TArray<FOptimusCDIPinDefinition> PinDefinitions = InDataInterface->GetPinDefinitions();

	TArray<FShaderFunctionDefinition> ReadFunctions;
	InDataInterface->GetSupportedInputs(ReadFunctions);
	
	TMap<FString, const FShaderFunctionDefinition *> ReadFunctionMap;
	for (const FShaderFunctionDefinition& Def: ReadFunctions)
	{
		ReadFunctionMap.Add(Def.Name, &Def);
	}

	TArray<FShaderFunctionDefinition> WriteFunctions;
	InDataInterface->GetSupportedOutputs(WriteFunctions);
	
	TMap<FString, const FShaderFunctionDefinition *> WriteFunctionMap;
	for (const FShaderFunctionDefinition& Def: WriteFunctions)
	{
		WriteFunctionMap.Add(Def.Name, &Def);
	}
	
	for (const FOptimusCDIPinDefinition& Def: PinDefinitions)
	{
		if (ensure(!Def.PinName.IsNone()))
		{
			CreatePinFromDefinition(Def, ReadFunctionMap, WriteFunctionMap);
		}
	}
}


void UOptimusNode_DataInterface::CreatePinFromDefinition(
	const FOptimusCDIPinDefinition& InDefinition,
	const TMap<FString, const FShaderFunctionDefinition*>& InReadFunctionMap,
	const TMap<FString, const FShaderFunctionDefinition*>& InWriteFunctionMap
	)
{
	// TBD: Context
	const FOptimusDataTypeRegistry& TypeRegistry = FOptimusDataTypeRegistry::Get();

	// If there's no count function, then we have a value pin. The data function should
	// have a return parameter but no input parameters. The value function only exists in 
	// the read function map and so can only be an output pin.
	if (InDefinition.Contexts.IsEmpty())
	{
		if (!InReadFunctionMap.Contains(InDefinition.DataFunctionName))
		{
			UE_LOG(LogOptimusCore, Error, TEXT("Data function %s given for pin %s in %s does not exist"),
				*InDefinition.DataFunctionName, *InDefinition.PinName.ToString(), *DataInterfaceClass->GetName());
			return;
		}

		const FShaderFunctionDefinition* FuncDef = InReadFunctionMap[InDefinition.DataFunctionName];
		if (!FuncDef->bHasReturnType || FuncDef->ParamTypes.Num() != 1)
		{
			UE_LOG(LogOptimusCore, Error, TEXT("Data function %s given for pin %s in %s does not return a single value"),
				*InDefinition.DataFunctionName, *InDefinition.PinName.ToString(), *DataInterfaceClass->GetName());
			return;
		}

		const FShaderValueTypeHandle ValueTypeHandle = FuncDef->ParamTypes[0].ValueType;
		const FOptimusDataTypeRef PinDataType = TypeRegistry.FindType(ValueTypeHandle);
		if (!PinDataType.IsValid())
		{
			UE_LOG(LogOptimusCore, Error, TEXT("Data function %s given for pin %s in %s uses unsupported type '%s'"),
				*InDefinition.DataFunctionName, *InDefinition.PinName.ToString(), *DataInterfaceClass->GetName(),
				*ValueTypeHandle->ToString());
			return;
		}

		AddPinDirect(InDefinition.PinName, EOptimusNodePinDirection::Output, {}, PinDataType);
	}
	else if (!InDefinition.DataFunctionName.IsEmpty())
	{
		// The count function is always in the read function list.
		for (const FOptimusCDIPinDefinition::FContextInfo& ContextInfo: InDefinition.Contexts)
		{
			if (!InReadFunctionMap.Contains(ContextInfo.CountFunctionName))
			{
				UE_LOG(LogOptimusCore, Error, TEXT("Count function %s given for pin %s in %s does not exist"),
					*ContextInfo.CountFunctionName, *InDefinition.PinName.ToString(), *DataInterfaceClass->GetName());
				return;
			}
		}

		FShaderValueTypeHandle ValueTypeHandle;
		EOptimusNodePinDirection PinDirection;
		
		if (InReadFunctionMap.Contains(InDefinition.DataFunctionName))
		{
			PinDirection = EOptimusNodePinDirection::Output;
			const FShaderFunctionDefinition* FuncDef = InReadFunctionMap[InDefinition.DataFunctionName];

			// FIXME: Ensure it takes a scalar uint/int as input index.
			if (!FuncDef->bHasReturnType || FuncDef->ParamTypes.Num() != (1 + InDefinition.Contexts.Num()))
			{
				UE_LOG(LogOptimusCore, Error, TEXT("Data read function %s given for pin %s in %s is not properly declared."),
					*InDefinition.DataFunctionName, *InDefinition.PinName.ToString(), *DataInterfaceClass->GetName());
				return;
			}

			// The return type dictates the pin type.
			ValueTypeHandle = FuncDef->ParamTypes[0].ValueType;
		}
		else if (InWriteFunctionMap.Contains(InDefinition.DataFunctionName))
		{
			PinDirection = EOptimusNodePinDirection::Input;
			
			const FShaderFunctionDefinition* FuncDef = InWriteFunctionMap[InDefinition.DataFunctionName];

			// FIXME: Ensure it takes a scalar uint/int as input index.
			if (FuncDef->bHasReturnType || FuncDef->ParamTypes.Num() != (1 + InDefinition.Contexts.Num()))
			{
				UE_LOG(LogOptimusCore, Error, TEXT("Data write function %s given for pin %s in %s is not properly declared."),
					*InDefinition.DataFunctionName, *InDefinition.PinName.ToString(), *DataInterfaceClass->GetName());
				return;
			}

			// The second argument dictates the pin type.
			ValueTypeHandle = FuncDef->ParamTypes[1].ValueType;
		}
		else
		{
			UE_LOG(LogOptimusCore, Error, TEXT("Data function %s given for pin %s in %s does not exist"),
				*InDefinition.DataFunctionName, *InDefinition.PinName.ToString(), *DataInterfaceClass->GetName());
			return;
		}

		const FOptimusDataTypeRef PinDataType = TypeRegistry.FindType(ValueTypeHandle);
		if (!PinDataType.IsValid())
		{
			UE_LOG(LogOptimusCore, Error, TEXT("Data function %s given for pin %s in %s uses unsupported type '%s'"),
				*InDefinition.DataFunctionName, *InDefinition.PinName.ToString(), *DataInterfaceClass->GetName(),
				*ValueTypeHandle->ToString());
			return;
		}

		TArray<FName> ContextNames;
		for (const FOptimusCDIPinDefinition::FContextInfo& ContextInfo: InDefinition.Contexts)
		{
			ContextNames.Add(ContextInfo.ContextName);
		}

		const FOptimusNodePinStorageConfig StorageConfig(ContextNames);
		AddPinDirect(InDefinition.PinName, PinDirection, StorageConfig, PinDataType);
	}
	else
	{
		UE_LOG(LogOptimusCore, Error, TEXT("No data function given for pin %s in %s"),
			*InDefinition.PinName.ToString(), *DataInterfaceClass->GetName());
	}
}
