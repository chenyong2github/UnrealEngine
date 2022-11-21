// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PCGPoint.h"

#include "PCGMetadata.h"
#include "PCGMetadataCommon.h"
#include "PCGMetadataAttribute.h"
#include "PCGMetadataAttributeTpl.h"
#include "PCGMetadataAttributeTraits.h"

#include "PCGMetadataAttributeWrapper.generated.h"

class FPCGPropertyAttributeWrapper;
class FPCGPropertyAttributeIterator;
class UPCGData;
struct FPCGAttributePropertySelector;

namespace PCGMetadataAttributeWrapper
{
	/**
	* Attempt to get a T value from an attribute. Will only work if the attribute is of type T, or we can broadcast
	* the attribute type to T
	* @param Attribute - The attribute to get the value from
	* @param Key - Entry key to get the value from
	* @param OutValue - Result of the get
	* @returns true if it succeeded, false otherwise.
	*/
	template <typename T>
	bool GetValueFromAttribute(const FPCGMetadataAttributeBase* Attribute, PCGMetadataEntryKey Key, T& OutValue);

	/**
	* Attempt to set a T value to an attribute. Will only work if the attribute is of type T, or we can broadcast
	* T to the attribute type
	* @param Attribute - The attribute to set the value to
	* @param Key - Entry key to set the value to. If it is an invalid key, it will override the default value.
	* @param InValue - Value to set
	* @returns true if it succeeded, false otherwise.
	*/
	template <typename T>
	bool SetValueToAttribute(FPCGMetadataAttributeBase* Attribute, PCGMetadataEntryKey Key, const T& InValue);

	/**
	* Get a property value and pass it as a parameter to a callback function.
	* @param InObject - The object to read from
	* @param InProperty - The property to look for
	* @param InFunc - Callback function that can return anything, and should have a single templated argument, where the property will be.
	* @returns Forward the result of the callback.
	*/
	template <typename ObjectType, typename Func>
	decltype(auto) GetPropertyValueWithCallback(const ObjectType* InObject, const FProperty* InProperty, Func InFunc);

	/**
	* Set a property value given by a callback function.
	* @param InObject - The object to write to
	* @param InProperty - The property to look for
	* @param InFunc - Callback function that take a reference to a templated type. It will set the property with this value. returns true if we should set, false otherwise.
	* @returns Forward the result of the callback
	*/
	template <typename ObjectType, typename Func>
	bool SetPropertyValueFromCallback(ObjectType* InObject, const FProperty* InProperty, Func InFunc);

	/**
	* Attempt to get a T value from a property. Will only work if the property is of type T, or we can broadcast
	* the property type to T
	* @param InObject - The object to read from
	* @param InProperty - The property to look for
	* @param OutValue - The value of the property
	* @returns true if it succeeded, false otherwise.
	*/
	template <typename T, typename ObjectType>
	bool GetValueFromProperty(const ObjectType* InObject, const FProperty* InProperty, T& OutValue);

	/**
	* Attempt to set a T value to a property. Will only work if the property is of type T, or we can broadcast
	* T to the property type
	* @param InObject - The object to write to
	* @param InProperty - The property to look for
	* @param InValue - Value to set
	* @returns true if it succeeded, false otherwise.
	*/
	template <typename T, typename ObjectType>
	bool SetValueToProperty(ObjectType* InObject, const FProperty* InProperty, const T& InValue);

	/**
	* Conversion between property type and PCG type.
	* @param InProperty - The property to look for
	* @returns PCG type if the property is supported, Unknown otherwise.
	*/
	PCG_API EPCGMetadataTypes GetMetadataTypeFromProperty(const FProperty* InProperty);

	/**
	* Returns if the name for this data matches a property, and optional type
	*/
	PCG_API bool IsPropertyWithType(const UPCGData* InData, FName InName, int16* OutType = nullptr);

