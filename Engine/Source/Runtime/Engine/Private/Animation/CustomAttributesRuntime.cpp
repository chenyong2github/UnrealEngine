// Copyright Epic Games, Inc. All Rights Reserved.

#include "Animation/CustomAttributesRuntime.h"
#include "Animation/CustomAttributes.h"
#include "Animation/AnimTypes.h"
#include "Animation/AnimationSettings.h"
#include "Animation/AnimationAsset.h"

#include "Containers/UnrealString.h"
#include "Containers/Array.h"
#include "Containers/ArrayView.h"
#include "Containers/Map.h"
#include "Misc/AssertionMacros.h"
#include "Stats/Stats.h"
#include "BoneIndices.h"
#include "BoneContainer.h" 

#include "CustomAttributesRuntimeHelpers.h"

FCustomAttributeInfo::FCustomAttributeInfo(const FName& InName, const FCompactPoseBoneIndex& InCompactBoneIndex, const ECustomAttributeBlendType& InBlendType) : BoneIndex(InCompactBoneIndex.GetInt()), BlendType(InBlendType)
{
	Hash = HashCombine(GetTypeHash(BoneIndex), GetTypeHash(InName));
}


#if WITH_EDITOR
void FCustomAttributesRuntime::GetAttributeValue(FStackCustomAttributes& OutAttributes, const FCompactPoseBoneIndex& PoseBoneIndex, const FCustomAttribute& Attribute, const FAnimExtractContext& ExtractionContext)
{
	ECustomAttributeBlendType BlendType = ECustomAttributeBlendType::Override;
	
	// Evaluate the time/typed value arrays to retrieve the attribute value for the provided time value
	const EVariantTypes VariantType = (EVariantTypes)Attribute.VariantType;
	switch (VariantType)
	{
		case EVariantTypes::Float:
		{
			const float Value = CustomAttributeEvaluation::GetTypedAttributeValue<float>(Attribute, ExtractionContext.CurrentTime);
			OutAttributes.AddBoneAttribute<float>(PoseBoneIndex, Attribute.Name, BlendType, Value);
			break;
		}

		case EVariantTypes::Int32:
		{
			const int32 Value = CustomAttributeEvaluation::GetTypedAttributeValue<int32>(Attribute, ExtractionContext.CurrentTime);
			OutAttributes.AddBoneAttribute<int32>(PoseBoneIndex, Attribute.Name, BlendType, Value);
			break;
		}

		case EVariantTypes::String:
		{
			const FString Value = CustomAttributeEvaluation::GetTypedAttributeValue<FString>(Attribute, ExtractionContext.CurrentTime);
			OutAttributes.AddBoneAttribute<FString>(PoseBoneIndex, Attribute.Name, ECustomAttributeBlendType::Override, Value);
			break;
		}

		default:
		{
			check(false);
			break;
		}
	}
}

void FCustomAttributesRuntime::GetAttributeValue(const struct FCustomAttribute& Attribute, float Time, float& OutValue)
{
	OutValue = CustomAttributeEvaluation::GetTypedAttributeValue<float>(Attribute, Time);
}

void FCustomAttributesRuntime::GetAttributeValue(const struct FCustomAttribute& Attribute, float Time, int32& OutValue)
{
	OutValue = CustomAttributeEvaluation::GetTypedAttributeValue<int32>(Attribute, Time);
}

void FCustomAttributesRuntime::GetAttributeValue(const struct FCustomAttribute& Attribute, float Time, FString& OutValue)
{
	OutValue = CustomAttributeEvaluation::GetTypedAttributeValue<FString>(Attribute, Time);
}
#endif // WITH_EDITOR

void FCustomAttributesRuntime::BlendAttributes(const TArrayView<const FStackCustomAttributes> SourceAttributes, const TArrayView<const float> SourceWeights, FStackCustomAttributes& OutAttributes)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_BlendAttributes);

	if (SourceAttributes.Num())
	{
		float MaxWeight = -1.f;
		// From here, only add attributes that do not exist yet or override those that do exist if these attributes have a higher weight (meaning we end up with the highest weighted ones for each in SourceAttributes)
		const int32 NumCompactAttributes = SourceAttributes.Num();
		for (int32 Index = 0; Index < NumCompactAttributes; ++Index)
		{
			const float AttributeWeight = SourceWeights[Index];

			if (FAnimWeight::IsRelevant(AttributeWeight))
			{
				const FStackCustomAttributes& Attributes = SourceAttributes[Index];

				// Determine if this is the highest weight so far processed, if so previous attribute values should be overriden (when the attribute uses the Override blend mode)
				const bool bHigherWeight = AttributeWeight > MaxWeight;
				MaxWeight = FMath::Max(MaxWeight, AttributeWeight);

				Blending::ProcessAttributesByBlendType<float>(OutAttributes, Attributes, AttributeWeight, bHigherWeight);
				Blending::ProcessAttributesByBlendType<int32>(OutAttributes, Attributes, AttributeWeight, bHigherWeight);
				Blending::AddOrOverrideAttributes<FString>(OutAttributes, Attributes, bHigherWeight);
			}
		}
	}
}

