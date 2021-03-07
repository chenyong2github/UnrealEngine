// Copyright Epic Games, Inc. All Rights Reserved.

#include "RemoteControlProtocolBinding.h"

#include "IRemoteControlModule.h"
#include "RemoteControlPreset.h"

#include "Algo/Sort.h"
#include "Backends/CborStructDeserializerBackend.h"
#include "CborWriter.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/MemoryReader.h"
#include "UObject/StructOnScope.h"

#if WITH_EDITOR
#include "ScopedTransaction.h"
#endif

#define LOCTEXT_NAMESPACE "RemoteControl"

namespace EntityInterpolation
{
	/** 
	 * The function is taking the map with the range buffer pointer and convert it to the map with value pointers instead. 
	 * Range keys stay the same.
	 * For example, we have an input map TArray<TPair<int32, uint8*>> where uint8* points to the float buffer (4 bytes)
	 * Then we need to convert it to value buffer where we convert uint8* to value pointer. In this case, is float*
	 * 
	 * But in cases when we are dealing with Struct or any containers (Array, Map, Structs) inner elements we need to convert the container pointer to value pointer
	 * And that means if there no FProperty Outer wh should convert uint8* to ValueType* directly, 
	 * but in case with Outer != nullptr we need to convert with InProperty->ContainerPtrToValuePtr<ValueType>
	 */
	template <typename ValueType, typename PropertyType, typename ProtocolValueType>
	TArray<TPair<const ProtocolValueType*, const ValueType*>> ContainerPtrMapToValuePtrMap(PropertyType* InProperty, FProperty* Outer, const TArray<TPair<const uint8*, const uint8*>>& InRangeMappingBuffers, int32 ArrayIndex)
	{
		TArray<TPair<const ProtocolValueType*, const ValueType*>> ValuePtrMap;
		ValuePtrMap.Reserve(InRangeMappingBuffers.Num());

		int32 MappingIndex = 0;
		for (const TPair<const uint8*, const uint8*>& RangePair : InRangeMappingBuffers)
		{
			const ValueType* PropertyValuePtr = nullptr;
			if (Outer)
			{
				PropertyValuePtr = InProperty->template ContainerPtrToValuePtr<const ValueType>(RangePair.Value, ArrayIndex);
			}
			// In there are no outer the buffer value holds the bound property value itself
			else
			{
				PropertyValuePtr = reinterpret_cast<const ValueType*>(RangePair.Value);
			}

			ValuePtrMap.Emplace(reinterpret_cast<const ProtocolValueType*>(RangePair.Key), PropertyValuePtr);
			MappingIndex++;
		}

		return ValuePtrMap;
	}

	template <typename ValueType, typename PropertyType, typename ProtocolValueType>
	bool InterpolateValue(PropertyType* InProperty, FProperty* Outer, const TArray<TPair<const uint8*, const uint8*>>& InRangeMappingBuffers, ProtocolValueType InProtocolValue, ValueType& OutResultValue, int32 ArrayIndex)
	{
		TArray<TPair<const ProtocolValueType*, const ValueType*>> ValuePtrMap = ContainerPtrMapToValuePtrMap<ValueType, PropertyType, ProtocolValueType>(InProperty, Outer, InRangeMappingBuffers, ArrayIndex);

		// sort by input protocol value
		Algo::SortBy(ValuePtrMap, [](const TPair<const ProtocolValueType*, const ValueType*>& Item) -> ProtocolValueType
		{
			return *Item.Key;
		});

		// clamp to min and max mapped values
		ProtocolValueType ClampProtocolValue = FMath::Clamp(InProtocolValue, *ValuePtrMap[0].Key, *ValuePtrMap.Last().Key);

		TPair<const ProtocolValueType*, const ValueType*> RangeMin{ nullptr, nullptr };
		TPair<const ProtocolValueType*, const ValueType*> RangeMax{ nullptr, nullptr };

		for (int32 RangeIdx = 0; RangeIdx < ValuePtrMap.Num(); ++RangeIdx)
		{
			const TPair<const ProtocolValueType*, const ValueType*>& Range = ValuePtrMap[RangeIdx];
			if (ClampProtocolValue > *Range.Key || RangeMin.Value == nullptr)
			{
				RangeMin = Range;
			}

			if (ClampProtocolValue <= *Range.Key)
			{
				RangeMax = ValuePtrMap[FMath::Min(ValuePtrMap.Num() - 1, RangeIdx + 1)];
				break;
			}
		}

		if (RangeMax.Value == nullptr || RangeMin.Value == nullptr)
		{
			return ensure(false);
		}
		if (RangeMax.Key == nullptr || RangeMax.Key == nullptr)
		{
			return ensure(false);
		}
		else if (*RangeMax.Key == *RangeMin.Key)
		{
			return ensure(false);
		}

		// Normalize value to range for interpolation
		const float Percentage = static_cast<float>(ClampProtocolValue - *RangeMin.Key) / static_cast<float>(*RangeMax.Key - *RangeMin.Key);

		OutResultValue = FMath::Lerp(*RangeMin.Value, *RangeMax.Value, Percentage);

		return true;
	}