	/**
	* Create a wrapper around some data with a given name.
	* cf FPCGPropertyAttributeWrapper for more information
	* If it is a point data and the name matches a property, it will wrap a property
	* Otherwise it will wrap an attribute.
	* Note: It is not threadsafe for properties and won't work if the selection is invalid (example: Property for a param data)
	* @param InData - The data to extract the attribute/metadata or the point property
	* @param InSelection - Select a property or an attribute name. 
	* @return The resulting wrapper. Can check "IsValid" to know if the wrapper is valid.
	*/
	PCG_API FPCGPropertyAttributeWrapper CreateWrapper(UPCGData* InData, const FPCGAttributePropertySelector& InSelector);
	PCG_API FPCGPropertyAttributeWrapper CreateWrapper(const UPCGData* InData, const FPCGAttributePropertySelector& InSelector);

	/**
	* Create an iterator wrapper around some data with a given name.
	* cf. FPCGPropertyAttributeIterator for more information.
	* If it is a point data it will iterate over points, otherwise it will iterate over the metadata entry to value key map.
	* cf. CreateWrapper for the wrapping of the attribute/property.
	* Note: It is not threadsafe for properties and won't work if the selection is invalid (example: Property for a param data)
	* @param InData - The data to extract the list of points or the entry key map.
	* @param InSelection - Select a property or an attribute name. 
	* @return The resulting iterator. Can check "IsValid" to know if the iterator is valid.
	*/
	PCG_API FPCGPropertyAttributeIterator CreateIteratorWrapper(UPCGData* InData, const FPCGAttributePropertySelector& InSelector);
	PCG_API FPCGPropertyAttributeIterator CreateIteratorWrapper(const UPCGData* InData, const FPCGAttributePropertySelector& InSelector);
}

UENUM()
enum class EPCGAttributePropertySelection
{
	Attribute,
	PointProperty,
};

USTRUCT(BlueprintType)
struct PCG_API FPCGAttributePropertySelector
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Settings")
	EPCGAttributePropertySelection Selection = EPCGAttributePropertySelection::Attribute;

	UPROPERTY(EditAnywhere, Category = "Settings", meta = (EditCondition = "Selection == EPCGAttributePropertySelection::Attribute", EditConditionHides))
	FName AttributeName = NAME_None;

	UPROPERTY(EditAnywhere, Category = "Settings", meta = (EditCondition = "Selection == EPCGAttributePropertySelection::PointProperty", EditConditionHides))
	EPCGPointProperties PointProperty = EPCGPointProperties::Position;

	FName GetName() const;
};

/**
* Wrapping class around an attribute, property or some custom getter/setters. Allow to design generic algorithms that doesn't need to know
* how to data under is stored. (Similar to a Visitor pattern).
* Get/Set are not threadsafe for properties.
* Set is only available if the wrapper is not read only.
*/
class PCG_API FPCGPropertyAttributeWrapper
{
public:
	friend FPCGPropertyAttributeIterator;

	FPCGPropertyAttributeWrapper() = default;

	FPCGPropertyAttributeWrapper(FPCGMetadataAttributeBase* InAttribute, UPCGMetadata* InMetadata)
		: Attribute(InAttribute)
		, Metadata(InMetadata)
		, Property(nullptr)
		, CustomPointGetterSetter()
		, bReadOnly(false)
	{
		Type = InAttribute ? InAttribute->GetTypeId() : (int16)EPCGMetadataTypes::Unknown;
		Name = InAttribute ? InAttribute->Name : NAME_None;
	}

	FPCGPropertyAttributeWrapper(const FPCGMetadataAttributeBase* InAttribute, const UPCGMetadata* InMetadata)
		: Attribute(const_cast<FPCGMetadataAttributeBase*>(InAttribute))
		, Metadata(const_cast<UPCGMetadata*>(InMetadata))
		, Property(nullptr)
		, CustomPointGetterSetter()
		, bReadOnly(true)
	{
		Type = InAttribute ? InAttribute->GetTypeId() : (int16)EPCGMetadataTypes::Unknown;
		Name = InAttribute ? InAttribute->Name : NAME_None;
	}

