// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NameTypes.h"

#include "ShaderParamTypeDefinition.generated.h"


class FArchive;
struct FShaderValueType;


/* The base types of data that shaders can consume/expose */
UENUM()
enum class EShaderFundamentalType : uint8
{
	Bool,
	Int,
	Uint,
	Float,
	Struct
};

/*
 * Shader types can then be in the form of a scalar, vector, matrix.
 * e.g Scalar: float a; 	Vector: float3 n; 		Matrix: float3x4 WVP;
 * Note: float b[5]; is still considered scalar. It is an array of scalars.
 */
UENUM()
enum class EShaderFundamentalDimensionType : uint8
{
	Scalar,
	Vector,
	Matrix,
};

/* Describes how the shader parameters are bound. */
UENUM()
enum class EShaderParamBindingType : uint8
{
	ConstantParameter,
	ReadOnlyResource,  // SRV, treated as Inputs
	ReadWriteResource, // UAV, treated as Outputs
};

UENUM()
enum class EShaderResourceType : uint8
{
	Texture1D,
	Texture2D,
	Texture3D,
	TextureCube,
	Buffer,
	StructuredBuffer,
	ByteAddressBuffer,
};

USTRUCT()
struct FShaderValueTypeHandle
{
	GENERATED_BODY()

	const FShaderValueType *ValueTypePtr = nullptr;

	bool IsValid() const
	{
		return ValueTypePtr != nullptr;
	}

	explicit operator bool() const
	{
		return IsValid();
	}

	const FShaderValueType *operator->() const
	{
		return ValueTypePtr;
	}

	const FShaderValueType &operator*() const
	{
		return *ValueTypePtr;
	}

};


USTRUCT()
struct ENGINE_API FShaderValueType
{
	GENERATED_BODY()

	// A simple container representing a single, named element in a shader value struct.
	struct FStructElement
	{
		FStructElement() = default;
		FStructElement(FName InName, FShaderValueTypeHandle InType)
		    : Name(InName), Type(InType) {}

		bool operator==(const FStructElement &InOther) const
		{
			return Name == InOther.Name && Type.ValueTypePtr == InOther.Type.ValueTypePtr;
		}

		bool operator!=(const FStructElement& InOther) const { return !(*this == InOther); }

		friend FArchive& operator<<(FArchive& InArchive, FStructElement& InElement);

		FName Name;
		FShaderValueTypeHandle Type;
	};



	/** Returns a scalar value type. If the fundamental type given is invalid for scalar values 
	  * (e.g. struct), then this function returns a nullptr. 
	  */
	static FShaderValueTypeHandle Get(EShaderFundamentalType InType);

	/** Returns a vector value type. InElemCount can be any value between 1-4. If the type 
	  * given is invalid for scalar values (e.g. struct) or InElemCount is out of range, then 
	  * this function returns a nullptr. 
	  */
	static FShaderValueTypeHandle Get(EShaderFundamentalType InType, int32 InElemCount);

	/** Constructor for vector values. InElemCount can be any value between 1-4 */
	static FShaderValueTypeHandle Get(EShaderFundamentalType InType, int32 InRowCount, int32 InColumnCount);

	static FShaderValueTypeHandle Get(FName InName, std::initializer_list<FStructElement> InStructElements);

	/** Returns true if this type and the other type are exactly equal. */
	bool operator==(const FShaderValueType &InOtherType) const;

	/** Returns true if this type and the other type are not equal. */
	bool operator!=(const FShaderValueType& InOtherType) const
	{
		return !(*this == InOtherType);
	}

	friend FArchive& operator<<(FArchive& InArchive, FShaderValueTypeHandle& InHandle);

	/** Returns the type name as a string (e.g. 'vector2', 'matrix2x3' or 'struct_name') for 
	    use in variable declarations. */
	FString ToString() const;

	/** Returns the type declaration if this type is a struct, or the empty string if not. */
	FString GetTypeDeclaration() const;

	UPROPERTY()
	EShaderFundamentalType Type;

	UPROPERTY()
	EShaderFundamentalDimensionType DimensionType;

	union
	{
		uint8 VectorElemCount;

		struct
		{
			uint8 MatrixRowCount;
			uint8 MatrixColumnCount;
		};
	};

	UPROPERTY()
	FName Name;

	TArray<FStructElement> StructElements;

private:
	static FShaderValueTypeHandle GetOrCreate(FShaderValueType &&InValueType);
};

/**
* A hashing function to allow the FShaderValueType class to be used with hashing containers (e.g.
* TSet or TMap).
*/
uint32 GetTypeHash(const FShaderValueType& InShaderValueType);


/* Fully describes the name and type of a parameter a shader exposes. */
USTRUCT()
struct FShaderParamTypeDefinition
{
	GENERATED_BODY()

public:
	static EShaderFundamentalType ParseFundamental(
		const FString& Str
		);

	static EShaderFundamentalDimensionType ParseDimension(
		const FString& Str
		);

	static uint8 ParseVectorDimension(
		const FString& Str
		);

	static FIntVector2 ParseMatrixDimension(
		const FString& Str
		);

	static EShaderResourceType ParseResource(
		const FString& Str
		);

public:
	UPROPERTY(VisibleAnywhere, Category = "Kernel")
	FString TypeDeclaration;

	UPROPERTY(EditAnywhere, Category = "Kernel")
	FString	Name;

	// The value type for this definition.
	UPROPERTY()
	FShaderValueTypeHandle ValueType;

	UPROPERTY()
	uint16 ArrayElementCount; // 0 indicates not an array. >= 1 indicates an array

	UPROPERTY()
	EShaderFundamentalType FundamentalType;

	UPROPERTY()
	EShaderFundamentalDimensionType	DimType;

	UPROPERTY()
	EShaderParamBindingType BindingType;

	UPROPERTY()
	EShaderResourceType	ResourceType;

	union
	{
		uint8 VectorDimension : 3;

		struct
		{
			uint8 MatrixColumnCount : 2;
			uint8 MatrixRowCount : 2;
		};
	};

	bool IsAnyBufferType() const
	{
		return
			ResourceType == EShaderResourceType::Buffer ||
			ResourceType == EShaderResourceType::ByteAddressBuffer ||
			ResourceType == EShaderResourceType::StructuredBuffer;
	}

	bool IsAnyTextureType() const
	{
		return
			ResourceType == EShaderResourceType::Texture1D ||
			ResourceType == EShaderResourceType::Texture2D ||
			ResourceType == EShaderResourceType::Texture3D ||
			ResourceType == EShaderResourceType::TextureCube;
	}

	/* Determines if the type definition is valid according to HLSL rules. */
	bool IsValid() const
	{
		if (FundamentalType == EShaderFundamentalType::Struct && DimType != EShaderFundamentalDimensionType::Scalar)
		{
			// cannot have anything but scalar struct types
			return false;
		}

		if (IsAnyTextureType() && FundamentalType == EShaderFundamentalType::Struct)
		{
			// cannot have textures of structs
			return false;
		}

		if (IsAnyTextureType() && DimType == EShaderFundamentalDimensionType::Matrix)
		{
			// cannot have textures of matrices
			return false;
		}

		if ((IsAnyBufferType() || IsAnyTextureType()) && BindingType == EShaderParamBindingType::ConstantParameter)
		{
			// cannot have buffers and textures bound as const params
			return false;
		}

		return true;
	}

	void ResetTypeDeclaration(
		);
};
