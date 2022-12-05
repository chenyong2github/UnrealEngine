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
	
template <typename DimType, class FinalType> struct TTensorShapeBase
{
	constexpr static int32	MaxRank = 8;
	TArray<DimType, TInlineAllocator<MaxRank>> Data;

	int32 Rank() const
	{
		return Data.Num();
	}

	inline bool operator==(const TTensorShapeBase<DimType,FinalType>& OtherShape) const
	{
		check(Rank() <= MaxRank);

		if (Rank() != OtherShape.Rank())
			return false;

		for (int32 i = 0; i < Rank(); ++i)
		{
			if (Data[i] != OtherShape.Data[i])
			return false;
		}

		return true;
	}
	
	inline bool operator!=(const TTensorShapeBase<DimType, FinalType>& OtherShape) const { return !(*this == OtherShape); }

	static FinalType Make(TConstArrayView<DimType> Data)
	{
		if (Data.Num() > MaxRank)
		{
			//UE_LOG(LogNNX, Warning, TEXT("Cannot create tensor shape, input is rank %d while max rank is %d"), Data.Num(), MaxRank);
			return {};
		}

		FinalType Shape;
		Shape.Data.Append(Data);
		return Shape;
	}
};


struct FSymbolicTensorShape : TTensorShapeBase<int32, FSymbolicTensorShape>
{
	bool IsConcrete() const
	{
		for (int32 i = 0; i < Rank(); ++i)
		{
			if (Data[i] < 0)
			{
				return false;
			}
		}
		return true;
	}
};

struct FTensorShape : TTensorShapeBase<uint32, FTensorShape>
{
	uint64 Volume() const
	{
		uint64 Result = 1;

		for (int32 Idx = 0; Idx < Data.Num(); ++Idx)
		{
			Result *= Data[Idx];
		}
		
		return Result;
	}

	bool IsCompatibleWith(FSymbolicTensorShape SymbolicShape) const
	{
		if (Rank() != SymbolicShape.Rank())
		{
			return false;
		}
		for (int32 i = 0; i < Rank(); ++i)
		{
			if (SymbolicShape.Data[i] >= 0 && SymbolicShape.Data[i] != Data[i])
			{
				return false;
			}
		}
		return true;
	}
	
	static FTensorShape MakeFromSymbolic(const FSymbolicTensorShape& SymbolicShape)
	{
		FTensorShape ConcreteShape;
		for (int32 i = 0; i < SymbolicShape.Data.Num(); ++i)
		{
			int32 Dim = SymbolicShape.Data[i];
			ConcreteShape.Data.Add(Dim < 0 ? 1 : Dim);
		}
		return ConcreteShape;
	}
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

class FTensorDescBase
{
protected:
	FString					Name;
	EMLTensorDataType		DataType;

	FTensorDescBase() = default;
	
public:
	const FString& GetName() const
	{
		return Name;
	}
	
	EMLTensorDataType GetDataType() const
	{
		return DataType;
	}

	/** Return size of one element in bytes */
	uint32 GetElemByteSize() const
	{
		return GetTensorDataTypeSizeInBytes(DataType);
	}
	
	bool IsValid() const
	{
		return DataType != EMLTensorDataType::None;
	}
};

/** Symbolic tensor descriptor without data */
class FTensorDesc : public FTensorDescBase
{
	FSymbolicTensorShape	Shape;

public:
	const FSymbolicTensorShape& GetShape() const
	{
		return Shape;
	}

	static FTensorDesc Make(const FString& Name, const FSymbolicTensorShape& Shape, EMLTensorDataType DataType)
	{
		FTensorDesc Desc;
		Desc.Name = Name;
		Desc.DataType = DataType;
		Desc.Shape = Shape;
		return Desc;
	}

	bool IsConcrete() const
	{
		return Shape.IsConcrete();
	}
};

/** Concrete tensor backed by data */
class FTensor : public FTensorDescBase
{
protected:
	FTensorShape Shape;
	uint32 Volume = 0;
	uint64 DataSize = 0;

public:
	const FTensorShape& GetShape() const
	{
		return Shape;
	}

	uint32 GetVolume() const
	{
		return Volume;
	}

	uint64 GetDataSize() const
	{
		return DataSize;
	}
	
	static FTensor Make(const FString& Name, const FTensorShape& Shape, EMLTensorDataType DataType)
	{
		FTensor Desc;
		Desc.Name = Name;
		Desc.DataType = DataType;
		Desc.Shape = Shape;
		Desc.Volume = Shape.Volume();
		Desc.DataSize = (uint64)GetTensorDataTypeSizeInBytes(DataType) * Desc.Volume;
		check(Desc.Volume <= TNumericLimits<uint32>::Max());
		return Desc;
	}

	static FTensor MakeFromSymbolicDesc(const FTensorDesc& TensorDesc)
	{
		return Make(TensorDesc.GetName(), FTensorShape::MakeFromSymbolic(TensorDesc.GetShape()), TensorDesc.GetDataType());
	}
};

} // namespace NNX
