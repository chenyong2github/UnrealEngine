// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Object.h"
#include "NNXTypes.h"
#include "NNXRuntime.h"
#include "NNXRuntimeFormat.generated.h"

UENUM()
enum class EMLFormatTensorType : uint8
{
	None,	
	Input,
	Output,
	Intermediate
};

USTRUCT()
struct FMLFormatOperatorDesc
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(VisibleAnywhere, Category = "Neural Network Inference")
	FString TypeName;			//!< For example "Relu"

	UPROPERTY(VisibleAnywhere, Category = "Neural Network Inference")
	TArray<uint32> InTensors;

	UPROPERTY(VisibleAnywhere, Category = "Neural Network Inference")
	TArray<uint32> OutTensors;

	//UPROPERTY(VisibleAnywhere, Category = "Neural Network Inference")
	//FString InstanceName;		//!< For example "Relu1"

	/*
	UPROPERTY(VisibleAnywhere, Category = "Neural Network Inference")
	uint32 InTensorStart;

	UPROPERTY(VisibleAnywhere, Category = "Neural Network Inference")
	uint32 InTensorCount;

	UPROPERTY(VisibleAnywhere, Category = "Neural Network Inference")
	uint32 OutTensorStart;

	UPROPERTY(VisibleAnywhere, Category = "Neural Network Inference")
	uint32 OutTensorCount;
	*/
};

USTRUCT()
struct FMLFormatTensorShapeDesc
{
	static constexpr int32 MaxDimension = NNX::FMLTensorDesc::MaxTensorDimension;

	GENERATED_USTRUCT_BODY()

	UPROPERTY(VisibleAnywhere, Category = "Neural Network Inference")
	uint32	Sizes[MaxDimension];

	UPROPERTY(VisibleAnywhere, Category = "Neural Network Inference")
	uint32	Dimension;

	template<typename T>
	void operator=(TArrayView<const T> OtherShape)
	{
		Dimension = OtherShape.Num();
		if (Dimension > MaxDimension)
		{
			Dimension = MaxDimension;
		}

		for (uint32 Idx = 0; Idx < Dimension; ++Idx)
		{
			Sizes[Idx] = static_cast<uint32>(OtherShape[Idx]);
		}
	}

	int32 Volume() const
	{
		int32 Result = 1;

		for (uint32 Idx = 0; Idx < Dimension; ++Idx)
		{
			Result *= Sizes[Idx];
		}

		return Result;
	}
};

USTRUCT()
struct FMLFormatTensorDesc
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(VisibleAnywhere, Category = "Neural Network Inference")
	FString Name;

	UPROPERTY(VisibleAnywhere, Category = "Neural Network Inference")
	FMLFormatTensorShapeDesc	Shape;

	UPROPERTY(VisibleAnywhere, Category = "Neural Network Inference")
	EMLFormatTensorType	Type;

	UPROPERTY(VisibleAnywhere, Category = "Neural Network Inference")
	EMLTensorDataType	DataType;

	UPROPERTY(VisibleAnywhere, Category = "Neural Network Inference")
	uint64	DataSize;

	UPROPERTY(VisibleAnywhere, Category = "Neural Network Inference")
	uint64	DataOffset;
};

/// NNX Runtime format
USTRUCT()
struct FMLRuntimeFormat
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(VisibleAnywhere, Category = "Neural Network Inference")
	TArray<FMLFormatTensorDesc> Tensors;

	UPROPERTY(VisibleAnywhere, Category = "Neural Network Inference")
	TArray<FMLFormatOperatorDesc> Operators;

	UPROPERTY(VisibleAnywhere, Category = "Neural Network Inference")
	TArray<uint8> TensorData;
};
