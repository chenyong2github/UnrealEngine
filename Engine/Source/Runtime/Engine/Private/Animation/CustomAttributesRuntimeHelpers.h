// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Animation/CustomAttributes.h"
#include "Animation/AnimationAsset.h"
#include "Animation/CustomAttributes.h"

#include "BoneIndices.h"
#include "BoneContainer.h"

#include "Containers/UnrealString.h"
#include "Containers/Array.h"
#include "Containers/ArrayView.h"
#include "Containers/Map.h"
#include "Stats/Stats.h"
#include "Misc/AssertionMacros.h"

#if WITH_EDITOR
namespace CustomAttributeEvaluation
{
	// Helper function to retrieve the two closest time keys and alpha between them for a given time
	void GetKeyIndices(const TArrayView<const float> Keys, float InTime, int32& OutKeyOne, int32& OutKeyTwo, float& OutAlpha)
	{
		const int32 NumKeys = Keys.Num();

		if (NumKeys == 0)
		{
			// No keys
			OutKeyOne = INDEX_NONE;
			OutKeyTwo = INDEX_NONE;
			OutAlpha = 0.f;
		}
		else if (NumKeys < 2 || (InTime <= Keys[0]))
		{
			OutKeyOne = OutKeyTwo = 0;
			OutAlpha = 0.f;
		}
		else if (InTime < Keys[NumKeys - 1])
		{
			// perform a lower bound to get the second of the interpolation nodes
			int32 First = 1;
			const int32 Last = NumKeys - 1;
			int32 Count = Last - First;

			while (Count > 0)
			{
				const int32 Step = Count / 2;
				const int32 Middle = First + Step;

				if (InTime >= Keys[Middle])
				{
					First = Middle + 1;
					Count -= Step + 1;
				}
				else
				{
					Count = Step;
				}
			}

			OutKeyOne = First - 1;
			OutKeyTwo = First;

			OutAlpha = (InTime - Keys[OutKeyOne]) / (Keys[OutKeyTwo] - Keys[OutKeyOne]);
		}
		else
		{
			OutKeyOne = OutKeyTwo = NumKeys - 1;
			OutAlpha = 0.f;
		}
	}


	template<typename DataType>
	DataType GetTypedAttributeValue(const FCustomAttribute& Attribute, float TimeValue)
	{
		checkf(false, TEXT("Unsupported data type"));
		DataType Value;
		return Value;
	}

	template<>
	float GetTypedAttributeValue<float>(const FCustomAttribute& Attribute, float TimeValue)
	{
		float Value = 0.f;

		int32 KeyOne, KeyTwo;
		float Alpha = 0.f;

		GetKeyIndices(Attribute.Times, TimeValue, KeyOne, KeyTwo, Alpha);

		const float ValueOne = Attribute.Values[KeyOne].GetValue<float>();
		const float ValueTwo = Attribute.Values[KeyTwo].GetValue<float>();

		Value = ValueOne + ((ValueTwo - ValueOne) * Alpha);

		return Value;
	}

	template<>
	int32 GetTypedAttributeValue<int32>(const FCustomAttribute& Attribute, float TimeValue)
	{
		int32 Value = 0;

		int32 KeyOne, KeyTwo;
		float Alpha = 0.f;

		GetKeyIndices(Attribute.Times, TimeValue, KeyOne, KeyTwo, Alpha);

		const int32 ValueOne = Attribute.Values[KeyOne].GetValue<int32>();
		const int32 ValueTwo = Attribute.Values[KeyTwo].GetValue<int32>();

		Value = FMath::TruncToInt((float)ValueOne + (((float)ValueTwo - (float)ValueOne) * Alpha));

		return Value;
	}

	template<>
	FString GetTypedAttributeValue<FString>(const FCustomAttribute& Attribute, float TimeValue)
	{
		int32 KeyOne, KeyTwo;
		float Alpha = 0.f;
		const int32 IntAlpha = FMath::RoundToInt(Alpha);

		GetKeyIndices(Attribute.Times, TimeValue, KeyOne, KeyTwo, Alpha);

		FString Value = IntAlpha == 0 ? Attribute.Values[KeyOne].GetValue<FString>() : Attribute.Values[KeyTwo].GetValue<FString>();
		return Value;
	}
}
#endif // WITH_EDITOR

