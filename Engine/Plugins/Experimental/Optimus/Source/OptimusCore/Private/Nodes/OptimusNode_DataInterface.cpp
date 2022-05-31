// Copyright Epic Games, Inc. All Rights Reserved.

#include "Nodes/OptimusNode_DataInterface.h"

#include "OptimusCoreModule.h"
#include "OptimusNodePin.h"
#include "OptimusDataTypeRegistry.h"

#include "ComputeFramework/ShaderParamTypeDefinition.h"


UOptimusNode_DataInterface::UOptimusNode_DataInterface()
{
}


UOptimusComputeDataInterface* UOptimusNode_DataInterface::GetDataInterface(UObject* InOuter) const
{
	// Legacy data may not have a DataInterfaceData object.
	if (DataInterfaceData == nullptr || !DataInterfaceData->IsA(DataInterfaceClass))
	{
		return NewObject<UOptimusComputeDataInterface>(InOuter, DataInterfaceClass);
	}

	FObjectDuplicationParameters DupParams = InitStaticDuplicateObjectParams(DataInterfaceData, InOuter);
	return Cast<UOptimusComputeDataInterface>(StaticDuplicateObjectEx(DupParams));
}


int32 UOptimusNode_DataInterface::GetDataFunctionIndexFromPin(const UOptimusNodePin* InPin) const
{
	if (!InPin || InPin->GetParentPin() != nullptr)
	{
		return INDEX_NONE;
	}

	// FIXME: This information should be baked into the pin definition so we don't have to
	// look it up repeatedly.
	const TArray<FOptimusCDIPinDefinition> PinDefinitions = DataInterfaceData->GetPinDefinitions();

	int32 PinIndex = INDEX_NONE;
	for (int32 Index = 0 ; Index < PinDefinitions.Num(); ++Index)
	{
		if (InPin->GetUniqueName() == PinDefinitions[Index].PinName)
		{
			PinIndex = Index;
			break;
		}
	}
	if (!ensure(PinIndex != INDEX_NONE))
	{
		return INDEX_NONE;
	}

	const FString FunctionName = PinDefinitions[PinIndex].DataFunctionName;
	
	TArray<FShaderFunctionDefinition> FunctionDefinitions;
	if (InPin->GetDirection() == EOptimusNodePinDirection::Input)
	{
		DataInterfaceData->GetSupportedOutputs(FunctionDefinitions);
	}
	else
	{
		DataInterfaceData->GetSupportedInputs(FunctionDefinitions);
	}
	
	return FunctionDefinitions.IndexOfByPredicate(
		[FunctionName](const FShaderFunctionDefinition& InDef)
		{
			return InDef.Name == FunctionName;
		});
}


void UOptimusNode_DataInterface::SetDataInterfaceClass(
	TSubclassOf<UOptimusComputeDataInterface> InDataInterfaceClass
	)
{
	DataInterfaceClass = InDataInterfaceClass;
	DataInterfaceData = NewObject<UOptimusComputeDataInterface>(this, DataInterfaceClass);
}


void UOptimusNode_DataInterface::PostLoad()
{
	Super::PostLoad();

	// Previously DataInterfaceData wasn't always created.
	if (!DataInterfaceClass.IsNull() && DataInterfaceData.IsNull())
	{
		DataInterfaceData = NewObject<UOptimusComputeDataInterface>(this, DataInterfaceClass);
	}
}


void UOptimusNode_DataInterface::ConstructNode()
{
	if (ensure(!DataInterfaceClass.IsNull()))
	{
		if (ensure(DataInterfaceData))
		{
			SetDisplayName(FText::FromString(DataInterfaceData->GetDisplayName()));
			CreatePinsFromDataInterface(DataInterfaceData);
		}
	}
}


void UOptimusNode_DataInterface::PostDuplicate(EDuplicateMode::Type DuplicateMode)
{
	// Currently duplication doesn't set the correct outer so fix here.
	// We can remove this when duplication handles the outer correctly.
	if (ensure(DataInterfaceData))
	{
		DataInterfaceData->Rename(nullptr, GetOuter());
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
