// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include <UObject/ObjectMacros.h>
#include "UObject/NameTypes.h"
#include "Misc/Variant.h"
#include "Animation/AnimTypes.h"
#include "BoneIndices.h"
#include "Templates/Tuple.h"
#include "Curves/StringCurve.h"
#include "Curves/IntegralCurve.h"
#include "Curves/SimpleCurve.h"

#include "CustomAttributes.generated.h"

UENUM(Experimental)
enum class ECustomAttributeBlendType : uint8
{
	/** Overrides Custom attributes according to highest weighted pose */
	Override,
	/** Blends Custom attributes according to weights per pose */
	Blend
};

USTRUCT()
struct ENGINE_API FCustomAttributeSetting
{
	GENERATED_BODY()

	/** Name of the custom attribute */
	UPROPERTY(EditAnywhere, Category = CustomAttributeSetting)
	FString Name;

	/** Optional property describing the meaning (or role) of the custom attribute, allowing to add context to an attribute */
	UPROPERTY(EditAnywhere, Category = CustomAttributeSetting)
	FString Meaning;
};

USTRUCT(Experimental)
struct ENGINE_API FCustomAttribute
{
	GENERATED_BODY()

	/** Name of this attribute */
	UPROPERTY(VisibleAnywhere, Category = CustomAttribute)
	FName Name;

	/** (FVariant) type contained by Values array */
	UPROPERTY(VisibleAnywhere, Category = CustomAttribute)
	int32 VariantType = 0;

	/** Time keys (should match number of Value entries) */
	UPROPERTY(VisibleAnywhere, Category = CustomAttributeBoneData)
	TArray<float> Times;
		
	/** Value keys (should match number of Times entries) */
	TArray<FVariant> Values;

	bool Serialize(FArchive& Ar)
	{
		Ar << Name;
		Ar << VariantType;
		Ar << Times;
		Ar << Values;

		return true;
	}
};

// Custom serializer required for FVariant array
template<>
struct TStructOpsTypeTraits<FCustomAttribute> : public TStructOpsTypeTraitsBase2<FCustomAttribute>
{
	enum
	{
		WithSerializer = true
	};
};

/** Structure describing custom attributes for a single bone (index) */
USTRUCT(Experimental)
struct ENGINE_API FCustomAttributePerBoneData
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category = CustomAttributeBoneData)
	int32 BoneTreeIndex = 0;

	UPROPERTY(VisibleAnywhere, EditFixedSize, Category = CustomAttributeBoneData)
	TArray<FCustomAttribute> Attributes;
};

/** (Baked) string custom attribute, uses FStringCurve for evaluation instead of FVariant array */
USTRUCT(Experimental)
struct FBakedStringCustomAttribute
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category = CustomAttributeBoneData)
	FName AttributeName;

	UPROPERTY(VisibleAnywhere, Category = CustomAttributeBoneData)
	FStringCurve StringCurve;
};

/** (Baked) int32 custom attribute, uses FIntegralCurve for evaluation instead of FVariant array */
USTRUCT(Experimental)
struct FBakedIntegerCustomAttribute
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category = CustomAttributeBoneData)
	FName AttributeName;

	UPROPERTY(VisibleAnywhere, Category = CustomAttributeBoneData)
	FIntegralCurve IntCurve;
};

/** (Baked) float custom attribute, uses FSimpleCurve for evaluation instead of FVariant array */
USTRUCT(Experimental)
struct FBakedFloatCustomAttribute
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category = CustomAttributeBoneData)
	FName AttributeName;

	UPROPERTY(VisibleAnywhere, Category = CustomAttributeBoneData)
	FSimpleCurve FloatCurve;
};

/** Structure describing baked custom attributes for a single bone (index) */
USTRUCT(Experimental)
struct FBakedCustomAttributePerBoneData
{
	GENERATED_BODY()

	UPROPERTY()
	int32 BoneTreeIndex = 0;

	UPROPERTY(VisibleAnywhere, EditFixedSize, Category = CustomAttributeBoneData)
	TArray<FBakedStringCustomAttribute> StringAttributes;

	UPROPERTY(VisibleAnywhere, EditFixedSize, Category = CustomAttributeBoneData)
	TArray<FBakedIntegerCustomAttribute> IntAttributes;

	UPROPERTY(VisibleAnywhere, EditFixedSize, Category = CustomAttributeBoneData)
	TArray<FBakedFloatCustomAttribute> FloatAttributes;
};