namespace AdditiveBlending
{
	/** Accumulate (adding a new, or adding to existing) attribute values according to the provided weight */
	template<typename DataType>
	static void AccumulateAttributes_Weight(FStackCustomAttributes& TargetAttributes, const FStackCustomAttributes& SourceAttributes, float Weight)
	{
		const TArray<FCustomAttributeInfo, FAnimStackAllocator>& AttributeInfos = SourceAttributes.GetAttributeInfo<DataType>();
		TArray<DataType, FAnimStackAllocator>& TargetValueArray = TargetAttributes.GetValuesArray<DataType>();
		const TArray<DataType, FAnimStackAllocator>& ValueArray = SourceAttributes.GetValuesArray<DataType>();

		for (int32 EntryIndex = 0; EntryIndex < AttributeInfos.Num(); ++EntryIndex)
		{
			const FCustomAttributeInfo& AttributeInfo = AttributeInfos[EntryIndex];
			const int32 ExistingAttributeIndex = TargetAttributes.IndexOfBoneAttribute<DataType>(AttributeInfo.Hash, AttributeInfo.BoneIndex);
			const DataType WeightedValue = ValueArray[EntryIndex] * Weight;
			if (ExistingAttributeIndex == INDEX_NONE)
			{
				TargetAttributes.AddBoneAttribute<DataType>(AttributeInfo, WeightedValue);
			}
			else
			{
				TargetValueArray[ExistingAttributeIndex] += WeightedValue;
			}
		}
	}

	/** Subtracts from existing or adds the negated value of the attributes */
	template<typename DataType>
	static void SubtractTypedAttributes(FStackCustomAttributes& TargetAttributes, const FStackCustomAttributes& SourceAttributes)
	{
		const TArray<FCustomAttributeInfo, FAnimStackAllocator>& AttributeInfos = SourceAttributes.GetAttributeInfo<DataType>();
		TArray<DataType, FAnimStackAllocator>& TargetValueArray = TargetAttributes.GetValuesArray<DataType>();
		const TArray<DataType, FAnimStackAllocator>& ValueArray = SourceAttributes.GetValuesArray<DataType>();

		for (int32 EntryIndex = 0; EntryIndex < AttributeInfos.Num(); ++EntryIndex)
		{
			const FCustomAttributeInfo& AttributeInfo = AttributeInfos[EntryIndex];

			const int32 ExistingAttributeIndex = TargetAttributes.IndexOfBoneAttribute<DataType>(AttributeInfo.Hash, AttributeInfo.BoneIndex);
			if (ExistingAttributeIndex != INDEX_NONE)
			{
				// Subtract value from base
				TargetValueArray[ExistingAttributeIndex] -= ValueArray[EntryIndex];
			}
			else
			{
				// Set to negated value
				TargetAttributes.AddBoneAttribute<DataType>(AttributeInfo, -ValueArray[EntryIndex]);
			}
		}
	}
}

namespace CopyPoseFromMesh
{
	/** Copies over the source into the target attributes, using the provided remapping bone table to support different skeletal mesh setups */
	template<typename DataType>
	static void CopyAndRemapTypedAttributes(FStackCustomAttributes& TargetAttributes, const FHeapCustomAttributes& SourceAttributes, const TMap<int32, int32>& BoneMapToSource, const FBoneContainer& RequiredBones)
	{
		const TArray<FCustomAttributeInfo>& AttributeInfos = SourceAttributes.GetAttributeInfo<DataType>();
		TArray<DataType, FAnimStackAllocator>& TargetValueArray = TargetAttributes.GetValuesArray<DataType>();
		const TArray<DataType>& ValueArray = SourceAttributes.GetValuesArray<DataType>();

		for (int32 EntryIndex = 0; EntryIndex < AttributeInfos.Num(); ++EntryIndex)
		{
			const FCustomAttributeInfo& AttributeInfo = AttributeInfos[EntryIndex];

			const int32 PoseBoneIndex = AttributeInfo.BoneIndex;
			const int32 SkeletonBoneIndex = RequiredBones.GetSkeletonIndex(FCompactPoseBoneIndex(PoseBoneIndex));
			const int32 MeshBoneIndex = RequiredBones.GetSkeletonToPoseBoneIndexArray()[SkeletonBoneIndex];
			const int32* Value = BoneMapToSource.Find(MeshBoneIndex);

			if (Value)
			{
				const int32 RemappedBoneIndex = *Value;
				const int32 ExistingAttributeIndex = TargetAttributes.IndexOfBoneAttribute<DataType>(AttributeInfo.Hash, RemappedBoneIndex);
				if (ExistingAttributeIndex == INDEX_NONE)
				{
					TargetAttributes.AddBoneAttribute<DataType>(AttributeInfo, ValueArray[EntryIndex]);
				}
				else
				{
					TargetValueArray[ExistingAttributeIndex] = ValueArray[EntryIndex];
				}
			}
		}
	}
};