	FPCGPropertyAttributeWrapper(const FProperty* InProperty, bool bInReadOnly)
		: Attribute(nullptr)
		, Metadata(nullptr)
		, Property(InProperty)
		, CustomPointGetterSetter()
		, bReadOnly(bInReadOnly)
	{
		Type = (int16)PCGMetadataAttributeWrapper::GetMetadataTypeFromProperty(InProperty);
		Name = InProperty ? InProperty->GetFName() : NAME_None;
	}

	FPCGPropertyAttributeWrapper(FPCGPoint::PointCustomPropertyGetterSetter&& InCustomGetterSetter, bool bInReadOnly)
		: Attribute(nullptr)
		, Metadata(nullptr)
		, Property(nullptr)
		, CustomPointGetterSetter(std::forward<FPCGPoint::PointCustomPropertyGetterSetter>(InCustomGetterSetter))
		, bReadOnly(bInReadOnly)
	{
		Type = CustomPointGetterSetter.GetType();
		Name = CustomPointGetterSetter.GetName();
	}

	/**
	* Attempt to get a value of type T for a given point.
	* If T doesn't match the attribute/property type, we will try to broadcast it.
	* @param Point - The point to look for our value
	* @param OutValue - Output value
	* @returns true if the get succeeded, false otherwise.
	*/
	template <typename T>
	bool Get(const FPCGPoint& Point, T& OutValue) const;

	/**
	* Attempt to get a value of type T for a given entry key.
	* If T doesn't match the attribute type, we will try to broadcast it.
	* @param Key - The entry key to get our value
	* @param OutValue - Output value
	* @returns true if the get succeeded, false otherwise.
	*/
	template <typename T>
	bool Get(PCGMetadataEntryKey Key, T& OutValue) const;

	/**
	* Attempt to set a value of type T for a given point.
	* If T doesn't match the attribute/property type, we will try to broadcast it.
	* The wrapper needs to not be read only, otherwise it will return false.
	* @param Point - The point to ser our value to.
	* @param InValue - Value to set
	* @returns true if the set succeeded, false otherwise.
	*/
	template <typename T>
	bool Set(FPCGPoint& Point, const T& InValue);

	/**
	* Attempt to set a value of type T for a given entry key.
	* If T doesn't match the attribute type, we will try to broadcast it.
	* The wrapper needs to not be read only, otherwise it will return false.
	* @param Key - The entry key to set our value
	* @param InValue - Value to set
	* @returns true if the set succeeded, false otherwise.
	*/
	template <typename T>
	bool Set(PCGMetadataEntryKey Key, const T& InValue);

	int16 GetType() const { return Type; }

	bool IsValid() const { return (Attribute && Metadata) || (Property) || (CustomPointGetterSetter.IsValid()); }

	const FPCGMetadataAttributeBase* GetAttribute() const { return Attribute; }
	const FProperty* GetProperty() const { return Property; }
	const FPCGPoint::PointCustomPropertyGetterSetter& GetCustomPointGetterSetter() const { return CustomPointGetterSetter; }
	FName GetName() const { return Name; }

private:
	FPCGMetadataAttributeBase* Attribute = nullptr;
	UPCGMetadata* Metadata = nullptr;
	const FProperty* Property = nullptr;
	FPCGPoint::PointCustomPropertyGetterSetter CustomPointGetterSetter;

	int16 Type = (int16)EPCGMetadataTypes::Unknown;
	bool bReadOnly = true;
	FName Name = NAME_None;
};

/**
* Iterator over a property/attribute wrapper. Will allow to iterate over points or entry map for attributes.
* Note that it is not threadsafe for properties.
* If the iterable is an array of const points, the wrapper needs to be readonly.
*/
class PCG_API FPCGPropertyAttributeIterator
{
public:
	FPCGPropertyAttributeIterator() = default;

	FPCGPropertyAttributeIterator(const FPCGPropertyAttributeWrapper& InWrapper, const TArrayView<FPCGPoint>& InPoints)
		: Wrapper(InWrapper)
		, Points(InPoints)
		, Entries()
		, Index(0)
	{}

