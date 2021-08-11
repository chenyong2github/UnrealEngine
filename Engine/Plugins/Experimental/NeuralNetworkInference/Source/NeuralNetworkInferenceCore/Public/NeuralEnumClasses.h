// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PixelFormat.h"
#include <type_traits>
#include "NeuralEnumClasses.generated.h"

UENUM()
enum class ENeuralDeviceType : uint8
{
	CPU,
	GPU,
	None
};

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

class NEURALNETWORKINFERENCECORE_API FDataType
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
	 * FNeuralTensor(FDataType::GetDataType<T>(), InArray.GetData(), ...)
	 */
	template <typename T>
	static ENeuralDataType GetDataType();
};



/* FDataType inline functions
 *****************************************************************************/

template <typename T>
ENeuralDataType FDataType::GetDataType()
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
