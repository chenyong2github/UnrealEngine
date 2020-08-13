// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ShaderParamTypeDefinition.generated.h"

/* The base types of data that shaders can consume/expose */
UENUM()
enum class EShaderFundamentalType : uint8
{
	Bool,
	Int,
	Uint,
	Float,
	Double,
	Struct,
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

/* Fully describes the name and type of a parameter a shader exposes. */
USTRUCT()
struct FShaderParamTypeDefinition
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Kernel")
	FString	Name;

// #TODO_ZABIR: Shader reflection needs to extract much richer type info. Currently skip param type validation
#if 0
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
		uint8 VectorElemCount : 2;

		struct
		{
			uint8 MatrixRowCount : 2;
			uint8 MatrixColumnCount : 2;
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
#endif
};