void FCustomAttributesRuntime::BlendAttributes(const TArrayView<const FStackCustomAttributes* const> SourceAttributes, const TArrayView<const float> SourceWeights, FStackCustomAttributes& OutAttributes)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_BlendAttributes_Indirect);

	if (SourceAttributes.Num())
	{
		float MaxWeight = -1.f;
		// From here, only add attributes that do not exist yet or override those that do exist if these attributes have a higher weight (meaning we end up with the highest weighted ones for each in SourceAttributes)
		const int32 NumCompactAttributes = SourceAttributes.Num();
		for (int32 Index = 0; Index < NumCompactAttributes; ++Index)
		{
			const float AttributeWeight = SourceWeights[Index];

			if (FAnimWeight::IsRelevant(AttributeWeight))
			{
				const FStackCustomAttributes& Attributes = *SourceAttributes[Index];

				// Determine if this is the highest weight so far processed, if so previous attribute values should be overriden (when the attribute uses the Override blend mode)
				const bool bHigherWeight = AttributeWeight > MaxWeight;
				MaxWeight = FMath::Max(MaxWeight, AttributeWeight);

				Blending::ProcessAttributesByBlendType<float>(OutAttributes, Attributes, AttributeWeight, bHigherWeight);
				Blending::ProcessAttributesByBlendType<int32>(OutAttributes, Attributes, AttributeWeight, bHigherWeight);
				Blending::AddOrOverrideAttributes<FString>(OutAttributes, Attributes, bHigherWeight);
			}
		}
	}
}

void FCustomAttributesRuntime::BlendAttributes(const TArrayView<const FStackCustomAttributes> SourceAttributes, const TArrayView<const float> SourceWeights, const TArrayView<const int32> SourceWeightsIndices, FStackCustomAttributes& OutAttributes)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_BlendAttributes_WeightsIndices);
	if (SourceAttributes.Num())
	{
		float MaxWeight = -1.f;
		const int32 NumCompactAttributes = SourceAttributes.Num();
		for (int32 Index = 0; Index < NumCompactAttributes; ++Index)
		{
			const int32 AttributeIndex = SourceWeightsIndices[Index];
			const float AttributeWeight = SourceWeights[AttributeIndex];

			if (FAnimWeight::IsRelevant(AttributeWeight))
			{
				const FStackCustomAttributes& Attributes = SourceAttributes[Index];

				// Determine if this is the highest weight so far processed, if so previous attribute values should be overriden (when the attribute uses the Override blend mode)
				const bool bHigherWeight = AttributeWeight > MaxWeight;
				MaxWeight = FMath::Max(MaxWeight, AttributeWeight);

				Blending::ProcessAttributesByBlendType<float>(OutAttributes, Attributes, AttributeWeight, bHigherWeight);
				Blending::ProcessAttributesByBlendType<int32>(OutAttributes, Attributes, AttributeWeight, bHigherWeight);
				Blending::AddOrOverrideAttributes<FString>(OutAttributes, Attributes, bHigherWeight);
			}
		}
	}
}

void FCustomAttributesRuntime::OverrideAttributes(const FStackCustomAttributes& SourceAttributes, FStackCustomAttributes& OutAttributes, float Weight)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_OverrideAttributes_Weighted);

	const bool bShouldOverride = true;

	if (FMath::IsNearlyEqual(Weight, 1.f))
	{	
		Blending::AddOrOverrideAttributes<float>(OutAttributes, SourceAttributes, bShouldOverride);
		Blending::AddOrOverrideAttributes<int32>(OutAttributes, SourceAttributes, bShouldOverride);
		Blending::AddOrOverrideAttributes<FString>(OutAttributes, SourceAttributes, bShouldOverride);
	}	
	else
	{
		Blending::AddOrOverrideWeightedAttributes<float>(OutAttributes, SourceAttributes, bShouldOverride, Weight);
		Blending::AddOrOverrideWeightedAttributes<int32>(OutAttributes, SourceAttributes, bShouldOverride, Weight);
		// String can not be weighted
		Blending::AddOrOverrideAttributes<FString>(OutAttributes, SourceAttributes, bShouldOverride);
	}
}

void FCustomAttributesRuntime::AccumulateAttributes(const FStackCustomAttributes& SourceAttributes, FStackCustomAttributes& OutAttributes, float Weight)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_AccumulateAttributes);

	if (FAnimWeight::IsRelevant(Weight))
	{
		AdditiveBlending::AccumulateAttributes_Weight<float>(OutAttributes, SourceAttributes, Weight);
		AdditiveBlending::AccumulateAttributes_Weight<int32>(OutAttributes, SourceAttributes, Weight);
		// Add any not yet existing string attributes
		Blending::AddOrOverrideAttributes<FString>(OutAttributes, SourceAttributes, false);
	}
}

