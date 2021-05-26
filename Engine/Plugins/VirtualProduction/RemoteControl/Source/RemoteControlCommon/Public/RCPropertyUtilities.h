// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "RCTypeTraits.h"
#include "RCTypeUtilities.h"
#include "Serialization/BufferArchive.h"
#include "UObject/WeakFieldPtr.h"

#if WITH_EDITOR
#include "PropertyHandle.h"
#endif

namespace RemoteControlPropertyUtilities
{
#if WITH_EDITOR
	/** Container that can hold either a PropertyHandle, or Property/Data pair. Similar to FFieldVariant */
	class FRCPropertyVariant
	{
		TSharedPtr<IPropertyHandle> PropertyHandle = nullptr;

		TWeakFieldPtr<FProperty> Property = nullptr;
		void* PropertyData = nullptr;
		TArray<uint8>* PropertyContainer = nullptr;

		bool bHasHandle = false;
		int32 NumElements = -1;

	public:
		explicit FRCPropertyVariant() = default;

		/** Construct from an IPropertyHandle. */
		FRCPropertyVariant(const TSharedPtr<IPropertyHandle>& InPropertyHandle)
			: bHasHandle(true)
		{
			PropertyHandle = InPropertyHandle;
			Property = PropertyHandle->GetProperty();
		}

		/** Construct from a Property, PropertyData ptr, and the expected element count (needed for arrays, strings, etc.). */
		FRCPropertyVariant(const FProperty* InProperty, const void* InPropertyData, const int32& InNumElements = -1)
			: NumElements(InNumElements)
		{
			Property = InProperty;
			PropertyData = const_cast<void*>(InPropertyData);
		}

		/** Construct from a Property and backing data array. Preferred over a raw ptr. */
		FRCPropertyVariant(const FProperty* InProperty, TArray<uint8>& InPropertyData)
		{
			Property = InProperty;

			PropertyData = InPropertyData.GetData();
			PropertyContainer = &InPropertyData;
			NumElements = InPropertyData.Num();
		}

		virtual ~FRCPropertyVariant() = default;

		/** Gets the property. */
		const FProperty* GetProperty() const
		{
			if(Property.IsValid())
			{
				return Property.Get();
			}

			if(bHasHandle && PropertyHandle.IsValid() && PropertyHandle->IsValidHandle())
			{
				return PropertyHandle->GetProperty();
			}

			return nullptr;
		}

		/** Gets the typed property, returns nullptr if not cast. */
		template <typename PropertyType>
		const PropertyType* GetProperty() const
		{
			static_assert(TIsDerivedFrom<PropertyType, FProperty>::Value, "PropertyType must derive from FProperty");

			return CastField<PropertyType>(GetProperty());
		} 

		/** Gets the property container (byte array), if available. */
		TArray<uint8>* GetPropertyContainer() const
		{
			if(PropertyContainer)
			{
				return PropertyContainer;
			}

			return nullptr;
		}

		/** Gets the data pointer */
		void* GetPropertyData(int32 InIdx = 0) const
		{
			if(bHasHandle)
			{
				TArray<void*> Data;
				PropertyHandle->AccessRawData(Data);
				check(Data.IsValidIndex(InIdx)); // check that there's at least one set of data

				return Data[InIdx];
			}

			if(PropertyContainer)
			{
				return PropertyContainer->GetData();
			}

			return PropertyData;
		}

		/** Returns the data as the ValueType. */
		template <typename ValueType>
		ValueType* GetPropertyValue(int32 InIdx = 0) const
		{
			return (ValueType*)(GetPropertyData(InIdx));
		}

		/** Is this backed by an IPropertyHandle? */
		bool IsHandle() const { return bHasHandle; }

