// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosClothAsset/ClothPreset.h"
#include "ChaosClothAsset/ClothPresetCollection.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(ClothPreset)

template<>
EChaosClothPresetPropertyType FChaosClothPresetPropertyDescriptor::GetType<bool>()
{
	return EChaosClothPresetPropertyType::Boolean;
}

template<>
EChaosClothPresetPropertyType FChaosClothPresetPropertyDescriptor::GetType<int32>()
{
	return EChaosClothPresetPropertyType::Integer;
}

template<>
EChaosClothPresetPropertyType FChaosClothPresetPropertyDescriptor::GetType<float>()
{
	return EChaosClothPresetPropertyType::Float;
}

template<>
EChaosClothPresetPropertyType FChaosClothPresetPropertyDescriptor::GetType<FIntVector3>()
{
	return EChaosClothPresetPropertyType::Vector3Integer;
}

template<>
EChaosClothPresetPropertyType FChaosClothPresetPropertyDescriptor::GetType<FVector3f>()
{
	return EChaosClothPresetPropertyType::Vector3Float;
}

template<>
EChaosClothPresetPropertyType FChaosClothPresetPropertyDescriptor::GetType<FString>()
{
	return EChaosClothPresetPropertyType::String;
}

UChaosClothPreset::UChaosClothPreset(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, ClothPresetCollection(MakeShared<UE::Chaos::ClothAsset::FClothPresetCollection>())
{
}

void UChaosClothPreset::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	ClothPresetCollection->Serialize(Ar);
}

bool UChaosClothPreset::AddProperty(const FName& Name, EChaosClothPresetPropertyType Type)
{
	// Return a nullptr if the property already exists
	if (PropertyDescriptors.FindByPredicate([&Name](const FChaosClothPresetPropertyDescriptor& PropertyDescriptor)->bool { return PropertyDescriptor.Name == Name; }))
	{
		return false;
	}

	// Add the new property
	PropertyDescriptors.Emplace(Name, Type);

	// Re-sync the collection
	SyncCollection();

	return true;
}

void UChaosClothPreset::SetProperty(const FName& Name, const FString& StringValue, bool bEnable, bool bAnimatable)
{
	const FChaosClothPresetPropertyDescriptor* const PropertyDescriptor = GetPropertyDescriptor(Name);
	if (ensure(PropertyDescriptor) && ensure(PropertyDescriptor->Type == FChaosClothPresetPropertyDescriptor::GetType<FString>()))
	{
		SetPropertyInternal(*PropertyDescriptor, FVector3f::ZeroVector, FVector3f::ZeroVector, StringValue, bEnable, bAnimatable);
	}
}

void UChaosClothPreset::SetPropertyStringValue(const FName& Name, const FString& StringValue)
{
	const FChaosClothPresetPropertyDescriptor* const PropertyDescriptor = GetPropertyDescriptor(Name);
	if (ensure(PropertyDescriptor))
	{
		SyncProperty(*PropertyDescriptor);

		const int32 Index = GetPropertyElementIndex(*PropertyDescriptor);
		check(Index != INDEX_NONE);

		ClothPresetCollection->StringValue[Index] = StringValue;
	}
}

void UChaosClothPreset::SetPropertyEnable(const FName& Name, bool bEnable)
{
	const FChaosClothPresetPropertyDescriptor* const PropertyDescriptor = GetPropertyDescriptor(Name);
	if (ensure(PropertyDescriptor))
	{
		SyncProperty(*PropertyDescriptor);

		const int32 Index = GetPropertyElementIndex(*PropertyDescriptor);
		check(Index != INDEX_NONE);

		ClothPresetCollection->Enable[Index] = bEnable;
	}
}

void UChaosClothPreset::SetPropertyAnimatable(const FName& Name, bool bAnimatable)
{
	const FChaosClothPresetPropertyDescriptor* const PropertyDescriptor = GetPropertyDescriptor(Name);
	if (ensure(PropertyDescriptor))
	{
		SyncProperty(*PropertyDescriptor);

		const int32 Index = GetPropertyElementIndex(*PropertyDescriptor);
		check(Index != INDEX_NONE);

		ClothPresetCollection->Animatable[Index] = bAnimatable;
	}
}

