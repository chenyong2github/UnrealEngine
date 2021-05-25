// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "RCTypeTraits.h"

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

		const FProperty* Property = nullptr;
		void* PropertyData = nullptr;
		TArray<uint8>* PropertyContainer = nullptr;

		bool bIsHandle = false;
		int32 NumElements = -1;

	public:
		explicit FRCPropertyVariant() = default;

		virtual ~FRCPropertyVariant()
		{
			auto o =123;
			auto n = o;
		}

		FRCPropertyVariant(const TSharedPtr<IPropertyHandle>& InPropertyHandle)
			: bIsHandle(true)
		{
			PropertyHandle = InPropertyHandle;
		}

		FRCPropertyVariant(const FProperty* InProperty, const void* InPropertyData, const int32& InNumElements = -1)
			: bIsHandle(false)
			, NumElements(InNumElements)
		{
			Property = InProperty;
			// @todo: make more elegant
			PropertyData = const_cast<void*>(InPropertyData);
		}

		FRCPropertyVariant(const FProperty* InProperty, TArray<uint8>& InPropertyData)
			: bIsHandle(false)
		{
			Property = InProperty;

			PropertyData = InPropertyData.GetData();
			PropertyContainer = &InPropertyData;
			NumElements = InPropertyData.Num();
		}
		
		/** Gets the property. */
		const FProperty* GetProperty() const
		{
			return bIsHandle ? PropertyHandle->GetProperty() : Property;
		}

		/** Gets the typed property, returns nullptr if not cast. */
		template <typename PropertyType>
		const PropertyType* GetProperty() const
		{
			static_assert(TIsDerivedFrom<PropertyType, FProperty>::Value, "PropertyType must derive from FProperty");

			return CastField<PropertyType>(GetProperty());
		}

		/** Gets the data pointer */
		void* GetPropertyData() const
		{
			if(bIsHandle)
			{
				TArray<void*> Data;
				PropertyHandle->AccessRawData(Data);
				check(Data.IsValidIndex(0)); // check that there's at least one set of data

				return Data[0];
			}			

			return PropertyData;
		}

		bool IsHandle() const { return bIsHandle; }

		void Init(const int32 InSize)
		{
			if(!bIsHandle && PropertyContainer)
			{
				PropertyContainer->SetNumUninitialized(InSize);
				PropertyData = PropertyContainer->GetData();
				NumElements = InSize;
				return;
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

	/** Gets from raw data to input property handle. */

	// Non 'String-like' property types
	template <typename PropertyType>
	typename TEnableIf<TNot<RemoteControlTypeTraits::TIsStringLikeProperty<PropertyType>>::Value, bool>::Type
	FromBytes(const FRCPropertyVariant& InSrc, FRCPropertyVariant& InDstProperty)
	{
		InDstProperty.GetProperty()->CopyCompleteValue((uint8*)InDstProperty.GetPropertyData(), (uint8*)InSrc.GetPropertyData());

		return true;
	}

	template <>
	REMOTECONTROLCOMMON_API bool FromBytes<FStructProperty>(const FRCPropertyVariant& InSrc, FRCPropertyVariant& InDstProperty);

	template <>
	REMOTECONTROLCOMMON_API bool FromBytes<FArrayProperty>(const FRCPropertyVariant& InSrc, FRCPropertyVariant& InDstProperty);

	template <>
	REMOTECONTROLCOMMON_API bool FromBytes<FSetProperty>(const FRCPropertyVariant& InSrc, FRCPropertyVariant& InDstProperty);

	// Specialization for 'String-like' property types
	template <typename PropertyType>
	typename TEnableIf<RemoteControlTypeTraits::TIsStringLikeProperty<PropertyType>::Value, bool>::Type
	FromBytes(const FRCPropertyVariant& InSrc, FRCPropertyVariant& OutDst)
	{
		const int32 StrLen = InSrc.Num();
		FString Str = BytesToString((uint8*)InSrc.GetPropertyData(), StrLen);

		if(!OutDst.IsHandle())
		{
			OutDst.Init(StrLen);
		}

		OutDst.GetProperty()->InitializeValue((uint8*)OutDst.GetPropertyData());
		OutDst.GetProperty()->ImportText(*Str, (uint8*)OutDst.GetPropertyData(), 0, nullptr);

#if !UE_BUILD_SHIPPING && UE_BUILD_DEBUG
		// const typename PropertyType::TCppType* Value = reinterpret_cast<const typename PropertyType::TCppType*>((uint8*)OutDst.GetPropertyData());
		// auto o = Value;
#endif

		return true;
	}

	/** Specialization for FProperty casts and forwards to specializations. */
	template <> 
	REMOTECONTROLCOMMON_API bool FromBytes<FProperty>(const FRCPropertyVariant& InSrc, FRCPropertyVariant& InDstProperty);

	/** Sets raw data from input property handle. */

	// Non 'String-like' property types
	template <typename PropertyType>
	typename TEnableIf<TNot<RemoteControlTypeTraits::TIsStringLikeProperty<PropertyType>>::Value, bool>::Type
	ToBytes(const FRCPropertyVariant& InSrc, FRCPropertyVariant& OutDst)
	{
		InSrc.GetProperty()->CopyCompleteValue((uint8*)OutDst.GetPropertyData(), (uint8*)InSrc.GetPropertyData());

		return true;
	}

	template <>
	REMOTECONTROLCOMMON_API bool ToBytes<FStructProperty>(const FRCPropertyVariant& InSrc, FRCPropertyVariant& OutDst);

	template <>
	REMOTECONTROLCOMMON_API bool ToBytes<FArrayProperty>(const FRCPropertyVariant& InSrc, FRCPropertyVariant& OutDst);

	template <>
	REMOTECONTROLCOMMON_API bool ToBytes<FSetProperty>(const FRCPropertyVariant& InSrc, FRCPropertyVariant& OutDst);

	template <>
	REMOTECONTROLCOMMON_API bool ToBytes<FMapProperty>(const FRCPropertyVariant& InSrc, FRCPropertyVariant& OutDst);

	// Specialization for 'String-like' property types
	template <typename PropertyType>
	typename TEnableIf<RemoteControlTypeTraits::TIsStringLikeProperty<PropertyType>::Value, bool>::Type
	ToBytes(const FRCPropertyVariant& InSrc, FRCPropertyVariant& OutDst)
	{
		FString Str;
		InSrc.GetProperty()->ExportTextItem(Str, (uint8*)InSrc.GetPropertyData(), nullptr, nullptr, PPF_None, nullptr);

		TArray<uint8> Buffer;
		Buffer.SetNumUninitialized(Str.Len());
		StringToBytes(Str, Buffer.GetData(), Buffer.Num());

		OutDst.Init(Str.Len());
		FMemory::Memcpy((uint8*)OutDst.GetPropertyData(), Buffer.GetData(), Buffer.Num());

		return true;
	}

	/** Specialization for FProperty casts and forwards to specializations. */
	template <>
	REMOTECONTROLCOMMON_API bool ToBytes<FProperty>(const FRCPropertyVariant& InSrc, FRCPropertyVariant& OutDst);

#endif
}
