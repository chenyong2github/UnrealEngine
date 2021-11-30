// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PixelFormat.h"
#include <type_traits>
#include "NeuralEnumClasses.generated.h"

UENUM()
enum class ENeuralDataType : uint8
{
	Float,
	//Double,
	//Int8,
	//Int16,
	Int32,
	Int64,
	//UInt8,
	UInt32,
	UInt64,
	None
};

UENUM()
enum class ENeuralDeviceType : uint8
{
	CPU,
	GPU,
	None
};

/**
 * Whether UNeuralNetwork::Run() will block the thread until completed (Synchronous), or whether it will run on a background thread,
 * not blocking the calling thread (Asynchronous).
 */
UENUM()
enum class ENeuralNetworkSynchronousMode : uint8
{
	Synchronous, /* UNeuralNetwork::Run() will block the thread until the network evaluation (i.e., forward pass) has finished. */
	/**
	 * UNeuralNetwork::Run() will initialize a forward pass request on a background thread, not blocking the thread that called it.
	 * The user should register to UNeuralNetwork's delegate to know when the forward pass has finished.
	 *
	 * Very important: It takes ~1 millisecond to start the background thread. If your network runs synchronously faster than 1 msec,
	 * using asynchronous running will make the game (main) thread slower than running it synchronously.
	 */
	Asynchronous
};

UENUM()
enum class ENeuralNetworkDelegateThreadMode : uint8
{
	GameThread, /* Recommended and default value. The UNeuralNetwork delegate will be called from the game thread. */
	/**
	 * Not recommended, use at your own risk.
	 * The UNeuralNetwork delegate could be called from any thread.
	 * Running UClass functions from background threads is not safe (e.g., it might crash if the editor is closed while accessing UNeuralNetwork information).
	 * Thus "AnyThread" is only safe if you have guarantees that the program will not be terminated while calling UNeuralNetwork functions.
	 */
	AnyThread
};


/**
 * Although conceptually this could apply to both the CPU and GPU versions, in practice only the GPU performance is affected by this setting.
 * Input and Intermediate(Not)Initialized currently share the same attributes because input might become intermediate (e.g., if input tensor fed into a ReLU, which simply modifies
 * the input FNeuralTensor). However, Intermediate(Not)Initialized and Output do not copy the memory from CPU to GPU but rather simply allocates it.
 * Output might also become Intermediate(Not)Initialized (e.g., if Output -> ReLU -> Output), so it is kept as ReadWrite rather than written once to account for this.
 */
UENUM()
enum class ENeuralTensorTypeGPU : uint8
{
	Generic,					/** Generic tensor that works in every situation (ReadWrite), although it might not be the most efficient one. */
	Input,						/** Input tensor of the UNeuralNetworkLegacy. Copied from CPU and ReadWrite (but usually ReadOnly). */
	IntermediateNotInitialized,	/** Intermediate tensor of the UNeuralNetworkLegacy (output of at least a layer and input of at least some other layer). Not copied from CPU, ReadWrite, and transient. */
	IntermediateInitialized,	/** Intermediate tensor that is initialized with CPU data (e.g., XWithZeros in FConvTranpose). Copied from CPU. */
	Output,						/** Output tensor of the UNeuralNetworkLegacy. Not copied from CPU and ReadWrite. */
	Weight						/** Weights of a particular operator/layer. Copied from CPU, ReadOnly, and initialized from CPU memory. */
};



/**
 * Auxiliary utils class for ENeuralDataType
 */

class NEURALNETWORKINFERENCE_API FNeuralDataTypeUtils
{
public:
	static FString ToString(const ENeuralDataType InDataType);
	static int64 GetSize(const ENeuralDataType InDataType);
	static EPixelFormat GetPixelFormat(const ENeuralDataType InDataType);

	/**
	 * It checks whether T and InDataType are the same type. E.g.,
	 * checkf(CheckTAndDataType<float>(), TEXT("Expected a ENeuralDataType::Float type."));
	 */
	template <typename T>
	static bool CheckTAndDataType(const ENeuralDataType InDataType)
	{
		return InDataType == GetDataType<T>();
	}

	/**
	 * It gets the data type from the type T. E.g.,
	 * checkf(InDataType == GetDataType<float>(), TEXT("InDataType == GetDataType<float>() failed!"))
	 * FNeuralTensor(FNeuralDataTypeUtils::GetDataType<T>(), InArray.GetData(), ...)
	 */
	template <typename T>
	static ENeuralDataType GetDataType();
};



/* FNeuralDataTypeUtils templated functions
 *****************************************************************************/

template <typename T>
ENeuralDataType FNeuralDataTypeUtils::GetDataType()
{
	if (std::is_same<T, float>::value)
	{
		return ENeuralDataType::Float;
	}
	else if (std::is_same<T, int32>::value)
	{
		return ENeuralDataType::Int32;
	}
	else if (std::is_same<T, int64>::value)
	{
		return ENeuralDataType::Int64;
	}
	else if (std::is_same<T, uint32>::value)
	{
		return ENeuralDataType::UInt32;
	}
	else if (std::is_same<T, uint64>::value)
	{
		return ENeuralDataType::UInt64;
	}
	return ENeuralDataType::None;
}
