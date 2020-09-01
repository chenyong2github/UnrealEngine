// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComputeFramework/ShaderParamTypeDefinition.h"
#include "UObject/ObjectMacros.h"
#include "UObject/NameTypes.h"
#include "Misc/EnumClassFlags.h"

#include "OptimusDataType.generated.h"


UENUM(meta = (Bitflags))
enum class EOptimusDataTypeFlags : uint32
{
	None				= 0,
	UseInResource		= 1 << 0,		// This type can be used in a resource
	UseInVariable		= 1 << 1,		// This type can be used in a variable

	ShowElements		= 1 << 8,		// If a struct type, show the struct elements.

	UserFlagsMask		= 0x0000FFFF,

	// The following flags are ignored at registration time and set internally.
	IsStructType		= 1 << 16,		// This is a UScriptStruct-based type.

	InternalFlagsMask	= 0xFFFF0000,
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
	FName PinCategory;

	UPROPERTY()
	TWeakObjectPtr<UObject> PinSubCategory;

	UPROPERTY()
	bool bHasCustomPinColor = false;

	UPROPERTY()
	FLinearColor CustomPinColor;
	
	UPROPERTY()
	EOptimusDataTypeFlags Flags;
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