	// Writes a property value to the serialization output.
	template<typename ValueType>
	void WritePropertyValue(FCborWriter& InCborWriter, FProperty* InProperty, const ValueType& Value)
	{
		InCborWriter.WriteValue(InProperty->GetName());
		InCborWriter.WriteValue(Value);
	}

	template<typename ProtocolValueType>
	bool WriteProperty(FProperty* InProperty, FProperty* OuterProperty, const TArray<TPair<const uint8*, const uint8*>>& InRangeMappingBuffers, ProtocolValueType InProtocolValue, FCborWriter& InCborWriter, int32 ArrayIndex = 0)
	{
		bool bSuccess = false;
		if (FBoolProperty* BoolProperty = CastField<FBoolProperty>(InProperty))
		{
			bool BoolValue = false;
			bSuccess = InterpolateValue(BoolProperty, OuterProperty, InRangeMappingBuffers, InProtocolValue, BoolValue, ArrayIndex);
			WritePropertyValue(InCborWriter, InProperty, BoolValue);
		}
		else if (FNumericProperty* NumericProperty = CastField<FNumericProperty>(InProperty))
		{
			if (CastField<FFloatProperty>(InProperty))
			{
				float FloatValue = 0.f;
				bSuccess = InterpolateValue(NumericProperty, OuterProperty, InRangeMappingBuffers, InProtocolValue, FloatValue, ArrayIndex);
				WritePropertyValue(InCborWriter, InProperty, FloatValue);
			}
			else if (CastField<FDoubleProperty>(InProperty))
			{
				double DoubleValue = 0.0;
				bSuccess = InterpolateValue(NumericProperty, OuterProperty, InRangeMappingBuffers, InProtocolValue, DoubleValue, ArrayIndex);
				WritePropertyValue(InCborWriter, InProperty, DoubleValue);
			}
			else if (NumericProperty->IsInteger() && !NumericProperty->IsEnum())
			{
				if (FByteProperty* ByteProperty = CastField<FByteProperty>(InProperty))
				{
					uint8 IntValue = 0;
					bSuccess = InterpolateValue(NumericProperty, OuterProperty, InRangeMappingBuffers, InProtocolValue, IntValue, ArrayIndex);
					WritePropertyValue(InCborWriter, InProperty, static_cast<int64>(IntValue));
				}
				else if (FIntProperty* IntProperty = CastField<FIntProperty>(InProperty))
				{
					int IntValue = 0;
					bSuccess = InterpolateValue(NumericProperty, OuterProperty, InRangeMappingBuffers, InProtocolValue, IntValue, ArrayIndex);
					WritePropertyValue(InCborWriter, InProperty, static_cast<int64>(IntValue));
				}
				else if (FUInt32Property* UInt32Property = CastField<FUInt32Property>(InProperty))
				{
					uint32 IntValue = 0;
					bSuccess = InterpolateValue(NumericProperty, OuterProperty, InRangeMappingBuffers, InProtocolValue, IntValue, ArrayIndex);
					WritePropertyValue(InCborWriter, InProperty, static_cast<int64>(IntValue));
				}
				else if (FInt16Property* Int16Property = CastField<FInt16Property>(InProperty))
				{
					int16 IntValue = 0;
					bSuccess = InterpolateValue(NumericProperty, OuterProperty, InRangeMappingBuffers, InProtocolValue, IntValue, ArrayIndex);
					WritePropertyValue(InCborWriter, InProperty, static_cast<int64>(IntValue));
				}
				else if (FUInt16Property* FInt16Property = CastField<FUInt16Property>(InProperty))
				{
					uint16 IntValue = 0;
					bSuccess = InterpolateValue(NumericProperty, OuterProperty, InRangeMappingBuffers, InProtocolValue, IntValue, ArrayIndex);
					WritePropertyValue(InCborWriter, InProperty, static_cast<int64>(IntValue));
				}
				else if (FInt64Property* Int64Property = CastField<FInt64Property>(InProperty))
				{
					int64 IntValue = 0;
					bSuccess = InterpolateValue(NumericProperty, OuterProperty, InRangeMappingBuffers, InProtocolValue, IntValue, ArrayIndex);
					WritePropertyValue(InCborWriter, InProperty, static_cast<int64>(IntValue));
				}
				else if (FUInt64Property* FInt64Property = CastField<FUInt64Property>(InProperty))
				{
					uint64 IntValue = 0;
					bSuccess = InterpolateValue(NumericProperty, OuterProperty, InRangeMappingBuffers, InProtocolValue, IntValue, ArrayIndex);
					WritePropertyValue(InCborWriter, InProperty, static_cast<int64>(IntValue));
				}
				else if (FInt8Property* Int8Property = CastField<FInt8Property>(InProperty))
				{
					int8 IntValue = 0;
					bSuccess = InterpolateValue(NumericProperty, OuterProperty, InRangeMappingBuffers, InProtocolValue, IntValue, ArrayIndex);
					WritePropertyValue(InCborWriter, InProperty, static_cast<int64>(IntValue));
				}
			}
		}
		else if (FStructProperty* StructProperty = CastField<FStructProperty>(InProperty))
		{
			InCborWriter.WriteValue(StructProperty->GetName());
			InCborWriter.WriteContainerStart(ECborCode::Map, -1/*Indefinite*/);

			bool bStructSuccess = true;
			for (TFieldIterator<FProperty> It(StructProperty->Struct); It; ++It)
			{
				TArray<TPair<const uint8*, const uint8*>> RangeMappingBuffers;
				RangeMappingBuffers.Reserve(InRangeMappingBuffers.Num());

				for (const TPair<const uint8*, const uint8*>& RangePair : InRangeMappingBuffers)
				{
					const uint8* DataPtr = StructProperty->ContainerPtrToValuePtr<const uint8>(RangePair.Value, ArrayIndex);
					RangeMappingBuffers.Emplace(RangePair.Key, DataPtr);
				}

				bStructSuccess &= WriteProperty(*It, StructProperty, RangeMappingBuffers, InProtocolValue, InCborWriter, ArrayIndex);
			}

			bSuccess = bStructSuccess;
			InCborWriter.WriteContainerEnd();
		}

		return bSuccess;
	}

