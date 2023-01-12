// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Class.h"

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
	class NNXCORE_API FSymbolicTensorShape
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

	class NNXCORE_API FTensorShape
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

	/** Return data size in bytes for tensor data type */
	size_t NNXCORE_API GetTensorDataTypeSizeInBytes(ENNETensorDataType InType);

	class NNXCORE_API FTensorDesc
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
