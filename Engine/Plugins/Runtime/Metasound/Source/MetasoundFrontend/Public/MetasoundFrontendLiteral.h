// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MetasoundLiteral.h"

#include <type_traits>

#include "MetasoundFrontendLiteral.generated.h"

// The type of a given literal for an input value.
//
// The EMetasoundFrontendLiteralType's are matched to Metasound::ELiteralType`s 
// by giving them the same value. This supports easy conversion from one type to
// another.
UENUM()
enum class EMetasoundFrontendLiteralType : uint8
{

	None = static_cast<uint8>(Metasound::ELiteralType::None), //< A value of None expresses that an object being constructed with a literal should be default constructed.
	Boolean = static_cast<uint8>(Metasound::ELiteralType::Boolean),
	Integer = static_cast<uint8>(Metasound::ELiteralType::Integer),
	Float = static_cast<uint8>(Metasound::ELiteralType::Float),
	String = static_cast<uint8>(Metasound::ELiteralType::String),
	UObject = static_cast<uint8>(Metasound::ELiteralType::UObjectProxy),

	NoneArray = static_cast<uint8>(Metasound::ELiteralType::NoneArray), //< A NoneArray expresses the number of objects to be default constructed.
	BooleanArray = static_cast<uint8>(Metasound::ELiteralType::BooleanArray),
	IntegerArray = static_cast<uint8>(Metasound::ELiteralType::IntegerArray),
	FloatArray = static_cast<uint8>(Metasound::ELiteralType::FloatArray),
	StringArray = static_cast<uint8>(Metasound::ELiteralType::StringArray),
	UObjectArray = static_cast<uint8>(Metasound::ELiteralType::UObjectProxyArray),


	Invalid UMETA(Hidden)
};

// Check that the static_cast<>s above are using the correct type.
static_assert(std::is_same<uint8, std::underlying_type<EMetasoundFrontendLiteralType>::type>::value, "Update type in static_cast<TYPE> from Metasound::ELiteralType to EMetasoundFrontendLiteralType in EMetasoundFrontendLiteralType declaration.");
static_assert(std::is_same<std::underlying_type<Metasound::ELiteralType>::type, std::underlying_type<EMetasoundFrontendLiteralType>::type>::value, "EMetasoundFrontendLiteralType and Metasound::ELiteralType must have matching underlying types to support conversion.");


// Represents the serialized version of variant literal types. 
USTRUCT()
struct METASOUNDFRONTEND_API FMetasoundFrontendLiteral
{
	GENERATED_BODY()

	struct FDefault {};
	struct FDefaultArray { int32 Num = 0; };

private:
	// The set type of this literal.
	UPROPERTY()
	EMetasoundFrontendLiteralType Type = EMetasoundFrontendLiteralType::None;

	UPROPERTY()
	int32 AsNumDefault = 0;

	UPROPERTY()
	TArray<bool> AsBoolean;

	UPROPERTY()
	TArray<int32> AsInteger;

	UPROPERTY()
	TArray<float> AsFloat;

	UPROPERTY()
	TArray<FString> AsString;

	UPROPERTY()
	TArray<UObject*> AsUObject;

public:
	// Returns true if the stored Type is an array type.
	bool IsArray() const;

	// Sets the literal to the given type and value to default;
	void SetType(EMetasoundFrontendLiteralType InType);

	void Set(FDefault InValue);
	void Set(const FDefaultArray& InValue);
	void Set(bool InValue);
	void Set(const TArray<bool>& InValue);
	void Set(int32 InValue);
	void Set(const TArray<int32>& InValue);
	void Set(float InValue);
	void Set(const TArray<float>& InValue);
	void Set(const FString& InValue);
	void Set(const TArray<FString>& InValue);
	void Set(UObject* InValue);
	void Set(const TArray<UObject*>& InValue);

	void SetFromLiteral(const Metasound::FLiteral& InLiteral);

	EMetasoundFrontendLiteralType GetType() const;

	// Return the literal description parsed into a init param. 
	// @Returns an invalid init param if the data type couldn't be found, or if the literal type was incompatible with the data type.
	Metasound::FLiteral ToLiteral(const FName& InMetasoundDataType) const;

	// Does not do type checking or handle proxy
	Metasound::FLiteral ToLiteralNoProxy() const;

	// Convert the value to a string for printing. 
	FString ToString() const;

	// Remove any stored data and set to an invalid state.
	void Clear();

private:
	// Remove all values. 
	void Empty();
};

namespace Metasound
{
	namespace Frontend
	{
		// Returns true if the data type can be constructed using the literal type.
		METASOUNDFRONTEND_API bool DoesDataTypeSupportLiteralType(FName InDataType, EMetasoundFrontendLiteralType InLiteralType);

		// Returns true if the data type can be constructed using the literal type.
		METASOUNDFRONTEND_API bool DoesDataTypeSupportLiteralType(FName InDataType, ELiteralType InLiteralType);

		// Returns the defaulted version of a literal param for the given data type.
		// @Returns an invalid init param if the data type couldn't be found.
		METASOUNDFRONTEND_API FLiteral GetDefaultParamForDataType(FName InDataType);

		// Convenience function to convert Metasound::ELiteralType to EMetasoundFrontendLiteralType.
		METASOUNDFRONTEND_API EMetasoundFrontendLiteralType GetMetasoundFrontendLiteralType(ELiteralType InLiteralType);

		// Convenience function to convert EMetasoundFrontendLiteralType to Metasound::ELiteralType.
		METASOUNDFRONTEND_API ELiteralType GetMetasoundLiteralType(EMetasoundFrontendLiteralType InLiteralType);
	}
}