namespace PerBoneBlending
{
	/** Add new or override existing attributes, whether not to override is determined by Bone indices contained by OverrideBoneIndices */
	template<typename DataType>
	static void AddOrOverrideAttributes(FStackCustomAttributes& TargetAttributes, const FStackCustomAttributes& SourceAttributes, const TSet<int32>& OverrideBoneIndices)
	{
		const TArray<FCustomAttributeInfo, FAnimStackAllocator>& AttributeInfos = SourceAttributes.GetAttributeInfo<DataType>();
		TArray<DataType, FAnimStackAllocator>& TargetValueArray = TargetAttributes.GetValuesArray<DataType>();
		const TArray<DataType, FAnimStackAllocator>& ValueArray = SourceAttributes.GetValuesArray<DataType>();

		for (int32 EntryIndex = 0; EntryIndex < AttributeInfos.Num(); ++EntryIndex)
		{
			const FCustomAttributeInfo& AttributeInfo = AttributeInfos[EntryIndex];

			const int32 ExistingAttributeIndex = TargetAttributes.IndexOfBoneAttribute<DataType>(AttributeInfo.Hash, AttributeInfo.BoneIndex);
			if (ExistingAttributeIndex == INDEX_NONE)
			{
				TargetAttributes.AddBoneAttribute<DataType>(AttributeInfo, ValueArray[EntryIndex]);
			}
			else
			{
				if (OverrideBoneIndices.Contains(AttributeInfo.BoneIndex))
				{
					TargetValueArray[ExistingAttributeIndex] = ValueArray[EntryIndex];
				}
			}
		}
	}

	/** Generates an array of bone indices which are considered highest weighted */
	template<typename DataType>
	static void DetermineOverrideBones(const FStackCustomAttributes& TargetAttributes, const FStackCustomAttributes& SourceAttributes, const TArrayView<const float> SourceBoneWeights, TSet<int32>& OutOverrideBoneIndices)
	{
		const TArray<int32, FAnimStackAllocator>& TargetBoneIndices = TargetAttributes.GetUniqueBoneIndices<DataType>();
		const TArray<int32, FAnimStackAllocator>& SourceBoneIndices = SourceAttributes.GetUniqueBoneIndices<DataType>();

		// if the bone is in both source 1 and 2, we write source highest weight
		for (const int32 BoneIndex : SourceBoneIndices)
		{
			const bool bTargetHasBone = TargetBoneIndices.Contains(BoneIndex);

			// If source one does not have the bone, or has the smaller weight, this bone attribute should be written for 
			if (!bTargetHasBone || SourceBoneWeights[BoneIndex] > 0.5f)
			{
				OutOverrideBoneIndices.Add(BoneIndex);
			}
		}
	}

