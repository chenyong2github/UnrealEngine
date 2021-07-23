// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "RCTypeTraits.h"
#include "RCTypeUtilities.h"
#include "Serialization/BufferArchive.h"
#include "Serialization/MemoryReader.h"
#include "UObject/WeakFieldPtr.h"

#if WITH_EDITOR
#include "PropertyEditorModule.h"
#include "PropertyHandle.h"
#endif

namespace RemoteControlPropertyUtilities
{
	static TMap<TWeakFieldPtr<FProperty>, TWeakObjectPtr<UFunction>> CachedSetterFunctions;
	static const FName NAME_BlueprintGetter(TEXT("BlueprintGetter"));
	static const FName NAME_BlueprintSetter(TEXT("BlueprintSetter"));

	/** Container that can hold either a PropertyHandle, or Property/Data pair. Similar to FFieldVariant */
	class FRCPropertyVariant
	{
#if WITH_EDITOR
		TSharedPtr<IPropertyHandle> PropertyHandle = nullptr;
#endif
		TWeakFieldPtr<FProperty> Property = nullptr;
		void* PropertyData = nullptr;
		TArray<uint8>* PropertyContainer = nullptr;

		bool bHasHandle = false;
		int32 NumElements = 1;

	public:
		explicit FRCPropertyVariant() = default;

#if WITH_EDITOR
		/** Construct from an IPropertyHandle. */
		FRCPropertyVariant(const TSharedPtr<IPropertyHandle>& InPropertyHandle)
			: bHasHandle(true)
		{
			PropertyHandle = InPropertyHandle;
			Property = PropertyHandle->GetProperty();
		}
#endif

		/** Construct from a Property, PropertyData ptr, and the expected element count (needed for arrays, strings, etc.). */
		FRCPropertyVariant(const FProperty* InProperty, const void* InPropertyData, const int32& InNumElements = 1)
			: NumElements(InNumElements)
		{
			Property = InProperty;
			PropertyData = const_cast<void*>(InPropertyData);
		}

		/** Construct from a Property and backing data array. Preferred over a raw ptr. */
		FRCPropertyVariant(const FProperty* InProperty, TArray<uint8>& InPropertyData, const int32& InNumElements = -1)
		{
			Property = InProperty;

			PropertyData = InPropertyData.GetData();
			PropertyContainer = &InPropertyData;
			NumElements = InNumElements > 0 ? InNumElements : InPropertyData.Num() / InProperty->GetSize();
		}

		virtual ~FRCPropertyVariant() = default;

		/** Gets the property. */
		FProperty* GetProperty() const
		{
			if(Property.IsValid())
			{
				return Property.Get();
			}

#if WITH_EDITOR
			if(bHasHandle && PropertyHandle.IsValid() && PropertyHandle->IsValidHandle())
			{
				return PropertyHandle->GetProperty();
			}
#endif

			return nullptr;
		}

		/** Gets the typed property, returns nullptr if not cast. */
		template <typename PropertyType>
		PropertyType* GetProperty() const
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
		void* GetPropertyData(const FProperty* InContainer = nullptr, int32 InIdx = 0) const
		{
#if WITH_EDITOR
			if(bHasHandle)
			{
				TArray<void*> Data;
				TSharedPtr<IPropertyHandle> InnerHandle = PropertyHandle;

				InnerHandle->AccessRawData(Data);
				check(Data.IsValidIndex(0)); // check that there's at least one set of data

				return Data[0];
			}
#endif

			if(PropertyContainer)
			{
				return PropertyContainer->GetData();
			}

			return PropertyData;
		}

		/** Returns the data as the ValueType. */
		template <typename ValueType>
		ValueType* GetPropertyValue(const FProperty* InContainer = nullptr, int32 InIdx = 0) const
		{
			return (ValueType*)(GetPropertyData(InContainer, InIdx));
		}

		/** Is this backed by an IPropertyHandle? */
		bool IsHandle() const { return bHasHandle; }

