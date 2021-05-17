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
	Node				= 0,
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
struct FOptimusDataType
{
	GENERATED_BODY()

	FOptimusDataType() = default;

	UPROPERTY()
	FName TypeName;

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
	FLinearColor CustomPinColor;
	
	UPROPERTY()
	EOptimusDataTypeUsageFlags UsageFlags = EOptimusDataTypeUsageFlags::Node;

	UPROPERTY()
	EOptimusDataTypeFlags TypeFlags = EOptimusDataTypeFlags::None;
};


using FOptimusDataTypeHandle = TSharedPtr<const FOptimusDataType>;


/** A reference object for an Optimus data type to use in UObjects and other UStruct-like things */
USTRUCT(BlueprintType)
struct OPTIMUSDEVELOPER_API FOptimusDataTypeRef
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