	template<typename DataType>
	static void ProcessAttributesByBlendType(FStackCustomAttributes& TargetAttributes, const FStackCustomAttributes& SourceAttributes, int32 AttributeIndex, const TArray<FPerBoneBlendWeight>& BoneBlendWeights)
	{
		const TArray<FCustomAttributeInfo, FAnimStackAllocator>& AttributeInfos = SourceAttributes.GetAttributeInfo<DataType>();
		TArray<DataType, FAnimStackAllocator>& TargetValueArray = TargetAttributes.GetValuesArray<DataType>();
		const TArray<DataType, FAnimStackAllocator>& ValueArray = SourceAttributes.GetValuesArray<DataType>();

		for (int32 EntryIndex = 0; EntryIndex < AttributeInfos.Num(); ++EntryIndex)
		{
			const FCustomAttributeInfo& AttributeInfo = AttributeInfos[EntryIndex];

			const int32 BoneIndex = AttributeInfo.BoneIndex;
			const ECustomAttributeBlendType& BlendType = AttributeInfo.BlendType;

			if (BoneBlendWeights[BoneIndex].SourceIndex == AttributeIndex)
			{
				// Should override or add
				const int32 ExistingAttributeIndex = TargetAttributes.IndexOfBoneAttribute<DataType>(AttributeInfo.Hash, AttributeInfo.BoneIndex);
				const float Weight = BoneBlendWeights[BoneIndex].BlendWeight;
				const bool bHighestWeight = Weight > 0.5f;

				const DataType Value = [BlendType, ExistingAttributeIndex, bHighestWeight, Weight, EntryIndex , &ValueArray, &TargetValueArray]() -> DataType
				{
					if (BlendType == ECustomAttributeBlendType::Override)
					{
						if (ExistingAttributeIndex == INDEX_NONE || bHighestWeight)
						{
							return ValueArray[EntryIndex];
						}
					}

					if (BlendType == ECustomAttributeBlendType::Blend)
					{
						if (ExistingAttributeIndex == INDEX_NONE)
						{
							return ValueArray[EntryIndex] * Weight;
						}
						else
						{
							return TargetValueArray[ExistingAttributeIndex] + (ValueArray[EntryIndex] * Weight);
						}
					}

					return TargetValueArray[ExistingAttributeIndex];
				}();


				if (ExistingAttributeIndex == INDEX_NONE)
				{
					TargetAttributes.AddBoneAttribute<DataType>(AttributeInfo, Value);
				}
				else
				{
					TargetValueArray[ExistingAttributeIndex] = Value;
				}
			}
		}
	}

	template<typename DataType>
	static void AddOrOverrideAttributes(FStackCustomAttributes& TargetAttributes, const FStackCustomAttributes& SourceAttributes, int32 AttributeIndex, const TArray<FPerBoneBlendWeight>& BoneBlendWeights)
	{
		const TArray<FCustomAttributeInfo, FAnimStackAllocator>& AttributeInfos = SourceAttributes.GetAttributeInfo<DataType>();
		TArray<DataType, FAnimStackAllocator>& TargetValueArray = TargetAttributes.GetValuesArray<DataType>();
		const TArray<DataType, FAnimStackAllocator>& ValueArray = SourceAttributes.GetValuesArray<DataType>();

		for (int32 EntryIndex = 0; EntryIndex < AttributeInfos.Num(); ++EntryIndex)
		{
			const FCustomAttributeInfo& AttributeInfo = AttributeInfos[EntryIndex];
			const int32 BoneIndex = AttributeInfo.BoneIndex;
			if (BoneBlendWeights[BoneIndex].SourceIndex == AttributeIndex)
			{
				// Should override or add
				const int32 ExistingAttributeIndex = TargetAttributes.IndexOfBoneAttribute<DataType>(AttributeInfo.Hash, AttributeInfo.BoneIndex);
				if (ExistingAttributeIndex == INDEX_NONE)
				{
					TargetAttributes.AddBoneAttribute<DataType>(AttributeInfo, ValueArray[EntryIndex]);
				}
				// Override when highest weighted
				else if (BoneBlendWeights[BoneIndex].BlendWeight >= 0.5f)
				{
					TargetValueArray[ExistingAttributeIndex] = ValueArray[EntryIndex];
				}
			}
		}
	}