		/** Initialize/allocate if necessary. */
		void Init(int32 InSize = -1)
		{
			InSize = InSize > 0 ? InSize : GetElementSize();		
			if(!bHasHandle)
			{
				if(PropertyContainer)
				{
					PropertyContainer->SetNumZeroed(InSize);
					PropertyData = PropertyContainer->GetData();
				}
				else
				{
					PropertyData = FMemory::Malloc(InSize, GetProperty()->GetMinAlignment());
				}
				NumElements = 1;
				//GetProperty()->InitializeValue(PropertyData);
			}
		}

		/** Gets the number of elements, more than 1 if an array. */
		int32 Num() const
		{
			return NumElements;
		}
		
		/** Gets the size of the underlying data. */
		int32 Size() const
		{
			return PropertyContainer ? PropertyContainer->Num() : GetElementSize() * Num();
		}

		/** Gets individual element size (for arrays, etc.). */
		int32 GetElementSize() const
		{
			int32 ElementSize = GetProperty()->GetSize();
			if(const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(GetProperty()))
			{
				ElementSize = ArrayProperty->Inner->GetSize();
			}
			else if(const FSetProperty* SetProperty = CastField<FSetProperty>(GetProperty()))
			{
				ElementSize = SetProperty->ElementProp->GetSize();
			}
			else if(const FMapProperty* MapProperty = CastField<FMapProperty>(GetProperty()))
			{
				ElementSize = MapProperty->ValueProp->GetSize();
			}
			
			return ElementSize;
		}

		/** Calculate number of elements based on available info. If OtherProperty is specified, that's used instead. */
		void InferNum(const FRCPropertyVariant* InOtherProperty = nullptr)
		{
			const FRCPropertyVariant* SrcVariant = InOtherProperty ? InOtherProperty : this;
			
			const int32 ElementSize = SrcVariant->GetElementSize();
			int32 TotalSize = ElementSize;

			if(SrcVariant->PropertyContainer != nullptr)
			{
				// the first element is the item count (int32) so remove to get actual element count
				//TotalSize = InOtherProperty->PropertyContainer->Num() - sizeof(int32);

				if(const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(SrcVariant->GetProperty()))
				{
					FScriptArrayHelper Helper(ArrayProperty, SrcVariant->GetPropertyData());
					NumElements = Helper.Num();
					return;
				}
				else if(const FSetProperty* SetProperty = CastField<FSetProperty>(SrcVariant->GetProperty()))
				{
				}
				else if(const FMapProperty* MapProperty = CastField<FMapProperty>(SrcVariant->GetProperty()))
				{
				}
			}
#if WITH_EDITOR
			else if(SrcVariant->bHasHandle)
			{
				if(const TSharedPtr<IPropertyHandleArray> ArrayHandle = SrcVariant->PropertyHandle->AsArray())
				{
					uint32 N;
					if(ArrayHandle->GetNumElements(N) == FPropertyAccess::Success)
					{
						NumElements = N;
						return;	
					}
				}
				else if(TSharedPtr<IPropertyHandleSet> SetHandle = SrcVariant->PropertyHandle->AsSet())
				{
					uint32 N;
					if(SetHandle->GetNumElements(N) == FPropertyAccess::Success)
					{
						NumElements = N;
						return;	
					}
				}
				else if(TSharedPtr<IPropertyHandleMap> MapHandle = SrcVariant->PropertyHandle->AsMap())
				{
					uint32 N;
					if(MapHandle->GetNumElements(N) == FPropertyAccess::Success)
					{
						NumElements = N;
						return;	
					}
				}
			}
#endif
		}

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
	inline FString* FRCPropertyVariant::GetPropertyValue<FString>(const FProperty* InContainer, int32 InIdx) const
	{
		return GetProperty<FStrProperty>()->GetPropertyValuePtr(GetPropertyData(InContainer, InIdx));
	}

