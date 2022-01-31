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
	const FOptimusDataTypeRegistry::PropertyCreateFuncT PropertyCreateFunc =
		FOptimusDataTypeRegistry::Get().FindPropertyCreateFunc(TypeName);

	if (PropertyCreateFunc)
	{
		return PropertyCreateFunc(InScope, InName);
	}
	else
	{
		return nullptr;
	}
}


bool FOptimusDataType::ConvertPropertyValueToShader(
	TArrayView<const uint8> InValue,
	TArray<uint8>& OutConvertedValue
	) const
{
	const FOptimusDataTypeRegistry::PropertyValueConvertFuncT PropertyConversionFunc =
		FOptimusDataTypeRegistry::Get().FindPropertyValueConvertFunc(TypeName);
	if (PropertyConversionFunc)
	{
		return PropertyConversionFunc(InValue, OutConvertedValue);
	}
	else
	{
		return false;
	}
}


bool FOptimusDataType::CanCreateProperty() const
{
	return static_cast<bool>(FOptimusDataTypeRegistry::Get().FindPropertyCreateFunc(TypeName));
}
