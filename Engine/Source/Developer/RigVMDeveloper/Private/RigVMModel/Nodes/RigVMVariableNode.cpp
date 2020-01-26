// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigVMModel/Nodes/RigVMVariableNode.h"

const FString URigVMVariableNode::VariableName = TEXT("Variable");
const FString URigVMVariableNode::ValueName = TEXT("Value");

URigVMVariableNode::URigVMVariableNode()
{
}

FString URigVMVariableNode::GetNodeTitle() const
{
	if (IsGetter())
	{
		return FString::Printf(TEXT("Get %s"), *GetVariableName().ToString());
	}
	return FString::Printf(TEXT("Set %s"), *GetVariableName().ToString());
}

FName URigVMVariableNode::GetVariableName() const
{
	URigVMPin* VariablePin = FindPin(VariableName);
	if (VariablePin == nullptr)
	{
		return NAME_None;
	}
	return *VariablePin->GetDefaultValue();
}

bool URigVMVariableNode::IsGetter() const
{
	URigVMPin* ValuePin = FindPin(ValueName);
	if (ValuePin == nullptr)
	{
		return false;
	}
	return ValuePin->GetDirection() == ERigVMPinDirection::Output;
}

FString URigVMVariableNode::GetCPPType() const
{
	URigVMPin* ValuePin = FindPin(ValueName);
	if (ValuePin == nullptr)
	{
		return FString();
	}
	return ValuePin->GetCPPType();
}

UObject* URigVMVariableNode::GetCPPTypeObject() const
{
	URigVMPin* ValuePin = FindPin(ValueName);
	if (ValuePin == nullptr)
	{
		return nullptr;
	}
	return ValuePin->GetCPPTypeObject();
}

FString URigVMVariableNode::GetDefaultValue() const
{

	URigVMPin* ValuePin = FindPin(ValueName);
	if (ValuePin == nullptr)
	{
		return FString();
	}
	return ValuePin->GetDefaultValue();
}

FRigVMGraphVariableDescription URigVMVariableNode::GetVariableDescription() const
{
	FRigVMGraphVariableDescription Variable;
	Variable.Name = GetVariableName();
	Variable.CPPType = GetCPPType();
	Variable.CPPTypeObject = GetCPPTypeObject();
	Variable.DefaultValue = GetDefaultValue();
	return Variable;
}
