// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "MetasoundDataReference.h"
#include "MetasoundOutputNode.h"
#include "MetasoundInputNode.h"
#include "MetasoundOperatorInterface.h"
#include "MetasoundFrontendRegistries.h"

typedef TUniqueFunction<TUniquePtr<Metasound::INode>(const ::Metasound::FInputNodeConstructorParams&)> FInputNodeConstructorCallback;
typedef TUniqueFunction<TUniquePtr<Metasound::INode>(const ::Metasound::FOutputNodeConstrutorParams&)> FOutputNodeConstructorCallback;

template<typename TDataType>
bool RegisterDataTypeWithFrontend(::Metasound::ELiteralArgType PreferredArgType = ::Metasound::ELiteralArgType::None)
{
	// if we reenter this code (because DECLARE_METASOUND_DATA_REFERENCE_TYPES was called twice with the same type),
	// we catch it here.
	static bool bAlreadyRegisteredThisDataType = false;
	if (bAlreadyRegisteredThisDataType)
	{
		UE_LOG(LogTemp, Display, TEXT("Tried to call REGISTER_METASOUND_DATATYPE twice with the same class %s. ignoring the second call. Likely because REGISTER_METASOUND_DATATYPE is in a header that's used in multiple modules. Consider moving it to a private header or cpp file."), ::Metasound::TDataReferenceTypeInfo<TDataType>::TypeName)
		return false;
	}

	bAlreadyRegisteredThisDataType = true;

	// Lambdas that generate our template-instantiated input and output nodes:
	FInputNodeConstructorCallback InputNodeConstructor = [](const ::Metasound::FInputNodeConstructorParams& InParams) -> TUniquePtr<Metasound::INode>
	{
		return TUniquePtr<Metasound::INode>(new ::Metasound::TInputNode<TDataType>(InParams.InNodeName, InParams.InVertexName, InParams.InitParam, InParams.InSettings));
	};

	FOutputNodeConstructorCallback OutputNodeConstructor = [](const ::Metasound::FOutputNodeConstrutorParams& InParams) -> TUniquePtr<Metasound::INode>
	{
		return TUniquePtr<Metasound::INode>(new ::Metasound::TOutputNode<TDataType>(InParams.InNodeName, InParams.InVertexName));
	};
	
	static FName DataTypeName = FName(::Metasound::TDataReferenceTypeInfo<TDataType>::TypeName);

	::Metasound::FDataTypeRegistryInfo RegistryInfo;
	RegistryInfo.DataTypeName = DataTypeName;
	RegistryInfo.PreferredLiteralType = PreferredArgType;
	RegistryInfo.bIsBoolParsable = ::Metasound::TDataReferenceTypeInfo<TDataType>::bIsBoolParsable;
	RegistryInfo.bIsIntParsable = ::Metasound::TDataReferenceTypeInfo<TDataType>::bIsIntParsable;
	RegistryInfo.bIsFloatParsable = ::Metasound::TDataReferenceTypeInfo<TDataType>::bIsFloatParsable;
	RegistryInfo.bIsStringParsable = ::Metasound::TDataReferenceTypeInfo<TDataType>::bIsStringParsable;
	RegistryInfo.bIsConstructableWithSettings = ::Metasound::TDataReferenceTypeInfo<TDataType>::bIsConstructableWithSettings;
	RegistryInfo.bIsDefaultConstructible = ::Metasound::TDataReferenceTypeInfo<TDataType>::bCanUseDefaultConstructor;

	bool bSucceeded = FMetasoundFrontendRegistryContainer::Get()->RegisterDataType(RegistryInfo, MoveTemp(InputNodeConstructor), MoveTemp(OutputNodeConstructor));
	ensureAlwaysMsgf(bSucceeded, TEXT("Failed to register data type %s in the node registry!"), ::Metasound::TDataReferenceTypeInfo<TDataType>::TypeName);
	return bSucceeded;
}

// This should be used to expose a datatype as a potential input or output for a metasound graph.
// The first argument to the macro is the class to expose.
// Optionally, a Metasound::ELiteralArgType can be passed in to designate a preferred literal type-
// For example, if Metasound::ELiteralArgType::Float is passed in, we will default to using a float parameter to create this datatype.
// If no argument is passed in, we will infer a literal type to use.
// Metasound::ELiteralArgType::Invalid can be used to enforce that we don't provide space for a literal, in which case you should have a default constructor or a constructor that takes [const FOperatorSettings&] implemented.
// If you pass in a preferred arg type, please make sure that the passed in datatype has a matching constructor, since we won't check this until runtime.
#define REGISTER_METASOUND_DATATYPE(DataType, ...) \
	static_assert(::Metasound::TDataReferenceTypeInfo<DataType>::bIsValidSpecialization, "Please call DECLARE_METASOUND_DATA_REFERENCE_TYPES(" #DataType "...) before calling REGISTER_METASOUND_DATATYPE(" #DataType")."); \
	static constexpr bool bCanRegister##DataType = ::Metasound::TDataReferenceTypeInfo<DataType>::bIsStringParsable || ::Metasound::TDataReferenceTypeInfo<DataType>::bIsBoolParsable || ::Metasound::TDataReferenceTypeInfo<DataType>::bIsIntParsable || ::Metasound::TDataReferenceTypeInfo<DataType>::bIsFloatParsable || ::Metasound::TDataReferenceTypeInfo<DataType>::bIsConstructableWithSettings || ::Metasound::TDataReferenceTypeInfo<DataType>::bCanUseDefaultConstructor; \
	static_assert(bCanRegister##DataType , "To register " #DataType " to be used as a Metasounds input or output type, it needs a default constructor or one of the following constructors must be implemented:  " #DataType "(const ::Metasound::FOperatorSettings& InSettings), " #DataType "(int32 InValue, const ::Metasound::FOperatorSettings& InSettings), " #DataType "(float InValue, const ::Metasound::FOperatorSettings& InSettings), " #DataType "(bool InValue, const ::Metasound::FOperatorSettings& InSettings), or " #DataType "(const FString& InString, const ::Metasound::FOperatorSettings& InSettings)."); \
	static const bool bSuccessfullyRegistered##DataType = FMetasoundFrontendRegistryContainer::Get()->EnqueueInitCommand([](){ ::RegisterDataTypeWithFrontend<DataType>(__VA_ARGS__); }); // This static bool is useful for debugging, but also is the only way the compiler will let us call this function outside of an expression.