	template<typename DataType>
	static void ProcessAttributesByBlendType_PerBone(FStackCustomAttributes& TargetAttributes, const FStackCustomAttributes& SourceAttributesOne, const FStackCustomAttributes& SourceAttributesTwo, const TArrayView<const float> WeightsOfSourceTwo)
	{
		// Per bone weighted weighted override/add
		{
			const TArray<FCustomAttributeInfo, FAnimStackAllocator>& AttributeInfos = SourceAttributesOne.GetAttributeInfo<DataType>();
			TArray<DataType, FAnimStackAllocator>& TargetValueArray = TargetAttributes.GetValuesArray<DataType>();
			const TArray<DataType, FAnimStackAllocator>& ValueArray = SourceAttributesOne.GetValuesArray<DataType>();

			for (int32 EntryIndex = 0; EntryIndex < AttributeInfos.Num(); ++EntryIndex)
			{
				const FCustomAttributeInfo& AttributeInfo = AttributeInfos[EntryIndex];

				const int32 BoneIndex = AttributeInfo.BoneIndex;
				const int32 ExistingAttributeIndex = TargetAttributes.IndexOfBoneAttribute<DataType>(AttributeInfo.Hash, AttributeInfo.BoneIndex);
				const ECustomAttributeBlendType& BlendType = AttributeInfo.BlendType;

				// Inversed weight
				const float Weight = 1.f - WeightsOfSourceTwo[BoneIndex];
				const DataType Value = [BlendType, Weight, EntryIndex, &ValueArray]() -> DataType
				{
					if (BlendType == ECustomAttributeBlendType::Blend)
					{
						return ValueArray[EntryIndex] * Weight;
					}

					return ValueArray[EntryIndex];
				}();

				if (ExistingAttributeIndex == INDEX_NONE)
				{
					TargetAttributes.AddBoneAttribute<DataType>(AttributeInfo, Value);
				}
				else
				{
					TargetValueArray[ExistingAttributeIndex] = Value;
				}
			}
		}

		// Per bone accumulate
		{
			const TArray<FCustomAttributeInfo, FAnimStackAllocator>& AttributeInfos = SourceAttributesTwo.GetAttributeInfo<DataType>();
			TArray<DataType, FAnimStackAllocator>& TargetValueArray = TargetAttributes.GetValuesArray<DataType>();
			const TArray<DataType, FAnimStackAllocator>& ValueArray = SourceAttributesTwo.GetValuesArray<DataType>();

			for (int32 EntryIndex = 0; EntryIndex < AttributeInfos.Num(); ++EntryIndex)
			{
				const FCustomAttributeInfo& AttributeInfo = AttributeInfos[EntryIndex];

				const int32 BoneIndex = AttributeInfo.BoneIndex;
				const int32 ExistingAttributeIndex = TargetAttributes.IndexOfBoneAttribute<DataType>(AttributeInfo.Hash, AttributeInfo.BoneIndex);
				const ECustomAttributeBlendType& BlendType = AttributeInfo.BlendType;

				const float Weight = WeightsOfSourceTwo[BoneIndex];

				const bool bHighestWeight = Weight > 0.5f;
				const DataType Value = [BlendType, ExistingAttributeIndex, Weight, EntryIndex, bHighestWeight, &ValueArray, &TargetValueArray]() -> DataType
				{
					if (BlendType == ECustomAttributeBlendType::Override)
					{
						if (ExistingAttributeIndex == INDEX_NONE || bHighestWeight)
						{
							return ValueArray[EntryIndex];
						}
					}

					if (BlendType == ECustomAttributeBlendType::Blend)
					{
						if (ExistingAttributeIndex == INDEX_NONE)
						{
							return ValueArray[EntryIndex] * Weight;
						}
						else
						{
							return TargetValueArray[ExistingAttributeIndex] + (ValueArray[EntryIndex] * Weight);
						}
					}

					return TargetValueArray[ExistingAttributeIndex];
				}();

				if (ExistingAttributeIndex == INDEX_NONE)
				{
					TargetAttributes.AddBoneAttribute<DataType>(AttributeInfo, Value);
				}
				else
				{
					TargetValueArray[ExistingAttributeIndex] = Value;
				}
			}
		}
	}
}

