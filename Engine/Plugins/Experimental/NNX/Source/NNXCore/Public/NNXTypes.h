// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Object.h"
#include "UObject/Class.h"
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

/** Attribute data types */
UENUM()
enum class EMLAttributeDataType : uint8
{
	None,
	Float,								//!< 32-bit floating number
	Int32								//!< 32-bit signed integer
    //TODO add more AttributeDataType support
};

USTRUCT()
struct FMLAttributeValue
{
	GENERATED_USTRUCT_BODY()

	FMLAttributeValue()
	{
		Type = EMLAttributeDataType::None;
	}

    //TODO add more AttributeDataType support
	explicit FMLAttributeValue(float Value)
	{
		Type = EMLAttributeDataType::Float;
		Data.SetNumUninitialized(sizeof(float));
		FMemory::Memcpy(Data.GetData(), &Value, sizeof(float));
	}

	explicit FMLAttributeValue(int Value)
	{
		Type = EMLAttributeDataType::Int32;
		Data.SetNumUninitialized(sizeof(int32));
		FMemory::Memcpy(Data.GetData(), &Value, sizeof(int32));
	}

	float AsFloat() const
	{
		check(Type == EMLAttributeDataType::Float);
		return *(float*)Data.GetData();
	}

	int32 AsInt32() const
	{
		check(Type == EMLAttributeDataType::Int32);
		return *(int32*)Data.GetData();
	}

	EMLAttributeDataType GetType() const
	{
		return Type;
	}
	
private:
	
	UPROPERTY(VisibleAnywhere, Category = "Neural Network Inference")
	EMLAttributeDataType	Type;
	
	UPROPERTY(VisibleAnywhere, Category = "Neural Network Inference")
	TArray<uint8>			Data;
};

class FMLAttributeMap
{
public:
	//Set attribute
	void SetAttribute(const FString& Name, const FMLAttributeValue& Value)
	{
		checkf(nullptr == Attributes.FindByPredicate([Name](const FEntry& Other) { return Other.Name == Name; }), TEXT("Attribute name should be unique"));
		Attributes.Emplace(Name, Value);
	}

	//Query attributes
	float GetFloat(const FString& Name) const
	{
		const FMLAttributeValue* Value = GetAttributeValue(Name);
		checkf(Value != nullptr, TEXT("Required attribute %s not found"), *Name);
		return Value->AsFloat();
	}

	int32 GetInt32(const FString& Name) const
	{
		const FMLAttributeValue* Value = GetAttributeValue(Name);
		checkf(Value != nullptr, TEXT("Required attribute %s not found"), *Name);
		return Value->AsInt32();
	}

	float GetOptionalFloat(const FString& Name, float Default) const
	{
		const FMLAttributeValue* Value = GetAttributeValue(Name);
		return Value == nullptr ? Default : Value->AsFloat();
	}

	int32 GetOptionalInt32(const FString& Name, int32 Default) const
	{
		const FMLAttributeValue* Value = GetAttributeValue(Name);
		return Value == nullptr ? Default : Value->AsInt32();
	}
	
	const FMLAttributeValue* GetAttributeValue(const FString& Name) const
	{
		const FEntry * entry = Attributes.FindByPredicate([Name](const FEntry& Other) { return Other.Name == Name; });
		if (entry != nullptr)
		{
			return &entry->Value;
		}
		return nullptr;
	}

	//Iteration
	int32 Num() const
	{
		return Attributes.Num();
	}

	const FString& GetName(int32 Idx) const
	{
		check(Idx < Num());
		return Attributes[Idx].Name;
	}

	const FMLAttributeValue& GetAttributeValue(int32 Idx) const
	{
		check(Idx < Num());
		return Attributes[Idx].Value;
	}

private:

	struct FEntry
	{
		FEntry(const FString& InName, const FMLAttributeValue& InValue)
			: Name(InName), Value(InValue)
		{
		}
		
		FString Name;
		FMLAttributeValue Value;
	};

	TArray<FEntry> Attributes;
};

namespace NNX
{

/**
 *  Tensor descriptor
 */
struct FMLTensorDesc
{
	constexpr static uint32	MaxTensorDimension = 5;

	FString					Name;
	uint32					Dimension;							//!< Number of dimensions in the Sizes
	uint32					Sizes[MaxTensorDimension];			//!< Sizes
	uint64					DataSize;							//!< Size of data in bytes
	EMLTensorDataType		DataType;

	/** Make tensor descriptor */
	static FMLTensorDesc Make(const FString& Name, TArrayView<const uint32> Shape, EMLTensorDataType DataType, uint64 DataSize = 0)
	{
		FMLTensorDesc Desc{};

		uint32 Dimension = Shape.Num();

		check(Dimension <= FMLTensorDesc::MaxTensorDimension);
		if (Dimension > FMLTensorDesc::MaxTensorDimension)
		{
			//UE_LOG(LogNNX, Warning, TEXT("Unsupported tensor dimension:%d"), Dimension);
			return Desc;
		}

		Desc.Name = Name;
		Desc.Dimension = Dimension;
		
		for (uint32 Idx = 0; Idx < Desc.Dimension; ++Idx)
		{
			Desc.Sizes[Idx] = Shape[Idx];
		}

		Desc.DataType = DataType;
		Desc.DataSize = DataSize;

		return Desc;
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
	return DataType != EMLTensorDataType::None && Dimension > 0 && Dimension <= MaxTensorDimension; //&& Volume() >= 1;
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

	for (uint32 Idx = 0; Idx < Dimension; ++Idx)
	{
		Result *= Sizes[Idx];
	}

	return Result;
}

/** Return number of elements */
inline int32 FMLTensorDesc::Num() const
{
	return Volume();
}

} // namespace NNX
