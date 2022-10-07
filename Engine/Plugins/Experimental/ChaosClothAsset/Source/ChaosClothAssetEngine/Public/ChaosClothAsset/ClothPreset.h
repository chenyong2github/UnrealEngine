// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "ClothPreset.generated.h"

namespace UE::Chaos::ClothAsset { class FClothPresetCollection; }

/**
 * Preset property categories.
 */
UENUM()
enum class EChaosClothPresetPropertyCategory : uint8
{
	Mass,
	Constraint,
	Collision,
	Environmental,
	Animation,
	Experimental,
	Custom
};

/**
 * Preset property types.
 */
UENUM()
enum class EChaosClothPresetPropertyType : uint8
{
	Boolean,
	Integer,
	Float,
	Vector3Integer,
	Vector3Float,
	String
};

template <typename T> struct TIsClothPresetPropertyNonStringType { enum { Value = false }; };
template<> struct TIsClothPresetPropertyNonStringType<bool> { enum { Value = true }; };
template<> struct TIsClothPresetPropertyNonStringType<int32> { enum { Value = true }; };
template<> struct TIsClothPresetPropertyNonStringType<float> { enum { Value = true }; };
template<> struct TIsClothPresetPropertyNonStringType<FIntVector3> { enum { Value = true }; };
template<> struct TIsClothPresetPropertyNonStringType<FVector3f> { enum { Value = true }; };

template <typename T> struct TIsClothPresetPropertyType { enum { Value = TIsClothPresetPropertyNonStringType<T>::Value }; };
template<> struct TIsClothPresetPropertyType<FString> { enum { Value = true }; };

/**
 * Structure used to describe simulation properties.
 */
USTRUCT()
struct FChaosClothPresetPropertyDescriptor
{
	GENERATED_BODY()

	static const FName MassCategory;
	static const FName ConstraintCategory;
	static const FName CollisionCategory;
	static const FName EnvironmentalCategory;
	static const FName AnimationCategory;
	static const FName CustomCategory;

	UPROPERTY(EditAnywhere, Category = "Property Descriptor")
	FName Name;

	UPROPERTY(EditAnywhere, Category = "Property Descriptor")
	EChaosClothPresetPropertyType Type = EChaosClothPresetPropertyType::Vector3Float;

	UPROPERTY(EditAnywhere, Category = "Property Descriptor")
	EChaosClothPresetPropertyCategory Category = EChaosClothPresetPropertyCategory::Custom;

	UPROPERTY(EditAnywhere, Category = "Property Descriptor")
	FText Description;

	UPROPERTY(EditAnywhere, Category = "Property Descriptor")
	FVector3f DefaultValue = FVector3f::ZeroVector;  // Can only be used for continuous integer values from -16777216 to 16777216

	UPROPERTY(EditAnywhere, Category = "Property Descriptor")
	FString DefaultString;  // Only used for string type

	UPROPERTY(EditAnywhere, Category = "Property Descriptor")
	bool bDefaultEnable = true;

	UPROPERTY(EditAnywhere, Category = "Property Descriptor")
	bool bDefaultAnimatable = false;

	UPROPERTY(EditAnywhere, Category = "Property Descriptor")
	float UIMin = 0.f;

	UPROPERTY(EditAnywhere, Category = "Property Descriptor")
	float UIMax = 1000.f;

	UPROPERTY(EditAnywhere, Category = "Property Descriptor")
	float ClampMin = TNumericLimits<float>::Min();

	UPROPERTY(EditAnywhere, Category = "Property Descriptor")
	float ClampMax = TNumericLimits<float>::Max();

	UPROPERTY(EditAnywhere, Category = "Property Descriptor")
	FString EditCondition;

	FChaosClothPresetPropertyDescriptor() = default;

	/** Minimal viable constructor for use in UChaosClothPreset. */
	FChaosClothPresetPropertyDescriptor(
		const FName& InName,
		EChaosClothPresetPropertyType InType)
		: Name(InName)
		, Type(InType)
	{}

	template<typename T>
	static typename TEnableIf<TIsClothPresetPropertyType<T>::Value, EChaosClothPresetPropertyType>::Type GetType();
};

/**
 * Class used to generate custom list of simulation properties.
 */
UCLASS()
class UChaosClothPresetPropertyGenerator : public UClass
{
	GENERATED_BODY()

public:

};

/**
 * Tailored cloth simulation preset.
 */
UCLASS(hidecategories = Object, BlueprintType)
class CHAOSCLOTHASSETENGINE_API UChaosClothPreset : public UObject
{
	GENERATED_BODY()
public:
	UChaosClothPreset(const FObjectInitializer& ObjectInitializer);

	void Serialize(FArchive& Ar);

	/** Add a new property, return false if the property already exists or true otherwise. */
	bool AddProperty(const FName& Name, EChaosClothPresetPropertyType Type);

	/** Set non string properties. */
	template<typename T>
	inline typename TEnableIf<TIsClothPresetPropertyNonStringType<T>::Value, void>::Type SetProperty(
		const FName& Name,
		T LowValue,
		T HighValue,
		const FString& StringValue,
		bool bEnable,
		bool bAnimatable);

