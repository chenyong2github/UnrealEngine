// Copyright Epic Games, Inc. All Rights Reserved.

#include "Nodes/OptimusNode_GetVariable.h"

#include "OptimusCoreModule.h"
#include "OptimusDeformer.h"
#include "OptimusNodePin.h"
#include "OptimusVariableDescription.h"

#define LOCTEXT_NAMESPACE "OptimusGetVariable"

void UOptimusNode_GetVariable::PostDuplicate(EDuplicateMode::Type DuplicateMode)
{
	Super::PostDuplicate(DuplicateMode);

	if (!VariableDesc.IsValid())
	{
		return;
	}
	
	if (!GetOwningGraph())
	{
		return;
	}
	
	const UOptimusDeformer* OldDescOwner = VariableDesc->GetOwningDeformer();
	const UOptimusDeformer* NewDescOwner = Cast<UOptimusDeformer>(GetOwningGraph()->GetCollectionRoot());

	if (!NewDescOwner)
	{
		return;
	}
	
	// No action needed if we are copying/pasting within the same deformer asset 
	if (OldDescOwner == NewDescOwner)
	{
		return;
	}

	// Refresh the ResourceDesc so that we don't hold a reference to a VariableDesc in another deformer asset
	VariableDesc = NewDescOwner->ResolveVariable(VariableDesc->GetFName());
}

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


TOptional<FText> UOptimusNode_GetVariable::ValidateForCompile() const
{
	const UOptimusVariableDescription* VariableDescription = GetVariableDescription();
	if (!VariableDescription)
	{
		return LOCTEXT("NoDescriptor", "No variable descriptor set on this node");
	}

	return {};
}

FString UOptimusNode_GetVariable::GetValueName() const
{
	if (const UOptimusVariableDescription* Var = VariableDesc.Get())
	{
		return Var->VariableName.GetPlainNameString();
	}

	return {};
}


FOptimusDataTypeRef UOptimusNode_GetVariable::GetValueType() const
{
	if (const UOptimusVariableDescription* Var = VariableDesc.Get())
	{
		return Var->DataType;
	}

	return {};
}


FShaderValueType::FValue UOptimusNode_GetVariable::GetShaderValue() const
{
	if (const UOptimusVariableDescription* Var = VariableDesc.Get();
		Var && ensure(Var->DataType.IsValid()) && ensure(GetPins().Num() == 1))
	{
		FShaderValueType::FValue ValueResult = Var->DataType->MakeShaderValue();

		if (Var->DataType->ConvertPropertyValueToShader(Var->ValueData, ValueResult))
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

#undef LOCTEXT_NAMESPACE