	FPCGPropertyAttributeIterator(const FPCGPropertyAttributeWrapper& InWrapper, const TArrayView<const FPCGPoint>& InPoints)
		: Wrapper(InWrapper)
		, Points(const_cast<FPCGPoint*>(InPoints.GetData()), InPoints.Num())
		, Entries()
		, Index(0)
	{
		check(InWrapper.bReadOnly);
	}

	FPCGPropertyAttributeIterator(const FPCGPropertyAttributeWrapper& InWrapper)
		: Wrapper(InWrapper)
		, Points()
		, Entries()
		, Index(0)
	{
		const FPCGMetadataAttributeBase* Current = Wrapper.Attribute;
		while (Current)
		{
			TArray<PCGMetadataEntryKey> Temp;
			Current->GetEntryToValueKeyMap_NotThreadSafe().GenerateKeyArray(Temp);
			Entries.Append(Temp);
			Current = Current->GetParent();
		}

		// If the attribute doesn't have any entry, we will always take the default value.
		if (Entries.IsEmpty())
		{
			Entries.Add(PCGInvalidEntryKey);
		}
	}

	FPCGPropertyAttributeWrapper& GetWrapper() { return Wrapper; }
	const FPCGPropertyAttributeWrapper& GetWrapper() const { return Wrapper; }

	/**
	* Attempt to get a value of type T for our current item.
	* If T doesn't match the attribute/property type, we will try to broadcast it.
	* If it succeeded, we advance our iterator.
	* @param OutValue - Output value
	* @returns true if the get succeeded, false otherwise.
	*/
	template <typename T>
	bool GetAndAdvance(T& OutValue);

	/**
	* Attempt to set a value of type T for our current item.
	* If T doesn't match the attribute/property type, we will try to broadcast it.
	* If it succeeded, we advance our iterator.
	* @param InValue - The value to set
	* @returns true if the set succeeded, false otherwise.
	*/
	template <typename T>
	bool SetAndAdvance(const T& InValue);

	/**
	* Advance the iterator.
	* If we reach the end of our iterable, we re-start from index 0.
	*/
	void Advance();

	bool IsValid() const { return Wrapper.IsValid() && (!Points.IsEmpty() || !Entries.IsEmpty()); }

	int16 GetType() const { return Wrapper.GetType(); }

	int32 Num() const { return Points.Num() > 0 ? Points.Num() : Entries.Num(); }

private:
	FPCGPropertyAttributeWrapper Wrapper;
	TArrayView<FPCGPoint> Points;
	TArray<PCGMetadataEntryKey> Entries;
	int32 Index = 0;
};

//////
/// PCGMetadataAttributeWrapper Implementation
//////

template <typename T>
inline bool PCGMetadataAttributeWrapper::GetValueFromAttribute(const FPCGMetadataAttributeBase* Attribute, PCGMetadataEntryKey Key, T& OutValue)
{
	auto Getter = [Attribute, Key, &OutValue](auto&& DummyValue) -> bool
	{
		using InType = std::decay_t<decltype(DummyValue)>;

		if constexpr (std::is_same_v<InType, T>)
		{
			OutValue = static_cast<const FPCGMetadataAttribute<InType>*>(Attribute)->GetValueFromItemKey(Key);
			return true;
		}
		else
		{
			InType InValue = static_cast<const FPCGMetadataAttribute<InType>*>(Attribute)->GetValueFromItemKey(Key);
			return PCG::Private::GetValueWithBroadcast<InType, T>(InValue, OutValue);
		}
	};

	return PCGMetadataAttribute::CallbackWithRightType(Attribute->GetTypeId(), Getter);
}