	/** Reads the raw data from InSrc and deserializes to OutDst. */
	template <typename PropertyType>
	typename TEnableIf<
		TOr<
			TAnd<
				TIsDerivedFrom<PropertyType, FProperty>,
				TNot<TIsSame<PropertyType, FProperty>>,
				TNot<TIsSame<PropertyType, FNumericProperty>>>,
			TIsSame<PropertyType, FEnumProperty>
			>::Value, bool>::Type
	Deserialize(const FRCPropertyVariant& InSrc, FRCPropertyVariant& OutDst)
	{
		using ValueType = typename TRemoteControlPropertyTypeTraits<PropertyType>::ValueType;
		
		TArray<uint8>* SrcPropertyContainer = InSrc.GetPropertyContainer();
		checkf(SrcPropertyContainer != nullptr, TEXT("Deserialize requires Src to have a backing container."));

		OutDst.Init(InSrc.Size()); // initializes only if necessary
		
		ValueType* DstCurrentValue = OutDst.GetPropertyValue<ValueType>();
		InSrc.GetProperty()->InitializeValue(DstCurrentValue);

		const int32 SrcSize = InSrc.Size();

		// The stored data size doesn't match, so cast
		if(OutDst.GetProperty()->ElementSize != SrcSize)
		{
			if(const FNumericProperty* DstProperty = OutDst.GetProperty<FNumericProperty>())
			{
				// @note this only works for integers
				if(DstProperty->IsInteger())
				{
					if(SrcSize == 1)
					{
						uint8* SrcValue = InSrc.GetPropertyValue<uint8>();
						if(SrcValue)
						{
							DstProperty->SetIntPropertyValue(DstCurrentValue, static_cast<uint64>(*SrcValue));
							return true;
						}
					}
					else if(SrcSize == 2)
					{
						uint16* SrcValue = InSrc.GetPropertyValue<uint16>();
						if(SrcValue)
						{
							DstProperty->SetIntPropertyValue(DstCurrentValue, static_cast<uint64>(*SrcValue));
							return true;
						}
					}
					else if(SrcSize == 4)
					{
						uint32* SrcValue = InSrc.GetPropertyValue<uint32>();
						if(SrcValue)
						{
							DstProperty->SetIntPropertyValue(DstCurrentValue, static_cast<uint64>(*SrcValue));
							return true;
						}
					}
				}
			}
		}

		FMemoryReader Reader(*SrcPropertyContainer);
		InSrc.GetProperty()->SerializeItem(FStructuredArchiveFromArchive(Reader).GetSlot(), DstCurrentValue, nullptr);

 		return true;
	}
	
	/** Reads the property value from InSrc and serializes to OutDst. */
	template <typename PropertyType>
	typename TEnableIf<
		TOr<
			TAnd<
				TIsDerivedFrom<PropertyType, FProperty>,
				TNot<TIsSame<PropertyType, FProperty>>,
				TNot<TIsSame<PropertyType, FNumericProperty>>>,
			TIsSame<PropertyType, FEnumProperty>
			>::Value, bool>::Type
	Serialize(const FRCPropertyVariant& InSrc, FRCPropertyVariant& OutDst)
	{
		using ValueType = typename TRemoteControlPropertyTypeTraits<PropertyType>::ValueType;

		ValueType* SrcValue = InSrc.GetPropertyValue<ValueType>();

		TArray<uint8>* DstPropertyContainer = OutDst.GetPropertyContainer();
		checkf(DstPropertyContainer != nullptr, TEXT("Serialize requires Dst to have a backing container."));

		DstPropertyContainer->Empty();
		OutDst.Init(InSrc.GetProperty()->GetSize()); // initializes only if necessary
		InSrc.GetProperty()->InitializeValue(DstPropertyContainer->GetData());
		
		FMemoryWriter Writer(*DstPropertyContainer);
		OutDst.GetProperty()->SerializeItem(FStructuredArchiveFromArchive(Writer).GetSlot(), SrcValue, nullptr);

		OutDst.InferNum(&InSrc);

		return true;
	}

	/** Specialization for FStructProperty. */
	template <>
	inline bool Deserialize<FStructProperty>(const FRCPropertyVariant& InSrc, FRCPropertyVariant& OutDst)
	{
		TArray<uint8>* SrcPropertyContainer = InSrc.GetPropertyContainer();
		checkf(SrcPropertyContainer != nullptr, TEXT("Deserialize requires Src to have a backing container."));
		
		OutDst.Init(InSrc.Size()); // initializes only if necessary
		void* DstCurrentValue = OutDst.GetPropertyValue<void>();
		InSrc.GetProperty<FStructProperty>()->Struct->InitializeStruct(DstCurrentValue);
		
		FMemoryReader Reader(*SrcPropertyContainer);
		InSrc.GetProperty()->SerializeItem(FStructuredArchiveFromArchive(Reader).GetSlot(), DstCurrentValue, nullptr);

		return true;
	}