	template<typename ProtocolValueType>
	bool ApplyProtocolValueToProperty(FProperty* InProperty, ProtocolValueType InProtocolValue, const TArray<TPair<const uint8*, const uint8*>>& InRangeMappingBuffers, FCborWriter& InCborWriter)
	{
		// Structures
		if (FStructProperty* StructProperty = CastField<FStructProperty>(InProperty))
		{
			UScriptStruct* ScriptStruct = StructProperty->Struct;

			InCborWriter.WriteValue(StructProperty->GetName());
			InCborWriter.WriteContainerStart(ECborCode::Map, -1/*Indefinite*/);

			bool bStructSuccess = true;
			for (TFieldIterator<FProperty> It(ScriptStruct); It; ++It)
			{
				bStructSuccess &= WriteProperty(*It, StructProperty, InRangeMappingBuffers, InProtocolValue, InCborWriter);
			}

			InCborWriter.WriteContainerEnd();

			return bStructSuccess;
		}

		// Dynamic arrays
		else if (FArrayProperty* ArrayProperty = CastField<FArrayProperty>(InProperty))
		{
			UE_LOG(LogRemoteControl, Warning, TEXT("ArrayProperty not supported"));
			return false;
		}

		// Maps
		else if (FMapProperty* MapProperty = CastField<FMapProperty>(InProperty))
		{
			UE_LOG(LogRemoteControl, Warning, TEXT("MapProperty not supported"));
			return false;
		}

		// Sets
		else if (FSetProperty* SetProperty = CastField<FSetProperty>(InProperty))
		{
			UE_LOG(LogRemoteControl, Warning, TEXT("SetProperty not supported"));
			return false;
		}

		// Static arrays
		else if (InProperty->ArrayDim > 1)
		{
			UE_LOG(LogRemoteControl, Warning, TEXT("Static arrays not supported"));
			return false;
		}

		// All other properties
		else
		{
			return WriteProperty(InProperty, nullptr, InRangeMappingBuffers, InProtocolValue, InCborWriter);
		}
	}
}