void FCustomAttributesRuntime::SubtractAttributes(const FStackCustomAttributes& SourceAttributes, FStackCustomAttributes& OutAttributes)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_SubtractAttributes);

	AdditiveBlending::SubtractTypedAttributes<float>(OutAttributes, SourceAttributes);
	AdditiveBlending::SubtractTypedAttributes<int32>(OutAttributes, SourceAttributes);
	// Cannot subtract string attributes
}

void FCustomAttributesRuntime::CopyAndRemapAttributes(const FHeapCustomAttributes& SourceAttributes, FStackCustomAttributes& OutAttributes, const TMap<int32, int32>& BoneMapToSource, const FBoneContainer& RequiredBones)
{
	CopyPoseFromMesh::CopyAndRemapTypedAttributes<float>(OutAttributes, SourceAttributes, BoneMapToSource, RequiredBones);
	CopyPoseFromMesh::CopyAndRemapTypedAttributes<int32>(OutAttributes, SourceAttributes, BoneMapToSource, RequiredBones);
	CopyPoseFromMesh::CopyAndRemapTypedAttributes<FString>(OutAttributes, SourceAttributes, BoneMapToSource, RequiredBones);
}

void FCustomAttributesRuntime::InterpolateAttributes(const FHeapCustomAttributes& SourceAttributes, FHeapCustomAttributes& OutAttributes, float Alpha)
{
	if (!FAnimWeight::IsRelevant(FMath::Abs(Alpha)))
	{
		return;
	}
	else if (!FAnimWeight::IsRelevant(FMath::Abs(Alpha - 1.0f)))
	{
		// If fully blended just directly override the values
		URO::AddOrOverrideAttributes<float>(OutAttributes, SourceAttributes, true);		
		URO::AddOrOverrideAttributes<int32>(OutAttributes, SourceAttributes, true);
		URO::AddOrOverrideAttributes<FString>(OutAttributes, SourceAttributes, true);
	}
	else
	{
		URO::InterpolateAttributes<float>(OutAttributes, SourceAttributes, Alpha);
		URO::InterpolateAttributes<int32>(OutAttributes, SourceAttributes, Alpha);

		if (FMath::Abs(Alpha) > 0.5f)
		{
			URO::OverrideAttributes<FString>(OutAttributes, SourceAttributes);
		}
	}
}

void FCustomAttributesRuntime::BlendAttributesPerBone(const FStackCustomAttributes& SourceAttributes1, const FStackCustomAttributes& SourceAttributes2, const TArrayView<const float> WeightsOfSource2, FStackCustomAttributes& OutAttributes)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_BlendAttributesPerBone);

	PerBoneBlending::ProcessAttributesByBlendType_PerBone<float>(OutAttributes, SourceAttributes1, SourceAttributes2, WeightsOfSource2);
	PerBoneBlending::ProcessAttributesByBlendType_PerBone<int32>(OutAttributes, SourceAttributes1, SourceAttributes2, WeightsOfSource2);

	// Start with attributes from source one
	Blending::AddOrOverrideAttributes<FString>(OutAttributes, SourceAttributes1, true);

	// Override any attributes (per-bone) where valid
	TSet<int32> OverrideBoneIndices;
	PerBoneBlending::DetermineOverrideBones<FString>(SourceAttributes1, SourceAttributes2, WeightsOfSource2, OverrideBoneIndices);
	PerBoneBlending::AddOrOverrideAttributes<FString>(OutAttributes, SourceAttributes2, OverrideBoneIndices);
}

void FCustomAttributesRuntime::BlendAttributesPerBoneFilter(const TArrayView<const FStackCustomAttributes> BlendAttributes, const TArray<FPerBoneBlendWeight>& BoneBlendWeights, FStackCustomAttributes& OutAttributes)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_BlendAttributesPerBoneFilter);
	for (int32 AttributeIndex = 0; AttributeIndex < BlendAttributes.Num(); ++AttributeIndex)
	{
		const FStackCustomAttributes& Attribute = BlendAttributes[AttributeIndex];

		PerBoneBlending::ProcessAttributesByBlendType<float>(OutAttributes, Attribute, AttributeIndex, BoneBlendWeights);
		PerBoneBlending::ProcessAttributesByBlendType<int32>(OutAttributes, Attribute, AttributeIndex, BoneBlendWeights);
		PerBoneBlending::AddOrOverrideAttributes<FString>(OutAttributes, Attribute, AttributeIndex, BoneBlendWeights);
	}
}

ECustomAttributeBlendType FCustomAttributesRuntime::GetAttributeBlendType(const FName& InName)
{
	const UAnimationSettings* Settings = UAnimationSettings::Get();
	const ECustomAttributeBlendType* ModePtr = Settings->AttributeBlendModes.Find(InName);
	return ModePtr ? *ModePtr : Settings->DefaultAttributeBlendMode;
}
