// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/ArrayView.h"
#include "Misc/Variant.h"
#include "Animation/AnimTypes.h"
#include "UObject/NameTypes.h"
#include "Containers/ContainersFwd.h"

struct FCompactPoseBoneIndex;
struct FBlendSampleData;
struct FBoneContainer;

enum class ECustomAttributeBlendType : uint8;

/** "Header" structure describing a runtime custom attribute */
struct FCustomAttributeInfo
{
	FCustomAttributeInfo(const FName& InName, const FCompactPoseBoneIndex& InCompactBoneIndex, const ECustomAttributeBlendType& InBlendType);

	// Hash contains CombineHash(GetTypeHash(BoneIndex), GetTypeHash(AttributeName))
	uint32 Hash;
	// Index to the bone this attribute is stored for
	int32 BoneIndex;
	// Type of blending to use throughout the animation runtime
	ECustomAttributeBlendType BlendType;
};

template<class BoneIndexType, typename InAllocator>
struct TBaseCustomAttributes
{
	template <typename OtherBoneIndexType, typename OtherAllocator>
	friend struct TBaseCustomAttributes;	

	/** Retrieve the typed  array containing individual attribute values */
	template<typename DataType> TArray<DataType, InAllocator>& GetValuesArray()
	{
		TArray<DataType, InAllocator>* ArrayPtr = nullptr;
		GetValuesArray_Internal(ArrayPtr);
		return *ArrayPtr;
	}

	template<typename DataType>
	const TArray<DataType, InAllocator>& GetValuesArray() const
	{
		const TArray<DataType, InAllocator>* ArrayPtr = nullptr;
		GetValuesArray_Internal(ArrayPtr);
		return *ArrayPtr;
	}

	template<typename DataType>
	int32 AddBoneAttribute(const BoneIndexType& BoneIndex, const FName& AttributeName, ECustomAttributeBlendType BlendType, const DataType& Value)
	{
		FCustomAttributeInfo AttributeInfo(AttributeName, BoneIndex, BlendType);
		return AddBoneAttribute<DataType>(AttributeInfo, Value);
	}
	
	template<typename DataType>
	int32 AddBoneAttribute(const FCustomAttributeInfo& AttributeInfo, const DataType& Value)
	{
		constexpr int32 DataTypeIndex = GetIndexForType<DataType>();
		const int32 ExistingAttributeIndex = AttributeInfos[DataTypeIndex].IndexOfByPredicate([BoneAttributeHash = AttributeInfo.Hash](const FCustomAttributeInfo& Attribute)
		{
			return Attribute.Hash == BoneAttributeHash;
		});

		// Should only add an attribute once
		if (ensure(ExistingAttributeIndex == INDEX_NONE))
		{
			int32 NewIndex = INDEX_NONE;

			AttributeInfos[DataTypeIndex].Add(AttributeInfo);
			UniqueTypedBoneIndices[DataTypeIndex].AddUnique(AttributeInfo.BoneIndex);
			TArray<DataType, InAllocator>& TypedArray = GetValuesArray<DataType>();
			NewIndex = TypedArray.Add(Value);

			// Ensure arrays match in size
			ensure(AttributeInfos[DataTypeIndex].Num() == TypedArray.Num());

			return NewIndex;
		}

		return ExistingAttributeIndex;
	}

	template<typename DataType>
	bool GetBoneAttribute(const BoneIndexType& BoneIndex, const FName& AttributeName, DataType& OutValue) const
	{
		constexpr int32 DataTypeIndex = GetIndexForType<DataType>();
		const int32 BoneIndexInt = BoneIndex.GetInt();

		// Early out if for this bone index no attributes are currently contained
		if (UniqueTypedBoneIndices[DataTypeIndex].Contains(BoneIndexInt))
		{
			const uint32 BoneAttributeHash = HashCombine(GetTypeHash(BoneIndexInt), GetTypeHash(AttributeName));
			const int32 AttributeIndex = IndexOfBoneAttribute<DataType>(BoneAttributeHash, BoneIndexInt);
			if (AttributeIndex != INDEX_NONE)
			{
				const TArray<DataType, InAllocator>& TypedArray = GetValuesArray<DataType>();
				ensure(TypedArray.IsValidIndex(AttributeIndex));
				OutValue = TypedArray[AttributeIndex];

				return true;
			}
		}

		return false;
	}

	template<typename DataType>
	int32 IndexOfBoneAttribute(uint32 BoneAttributeHash, int32 BoneIndexInt) const
	{
		constexpr int32 DataTypeIndex = GetIndexForType<DataType>();

		if (UniqueTypedBoneIndices[DataTypeIndex].Contains(BoneIndexInt))
		{
			return AttributeInfos[DataTypeIndex].IndexOfByPredicate([BoneAttributeHash](const FCustomAttributeInfo& Attribute)
			{
				return Attribute.Hash == BoneAttributeHash;
			});
		}

		return INDEX_NONE;
	}