FRemoteControlProtocolMapping::FRemoteControlProtocolMapping(FProperty* InProperty, uint8 InRangeValueSize)
	: Id(FGuid::NewGuid())
{
	if (FBoolProperty* BoolProperty = CastField<FBoolProperty>(InProperty))
	{
		InterpolationMappingPropertyData.AddZeroed(sizeof(bool));
	}
	else if (FNumericProperty* NumericProperty = CastField<FNumericProperty>(InProperty))
	{
		InterpolationMappingPropertyData.AddZeroed(NumericProperty->ElementSize);
	}
	else if (FStructProperty* StructProperty = CastField<FStructProperty>(InProperty))
	{
		UScriptStruct* ScriptStruct = StructProperty->Struct;

		InterpolationMappingPropertyData.AddZeroed(ScriptStruct->GetStructureSize());
		ScriptStruct->InitializeStruct(InterpolationMappingPropertyData.GetData());
	}

	InterpolationRangePropertyData.SetNumZeroed(InRangeValueSize);

	BoundPropertyPath = InProperty;
}

bool FRemoteControlProtocolMapping::operator==(const FRemoteControlProtocolMapping& InProtocolMapping) const
{
	return Id == InProtocolMapping.Id;
}

bool FRemoteControlProtocolMapping::operator==(FGuid InProtocolMappingId) const
{
	return Id == InProtocolMappingId;
}

TSharedPtr<FStructOnScope> FRemoteControlProtocolMapping::GetMappingPropertyAsStructOnScope()
{
	if (FStructProperty* StructProperty = CastField<FStructProperty>(BoundPropertyPath.Get()))
	{
		UScriptStruct* ScriptStruct = StructProperty->Struct;
		check(InterpolationMappingPropertyData.Num() && InterpolationMappingPropertyData.Num() == ScriptStruct->GetStructureSize());

		return MakeShared<FStructOnScope>(ScriptStruct, InterpolationMappingPropertyData.GetData());
	}

	ensure(false);
	return nullptr;
}

uint8 FRemoteControlProtocolEntity::GetRangePropertySize() const
{
	if (const EName* PropertyType = GetRangePropertyName().ToEName())
	{
		switch (*PropertyType)
		{
		case NAME_Int8Property:
			return sizeof(int8);

		case NAME_Int16Property:
			return sizeof(int16);

		case NAME_IntProperty:
			return sizeof(int32);

		case NAME_Int64Property:
			return sizeof(int64);

		case NAME_ByteProperty:
			return sizeof(uint8);

		case NAME_UInt16Property:
			return sizeof(uint16);

		case NAME_UInt32Property:
			return sizeof(uint32);

		case NAME_UInt64Property:
			return sizeof(uint64);

		case NAME_FloatProperty:
			return sizeof(float);

		case NAME_DoubleProperty:
			return sizeof(double);

		default:
			break;
		}
	}

	checkNoEntry();
	return 0;
}

bool FRemoteControlProtocolEntity::ApplyProtocolValueToProperty(double InProtocolValue)
{
	if (Mappings.Num() == 0 || Mappings.Num() == 1)
	{
		return false;
	}

	URemoteControlPreset* Preset = IRemoteControlModule::Get().ResolvePreset(PresetName);
	if (!Preset)
	{
		return false;
	}

	TSharedPtr<FRemoteControlProperty> RemoteControlProperty = Preset->GetExposedEntity<FRemoteControlProperty>(PropertyId).Pin();
	if (!RemoteControlProperty.IsValid())
	{
		return false;
	}

	FProperty* Property = RemoteControlProperty->GetProperty();
	if (!Property)
	{
		return false;
	}

	FRCObjectReference ObjectRef;
	ObjectRef.Property = Property;
	ObjectRef.Access = bGenerateTransaction ? ERCAccess::WRITE_TRANSACTION_ACCESS : ERCAccess::WRITE_ACCESS;
	ObjectRef.PropertyPathInfo = RemoteControlProperty->FieldPathInfo.ToString();

	bool bSuccess = true;
	for (UObject* Object : RemoteControlProperty->ResolveFieldOwners())
	{
		IRemoteControlModule::Get().ResolveObjectProperty(ObjectRef.Access, Object, ObjectRef.PropertyPathInfo, ObjectRef);

		// Set properties after interpolation
		TArray<uint8> InterpolatedBuffer;
		if (GetInterpolatedPropertyBuffer(Property, InProtocolValue, InterpolatedBuffer))
		{
			FMemoryReader MemoryReader(InterpolatedBuffer);
			FCborStructDeserializerBackend CborStructDeserializerBackend(MemoryReader);
			bSuccess &= IRemoteControlModule::Get().SetObjectProperties(ObjectRef, CborStructDeserializerBackend);
		}
	}

	return bSuccess;
}