template <typename T>
inline bool PCGMetadataAttributeWrapper::SetValueToAttribute(FPCGMetadataAttributeBase* Attribute, PCGMetadataEntryKey Key, const T& InValue)
{
	auto Setter = [Attribute, Key, &InValue](auto&& DummyValue) -> bool
	{
		using OutType = std::decay_t<decltype(DummyValue)>;
		OutType OutValue{};
		if (PCG::Private::GetValueWithBroadcast<T, OutType>(InValue, OutValue))
		{
			if (Key == PCGInvalidEntryKey)
			{
				static_cast<FPCGMetadataAttribute<OutType>*>(Attribute)->SetDefaultValue(OutValue);
			}
			else
			{
				static_cast<FPCGMetadataAttribute<OutType>*>(Attribute)->SetValue(Key, OutValue);
			}

			return true;
		}
		else
		{
			return false;
		}
	};

	return PCGMetadataAttribute::CallbackWithRightType(Attribute->GetTypeId(), Setter);
}

// Func signature : auto(auto&&)
template <typename ObjectType, typename Func>
inline decltype(auto) PCGMetadataAttributeWrapper::GetPropertyValueWithCallback(const ObjectType* InObject, const FProperty* InProperty, Func InFunc)
{
	// Double property is supported by PCG, we use a dummy double to deduce the return type of InFunc.
	using ReturnType = decltype(InFunc(0.0));

	auto DefaultValue = []() -> ReturnType
	{
		if constexpr (std::is_same_v<ReturnType, void>)
		{
			return;
		}
		else
		{
			return ReturnType{};
		}
	};

	if (!InObject || !InProperty)
	{
		return DefaultValue();
	}

	const void* PropertyAddressData = InProperty->ContainerPtrToValuePtr<void>(InObject);

	if (const FNumericProperty* NumericProperty = CastField<FNumericProperty>(InProperty))
	{
		if (NumericProperty->IsFloatingPoint())
		{
			return InFunc(NumericProperty->GetFloatingPointPropertyValue(PropertyAddressData));
		}
		else if (NumericProperty->IsInteger())
		{
			return InFunc(NumericProperty->GetSignedIntPropertyValue(PropertyAddressData));
		}
	}
	else if (const FBoolProperty* BoolProperty = CastField<FBoolProperty>(InProperty))
	{
		return InFunc(BoolProperty->GetPropertyValue(PropertyAddressData));
	}
	else if (const FStrProperty* StringProperty = CastField<FStrProperty>(InProperty))
	{
		return InFunc(StringProperty->GetPropertyValue(PropertyAddressData));
	}
	else if (const FNameProperty* NameProperty = CastField<FNameProperty>(InProperty))
	{
		return InFunc(NameProperty->GetPropertyValue(PropertyAddressData));
	}
	else if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(InProperty))
	{
		return InFunc(EnumProperty->GetUnderlyingProperty()->GetSignedIntPropertyValue(PropertyAddressData));
	}
	else if (const FStructProperty* StructProperty = CastField<FStructProperty>(InProperty))
	{
		if (StructProperty->Struct == TBaseStructure<FVector>::Get())
		{
			return InFunc(*reinterpret_cast<const FVector*>(PropertyAddressData));
		}
		else if (StructProperty->Struct == TBaseStructure<FVector4>::Get())
		{
			return InFunc(*reinterpret_cast<const FVector4*>(PropertyAddressData));
		}
		else if (StructProperty->Struct == TBaseStructure<FQuat>::Get())
		{
			return InFunc(*reinterpret_cast<const FQuat*>(PropertyAddressData));
		}
		else if (StructProperty->Struct == TBaseStructure<FTransform>::Get())
		{
			return InFunc(*reinterpret_cast<const FTransform*>(PropertyAddressData));
		}
		else if (StructProperty->Struct == TBaseStructure<FRotator>::Get())
		{
			return InFunc(*reinterpret_cast<const FRotator*>(PropertyAddressData));
		}
		else if (StructProperty->Struct == TBaseStructure<FSoftObjectPath>::Get())
		{
			// Soft object path are transformed to strings
			return InFunc(reinterpret_cast<const FSoftObjectPath*>(PropertyAddressData)->ToString());
		}
		else if (StructProperty->Struct == TBaseStructure<FSoftClassPath>::Get())
		{
			// Soft class path are transformed to strings
			return InFunc(reinterpret_cast<const FSoftClassPath*>(PropertyAddressData)->ToString());
		}
		//else if (StructProperty->Struct == TBaseStructure<FColor>::Get())
		//else if (StructProperty->Struct == TBaseStructure<FLinearColor>::Get())
	}
	else if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(InProperty))
	{
		if (const UObject* Object = ObjectProperty->GetObjectPropertyValue(PropertyAddressData))
		{
			// Object are transformed into their soft path name (as a string attribute)
			return InFunc(Object->GetPathName());
		}
	}

	return DefaultValue();
}