	/** Specialization for FProperty casts and forwards to specializations. */
	template <typename PropertyType>
	typename TEnableIf<
		TOr<
			TIsSame<PropertyType, FProperty>,
			TIsSame<PropertyType, FNumericProperty>>::Value, bool>::Type
	Deserialize(const FRCPropertyVariant& InSrc, FRCPropertyVariant& OutDst)
	{
		const FProperty* Property = OutDst.GetProperty();
		FOREACH_CAST_PROPERTY(Property, Deserialize<CastPropertyType>(InSrc, OutDst))

		return true;
	}

	/** Specialization for FProperty casts and forwards to specializations. */
	template <typename PropertyType>
	typename TEnableIf<
		TOr<
			TIsSame<PropertyType, FProperty>,
			TIsSame<PropertyType, FNumericProperty>>::Value, bool>::Type
	Serialize(const FRCPropertyVariant& InSrc, FRCPropertyVariant& OutDst)
	{
		const FProperty* Property = InSrc.GetProperty();
		FOREACH_CAST_PROPERTY(Property, Serialize<CastPropertyType>(InSrc, OutDst))

		return true;
	}

	static FProperty* FindSetterArgument(UFunction* SetterFunction, FProperty* PropertyToModify)
	{
		FProperty* SetterArgument = nullptr;

		if (!ensure(SetterFunction))
		{
			return nullptr;
		}

		// Check if the first parameter for the setter function matches the parameter value.
		for (TFieldIterator<FProperty> PropertyIt(SetterFunction); PropertyIt; ++PropertyIt)
		{
			if (PropertyIt->HasAnyPropertyFlags(CPF_Parm) && !PropertyIt->HasAnyPropertyFlags(CPF_ReturnParm))
			{
				if (PropertyIt->SameType(PropertyToModify))
				{
					SetterArgument = *PropertyIt;
				}

				break;
			}
		}

		return SetterArgument;
	}

	static UFunction* FindSetterFunctionInternal(FProperty* Property, UClass* OwnerClass)
	{
		// Check if the property setter is already cached.
		TWeakObjectPtr<UFunction> SetterPtr = CachedSetterFunctions.FindRef(Property);
		if (SetterPtr.IsValid())
		{
			return SetterPtr.Get();
		}

		UFunction* SetterFunction = nullptr;
#if WITH_EDITOR
		const FString& SetterName = Property->GetMetaData(*NAME_BlueprintSetter.ToString());
		if (!SetterName.IsEmpty())
		{
			SetterFunction = OwnerClass->FindFunctionByName(*SetterName);
		}
#endif

		FString PropertyName = Property->GetName();
		if (Property->IsA<FBoolProperty>())
		{
			PropertyName.RemoveFromStart("b", ESearchCase::CaseSensitive);
		}

		static const TArray<FString> SetterPrefixes = {
			FString("Set"),
			FString("K2_Set")
		};

		for (const FString& Prefix : SetterPrefixes)
		{
			FName SetterFunctionName = FName(Prefix + PropertyName);
			SetterFunction = OwnerClass->FindFunctionByName(SetterFunctionName);
			if (SetterFunction)
			{
				break;
			}
		}

		if (SetterFunction && FindSetterArgument(SetterFunction, Property))
		{
			CachedSetterFunctions.Add(Property, SetterFunction);
		}
		else
		{
			// Arguments are not compatible so don't use this setter.
			SetterFunction = nullptr;
		}

		return SetterFunction;
	}

	static UFunction* FindSetterFunction(FProperty* Property, UClass* OwnerClass = nullptr)
	{
		// UStruct properties cannot have setters.
		if (!ensure(Property) || !Property->GetOwnerClass())
		{
			return nullptr;
		}

		UFunction* SetterFunction = nullptr;
		if (OwnerClass && OwnerClass != Property->GetOwnerClass())
		{
			SetterFunction = FindSetterFunctionInternal(Property, OwnerClass);
		}
		
		if (!SetterFunction)
		{
			SetterFunction = FindSetterFunctionInternal(Property, Property->GetOwnerClass());
		}
		
		return SetterFunction;
	}
}