bool FRemoteControlProtocolEntity::GetInterpolatedPropertyBuffer(FProperty* InProperty, double InProtocolValue, TArray<uint8>& OutBuffer)
{
	OutBuffer.Empty();

	const TArray<TPair<const uint8*, const uint8*>> RangeMappingBuffers = GetRangeMappingBuffers();
	bool bSuccess = false;

	// Write interpolated properties to Cbor buffer
	FMemoryWriter MemoryWriter(OutBuffer);
	FCborWriter CborWriter(&MemoryWriter);
	CborWriter.WriteContainerStart(ECborCode::Map, -1/*Indefinite*/);

	// Normalize before apply
	switch (*GetRangePropertyName().ToEName())
	{
		case NAME_Int8Property:
			bSuccess = EntityInterpolation::ApplyProtocolValueToProperty(InProperty, static_cast<int8>(InProtocolValue), RangeMappingBuffers, CborWriter);
			break;
		case NAME_Int16Property:
			bSuccess = EntityInterpolation::ApplyProtocolValueToProperty(InProperty, static_cast<int16>(InProtocolValue), RangeMappingBuffers, CborWriter);
			break;
		case NAME_IntProperty:
			bSuccess = EntityInterpolation::ApplyProtocolValueToProperty(InProperty, static_cast<int32>(InProtocolValue), RangeMappingBuffers, CborWriter);
			break;
		case NAME_Int64Property:
			bSuccess = EntityInterpolation::ApplyProtocolValueToProperty(InProperty, static_cast<int64>(InProtocolValue), RangeMappingBuffers, CborWriter);
			break;
		case NAME_ByteProperty:
			bSuccess = EntityInterpolation::ApplyProtocolValueToProperty(InProperty, static_cast<uint8>(InProtocolValue), RangeMappingBuffers, CborWriter);
			break;
		case NAME_UInt16Property:
			bSuccess = EntityInterpolation::ApplyProtocolValueToProperty(InProperty, static_cast<uint16>(InProtocolValue), RangeMappingBuffers, CborWriter);
			break;
		case NAME_UInt32Property:
			bSuccess = EntityInterpolation::ApplyProtocolValueToProperty(InProperty, static_cast<uint32>(InProtocolValue), RangeMappingBuffers, CborWriter);
			break;
		case NAME_UInt64Property:
			bSuccess = EntityInterpolation::ApplyProtocolValueToProperty(InProperty, static_cast<uint64>(InProtocolValue), RangeMappingBuffers, CborWriter);
			break;
		case NAME_FloatProperty:
			bSuccess = EntityInterpolation::ApplyProtocolValueToProperty(InProperty, static_cast<float>(InProtocolValue), RangeMappingBuffers, CborWriter);
			break;
		case NAME_DoubleProperty:
			bSuccess = EntityInterpolation::ApplyProtocolValueToProperty(InProperty, static_cast<double>(InProtocolValue), RangeMappingBuffers, CborWriter);
			break;
		default:
			checkNoEntry();
	}

	CborWriter.WriteContainerEnd();

	return bSuccess;
}

TArray<TPair<const uint8*, const uint8*>> FRemoteControlProtocolEntity::GetRangeMappingBuffers() const
{
	TArray<TPair<const uint8*, const uint8*>> RangeMappingBuffers;
	RangeMappingBuffers.Reserve(Mappings.Num());

	for (const FRemoteControlProtocolMapping& Mapping : Mappings)
	{
		RangeMappingBuffers.Emplace(Mapping.InterpolationRangePropertyData.GetData(), Mapping.InterpolationMappingPropertyData.GetData());
	}

	return RangeMappingBuffers;
}

FRemoteControlProtocolBinding::FRemoteControlProtocolBinding(const FName InProtocolName, const FGuid& InPropertyId, TSharedPtr<TStructOnScope<FRemoteControlProtocolEntity>> InRemoteControlProtocolEntityPtr)
	: Id(FGuid::NewGuid())
	, ProtocolName(InProtocolName)
	, PropertyId(InPropertyId)
	, RemoteControlProtocolEntityPtr(InRemoteControlProtocolEntityPtr)
{
}

bool FRemoteControlProtocolBinding::operator==(const FRemoteControlProtocolBinding& InProtocolBinding) const
{
	return Id == InProtocolBinding.Id;
}