// Func signature : bool(auto&)
// Will have property value in first arg, and boolean return if we should set the property after.
// Returns true if the set succeeded
template <typename ObjectType, typename Func>
inline bool PCGMetadataAttributeWrapper::SetPropertyValueFromCallback(ObjectType* InObject, const FProperty* InProperty, Func InFunc)
{
	if (!InObject || !InProperty)
	{
		return false;
	}

	bool bSuccess = false;
	void* PropertyAddressData = InProperty->ContainerPtrToValuePtr<void>(InObject);

	if (const FNumericProperty* NumericProperty = CastField<FNumericProperty>(InProperty))
	{
		if (NumericProperty->IsFloatingPoint())
		{
			double DoubleValue{};
			bSuccess = InFunc(DoubleValue);
			if (bSuccess)
			{
				NumericProperty->SetFloatingPointPropertyValue(PropertyAddressData, DoubleValue);
			}
		}
		else if (NumericProperty->IsInteger())
		{
			int64 IntValue{};
			bSuccess = InFunc(IntValue);
			if (bSuccess)
			{
				NumericProperty->SetIntPropertyValue(PropertyAddressData, IntValue);
			}
		}
	}
	else if (const FBoolProperty* BoolProperty = CastField<FBoolProperty>(InProperty))
	{
		bool BoolValue{};
		bSuccess = InFunc(BoolValue);
		if (bSuccess)
		{
			BoolProperty->SetPropertyValue(PropertyAddressData, BoolValue);
		}
	}
	else if (const FStrProperty* StringProperty = CastField<FStrProperty>(InProperty))
	{
		FString StringValue;
		bSuccess = InFunc(StringValue);
		if (bSuccess)
		{
			StringProperty->SetPropertyValue(PropertyAddressData, StringValue);
		}
	}
	else if (const FNameProperty* NameProperty = CastField<FNameProperty>(InProperty))
	{
		FName NameValue;
		bSuccess = InFunc(NameValue);
		if (bSuccess)
		{
			NameProperty->SetPropertyValue(PropertyAddressData, NameValue);
		}
	}
	else if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(InProperty))
	{
		int64 IntValue{};
		bSuccess = InFunc(IntValue);
		if (bSuccess)
		{
			EnumProperty->GetUnderlyingProperty()->SetIntPropertyValue(PropertyAddressData, IntValue);
		}
	}
	else if (const FStructProperty* StructProperty = CastField<FStructProperty>(InProperty))
	{
		if (StructProperty->Struct == TBaseStructure<FVector>::Get())
		{
			FVector VectorValue;
			bSuccess = InFunc(VectorValue);
			if (bSuccess)
			{
				*reinterpret_cast<FVector*>(PropertyAddressData) = VectorValue;
			}
		}
		else if (StructProperty->Struct == TBaseStructure<FVector4>::Get())
		{
			FVector4 Vector4Value;
			bSuccess = InFunc(Vector4Value);
			if (bSuccess)
			{
				*reinterpret_cast<FVector4*>(PropertyAddressData) = Vector4Value;
			}
		}
		else if (StructProperty->Struct == TBaseStructure<FVector2D>::Get())
		{
			FVector2D Vector2Value;
			bSuccess = InFunc(Vector2Value);
			if (bSuccess)
			{
				*reinterpret_cast<FVector2D*>(PropertyAddressData) = Vector2Value;
			}
		}
		else if (StructProperty->Struct == TBaseStructure<FQuat>::Get())
		{
			FQuat QuatValue;
			bSuccess = InFunc(QuatValue);
			if (bSuccess)
			{
				*reinterpret_cast<FQuat*>(PropertyAddressData) = QuatValue;
			}
		}
		else if (StructProperty->Struct == TBaseStructure<FTransform>::Get())
		{
			FTransform TransformValue;
			bSuccess = InFunc(TransformValue);
			if (bSuccess)
			{
				*reinterpret_cast<FTransform*>(PropertyAddressData) = TransformValue;
			}
		}
		else if (StructProperty->Struct == TBaseStructure<FRotator>::Get())
		{
			FRotator RotatorValue;
			bSuccess = InFunc(RotatorValue);
			if (bSuccess)
			{
				*reinterpret_cast<FRotator*>(PropertyAddressData) = RotatorValue;
			}
		}
		//	else if (StructProperty->Struct == TBaseStructure<FSoftObjectPath>::Get())
		//	{
		//		// Soft object path are transformed from strings
		//		reinterpret_cast<const FSoftObjectPath*>(PropertyAddressData)->SetPath(Converter(std::forward<T>(Value), FString{}));
		//	}
		//	else if (StructProperty->Struct == TBaseStructure<FSoftClassPath>::Get())
		//	{
		//		// Soft class path are transformed to strings
		//		reinterpret_cast<const FSoftClassPath*>(PropertyAddressData)->SetPath(Converter(std::forward<T>(Value), FString{}));
		//	}
		//	//else if (StructProperty->Struct == TBaseStructure<FColor>::Get())
		//	//else if (StructProperty->Struct == TBaseStructure<FLinearColor>::Get())
	}
	//else if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(InProperty))
	//{
	//	if (const UObject* Object = ObjectProperty->GetObjectPropertyValue(PropertyAddressData))
	//	{
	//		// TODO
	//		// Object are transformed into their soft path name (as a string attribute)
	//		//return Converter(Object->GetPathName());
	//	}
	//}

	return bSuccess;
}