		/** Initialize/allocate if necessary. */
		void Init(int32 InSize = -1)
		{
			InSize = InSize > 0 ? InSize : GetProperty()->GetSize();			
			if(!bHasHandle)
			{
				if(PropertyContainer)
				{
					PropertyContainer->SetNumUninitialized(InSize);
					PropertyData = PropertyContainer->GetData();
				}
				else
				{
					PropertyData = FMemory::Malloc(InSize, GetProperty()->GetMinAlignment());
				}
				NumElements = InSize;

				GetProperty()->InitializeValue(PropertyData);
			}
		}

		/** Gets the size of the allocated data. Not always available. */
		int32 Num() const { return NumElements; }

		/** Comparison is by property, not by property instance value. */
		bool operator==(const FRCPropertyVariant& InOther) const
		{
			return GetProperty() == InOther.GetProperty();
		}
		
		bool operator!=(const FRCPropertyVariant& InOther) const
		{
			return GetProperty() != InOther.GetProperty();
		}
	};

	template <>
	inline FString* FRCPropertyVariant::GetPropertyValue<FString>(int32 InIdx) const
	{
		return GetProperty<FStrProperty>()->GetPropertyValuePtr(GetPropertyData(InIdx));
	}

	/** Reads the raw data from InSrc and deserializes to OutDst. */
	template <typename PropertyType>
	typename TEnableIf<
		TNot<
			TOr<
				TIsSame<PropertyType, FProperty>,
				TIsSame<PropertyType, FNumericProperty>>>::Value, bool>::Type
	FromBinary(const FRCPropertyVariant& InSrc, FRCPropertyVariant& OutDst)
	{
		using ValueType = typename TRemoteControlPropertyTypeTraits<PropertyType>::ValueType;

		TArray<uint8>* PropertyContainer = InSrc.GetPropertyContainer();
		checkf(PropertyContainer != nullptr, TEXT("FromBinary requires Src to have a backing container."));

		OutDst.Init(); // initializes only if necessary
		ValueType* CurrentValue = OutDst.GetPropertyValue<ValueType>();
		FMemoryReader Reader(*PropertyContainer);
		InSrc.GetProperty()->SerializeItem(FStructuredArchiveFromArchive(Reader).GetSlot(), CurrentValue, nullptr);

		return false;
	}

	/** Reads the property value from InSrc and serializes to OutDst. */
	template <typename PropertyType>
	typename TEnableIf<
		TNot<
			TOr<
				TIsSame<PropertyType, FProperty>,
				TIsSame<PropertyType, FNumericProperty>>>::Value, bool>::Type
	ToBinary(const FRCPropertyVariant& InSrc, FRCPropertyVariant& OutDst)
	{
		using ValueType = typename TRemoteControlPropertyTypeTraits<PropertyType>::ValueType;

		ValueType* Value = InSrc.GetPropertyValue<ValueType>();		
		FMemoryWriter Writer(*OutDst.GetPropertyContainer());
		OutDst.GetProperty()->SerializeItem(FStructuredArchiveFromArchive(Writer).GetSlot(), Value, nullptr);

		return false;
	}

	/** Specialization for FProperty casts and forwards to specializations. */
	template <typename PropertyType>
	typename TEnableIf<
		TOr<
			TIsSame<PropertyType, FProperty>,
			TIsSame<PropertyType, FNumericProperty>>::Value, bool>::Type
	FromBinary(const FRCPropertyVariant& InSrc, FRCPropertyVariant& OutDst)
	{
		const FProperty* Property = OutDst.GetProperty();
		FOREACH_CAST_PROPERTY(Property, FromBinary<CastPropertyType>(InSrc, OutDst))

		return true;
	}

	/** Specialization for FProperty casts and forwards to specializations. */
	template <typename PropertyType>
	typename TEnableIf<
		TOr<
			TIsSame<PropertyType, FProperty>,
			TIsSame<PropertyType, FNumericProperty>>::Value, bool>::Type
	ToBinary(const FRCPropertyVariant& InSrc, FRCPropertyVariant& OutDst)
	{
		const FProperty* Property = InSrc.GetProperty();
		FOREACH_CAST_PROPERTY(Property, ToBinary<CastPropertyType>(InSrc, OutDst))

		return true;
	}

#endif
}