	/** Set string properties. */
	void SetProperty(const FName& Name, const FString& StringValue, bool bEnable, bool bAnimatable);

	/** Set (low) value. */
	template<typename T>
	inline typename TEnableIf<TIsClothPresetPropertyNonStringType<T>::Value, void>::Type SetPropertyValue(
		const FName& Name,
		T Value);

	/** Set low and high value. */
	template<typename T>
	inline typename TEnableIf<TIsClothPresetPropertyNonStringType<T>::Value, void>::Type SetPropertyValues(
		const FName& Name,
		T LowValue,
		T HighValue);

	/** Set string value. */
	void SetPropertyStringValue(const FName& Name, const FString& StringValue);

	/** Set property enabled. */
	void SetPropertyEnable(const FName& Name, bool bEnable);

	/** Set property animatable. */
	void SetPropertyAnimatable(const FName& Name, bool bAnimatable);

private:
	const FChaosClothPresetPropertyDescriptor* GetPropertyDescriptor(const FName& Name) const;

	int32 GetPropertyElementIndex(const FChaosClothPresetPropertyDescriptor& PropertyDescriptor) const;

	void SetPropertyInternal(
		const FChaosClothPresetPropertyDescriptor& PropertyDescriptor,
		const FVector3f& LowValue,
		const FVector3f& HighValue,
		const FString& StringValue,
		bool bEnable,
		bool bAnimatable);

	void SetPropertyValueInternal(const FChaosClothPresetPropertyDescriptor& PropertyDescriptor, const FVector3f& Value);
	void SetPropertyValuesInternal(const FChaosClothPresetPropertyDescriptor& PropertyDescriptor, const FVector3f& LowValue, const FVector3f& HighValue);

	void SyncProperty(const FChaosClothPresetPropertyDescriptor& PropertyDescriptor);
	void SyncCollection();

	UPROPERTY(EditAnywhere, Category = "PropertyDescriptor Editor")
	TArray<FChaosClothPresetPropertyDescriptor> PropertyDescriptors;

	TSharedPtr<UE::Chaos::ClothAsset::FClothPresetCollection, ESPMode::ThreadSafe> ClothPresetCollection;
};

template<typename T>
typename TEnableIf<TIsClothPresetPropertyNonStringType<T>::Value, void>::Type UChaosClothPreset::SetProperty(
	const FName& Name,
	T LowValue,
	T HighValue,
	const FString& StringValue,
	bool bEnable,
	bool bAnimatable)
{
	const FChaosClothPresetPropertyDescriptor* const PropertyDescriptor = GetPropertyDescriptor(Name);
	if (ensure(PropertyDescriptor) && ensure(PropertyDescriptor->Type == FChaosClothPresetPropertyDescriptor::GetType<T>()))
	{
		const FVector3f LowValueClamped = ClampVector(FVector3f(LowValue), FVector3f(PropertyDescriptor->ClampMin), FVector3f(PropertyDescriptor->ClampMax));
		const FVector3f HighValueClamped = ClampVector(FVector3f(HighValue), FVector3f(PropertyDescriptor->ClampMin), FVector3f(PropertyDescriptor->ClampMax));
		SetPropertyInternal(*PropertyDescriptor, LowValueClamped, HighValueClamped, StringValue, bEnable, bAnimatable);
	}
}

template<typename T>
typename TEnableIf<TIsClothPresetPropertyNonStringType<T>::Value, void>::Type UChaosClothPreset::SetPropertyValue(
	const FName& Name,
	T Value)
{
	const FChaosClothPresetPropertyDescriptor* const PropertyDescriptor = GetPropertyDescriptor(Name);
	if (ensure(PropertyDescriptor) && ensure(PropertyDescriptor->Type == FChaosClothPresetPropertyDescriptor::GetType<T>()))
	{
		const FVector3f ValueClamped = ClampVector(FVector3f(Value), FVector3f(PropertyDescriptor->ClampMin), FVector3f(PropertyDescriptor->ClampMax));
		SetPropertyValueInternal(*PropertyDescriptor, ValueClamped);
	}
}

template<typename T>
typename TEnableIf<TIsClothPresetPropertyNonStringType<T>::Value, void>::Type UChaosClothPreset::SetPropertyValues(
	const FName& Name,
	T LowValue,
	T HighValue)
{
	const FChaosClothPresetPropertyDescriptor* const PropertyDescriptor = GetPropertyDescriptor(Name);
	if (ensure(PropertyDescriptor) && ensure(PropertyDescriptor->Type == FChaosClothPresetPropertyDescriptor::GetType<T>()))
	{
		const FVector3f LowValueClamped = ClampVector(FVector3f(LowValue), FVector3f(PropertyDescriptor->ClampMin), FVector3f(PropertyDescriptor->ClampMax));
		const FVector3f HighValueClamped = ClampVector(FVector3f(HighValue), FVector3f(PropertyDescriptor->ClampMin), FVector3f(PropertyDescriptor->ClampMax));
		SetPropertyValuesInternal(*PropertyDescriptor, LowValueClamped, HighValueClamped);
	}
}