bool FRemoteControlProtocolBinding::operator==(FGuid InProtocolBindingId) const
{
	return Id == InProtocolBindingId;
}

int32 FRemoteControlProtocolBinding::RemoveMapping(const FGuid& InMappingId)
{
	if (FRemoteControlProtocolEntity* ProtocolEntity = GetRemoteControlProtocolEntity())
	{
		return ProtocolEntity->Mappings.RemoveByHash(GetTypeHash(InMappingId), InMappingId);
	}

	return ensure(0);
}

void FRemoteControlProtocolBinding::ClearMappings()
{
	if (FRemoteControlProtocolEntity* ProtocolEntity = GetRemoteControlProtocolEntity())
	{
		ProtocolEntity->Mappings.Empty();
		return;
	}

	ensure(false);
}

void FRemoteControlProtocolBinding::AddMapping(const FRemoteControlProtocolMapping& InMappingsData)
{
	if (FRemoteControlProtocolEntity* ProtocolEntity = GetRemoteControlProtocolEntity())
	{
		ProtocolEntity->Mappings.Add(InMappingsData);
		return;
	}

	ensure(false);
}

void FRemoteControlProtocolBinding::ForEachMapping(FGetProtocolMappingCallback InCallback)
{
	if (FRemoteControlProtocolEntity* ProtocolEntity = GetRemoteControlProtocolEntity())
	{
		for (FRemoteControlProtocolMapping& Mapping : ProtocolEntity->Mappings)
		{
			InCallback(Mapping);
		}
	}
}

bool FRemoteControlProtocolBinding::SetPropertyDataToMapping(const FGuid& InMappingId, const void* InPropertyValuePtr)
{
	if (FRemoteControlProtocolMapping* Mapping = FindMapping(InMappingId))
	{
		FMemory::Memcpy(Mapping->InterpolationMappingPropertyData.GetData(), InPropertyValuePtr, Mapping->InterpolationMappingPropertyData.Num());
		return true;
	}

	return false;
}

FRemoteControlProtocolMapping* FRemoteControlProtocolBinding::FindMapping(const FGuid& InMappingId)
{
	if (FRemoteControlProtocolEntity* ProtocolEntity = GetRemoteControlProtocolEntity())
	{
		return ProtocolEntity->Mappings.FindByHash(GetTypeHash(InMappingId), InMappingId);
	}

	ensure(false);
	return nullptr;
}

TSharedPtr<FStructOnScope> FRemoteControlProtocolBinding::GetStructOnScope() const
{
	return RemoteControlProtocolEntityPtr;
}

FRemoteControlProtocolEntity* FRemoteControlProtocolBinding::GetRemoteControlProtocolEntity()
{
	if (RemoteControlProtocolEntityPtr.IsValid())
	{
		return RemoteControlProtocolEntityPtr->Get();
	}
	return nullptr;
}

bool FRemoteControlProtocolBinding::Serialize(FArchive& Ar)
{
	if (Ar.IsLoading() || Ar.IsSaving())
	{
		Ar << *this;
	}

	return true;
}

FArchive& operator<<(FArchive& Ar, FRemoteControlProtocolBinding& InProtocolBinding)
{
	UScriptStruct* ScriptStruct = FRemoteControlProtocolBinding::StaticStruct();

	ScriptStruct->SerializeTaggedProperties(Ar, (uint8*)&InProtocolBinding, ScriptStruct, nullptr);

	// Serialize TStructOnScope
	if (Ar.IsLoading())
	{
		InProtocolBinding.RemoteControlProtocolEntityPtr = MakeShared<TStructOnScope<FRemoteControlProtocolEntity>>();
		Ar << *InProtocolBinding.RemoteControlProtocolEntityPtr;
	} 
	else if (Ar.IsSaving())
	{
		TSharedPtr<TStructOnScope<FRemoteControlProtocolEntity>> EntityPtr = InProtocolBinding.RemoteControlProtocolEntityPtr;

		if (FRemoteControlProtocolEntity* ProtocolEntity = InProtocolBinding.GetRemoteControlProtocolEntity())
		{
			Ar << *EntityPtr;
		}
	}

	return Ar;
}

uint32 GetTypeHash(const FRemoteControlProtocolMapping& InProtocolMapping)
{
	return GetTypeHash(InProtocolMapping.Id);
}

uint32 GetTypeHash(const FRemoteControlProtocolBinding& InProtocolBinding)
{
	return GetTypeHash(InProtocolBinding.Id);
}

#undef LOCTEXT_NAMESPACE
