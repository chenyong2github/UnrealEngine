// Copyright Epic Games, Inc. All Rights Reserved.

#include "OptimusDataType.h"

#include "OptimusDataTypeRegistry.h"


FOptimusDataTypeRef::FOptimusDataTypeRef(
	FOptimusDataTypeHandle InTypeHandle
	)
{
	Set(InTypeHandle);
}


void FOptimusDataTypeRef::Set(
	FOptimusDataTypeHandle InTypeHandle
	)
{
	if (InTypeHandle.IsValid())
	{
		TypeName = InTypeHandle->TypeName;
		checkSlow(FOptimusDataTypeRegistry::Get().FindType(TypeName) != nullptr);
	}
	else
	{
		TypeName = NAME_None;
	}
}


FOptimusDataTypeHandle FOptimusDataTypeRef::Resolve() const
{
	return FOptimusDataTypeRegistry::Get().FindType(TypeName);
}


FProperty* FOptimusDataType::CreateProperty(
	UStruct* InScope, 
	FName InName
	) const
{
	return FOptimusDataTypeRegistry::Get().CreateProperty(TypeName, InScope, InName);
}