const FChaosClothPresetPropertyDescriptor* UChaosClothPreset::GetPropertyDescriptor(const FName& Name) const
{
	return PropertyDescriptors.FindByPredicate(
		[&Name](const FChaosClothPresetPropertyDescriptor& PropertyDescriptor)
		{
			return PropertyDescriptor.Name == Name;
		});
}

int32 UChaosClothPreset::GetPropertyElementIndex(const FChaosClothPresetPropertyDescriptor& PropertyDescriptor) const
{
	return ClothPresetCollection->Name.Find(PropertyDescriptor.Name.ToString());
}

void UChaosClothPreset::SetPropertyInternal(
	const FChaosClothPresetPropertyDescriptor& PropertyDescriptor,
	const FVector3f& LowValue,
	const FVector3f& HighValue,
	const FString& StringValue,
	bool bEnable,
	bool bAnimatable)
{
	SyncProperty(PropertyDescriptor);

	const int32 Index = GetPropertyElementIndex(PropertyDescriptor);
	check(Index != INDEX_NONE);

	ClothPresetCollection->LowValue[Index] = LowValue;
	ClothPresetCollection->HighValue[Index] = HighValue;
	ClothPresetCollection->StringValue[Index] = StringValue;
	ClothPresetCollection->Enable[Index] = bEnable;
	ClothPresetCollection->Animatable[Index] = bAnimatable;
}

void UChaosClothPreset::SetPropertyValueInternal(const FChaosClothPresetPropertyDescriptor& PropertyDescriptor, const FVector3f& Value)
{
	SyncProperty(PropertyDescriptor);

	const int32 Index = GetPropertyElementIndex(PropertyDescriptor);
	check(Index != INDEX_NONE);

	ClothPresetCollection->LowValue[Index] = Value;
}

void UChaosClothPreset::SetPropertyValuesInternal(const FChaosClothPresetPropertyDescriptor& PropertyDescriptor, const FVector3f& LowValue, const FVector3f& HighValue)
{
	SyncProperty(PropertyDescriptor);

	const int32 Index = GetPropertyElementIndex(PropertyDescriptor);
	check(Index != INDEX_NONE);

	ClothPresetCollection->LowValue[Index] = LowValue;
	ClothPresetCollection->HighValue[Index] = HighValue;
}

void UChaosClothPreset::SyncProperty(const FChaosClothPresetPropertyDescriptor& PropertyDescriptor)
{
	using namespace UE::Chaos::ClothAsset;

	int32 Index = GetPropertyElementIndex(PropertyDescriptor);
	if (Index == INDEX_NONE)
	{
		// Add this property to the preset collection
		Index = ClothPresetCollection->AddElements(1, FClothPresetCollection::PropertyGroup);

		// Write the new property name and default values to the preset collection
		ClothPresetCollection->Name[Index] = PropertyDescriptor.Name.ToString();
		ClothPresetCollection->LowValue[Index] = PropertyDescriptor.DefaultValue;
		ClothPresetCollection->HighValue[Index] = PropertyDescriptor.DefaultValue;
		ClothPresetCollection->StringValue[Index] = PropertyDescriptor.DefaultString;
		ClothPresetCollection->Enable[Index] = PropertyDescriptor.bDefaultEnable;
		ClothPresetCollection->Animatable[Index] = PropertyDescriptor.bDefaultAnimatable;
	}
}

void UChaosClothPreset::SyncCollection()
{
	using namespace UE::Chaos::ClothAsset;

	// Cleanup the redundant collection elements that are no longer in the descriptor array
	const int32 NumElements = ClothPresetCollection->NumElements(FClothPresetCollection::PropertyGroup);
	TArray<int32> DeletionList;
	for (int32 Index = 0; Index < NumElements; ++Index)
	{
		const FName Name(*ClothPresetCollection->Name[Index]);
		if (!GetPropertyDescriptor(Name))
		{
			DeletionList.Add(Index);
		}
	}
	ClothPresetCollection->RemoveElements(FClothPresetCollection::PropertyGroup, DeletionList);

	// Add any new elements that may have been added since the last sync
	for (const FChaosClothPresetPropertyDescriptor& PropertyDescriptor : PropertyDescriptors)
	{
		SyncProperty(PropertyDescriptor);
	}
}

