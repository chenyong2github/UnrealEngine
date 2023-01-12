// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Object.h"
#include "NNECoreAttributeValue.h"
#include "NNECoreTypes.h"
#include "NNXRuntime.h"
#include "NNXRuntimeFormat.generated.h"

UENUM()
enum class EMLFormatTensorType : uint8
{
	None,	
	Input,
	Output,
	Intermediate,
	Initializer,

	NUM
};

//TODO jira 167589: convert EMLInferenceFormat to a CCCC for easier extension of the framework
UENUM()
enum class ENNXInferenceFormat : uint8
{
	Invalid,
	ONNX,				//!< ONNX Open Neural Network Exchange
	ORT,				//!< ONNX Runtime (only for CPU)
	NNXRT				//!< NNX Runtime format
};

USTRUCT()
struct FNNIModelRaw
{
	GENERATED_USTRUCT_BODY()
	
	UPROPERTY(VisibleAnywhere, Category = "Neural Network Inference")
	TArray<uint8>		Data;
	
	UPROPERTY(VisibleAnywhere, Category = "Neural Network Inference")
	ENNXInferenceFormat	Format { ENNXInferenceFormat::Invalid };
};

// Required by LoadModel() when loading operators in HLSL and DirectML runtime
USTRUCT()
struct FMLFormatAttributeDesc
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(VisibleAnywhere, Category = "Neural Network Inference")
	FString Name;

	UPROPERTY(VisibleAnywhere, Category = "Neural Network Inference")
	FNNEAttributeValue Value;
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

	UPROPERTY(VisibleAnywhere, Category = "Neural Network Inference")
	TArray<FMLFormatAttributeDesc> Attributes;
};

USTRUCT()
struct FMLFormatTensorDesc
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(VisibleAnywhere, Category = "Neural Network Inference")
	FString Name;

	UPROPERTY(VisibleAnywhere, Category = "Neural Network Inference")
	TArray<int32> Shape;

	UPROPERTY(VisibleAnywhere, Category = "Neural Network Inference")
	EMLFormatTensorType	Type = EMLFormatTensorType::None;

	UPROPERTY(VisibleAnywhere, Category = "Neural Network Inference")
	ENNETensorDataType	DataType = ENNETensorDataType::None;

	UPROPERTY(VisibleAnywhere, Category = "Neural Network Inference")
	uint64	DataSize = 0;

	UPROPERTY(VisibleAnywhere, Category = "Neural Network Inference")
	uint64	DataOffset = 0;
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