template <typename T, typename ObjectType>
inline bool PCGMetadataAttributeWrapper::GetValueFromProperty(const ObjectType* InObject, const FProperty* InProperty, T& OutValue)
{
	auto Converter = [&OutValue](auto&& PropertyValue) -> bool
	{
		using PropertyType = std::decay_t<decltype(PropertyValue)>;
		return PCG::Private::GetValueWithBroadcast<PropertyType, T>(PropertyValue, OutValue);
	};

	return GetPropertyValueWithCallback(InObject, InProperty, Converter);
}

template <typename T, typename ObjectType>
inline bool PCGMetadataAttributeWrapper::SetValueToProperty(ObjectType* InObject, const FProperty* InProperty, const T& InValue)
{
	auto Converter = [&InValue](auto& PropertyValue) -> bool
	{
		using PropertyType = std::decay_t<decltype(PropertyValue)>;
		return PCG::Private::GetValueWithBroadcast<T, PropertyType>(InValue, PropertyValue);
	};

	return SetPropertyValueFromCallback(InObject, InProperty, Converter);
}

//////
/// FPCGPropertyAttributeWrapper Implementation
//////

template <typename T>
inline bool FPCGPropertyAttributeWrapper::Get(const FPCGPoint& Point, T& OutValue) const
{
	if (CustomPointGetterSetter.IsValid())
	{
		auto Getter = [this, &Point, &OutValue](auto&& DummyValue) -> bool
		{
			using InType = std::decay_t<decltype(DummyValue)>;

			if constexpr (std::is_same_v<InType, T>)
			{
				CustomPointGetterSetter.Get<InType>(Point, OutValue);
				return true;
			}
			else
			{
				InType InValue{};
				if (CustomPointGetterSetter.Get<InType>(Point, InValue))
				{
					return PCG::Private::GetValueWithBroadcast<InType, T>(InValue, OutValue);
				}
				else
				{
					return false;
				}
			}
		};

		return PCGMetadataAttribute::CallbackWithRightType(Type, Getter);
	}
	else if (Attribute)
	{
		return PCGMetadataAttributeWrapper::GetValueFromAttribute(Attribute, Point.MetadataEntry, OutValue);
	}
	else if (Property)
	{
		return PCGMetadataAttributeWrapper::GetValueFromProperty(&Point, Property, OutValue);
	}
	else
	{
		return false;
	}
}