	template<typename DataType>
	bool ContainsBoneAttribute(uint32 BoneAttributeHash, int32 BoneIndexInt) const
	{
		return IndexOfBoneAttribute<DataType>(BoneAttributeHash, BoneIndexInt) != INDEX_NONE;
	}

	template<typename DataType>
	const TArray<FCustomAttributeInfo, InAllocator>& GetAttributeInfo() const
	{
		constexpr int32 DataTypeIndex = GetIndexForType<DataType>();
		return AttributeInfos[DataTypeIndex];
	}

	template<typename DataType>
	const TArray<int32, InAllocator>& GetUniqueBoneIndices() const
	{
		constexpr int32 DataTypeIndex = GetIndexForType<DataType>();
		return UniqueTypedBoneIndices[DataTypeIndex];
	}

	template <typename OtherAllocator>
	void CopyFrom(const TBaseCustomAttributes<BoneIndexType, OtherAllocator>& Other)
	{
		FloatValues = Other.FloatValues;
		IntValues = Other.IntValues;
		StringValues = Other.StringValues;

		for (int32 Index = 0; Index < NumSupportedDataTypes; ++Index)
		{
			AttributeInfos[Index] = Other.AttributeInfos[Index];
			UniqueTypedBoneIndices[Index] = Other.UniqueTypedBoneIndices[Index];
		}
	}

	void CopyFrom(const TBaseCustomAttributes<BoneIndexType, InAllocator>& Other)
	{
		if (&Other != this)
		{
			FloatValues = Other.FloatValues;
			IntValues = Other.IntValues;
			StringValues = Other.StringValues;

			for (int32 Index = 0; Index < NumSupportedDataTypes; ++Index)
			{
				AttributeInfos[Index] = Other.AttributeInfos[Index];
				UniqueTypedBoneIndices[Index] = Other.UniqueTypedBoneIndices[Index];
			}
		}
	}

	/** Once moved, source is invalid */
	void MoveFrom(TBaseCustomAttributes<BoneIndexType, InAllocator>& Other)
	{
		FloatValues = MoveTemp(Other.FloatValues);
		IntValues = MoveTemp(Other.IntValues);
		StringValues = MoveTemp(Other.StringValues);

		for (int32 Index = 0; Index < NumSupportedDataTypes; ++Index)
		{
			AttributeInfos[Index] = MoveTemp(Other.AttributeInfos[Index]);
			UniqueTypedBoneIndices[Index] = MoveTemp(Other.UniqueTypedBoneIndices[Index]);
		}
	}

	bool ContainsData() const
	{
		return FloatValues.Num() || IntValues.Num() || StringValues.Num();
	}

	void Empty()
	{
		FloatValues.Empty();
		IntValues.Empty();
		StringValues.Empty();

		for (int32 Index = 0; Index < NumSupportedDataTypes; ++Index)
		{
			AttributeInfos[Index].Empty();
			UniqueTypedBoneIndices[Index].Empty();
		}
	}

	bool operator!=(const TBaseCustomAttributes<BoneIndexType, InAllocator>& Other)
	{
		return FloatValues.Num() != Other.FloatValues.Num() 
		|| IntValues.Num() != Other.IntValues.Num() 
		|| StringValues.Num() != Other.StringValues.Num();
	}

protected:
	// Number of, and explicit types which are currently supported
	static CONSTEXPR int32 NumSupportedDataTypes = 3;
	static CONSTEXPR EVariantTypes SupportedTypes[NumSupportedDataTypes] = { EVariantTypes::Float, EVariantTypes::Int32, EVariantTypes::String };	
	
	template<typename DataType>
	static CONSTEXPR int32 GetIndexForType()
	{
		constexpr EVariantTypes VariantType = TVariantTraits<DataType>::GetType();
		for (int32 TypeIndex = 0; TypeIndex < NumSupportedDataTypes; ++TypeIndex)
		{
			if (SupportedTypes[TypeIndex] == VariantType)
			{
				return TypeIndex;
			}
		}

		return INDEX_NONE;
	};

	// Internal type array getters, used to retrieve by-reference array in the public API
	void GetValuesArray_Internal(TArray<float, InAllocator>*& OutPtr) { OutPtr = &FloatValues; }
	void GetValuesArray_Internal(TArray<int32, InAllocator>*& OutPtr) { OutPtr = &IntValues; }
	void GetValuesArray_Internal(TArray<FString, InAllocator>*& OutPtr) { OutPtr = &StringValues; }

