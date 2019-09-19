// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "RigVMModel/Nodes/RigVMVariableNode.h"

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
	return VariableName;
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

UScriptStruct* URigVMVariableNode::GetScriptStruct() const
{
	URigVMPin* ValuePin = FindPin(ValueName);
	if (ValuePin == nullptr)
	{
		return nullptr;
	}
	return ValuePin->GetScriptStruct();
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
	Variable.ScriptStruct = GetScriptStruct();
	Variable.DefaultValue = GetDefaultValue();
	return Variable;
}