namespace Blending
{
	template<typename DataType>
	static void ProcessAttributesByBlendType(FStackCustomAttributes& TargetAttributes, const FStackCustomAttributes& SourceAttributes, float Weight, bool bHighestWeight)
	{
		const TArray<FCustomAttributeInfo, FAnimStackAllocator>& AttributeInfos = SourceAttributes.GetAttributeInfo<DataType>();
		TArray<DataType, FAnimStackAllocator>& TargetValueArray = TargetAttributes.GetValuesArray<DataType>();
		const TArray<DataType, FAnimStackAllocator>& ValueArray = SourceAttributes.GetValuesArray<DataType>();

		for (int32 EntryIndex = 0; EntryIndex < AttributeInfos.Num(); ++EntryIndex)
		{
			const FCustomAttributeInfo& AttributeInfo = AttributeInfos[EntryIndex];

			const int32 BoneIndex = AttributeInfo.BoneIndex;
			const ECustomAttributeBlendType& BlendType = AttributeInfo.BlendType;

			const int32 ExistingAttributeIndex = TargetAttributes.IndexOfBoneAttribute<DataType>(AttributeInfo.Hash, AttributeInfo.BoneIndex);
			const DataType Value = [BlendType, ExistingAttributeIndex, Weight, EntryIndex, bHighestWeight, &ValueArray, &TargetValueArray]() -> DataType
			{
				if (BlendType == ECustomAttributeBlendType::Override)
				{
					if (ExistingAttributeIndex == INDEX_NONE || bHighestWeight)
					{
						return ValueArray[EntryIndex];
					}
				}

				if (BlendType == ECustomAttributeBlendType::Blend)
				{
					if (ExistingAttributeIndex == INDEX_NONE)
					{
						return ValueArray[EntryIndex] * Weight;
					}
					else
					{
						return TargetValueArray[ExistingAttributeIndex] + (ValueArray[EntryIndex] * Weight);
					}
				}
				return TargetValueArray[ExistingAttributeIndex];
			}();

			if (ExistingAttributeIndex == INDEX_NONE)
			{
				TargetAttributes.AddBoneAttribute<DataType>(AttributeInfo, Value);
			}
			else
			{
				TargetValueArray[ExistingAttributeIndex] = Value;
			}
		}
	}

	template<typename DataType>
	static void AddOrOverrideAttributes(FStackCustomAttributes& TargetAttributes, const FStackCustomAttributes& SourceAttributes, bool bOverride)
	{
		const TArray<FCustomAttributeInfo, FAnimStackAllocator>& AttributeInfos = SourceAttributes.GetAttributeInfo<DataType>();
		TArray<DataType, FAnimStackAllocator>& TargetValueArray = TargetAttributes.GetValuesArray<DataType>();
		const TArray<DataType, FAnimStackAllocator>& ValueArray = SourceAttributes.GetValuesArray<DataType>();

		for (int32 EntryIndex = 0; EntryIndex < AttributeInfos.Num(); ++EntryIndex)
		{
			const FCustomAttributeInfo& AttributeInfo = AttributeInfos[EntryIndex];

			const int32 ExistingAttributeIndex = TargetAttributes.IndexOfBoneAttribute<DataType>(AttributeInfo.Hash, AttributeInfo.BoneIndex);
			if (ExistingAttributeIndex == INDEX_NONE)
			{
				TargetAttributes.AddBoneAttribute<DataType>(AttributeInfo, ValueArray[EntryIndex]);
			}
			else if (bOverride)
			{
				TargetValueArray[ExistingAttributeIndex] = ValueArray[EntryIndex];
			}
		}
	}

	template<typename DataType>
	static void AddOrOverrideWeightedAttributes(FStackCustomAttributes& TargetAttributes, const FStackCustomAttributes& SourceAttributes, bool bOverride, float Weight)
	{
		const TArray<FCustomAttributeInfo, FAnimStackAllocator>& AttributeInfos = SourceAttributes.GetAttributeInfo<DataType>();
		TArray<DataType, FAnimStackAllocator>& TargetValueArray = TargetAttributes.GetValuesArray<DataType>();
		const TArray<DataType, FAnimStackAllocator>& ValueArray = SourceAttributes.GetValuesArray<DataType>();

		for (int32 EntryIndex = 0; EntryIndex < AttributeInfos.Num(); ++EntryIndex)
		{
			const FCustomAttributeInfo& AttributeInfo = AttributeInfos[EntryIndex];

			const int32 ExistingAttributeIndex = TargetAttributes.IndexOfBoneAttribute<DataType>(AttributeInfo.Hash, AttributeInfo.BoneIndex);

			const DataType WeightedValue = ValueArray[EntryIndex] * Weight;
			if (ExistingAttributeIndex == INDEX_NONE)
			{
				TargetAttributes.AddBoneAttribute<DataType>(AttributeInfo, WeightedValue);
			}
			else if (bOverride)
			{
				TargetValueArray[ExistingAttributeIndex] = WeightedValue;
			}
		}
	}
}

