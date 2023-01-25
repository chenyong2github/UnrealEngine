// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Class.h"

/**
 * The enum lists all different data types used in NNE 
 */
UENUM()
enum class ENNETensorDataType : uint8
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

namespace UE::NNECore
{

	/**
	 * A symbolic tensor shape represents the shape of a tensor with potentially variable dimension.
	 * 
	 * The variable dimensions are represented by negative values. 
	 */
	class NNECORE_API FSymbolicTensorShape
	{
	public:
		constexpr static int32	MaxRank = 8;

	private:
		TArray<int32, TInlineAllocator<MaxRank>> Data;

	public:
		static FSymbolicTensorShape Make(TConstArrayView<int32> Data);

		inline TConstArrayView<int32> GetData() const { return Data; };
		inline int32 Rank() const { return Data.Num(); }

		bool IsConcrete() const;
		bool operator==(const FSymbolicTensorShape& OtherShape) const;
		bool operator!=(const FSymbolicTensorShape& OtherShape) const;
		void operator=(const FSymbolicTensorShape& OtherShape);
	};

	/**
	 * The concrete shape of a tensor.
	 *
	 * Concrete tensor shapes are well defined through strictly positive values and thus have a well defined volume.
	 */
	class NNECORE_API FTensorShape
	{
	public:
		constexpr static int32	MaxRank = FSymbolicTensorShape::MaxRank;

	private:
		TArray<uint32, TInlineAllocator<MaxRank>> Data;

	public:
		static FTensorShape Make(TConstArrayView<uint32> Data);
		static FTensorShape MakeFromSymbolic(const FSymbolicTensorShape& SymbolicShape);

		inline TConstArrayView<uint32> GetData() const { return Data; };
		inline int32 Rank() const { return Data.Num(); }
		
		uint64 Volume() const;
		bool IsCompatibleWith(const FSymbolicTensorShape& SymbolicShape) const;
		bool operator==(const FTensorShape& OtherShape) const;
		bool operator!=(const FTensorShape& OtherShape) const;
		void operator=(const FTensorShape& OtherShape);
	};

	/**
	 * Return the data size in bytes for a tensor data type.
	 */
	size_t NNECORE_API GetTensorDataTypeSizeInBytes(ENNETensorDataType InType);

	/**
	 * The descriptor for a tensor.
	 *
	 * A tensor is described by its name, the type of data it contains and it's shape.
	 * Since input and output tensors of a neural network can have dynamic shapes, Shape is symbolic.
	 */
	class NNECORE_API FTensorDesc
	{
		FString					Name;
		ENNETensorDataType		DataType;
		FSymbolicTensorShape	Shape;

		FTensorDesc() = default;

	public:
		static FTensorDesc Make(const FString& Name, const FSymbolicTensorShape& Shape, ENNETensorDataType DataType);
		
		inline const FString& GetName() const { return Name; }
		inline ENNETensorDataType GetDataType() const { return DataType; }
		inline uint32 GetElemByteSize() const { return GetTensorDataTypeSizeInBytes(DataType); }
		inline const FSymbolicTensorShape& GetShape() const { return Shape; }
	};

} // namespace NNE
