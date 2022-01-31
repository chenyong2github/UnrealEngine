// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComputeFramework/ShaderParamTypeDefinition.h"
#include "UObject/ObjectMacros.h"
#include "UObject/NameTypes.h"
#include "Misc/EnumClassFlags.h"

#include "OptimusDataType.generated.h"


/** These flags govern how the data type can be used */
UENUM(meta = (Bitflags))
enum class EOptimusDataTypeUsageFlags : uint8
{
	None				= 0,
	
	Resource			= 1 << 0,		/** This type can be used in a resource */
	Variable			= 1 << 1,		/** This type can be used in a variable */
};
ENUM_CLASS_FLAGS(EOptimusDataTypeUsageFlags)


/** These flags are for indicating type behaviour */
UENUM(meta = (Bitflags))
enum class EOptimusDataTypeFlags : uint8
{
	None = 0,
	
	IsStructType		= 1 << 0,		/** This is a UScriptStruct-based type. */
	ShowElements		= 1 << 1,		/** If a struct type, show the struct elements. */
};
ENUM_CLASS_FLAGS(EOptimusDataTypeFlags)


USTRUCT()
struct OPTIMUSCORE_API FOptimusDataType
{
	GENERATED_BODY()

	FOptimusDataType() = default;

	// Create an FProperty with the given scope and name, but only if the UsageFlags contains 
	// EOptimusDataTypeUsageFLags::Variable. Otherwise it returns a nullptr.
	FProperty* CreateProperty(
		UStruct *InScope,
		FName InName
		) const;

	// Convert an FProperty value to a value compatible with the shader parameter data layout.
	// The InValue parameter should point at the memory location governed by the FProperty for
	// this data type, and OutConvertedValue is an array to store the bytes for the converted
	// value. If the function failed, a nullptr is returned and the state of the OutConvertedValue
	// is undefined. Upon success, the return value is a pointer to the value following the
	// converted input value, and the converted output value array will have been grown to
	// accommodate the newly converted value.
	bool ConvertPropertyValueToShader(
		TArrayView<const uint8> InValue,
		TArray<uint8>& OutConvertedValue
		) const;

	// Returns true if the data type can create a FProperty object to represent it.
	bool CanCreateProperty() const;

	UPROPERTY()
	FName TypeName;

	UPROPERTY()
	FText DisplayName;

	// Shader value type that goes with this Optimus pin type.
	UPROPERTY()
	FShaderValueTypeHandle ShaderValueType;

	UPROPERTY()
	FName TypeCategory;

	UPROPERTY()
	TWeakObjectPtr<UObject> TypeObject;

	UPROPERTY()
	bool bHasCustomPinColor = false;

	UPROPERTY()
	FLinearColor CustomPinColor = FLinearColor::Black;
	
	UPROPERTY()
	EOptimusDataTypeUsageFlags UsageFlags = EOptimusDataTypeUsageFlags::None;

	UPROPERTY()
	EOptimusDataTypeFlags TypeFlags = EOptimusDataTypeFlags::None;
};


using FOptimusDataTypeHandle = TSharedPtr<const FOptimusDataType>;


/** A reference object for an Optimus data type to use in UObjects and other UStruct-like things */
USTRUCT(BlueprintType)
struct OPTIMUSCORE_API FOptimusDataTypeRef
{
	GENERATED_BODY()

	FOptimusDataTypeRef(FOptimusDataTypeHandle InTypeHandle = {});

	bool IsValid() const
	{
		return !TypeName.IsNone();
	}

	explicit operator bool() const
	{
		return IsValid();
	}

	void Set(FOptimusDataTypeHandle InTypeHandle);

	FOptimusDataTypeHandle Resolve() const;

	const FOptimusDataType* operator->() const
	{
		return Resolve().Get();
	}

	const FOptimusDataType& operator*() const
	{
		return *Resolve().Get();
	}

	bool operator==(const FOptimusDataTypeRef& InOther) const
	{
		return TypeName == InOther.TypeName;
	}

	bool operator!=(const FOptimusDataTypeRef& InOther) const
	{
		return TypeName != InOther.TypeName;
	}

	UPROPERTY(EditAnywhere, Category=Type)
	FName TypeName;
};
