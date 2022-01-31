// Copyright Epic Games, Inc. All Rights Reserved.

#include "Nodes/OptimusNode_GetVariable.h"

#include "OptimusCoreModule.h"
#include "OptimusNodePin.h"
#include "OptimusVariableDescription.h"

void UOptimusNode_GetVariable::SetVariableDescription(UOptimusVariableDescription* InVariableDesc)
{
	if (!ensure(InVariableDesc))
	{
		return;
	}

	if (!EnumHasAnyFlags(InVariableDesc->DataType->UsageFlags, EOptimusDataTypeUsageFlags::Variable))
	{
		UE_LOG(LogOptimusCore, Error, TEXT("Data type '%s' is not usable in a resource"),
		    *InVariableDesc->DataType->TypeName.ToString());
		return;
	}

	VariableDesc = InVariableDesc;
}


UOptimusVariableDescription* UOptimusNode_GetVariable::GetVariableDescription() const
{
	return VariableDesc.Get();
}


TArray<uint8> UOptimusNode_GetVariable::GetShaderValue() const
{
	if (const UOptimusVariableDescription* Var = VariableDesc.Get();
		Var && ensure(Var->DataType.IsValid()) && ensure(GetPins().Num() == 1))
	{
		if (TArray<uint8> ValueResult;
			Var->DataType->ConvertPropertyValueToShader(Var->ValueData, ValueResult))
		{
			return ValueResult;
		}
	}

	return {};
}


void UOptimusNode_GetVariable::ConstructNode()
{
	if (const UOptimusVariableDescription *Var = VariableDesc.Get())
	{
		AddPinDirect(
			Var->VariableName, 
			EOptimusNodePinDirection::Output,
			{},
			Var->DataType);
	}
}
