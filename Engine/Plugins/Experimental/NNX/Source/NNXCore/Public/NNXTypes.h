// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Class.h"
#include "UObject/Object.h"
#include "NNXTypes.generated.h"

/** Tensor data types */
UENUM()
enum class EMLTensorDataType : uint8
{
	None,
	Char,								//!< Character type
	Boolean,							//!< Boolean type
	Half,								//!< 16-bit floating number
	Float,								//!< 32-bit floating number
	Double,								//!< 64-bit floating number
	Int8,								//!< 8-bit signed integer
	Int16,								//!< 16-bit signed integer
	Int32,								//!< 32-bit signed integer
	Int64,								//!< 64-bit signed integer
	UInt8,								//!< 8-bit unsigned integer
	UInt16,								//!< 16-bit unsigned integer
	UInt32,								//!< 32-bit unsigned integer
	UInt64,								//!< 64-bit unsigned integer
	Complex64,							//!< 64-bit Complex Number
	Complex128,							//!< 128-bit Complex Number
	BFloat16							//!< 16-bit floating number
};

namespace NNX
{

struct FSymbolicTensorShape
{
	constexpr static int32	MaxRank = 8;
	TArray<int32, TInlineAllocator<MaxRank>> Data;

	int32 Num() const
	{
		return Data.Num();
	}

	static FSymbolicTensorShape Make(TConstArrayView<const int32> Data)
	{
		if (Data.Num() > MaxRank)
		{
			//UE_LOG(LogNNX, Warning, TEXT("Cannot create symbolic tensor shape, input is rank %d while max rank is %d"), Data.Num(), MaxRank);
			return {};
		}

		FSymbolicTensorShape Shape;
		
		Shape.Data.Append(Data);
		return Shape;
	}
	
	bool IsConcrete()
	{
		for (int32 i = 0; i < Data.Num(); ++i)
		{
			if (Data[i] < 0)
			{
				return false;
			}
		}
		return true;
	}
};

struct FConcreteTensorShape
{
	TArray<uint32, TInlineAllocator<FSymbolicTensorShape::MaxRank>> Data;

	int32 Num() const
	{
		return Data.Num();
	}
	
	static FConcreteTensorShape Make(const FSymbolicTensorShape& SymbolicShape)
	{
		FConcreteTensorShape ConcreteShape;
		for (int32 i = 0; i < SymbolicShape.Data.Num(); ++i)
		{
			int32 Dim = SymbolicShape.Data[i];
			ConcreteShape.Data.Add(Dim < 0 ? 1 : Dim);
		}
		return ConcreteShape;
	}

	static FConcreteTensorShape Make(TConstArrayView<const uint32> Data)
	{
		if (Data.Num() > FSymbolicTensorShape::MaxRank)
		{
			//UE_LOG(LogNNX, Warning, TEXT("Cannot create concrete tensor shape, input is rank %d while max rank is %d"), Data.Num(), MaxRank);
			return {};
		}

		FConcreteTensorShape Shape;

		Shape.Data.Append(Data);
		return Shape;
	}
};

/**
 *  Tensor descriptor
 */
struct FMLTensorDesc
{
	constexpr static int32	MaxTensorDimension = 5;

	FString					Name;
	FConcreteTensorShape	Shape;
	uint64					DataSize;//!< Size of data in bytes
	EMLTensorDataType		DataType;

	/** Make tensor descriptor */
	static FMLTensorDesc Make(const FString& Name, const FConcreteTensorShape& Shape, EMLTensorDataType DataType, uint64 DataSize = 0)
	{
		return {Name, Shape, DataSize, DataType};
	}

	/** Check if descriptor is valid */
	bool IsValid() const;

	/** Return size of one element in bytes */
	int32 GetElemByteSize() const;

	/** Return Volume, i.e. number of elements */
	int32 Volume() const;

	/** Return number of elements, same as Volume() */
	int32 Num() const;
};

/** Return data size in bytes for tensor data type */
inline size_t GetTensorDataTypeSizeInBytes(EMLTensorDataType InType)
{
	switch (InType)
	{
		case EMLTensorDataType::Complex128:
			return 16;

		case EMLTensorDataType::Complex64:
			return 8;

		case EMLTensorDataType::Double:
		case EMLTensorDataType::Int64:
		case EMLTensorDataType::UInt64:
			return 8;

		case EMLTensorDataType::Float:
		case EMLTensorDataType::Int32:
		case EMLTensorDataType::UInt32:
			return 4;

		case EMLTensorDataType::Half:
		case EMLTensorDataType::BFloat16:
		case EMLTensorDataType::Int16:
		case EMLTensorDataType::UInt16:
			return 2;

		case EMLTensorDataType::Int8:
		case EMLTensorDataType::UInt8:
		case EMLTensorDataType::Char:
		case EMLTensorDataType::Boolean:
			return 1;
	}

	return 0;
}

/** Check if descriptor is valid */
inline bool FMLTensorDesc::IsValid() const
{
	return DataType != EMLTensorDataType::None && Shape.Data.Num() > 0 && Shape.Data.Num() <= MaxTensorDimension; //&& Volume() >= 1;
}

/** Return size of one element in bytes */
inline int32 FMLTensorDesc::GetElemByteSize() const
{
	return GetTensorDataTypeSizeInBytes(DataType);
}

/** Return Volume, i.e. number of elements */
inline int32 FMLTensorDesc::Volume() const
{
	int32 Result = 1;

	for (int32 Idx = 0; Idx < Shape.Data.Num(); ++Idx)
	{
		Result *= Shape.Data[Idx];
	}

	return Result;
}

/** Return number of elements */
inline int32 FMLTensorDesc::Num() const
{
	return Volume();
}

} // namespace NNX