namespace URO
{
	template<typename DataType>
	static void InterpolateAttributes(FHeapCustomAttributes& TargetAttributes, const FHeapCustomAttributes& SourceAttributes, float Alpha)
	{
		const TArray<FCustomAttributeInfo, FDefaultAllocator>& AttributeInfos = SourceAttributes.GetAttributeInfo<DataType>();
		TArray<DataType, FDefaultAllocator>& TargetValueArray = TargetAttributes.GetValuesArray<DataType>();
		const TArray<DataType, FDefaultAllocator>& ValueArray = SourceAttributes.GetValuesArray<DataType>();

		for (int32 EntryIndex = 0; EntryIndex < AttributeInfos.Num(); ++EntryIndex)
		{
			const FCustomAttributeInfo& AttributeInfo = AttributeInfos[EntryIndex];

			// Can only interpolate between attributes that exist in both containers
			const int32 ExistingAttributeIndex = TargetAttributes.IndexOfBoneAttribute<DataType>(AttributeInfo.Hash, AttributeInfo.BoneIndex);
			if (ExistingAttributeIndex != INDEX_NONE)
			{
				TargetValueArray[ExistingAttributeIndex] = FMath::Lerp(TargetValueArray[ExistingAttributeIndex], ValueArray[EntryIndex], Alpha);
			}
		}
	}


	template<typename DataType>
	static void AddOrOverrideAttributes(FHeapCustomAttributes& TargetAttributes, const FHeapCustomAttributes& SourceAttributes, bool bOverride)
	{
		const TArray<FCustomAttributeInfo, FDefaultAllocator>& AttributeInfos = SourceAttributes.GetAttributeInfo<DataType>();
		TArray<DataType, FDefaultAllocator>& TargetValueArray = TargetAttributes.GetValuesArray<DataType>();
		const TArray<DataType, FDefaultAllocator>& ValueArray = SourceAttributes.GetValuesArray<DataType>();

		for (int32 EntryIndex = 0; EntryIndex < AttributeInfos.Num(); ++EntryIndex)
		{
			const FCustomAttributeInfo& AttributeInfo = AttributeInfos[EntryIndex];

			const int32 ExistingAttributeIndex = TargetAttributes.IndexOfBoneAttribute<DataType>(AttributeInfo.Hash, AttributeInfo.BoneIndex);
			if (ExistingAttributeIndex == INDEX_NONE)
			{
				TargetAttributes.AddBoneAttribute<DataType>(AttributeInfo, ValueArray[EntryIndex]);
			}
			else if (bOverride)
			{
				TargetValueArray[ExistingAttributeIndex] = ValueArray[EntryIndex];
			}
		}
	}

	template<typename DataType>
	static void OverrideAttributes(FHeapCustomAttributes& TargetAttributes, const FHeapCustomAttributes& SourceAttributes)
	{
		const TArray<FCustomAttributeInfo, FDefaultAllocator>& AttributeInfos = SourceAttributes.GetAttributeInfo<DataType>();
		TArray<DataType, FDefaultAllocator>& TargetValueArray = TargetAttributes.GetValuesArray<DataType>();
		const TArray<DataType, FDefaultAllocator>& ValueArray = SourceAttributes.GetValuesArray<DataType>();

		for (int32 EntryIndex = 0; EntryIndex < AttributeInfos.Num(); ++EntryIndex)
		{
			const FCustomAttributeInfo& AttributeInfo = AttributeInfos[EntryIndex];

			// Can only interpolate between attributes that exist in both containers
			const int32 ExistingAttributeIndex = TargetAttributes.IndexOfBoneAttribute<DataType>(AttributeInfo.Hash, AttributeInfo.BoneIndex);
			if (ExistingAttributeIndex != INDEX_NONE)
			{
				TargetValueArray[ExistingAttributeIndex] = ValueArray[EntryIndex];
			}
		}
	}
}