	void GetValuesArray_Internal(const TArray<float, InAllocator>*& OutPtr) const { OutPtr = &FloatValues; }
	void GetValuesArray_Internal(const TArray<int32, InAllocator>*& OutPtr) const { OutPtr = &IntValues; }
	void GetValuesArray_Internal(const TArray<FString, InAllocator>*& OutPtr) const { OutPtr = &StringValues; }

protected:
	// Information for each stored custom attribute
	TArray<FCustomAttributeInfo, InAllocator> AttributeInfos[NumSupportedDataTypes];
	
	/* Contains the uniquely added bone indices, on a per-type basis (could make this a non-per-type array and index into it from BoneIndices, would add some cost for runtime evaluation to retrieve unique bone indices per type) */
	TArray<int32, InAllocator> UniqueTypedBoneIndices[NumSupportedDataTypes];

	// Attribute typed value arrays
	TArray<float, InAllocator> FloatValues;
	TArray<int32, InAllocator> IntValues;
	TArray<FString, InAllocator> StringValues;	
};

struct ENGINE_API FStackCustomAttributes : public TBaseCustomAttributes<FCompactPoseBoneIndex, FAnimStackAllocator> {};
struct ENGINE_API FHeapCustomAttributes : public TBaseCustomAttributes<FCompactPoseBoneIndex, FDefaultAllocator> {};

/** Helper functionality for custom attributes animation runtime */
struct ENGINE_API FCustomAttributesRuntime
{
#if WITH_EDITOR
	/** Editor functionality to retrieve custom attribute values from the raw data */
	static void GetAttributeValue(struct FStackCustomAttributes& OutAttributes, const struct FCompactPoseBoneIndex& PoseBoneIndex, const struct FCustomAttribute& Attribute, const struct FAnimExtractContext& ExtractionContext);
	static void GetAttributeValue(const struct FCustomAttribute& Attribute, float Time, float& OutValue);
	static void GetAttributeValue(const struct FCustomAttribute& Attribute, float Time, int32& OutValue);
	static void GetAttributeValue(const struct FCustomAttribute& Attribute, float Time, FString& OutValue);
#endif // WITH_EDITOR

	/** Blend custom attribute values from N set of inputs */
	static void BlendAttributes(const TArrayView<const FStackCustomAttributes> SourceAttributes, const TArrayView<const float> SourceWeights, FStackCustomAttributes& OutAttributes);

	/** Blend custom attribute values from N set of inputs (ptr-values) */
	static void BlendAttributes(const TArrayView<const FStackCustomAttributes* const> SourceAttributes, const TArrayView<const float> SourceWeights, FStackCustomAttributes& OutAttributes);

	/** Blend custom attribute values from N set of inputs, using input weight remapping */
	static void BlendAttributes(const TArrayView<const FStackCustomAttributes> SourceAttributes, const TArrayView<const float> SourceWeights, const TArrayView<const int32> SourceWeightsIndices, FStackCustomAttributes& OutAttributes);

	/* Blend custom attribute values from 2 inputs, using per-bone weights */ 
	static void BlendAttributesPerBone(const FStackCustomAttributes& SourceAttributes1, const FStackCustomAttributes& SourceAttributes2, const TArrayView<const float> WeightsOfSource2, FStackCustomAttributes& OutAttributes);
		
	/* Blend custom attribute values from N set of inputs, using bone filter pose weights */ 
	static void BlendAttributesPerBoneFilter(const TArrayView<const FStackCustomAttributes> BlendAttributes, const TArray<FPerBoneBlendWeight>& BoneBlendWeights, FStackCustomAttributes& OutAttributes);
		
	/** Add any new or override existing custom attributes */
	static void OverrideAttributes(const FStackCustomAttributes& SourceAttributes, FStackCustomAttributes& OutAttributes, float Weight);

	/** Add any new or accumulate with existing custom attributes */
	static void AccumulateAttributes(const FStackCustomAttributes& SourceAttributes, FStackCustomAttributes& OutAttributes, float Weight);

	/** Add (negated) any new or subtract from existing custom attributes */
	static void SubtractAttributes(const FStackCustomAttributes& SourceAttributes, FStackCustomAttributes& OutAttributes);

	/** Copy attributes from source, and remap the bone indices according to BoneMapToSource */
	static void CopyAndRemapAttributes(const FHeapCustomAttributes& SourceAttributes, FStackCustomAttributes& OutAttributes, const TMap<int32, int32>& BoneMapToSource, const FBoneContainer& RequiredBones);

	/** Interpolates between two sets of attributes */
	static void InterpolateAttributes(const FHeapCustomAttributes& SourceAttributes, FHeapCustomAttributes& OutAttributes, float Alpha);

	/** Helper functionality to retrieve the correct blend type (from UAnimationSettings) for the provided attribute name */
	static ECustomAttributeBlendType GetAttributeBlendType(const FName& InName);
};