template <typename T>
inline bool FPCGPropertyAttributeWrapper::Get(PCGMetadataEntryKey Key, T& OutValue) const
{
	if (Attribute)
	{
		return PCGMetadataAttributeWrapper::GetValueFromAttribute(Attribute, Key, OutValue);
	}
	else
	{
		return false;
	}
}

template <typename T>
inline bool FPCGPropertyAttributeWrapper::Set(FPCGPoint& Point, const T& InValue)
{
	if (bReadOnly)
	{
		return false;
	}

	if (CustomPointGetterSetter.IsValid())
	{
		auto Setter = [this, &Point, &InValue](auto&& DummyValue) -> bool
		{
			using OutType = std::decay_t<decltype(DummyValue)>;

			if constexpr (std::is_same_v<OutType, T>)
			{
				CustomPointGetterSetter.Set<OutType>(Point, InValue);
				return true;
			}
			else
			{
				OutType OutValue{};
				if (PCG::Private::GetValueWithBroadcast<T, OutType>(InValue, OutValue))
				{
					return CustomPointGetterSetter.Set<OutType>(Point, OutValue);
				}
				else
				{
					return false;
				}
			}
		};

		return PCGMetadataAttribute::CallbackWithRightType(Type, Setter);
	}
	else if (Attribute && Metadata)
	{
		Metadata->InitializeOnSet(Point.MetadataEntry);
		return PCGMetadataAttributeWrapper::SetValueToAttribute(Attribute, Point.MetadataEntry, InValue);
	}
	else if (Property)
	{
		return PCGMetadataAttributeWrapper::SetValueToProperty(&Point, Property, InValue);
	}
	else
	{
		return false;
	}
}

template <typename T>
inline bool FPCGPropertyAttributeWrapper::Set(PCGMetadataEntryKey Key, const T& InValue)
{
	if (Attribute && !bReadOnly)
	{
		return PCGMetadataAttributeWrapper::SetValueToAttribute(Attribute, Key, InValue);
	}
	else
	{
		return false;
	}
}

////////////////
// FPCGPropertyAttributeIterator implementation
////////////////

template <typename T>
inline bool FPCGPropertyAttributeIterator::GetAndAdvance(T& OutValue)
{
	bool bSuccess = false;
	if (Points.Num() != 0)
	{
		bSuccess = Wrapper.Get<T>(Points[Index], OutValue);
	}
	else if (!Entries.IsEmpty())
	{
		bSuccess = Wrapper.Get<T>(Entries[Index], OutValue);
	}

	if (bSuccess)
	{
		Advance();
	}

	return bSuccess;
}

template <typename T>
inline bool FPCGPropertyAttributeIterator::SetAndAdvance(const T& InValue)
{
	bool bSuccess = false;
	if (Points.Num() != 0)
	{
		bSuccess = Wrapper.Set<T>(Points[Index], InValue);
	}
	else if (!Entries.IsEmpty())
	{
		bSuccess = Wrapper.Set<T>(Entries[Index], InValue);
	}

	if (bSuccess)
	{
		Advance();
	}

	return bSuccess;
}

inline void FPCGPropertyAttributeIterator::Advance()
{
	if (Points.Num() != 0)
	{
		if (++Index == Points.Num())
		{
			Index = 0;
		}
	}
	else if (!Entries.IsEmpty())
	{
		if (++Index == Entries.Num())
		{
			Index = 0;
		}
	}
}