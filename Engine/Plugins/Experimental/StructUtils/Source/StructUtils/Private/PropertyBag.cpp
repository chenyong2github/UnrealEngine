// Copyright Epic Games, Inc. All Rights Reserved.

#include "PropertyBag.h"
#include "StructView.h"
#include "Hash/CityHash.h"
#include "UObject/TextProperty.h"

namespace UE::StructUtils::Private
{

	bool CanCastTo(const UStruct* From, const UStruct* To)
	{
		return From != nullptr && To != nullptr && From->IsChildOf(To);
	}

	uint64 GetObjectHash(const UObject* Object)
	{
		const FString PathName = GetPathNameSafe(Object);
		return CityHash64((const char*)GetData(PathName), PathName.Len() * sizeof(TCHAR));
	}

	uint64 CalcPropertyDescHash(const FPropertyBagPropertyDesc& Desc)
	{
#if WITH_EDITORONLY_DATA
		const uint32 Hashes[] = { GetTypeHash(Desc.ID), GetTypeHash(Desc.Name), GetTypeHash(Desc.ValueType), GetTypeHash(Desc.MetaData) };
#else
		const uint32 Hashes[] = { GetTypeHash(Desc.ID), GetTypeHash(Desc.Name), GetTypeHash(Desc.ValueType) };
#endif
		return CityHash64WithSeed((const char*)Hashes, sizeof(Hashes), GetObjectHash(Desc.ValueTypeObject));
	}

	uint64 CalcPropertyDescArrayHash(const TConstArrayView<FPropertyBagPropertyDesc> Descs)
	{
		uint64 Hash = 0;
		for (const FPropertyBagPropertyDesc& Desc : Descs)
		{
			Hash = CityHash128to64(Uint128_64(Hash, CalcPropertyDescHash(Desc)));
		}
		return Hash;
	}

	EPropertyBagPropertyType GetValueTypeFromProperty(const FProperty* InSourceProperty)
	{
		if (CastField<FBoolProperty>(InSourceProperty))
		{
			return EPropertyBagPropertyType::Bool;
		}
		if (const FByteProperty* ByteProperty = CastField<FByteProperty>(InSourceProperty))
		{
			return ByteProperty->IsEnum() ? EPropertyBagPropertyType::Enum : EPropertyBagPropertyType::Byte;
		}
		if (CastField<FIntProperty>(InSourceProperty))
		{
			return EPropertyBagPropertyType::Int32;
		}
		if (CastField<FInt64Property>(InSourceProperty))
		{
			return EPropertyBagPropertyType::Int64;
		}
		if (CastField<FFloatProperty>(InSourceProperty))
		{
			return EPropertyBagPropertyType::Float;
		}
		if (CastField<FDoubleProperty>(InSourceProperty))
		{
			return EPropertyBagPropertyType::Double;
		}
		if (CastField<FNameProperty>(InSourceProperty))
		{
			return EPropertyBagPropertyType::Name;
		}
		if (CastField<FStrProperty>(InSourceProperty))
		{
			return EPropertyBagPropertyType::String;
		}
		if (CastField<FTextProperty>(InSourceProperty))
		{
			return EPropertyBagPropertyType::Text;
		}
		if (CastField<FEnumProperty>(InSourceProperty))
		{
			return EPropertyBagPropertyType::Enum;
		}
		if (CastField<FStructProperty>(InSourceProperty))
		{
			return EPropertyBagPropertyType::Struct;
		}
		if (CastField<FObjectProperty>(InSourceProperty))
		{
			return EPropertyBagPropertyType::Object;
		}
		if (CastField<FSoftObjectProperty>(InSourceProperty))
		{
			return EPropertyBagPropertyType::SoftObject;
		}
		if (CastField<FClassProperty>(InSourceProperty))
		{
			return EPropertyBagPropertyType::Class;
		}
		if (CastField<FSoftClassProperty>(InSourceProperty))
		{
			return EPropertyBagPropertyType::SoftClass;
		}

		return EPropertyBagPropertyType::None;
	}

	UObject* GetValueTypeObjectFromProperty(const FProperty* InSourceProperty)
	{
		if (const FByteProperty* ByteProperty = CastField<FByteProperty>(InSourceProperty))
		{
			if (ByteProperty->IsEnum())
			{
				return ByteProperty->Enum;
			}
		}
		if (const FEnumProperty* EnumProp = CastField<FEnumProperty>(InSourceProperty))
		{
			return EnumProp->GetEnum();
		}
		if (const FStructProperty* StructProperty = CastField<FStructProperty>(InSourceProperty))
		{
			return StructProperty->Struct;
		}
		if (const FObjectProperty* ObjectProperty = CastField<FObjectProperty>(InSourceProperty))
		{
			return ObjectProperty->PropertyClass;
		}
		if (const FSoftObjectProperty* SoftObjectProperty = CastField<FSoftObjectProperty>(InSourceProperty))
		{
			return SoftObjectProperty->PropertyClass;
		}
		if (const FClassProperty* ClassProperty = CastField<FClassProperty>(InSourceProperty))
		{
			return ClassProperty->PropertyClass;
		}
		if (const FSoftClassProperty* SoftClassProperty = CastField<FSoftClassProperty>(InSourceProperty))
		{
			return SoftClassProperty->PropertyClass;
		}

		return nullptr;
	}

	FProperty* CreatePropertyFromDesc(const FPropertyBagPropertyDesc& Desc, UScriptStruct* PropertyScope)
	{
		switch (Desc.ValueType)
		{
		case EPropertyBagPropertyType::Bool:
			{
				FBoolProperty* Prop = new FBoolProperty(PropertyScope, Desc.Name, RF_Public);
				return Prop;
			}
		case EPropertyBagPropertyType::Byte:
			{
				FByteProperty* Prop = new FByteProperty(PropertyScope, Desc.Name, RF_Public);
				Prop->SetPropertyFlags(CPF_HasGetValueTypeHash);
				return Prop;
			}
		case EPropertyBagPropertyType::Int32:
			{
				FIntProperty* Prop = new FIntProperty(PropertyScope, Desc.Name, RF_Public);
				Prop->SetPropertyFlags(CPF_HasGetValueTypeHash);
				return Prop;
			}
		case EPropertyBagPropertyType::Int64:
			{
				FInt64Property* Prop = new FInt64Property(PropertyScope, Desc.Name, RF_Public);
				Prop->SetPropertyFlags(CPF_HasGetValueTypeHash);
				return Prop;
			}
		case EPropertyBagPropertyType::Float:
			{
				FFloatProperty* Prop = new FFloatProperty(PropertyScope, Desc.Name, RF_Public);
				Prop->SetPropertyFlags(CPF_HasGetValueTypeHash);
				return Prop;
			}
		case EPropertyBagPropertyType::Double:
			{
				FDoubleProperty* Prop = new FDoubleProperty(PropertyScope, Desc.Name, RF_Public);
				Prop->SetPropertyFlags(CPF_HasGetValueTypeHash);
				return Prop;
			}
		case EPropertyBagPropertyType::Name:
			{
				FNameProperty* Prop = new FNameProperty(PropertyScope, Desc.Name, RF_Public);
				Prop->SetPropertyFlags(CPF_HasGetValueTypeHash);
				return Prop;
			}
		case EPropertyBagPropertyType::String:
			{
				FStrProperty* Prop = new FStrProperty(PropertyScope, Desc.Name, RF_Public);
				Prop->SetPropertyFlags(CPF_HasGetValueTypeHash);
				return Prop;
			}
		case EPropertyBagPropertyType::Text:
			{
				FTextProperty* Prop = new FTextProperty(PropertyScope, Desc.Name, RF_Public);
				return Prop;
			}
		case EPropertyBagPropertyType::Enum:
			if (UEnum* Enum = Cast<UEnum>(Desc.ValueTypeObject))
			{
				FEnumProperty* Prop = new FEnumProperty(PropertyScope, Desc.Name, RF_Public);
				FNumericProperty* UnderlyingProp = new FByteProperty(Prop, "UnderlyingType", RF_Public); // HACK: Hardwire to byte property for now for BP compatibility
				Prop->SetEnum(Enum);
				Prop->AddCppProperty(UnderlyingProp);
				return Prop;
			}
			break;
		case EPropertyBagPropertyType::Struct:
			if (UScriptStruct* ScriptStruct = Cast<UScriptStruct>(Desc.ValueTypeObject))
			{
				FStructProperty* Prop = new FStructProperty(PropertyScope, Desc.Name, RF_Public);
				Prop->Struct = ScriptStruct;

				if (ScriptStruct->GetCppStructOps() && ScriptStruct->GetCppStructOps()->HasGetTypeHash())
				{
					Prop->SetPropertyFlags(CPF_HasGetValueTypeHash);
				}
				
				if (ScriptStruct->StructFlags & STRUCT_HasInstancedReference)
				{
					Prop->SetPropertyFlags(CPF_ContainsInstancedReference);
				}
				
				return Prop;
			}
			break;
		case EPropertyBagPropertyType::Object:
			if (UClass* Class = Cast<UClass>(Desc.ValueTypeObject))
			{
				FObjectProperty* Prop = new FObjectProperty(PropertyScope, Desc.Name, RF_Public);
				if (Class->HasAnyClassFlags(CLASS_DefaultToInstanced))
				{
					Prop->SetPropertyFlags(CPF_InstancedReference);
				}
				Prop->SetPropertyClass(Class);
				Prop->SetPropertyFlags(CPF_HasGetValueTypeHash);
				return Prop;
			}
			break;
		case EPropertyBagPropertyType::SoftObject:
			if (UClass* Class = Cast<UClass>(Desc.ValueTypeObject))
			{
				FSoftObjectProperty* Prop = new FSoftObjectProperty(PropertyScope, Desc.Name, RF_Public);
				if (Class->HasAnyClassFlags(CLASS_DefaultToInstanced))
				{
					Prop->SetPropertyFlags(CPF_InstancedReference);
				}
				Prop->SetPropertyClass(Class);
				Prop->SetPropertyFlags(CPF_HasGetValueTypeHash);
				return Prop;
			}
			break;
		case EPropertyBagPropertyType::Class:
			if (UClass* Class = Cast<UClass>(Desc.ValueTypeObject))
			{
				FClassProperty* Prop = new FClassProperty(PropertyScope, Desc.Name, RF_Public);
				Prop->SetMetaClass(Class);
				Prop->PropertyClass = UClass::StaticClass();
				Prop->SetPropertyFlags(CPF_HasGetValueTypeHash);
				return Prop;
			}
			break;
		case EPropertyBagPropertyType::SoftClass:
			if (UClass* Class = Cast<UClass>(Desc.ValueTypeObject))
			{
				FSoftClassProperty* Prop = new FSoftClassProperty(PropertyScope, Desc.Name, RF_Public);
				Prop->SetMetaClass(Class);
				Prop->PropertyClass = UClass::StaticClass();
				Prop->SetPropertyFlags(CPF_HasGetValueTypeHash);
				return Prop;
			}
			break;
		default:
			ensureMsgf(false, TEXT("Unhandled stype %s"), *UEnum::GetValueAsString(Desc.ValueType));
		}

		return nullptr;
	}

	EPropertyBagResult GetPropertyAsDouble(const FPropertyBagPropertyDesc& Desc, const void* Address, double& OutValue)
	{
		check(Desc.CachedProperty);
		check(Address);
		
		switch(Desc.ValueType)
		{
		case EPropertyBagPropertyType::Bool:
			{
				const FBoolProperty* Property = CastFieldChecked<FBoolProperty>(Desc.CachedProperty);
				OutValue = Property->GetPropertyValue(Address) ? 1.0 : 0.0;
				return EPropertyBagResult::Success;
			}
		case EPropertyBagPropertyType::Byte:
			{
				const FByteProperty* Property = CastFieldChecked<FByteProperty>(Desc.CachedProperty);
				OutValue = Property->GetPropertyValue(Address);
				return EPropertyBagResult::Success;
			}
		case EPropertyBagPropertyType::Int32:
			{
				const FIntProperty* Property = CastFieldChecked<FIntProperty>(Desc.CachedProperty);
				OutValue = Property->GetPropertyValue(Address);
				return EPropertyBagResult::Success;
			}
		case EPropertyBagPropertyType::Int64:
			{
				const FInt64Property* Property = CastFieldChecked<FInt64Property>(Desc.CachedProperty);
				OutValue = Property->GetPropertyValue(Address);
				return EPropertyBagResult::Success;
			}
		case EPropertyBagPropertyType::Float:
			{
				const FFloatProperty* Property = CastFieldChecked<FFloatProperty>(Desc.CachedProperty);
				OutValue = Property->GetPropertyValue(Address);
				return EPropertyBagResult::Success;
			}
		case EPropertyBagPropertyType::Double:
			{
				const FDoubleProperty* Property = CastFieldChecked<FDoubleProperty>(Desc.CachedProperty);
				OutValue = Property->GetPropertyValue(Address);
				return EPropertyBagResult::Success;
			}
		case EPropertyBagPropertyType::Enum:
			{
				const FEnumProperty* EnumProperty = CastFieldChecked<FEnumProperty>(Desc.CachedProperty);
				const FNumericProperty* UnderlyingProperty = EnumProperty->GetUnderlyingProperty();
				check(UnderlyingProperty);
				OutValue = UnderlyingProperty->GetSignedIntPropertyValue(Address);
				return EPropertyBagResult::Success;
			}
		default:
			return EPropertyBagResult::TypeMismatch;
		}
	}

	EPropertyBagResult SetPropertyFromDouble(const FPropertyBagPropertyDesc& Desc, void* Address, const double InValue)
	{
		check(Desc.CachedProperty);
		check(Address);
		
		switch(Desc.ValueType)
		{
		case EPropertyBagPropertyType::Bool:
			{
				const FBoolProperty* Property = CastFieldChecked<FBoolProperty>(Desc.CachedProperty);
				Property->SetPropertyValue(Address, FMath::IsNearlyZero(InValue) ? false : true);
				return EPropertyBagResult::Success;
			}
		case EPropertyBagPropertyType::Byte:
			{
				const FByteProperty* Property = CastFieldChecked<FByteProperty>(Desc.CachedProperty);
				Property->SetPropertyValue(Address, FMath::RoundToInt32(InValue));
				return EPropertyBagResult::Success;
			}
		case EPropertyBagPropertyType::Int32:
			{
				const FIntProperty* Property = CastFieldChecked<FIntProperty>(Desc.CachedProperty);
				Property->SetPropertyValue(Address, FMath::RoundToInt32(InValue));
				return EPropertyBagResult::Success;
			}
		case EPropertyBagPropertyType::Int64:
			{
				const FInt64Property* Property = CastFieldChecked<FInt64Property>(Desc.CachedProperty);
				Property->SetPropertyValue(Address, FMath::RoundToInt64(InValue));
				return EPropertyBagResult::Success;
			}
		case EPropertyBagPropertyType::Float:
			{
				const FFloatProperty* Property = CastFieldChecked<FFloatProperty>(Desc.CachedProperty);
				Property->SetPropertyValue(Address, (float)InValue);
				return EPropertyBagResult::Success;
			}
		case EPropertyBagPropertyType::Double:
			{
				const FDoubleProperty* Property = CastFieldChecked<FDoubleProperty>(Desc.CachedProperty);
				Property->SetPropertyValue(Address, InValue);
				return EPropertyBagResult::Success;
			}
		case EPropertyBagPropertyType::Enum:
			{
				const FEnumProperty* EnumProperty = CastFieldChecked<FEnumProperty>(Desc.CachedProperty);
				const FNumericProperty* UnderlyingProperty = EnumProperty->GetUnderlyingProperty();
				check(UnderlyingProperty);
				UnderlyingProperty->SetIntPropertyValue(Address, (uint64)InValue);
				return EPropertyBagResult::Success;
			}
		default:
			return EPropertyBagResult::TypeMismatch;
		}
	}

	EPropertyBagResult GetPropertyAsInt64(const FPropertyBagPropertyDesc& Desc, const void* Address, int64& OutValue)
	{
		check(Desc.CachedProperty);
		check(Address);
		
		switch(Desc.ValueType)
		{
		case EPropertyBagPropertyType::Bool:
			{
				const FBoolProperty* Property = CastFieldChecked<FBoolProperty>(Desc.CachedProperty);
				OutValue = Property->GetPropertyValue(Address) ? 1 : 0;
				return EPropertyBagResult::Success;
			}
		case EPropertyBagPropertyType::Byte:
			{
				const FByteProperty* Property = CastFieldChecked<FByteProperty>(Desc.CachedProperty);
				OutValue = Property->GetPropertyValue(Address);
				return EPropertyBagResult::Success;
			}
		case EPropertyBagPropertyType::Int32:
			{
				const FIntProperty* Property = CastFieldChecked<FIntProperty>(Desc.CachedProperty);
				OutValue = Property->GetPropertyValue(Address);
				return EPropertyBagResult::Success;
			}
		case EPropertyBagPropertyType::Int64:
			{
				const FInt64Property* Property = CastFieldChecked<FInt64Property>(Desc.CachedProperty);
				OutValue = Property->GetPropertyValue(Address);
				return EPropertyBagResult::Success;
			}
		case EPropertyBagPropertyType::Float:
			{
				const FFloatProperty* Property = CastFieldChecked<FFloatProperty>(Desc.CachedProperty);
				OutValue = (int64)Property->GetPropertyValue(Address);
				return EPropertyBagResult::Success;
			}
		case EPropertyBagPropertyType::Double:
			{
				const FDoubleProperty* Property = CastFieldChecked<FDoubleProperty>(Desc.CachedProperty);
				OutValue = (int64)Property->GetPropertyValue(Address);
				return EPropertyBagResult::Success;
			}
		case EPropertyBagPropertyType::Enum:
			{
				const FEnumProperty* EnumProperty = CastFieldChecked<FEnumProperty>(Desc.CachedProperty);
				const FNumericProperty* UnderlyingProperty = EnumProperty->GetUnderlyingProperty();
				check(UnderlyingProperty);
				OutValue = UnderlyingProperty->GetSignedIntPropertyValue(Address);
				return EPropertyBagResult::Success;
			}
		default:
			return EPropertyBagResult::TypeMismatch;
		}
	}

	EPropertyBagResult SetPropertyFromInt64(const FPropertyBagPropertyDesc& Desc, void* Address, const int64 InValue)
	{
		check(Desc.CachedProperty);
		check(Address);
		
		switch(Desc.ValueType)
		{
		case EPropertyBagPropertyType::Bool:
			{
				const FBoolProperty* Property = CastFieldChecked<FBoolProperty>(Desc.CachedProperty);
				Property->SetPropertyValue(Address, InValue != 0);
				return EPropertyBagResult::Success;
			}
		case EPropertyBagPropertyType::Byte:
			{
				const FByteProperty* Property = CastFieldChecked<FByteProperty>(Desc.CachedProperty);
				Property->SetPropertyValue(Address, (uint8)InValue);
				return EPropertyBagResult::Success;
			}
		case EPropertyBagPropertyType::Int32:
			{
				const FIntProperty* Property = CastFieldChecked<FIntProperty>(Desc.CachedProperty);
				Property->SetPropertyValue(Address, (int32)InValue);
				return EPropertyBagResult::Success;
			}
		case EPropertyBagPropertyType::Int64:
			{
				const FInt64Property* Property = CastFieldChecked<FInt64Property>(Desc.CachedProperty);
				Property->SetPropertyValue(Address, InValue);
				return EPropertyBagResult::Success;
			}
		case EPropertyBagPropertyType::Float:
			{
				const FFloatProperty* Property = CastFieldChecked<FFloatProperty>(Desc.CachedProperty);
				Property->SetPropertyValue(Address, (float)InValue);
				return EPropertyBagResult::Success;
			}
		case EPropertyBagPropertyType::Double:
			{
				const FDoubleProperty* Property = CastFieldChecked<FDoubleProperty>(Desc.CachedProperty);
				Property->SetPropertyValue(Address, (double)InValue);
				return EPropertyBagResult::Success;
			}
		case EPropertyBagPropertyType::Enum:
			{
				const FEnumProperty* EnumProperty = CastFieldChecked<FEnumProperty>(Desc.CachedProperty);
				const FNumericProperty* UnderlyingProperty = EnumProperty->GetUnderlyingProperty();
				check(UnderlyingProperty);
				UnderlyingProperty->SetIntPropertyValue(Address, (uint64)InValue);
				return EPropertyBagResult::Success;
			}
		default:
			return EPropertyBagResult::TypeMismatch;
		}
	}

	void CopyMatchingValuesByID(const FConstStructView Source, const FStructView Target)
	{
		if (!Source.IsValid() || !Target.IsValid())
		{
			return;
		}

		const UPropertyBag* SourceBagStruct = Cast<const UPropertyBag>(Source.GetScriptStruct());
		const UPropertyBag* TargetBagStruct = Cast<const UPropertyBag>(Target.GetScriptStruct());

		if (!SourceBagStruct || !TargetBagStruct)
		{
			return;
		}

		// Iterate over source and copy to target if possible. Source is expected to usually have less items.
		for (const FPropertyBagPropertyDesc& SourceDesc : SourceBagStruct->GetPropertyDescs())
		{
			const FPropertyBagPropertyDesc* PotentialTargetDesc = TargetBagStruct->FindPropertyDescByID(SourceDesc.ID);
			if (PotentialTargetDesc == nullptr
				|| PotentialTargetDesc->CachedProperty == nullptr
				|| SourceDesc.CachedProperty == nullptr)
			{
				continue;
			}

			const FPropertyBagPropertyDesc& TargetDesc = *PotentialTargetDesc;
			void* TargetAddress = Target.GetMutableMemory() + TargetDesc.CachedProperty->GetOffset_ForInternal();
			const void* SourceAddress = Source.GetMemory() + SourceDesc.CachedProperty->GetOffset_ForInternal();
			
			if (TargetDesc.CompatibleType(SourceDesc))
			{
				TargetDesc.CachedProperty->CopyCompleteValue(TargetAddress, SourceAddress);
			}
			else if (TargetDesc.IsNumericType() && SourceDesc.IsNumericType())
			{
				// Try to convert numeric types.
				if (TargetDesc.IsNumericFloatType())
				{
					double Value = 0;
					if (GetPropertyAsDouble(SourceDesc, SourceAddress, Value) == EPropertyBagResult::Success)
					{
						SetPropertyFromDouble(TargetDesc, TargetAddress, Value);
					}
				}
				else
				{
					int64 Value = 0;
					if (GetPropertyAsInt64(SourceDesc, SourceAddress, Value) == EPropertyBagResult::Success)
					{
						SetPropertyFromInt64(TargetDesc, TargetAddress, Value);
					}
				}
			}
			else if ((TargetDesc.IsObjectType() && SourceDesc.IsObjectType())
				|| (TargetDesc.IsClassType() && SourceDesc.IsClassType()))
			{
				// Try convert between compatible objects and classes.
				const UClass* TargetObjectClass = Cast<const UClass>(TargetDesc.ValueTypeObject);
				const UClass* SourceObjectClass = Cast<const UClass>(SourceDesc.ValueTypeObject);
				if (CanCastTo(SourceObjectClass, TargetObjectClass))
				{
					const FObjectPropertyBase* TargetProp = CastFieldChecked<FObjectPropertyBase>(TargetDesc.CachedProperty);
					const FObjectPropertyBase* SourceProp = CastFieldChecked<FObjectPropertyBase>(SourceDesc.CachedProperty);
					TargetProp->SetObjectPropertyValue(TargetAddress, SourceProp->GetObjectPropertyValue(SourceAddress));
				}
			}
		}
	}

	// Helper templates to reduce repeated work when dealing with property access.

	template<typename T>
	TValueOrError<T, EPropertyBagResult> GetValueInt64(const FInstancedPropertyBag& Bag, const FName Name)
	{
		const FPropertyBagPropertyDesc* Desc = Bag.FindPropertyDescByName(Name);
		if (Desc == nullptr || Desc->CachedProperty == nullptr)
		{
			return MakeError(EPropertyBagResult::PropertyNotFound);
		}
	 
		check(Bag.GetValue().IsValid());
		const void* Address = Bag.GetValue().GetMemory() + Desc->CachedProperty->GetOffset_ForInternal();
		int64 IntValue = 0;
		const EPropertyBagResult Result = GetPropertyAsInt64(*Desc, Address, IntValue);
		if (Result != EPropertyBagResult::Success)
		{
			return MakeError(Result);
		}
		return MakeValue(IntValue);
	}

	template<typename T>
	TValueOrError<T, EPropertyBagResult> GetValueDouble(const FInstancedPropertyBag& Bag, const FName Name)
	{
		const FPropertyBagPropertyDesc* Desc = Bag.FindPropertyDescByName(Name);
		if (Desc == nullptr || Desc->CachedProperty == nullptr)
		{
			return MakeError(EPropertyBagResult::PropertyNotFound);
		}

		check(Bag.GetValue().IsValid());
		const void* Address = Bag.GetValue().GetMemory() + Desc->CachedProperty->GetOffset_ForInternal();
		double DblValue = 0;
		const EPropertyBagResult Result = GetPropertyAsDouble(*Desc, Address, DblValue);
		if (Result != EPropertyBagResult::Success)
		{
			return MakeError(Result);
		}
		return MakeValue(DblValue);
	}

	template<typename T, typename PropT>
	TValueOrError<T, EPropertyBagResult> GetValue(const FInstancedPropertyBag& Bag, const FName Name)
	{
		const FPropertyBagPropertyDesc* Desc = Bag.FindPropertyDescByName(Name);
		if (Desc == nullptr || Desc->CachedProperty == nullptr || !Desc->CachedProperty->IsA<PropT>())
		{
			return MakeError(EPropertyBagResult::PropertyNotFound);
		}
		check(Desc->CachedProperty);
		const PropT* Property = CastFieldChecked<PropT>(Desc->CachedProperty);
		check(Bag.GetValue().IsValid());
		const void* Address = Bag.GetValue().GetMemory() + Desc->CachedProperty->GetOffset_ForInternal();
		return MakeValue(Property->GetPropertyValue(Address));
	}


	template<typename T>
	EPropertyBagResult SetValueInt64(const FInstancedPropertyBag& Bag, const FName Name, const T Value)
	{
		const FPropertyBagPropertyDesc* Desc = Bag.FindPropertyDescByName(Name);
		if (Desc == nullptr || Desc->CachedProperty == nullptr)
		{
			return EPropertyBagResult::PropertyNotFound;
		}
		check(Desc->CachedProperty);
		check(Bag.GetValue().IsValid());
		void* Address = Bag.GetMutableValue().GetMutableMemory() + Desc->CachedProperty->GetOffset_ForInternal();
		return SetPropertyFromInt64(*Desc, Address, (int64)Value);
	}

	template<typename T>
	EPropertyBagResult SetValueDouble(const FInstancedPropertyBag& Bag, const FName Name, const T Value)
	{
		const FPropertyBagPropertyDesc* Desc = Bag.FindPropertyDescByName(Name);
		if (Desc == nullptr || Desc->CachedProperty == nullptr)
		{
			return EPropertyBagResult::PropertyNotFound;
		}
		check(Desc->CachedProperty);
		check(Bag.GetValue().IsValid());
		void* Address = Bag.GetMutableValue().GetMutableMemory() + Desc->CachedProperty->GetOffset_ForInternal();
		return SetPropertyFromDouble(*Desc, Address, (double)Value);
	}

	template<typename T, typename PropT>
	EPropertyBagResult SetValue(const FInstancedPropertyBag& Bag, const FName Name, const T& Value)
	{
		const FPropertyBagPropertyDesc* Desc = Bag.FindPropertyDescByName(Name);
		if (Desc == nullptr || Desc->CachedProperty == nullptr || !Desc->CachedProperty->IsA<PropT>())
		{
			return EPropertyBagResult::PropertyNotFound;
		}
		check(Desc->CachedProperty);
		const PropT* Property = CastFieldChecked<PropT>(Desc->CachedProperty);
		check(Bag.GetValue().IsValid());
		void* Address = Bag.GetMutableValue().GetMutableMemory() + Desc->CachedProperty->GetOffset_ForInternal();
		Property->SetPropertyValue(Address, Value);
		return EPropertyBagResult::Success;
	}

	void RemovePropertyByName(TArray<FPropertyBagPropertyDesc>& Descs, const FName PropertyName, const int32 StartIndex = 0)
	{
		// Remove properties which dont have unique name.
		for (int32 Index = StartIndex; Index < Descs.Num(); Index++)
		{
			if (Descs[Index].Name == PropertyName)
			{
				Descs.RemoveAt(Index);
				Index--;
			}
		}
	}


}; // UE::StructUtils::Private


//----------------------------------------------------------------//
//  FPropertyBagPropertyDesc
//----------------------------------------------------------------//

void FPropertyBagPropertyDescMetaData::Serialize(FArchive& Ar)
{
	Ar << Key;
	Ar << Value;
}

FPropertyBagPropertyDesc::FPropertyBagPropertyDesc(const FName InName, const FProperty* InSourceProperty)
	: Name(InName)
{
	ValueType = UE::StructUtils::Private::GetValueTypeFromProperty(InSourceProperty);
	ValueTypeObject = UE::StructUtils::Private::GetValueTypeObjectFromProperty(InSourceProperty);

#if WITH_EDITORONLY_DATA
	if (const TMap<FName, FString>* SourcePropertyMetaData = InSourceProperty->GetMetaDataMap())
	{
		for (const TPair<FName, FString>& MetaDataPair : *SourcePropertyMetaData)
		{
			MetaData.Add({ MetaDataPair.Key, MetaDataPair.Value });
		}
	}
#endif
}

static FArchive& operator<<(FArchive& Ar, FPropertyBagPropertyDesc& Bag)
{
	Ar << Bag.ValueTypeObject;
	Ar << Bag.ID;
	Ar << Bag.Name;
	Ar << Bag.ValueType;

	bool bHasMetaData = false;
#if WITH_EDITORONLY_DATA
	if (Ar.IsSaving())
	{
		bHasMetaData = !Ar.IsCooking() && Bag.MetaData.Num() > 0;
	}
#endif
	Ar << bHasMetaData;

	if (bHasMetaData)
	{
#if WITH_EDITORONLY_DATA
		Ar << Bag.MetaData;
#else
		TArray<FPropertyBagPropertyDescMetaData> TempMetaData; 
		Ar << TempMetaData;
#endif
	}	
	
	return Ar;
}

bool FPropertyBagPropertyDesc::IsNumericType() const
{
	switch (ValueType)
	{
	case EPropertyBagPropertyType::Bool: return true;
	case EPropertyBagPropertyType::Byte: return true;
	case EPropertyBagPropertyType::Int32: return true;
	case EPropertyBagPropertyType::Int64: return true;
	case EPropertyBagPropertyType::Float: return true;
	case EPropertyBagPropertyType::Double: return true;
	default: return false;
	}
}

bool FPropertyBagPropertyDesc::IsNumericFloatType() const
{
	switch (ValueType)
	{
	case EPropertyBagPropertyType::Float: return true;
	case EPropertyBagPropertyType::Double: return true;
	default: return false;
	}
}

bool FPropertyBagPropertyDesc::IsObjectType() const
{
	switch (ValueType)
	{
	case EPropertyBagPropertyType::Object: return true;
	case EPropertyBagPropertyType::SoftObject: return true;
	default: return false;
	}
}

bool FPropertyBagPropertyDesc::IsClassType() const
{
	switch (ValueType)
	{
	case EPropertyBagPropertyType::Class: return true;
	case EPropertyBagPropertyType::SoftClass: return true;
	default: return false;
	}
}

bool FPropertyBagPropertyDesc::CompatibleType(const FPropertyBagPropertyDesc& Other) const
{
	// Values must match.
	if (ValueType != Other.ValueType)
	{
		return false;
	}

	// Struct and enum must have same value type class
	if (ValueType == EPropertyBagPropertyType::Enum || ValueType == EPropertyBagPropertyType::Struct)
	{
		return ValueTypeObject == Other.ValueTypeObject; 
	}

	// Objects should be castable.
	if (ValueType == EPropertyBagPropertyType::Object)
	{
		const UClass* ObjectClass = Cast<const UClass>(ValueTypeObject);
		const UClass* OtherObjectClass = Cast<const UClass>(Other.ValueTypeObject);
		return UE::StructUtils::Private::CanCastTo(OtherObjectClass, ObjectClass);
	}

	return true;
}

//----------------------------------------------------------------//
//  FInstancedPropertyBag
//----------------------------------------------------------------//

void FInstancedPropertyBag::InitializeFromBagStruct(const UPropertyBag* NewBagStruct)
{
	Value.InitializeAs(NewBagStruct);
}

void FInstancedPropertyBag::CopyMatchingValuesByID(const FInstancedPropertyBag& Other) const
{
	UE::StructUtils::Private::CopyMatchingValuesByID(Other.Value, Value);
}

int32 FInstancedPropertyBag::GetNumPropertiesInBag() const
{
	if (const UPropertyBag* BagStruct = GetPropertyBagStruct())
	{
		return BagStruct->PropertyDescs.Num();
	}

	return 0;
}

void FInstancedPropertyBag::AddProperties(const TConstArrayView<FPropertyBagPropertyDesc> NewDescs)
{
	TArray<FPropertyBagPropertyDesc> Descs;
	if (const UPropertyBag* CurrentBagStruct = GetPropertyBagStruct())
	{
		Descs = CurrentBagStruct->GetPropertyDescs();
	}

	for (const FPropertyBagPropertyDesc& NewDesc : NewDescs)
	{
		FPropertyBagPropertyDesc* ExistingProperty = Descs.FindByPredicate([&NewDesc](const FPropertyBagPropertyDesc& Desc) { return Desc.Name == NewDesc.Name; });
		if (ExistingProperty != nullptr)
		{
			ExistingProperty->ValueType = NewDesc.ValueType;
			ExistingProperty->ValueTypeObject = NewDesc.ValueTypeObject;
		}
		else
		{
			Descs.Add(NewDesc);
		}
	}

	const UPropertyBag* NewBagStruct = UPropertyBag::GetOrCreateFromDescs(Descs);
	MigrateToNewBagStruct(NewBagStruct);
}
	
void FInstancedPropertyBag::AddProperty(const FName InName, const EPropertyBagPropertyType InValueType, UObject* InValueTypeObject)
{
	AddProperties({ FPropertyBagPropertyDesc(InName, InValueType, InValueTypeObject) });
}

void FInstancedPropertyBag::AddProperty(const FName InName, const FProperty* InSourceProperty)
{
	AddProperties({ FPropertyBagPropertyDesc(InName, InSourceProperty ) });
}

void FInstancedPropertyBag::RemovePropertiesByName(const TConstArrayView<FName> PropertiesToRemove)
{
	TArray<FPropertyBagPropertyDesc> Descs;
	if (const UPropertyBag* CurrentBagStruct = GetPropertyBagStruct())
	{
		Descs = CurrentBagStruct->GetPropertyDescs();
	}

	for (const FName Name : PropertiesToRemove)
	{
		UE::StructUtils::Private::RemovePropertyByName(Descs, Name);
	}

	const UPropertyBag* NewBagStruct = UPropertyBag::GetOrCreateFromDescs(Descs);
	MigrateToNewBagStruct(NewBagStruct);
}
	
void FInstancedPropertyBag::RemovePropertyByName(const FName PropertyToRemove)
{
	RemovePropertiesByName({ PropertyToRemove });
}

void FInstancedPropertyBag::MigrateToNewBagStruct(const UPropertyBag* NewBagStruct)
{
	FInstancedStruct NewValue(NewBagStruct);

	UE::StructUtils::Private::CopyMatchingValuesByID(Value, NewValue);
	
	Value = MoveTemp(NewValue);
}

void FInstancedPropertyBag::MigrateToNewBagInstance(const FInstancedPropertyBag& NewBagInstance)
{
	FInstancedStruct NewValue(NewBagInstance.Value);

	UE::StructUtils::Private::CopyMatchingValuesByID(Value, NewValue);
	
	Value = MoveTemp(NewValue);
}

const UPropertyBag* FInstancedPropertyBag::GetPropertyBagStruct() const
{
	return Value.IsValid() ? Cast<const UPropertyBag>(Value.GetScriptStruct()) : nullptr;
}

const FPropertyBagPropertyDesc* FInstancedPropertyBag::FindPropertyDescByID(const FGuid ID) const
{
	if (const UPropertyBag* BagStruct = GetPropertyBagStruct())
	{
		return BagStruct->FindPropertyDescByID(ID);
	}
	return nullptr;
}
	
const FPropertyBagPropertyDesc* FInstancedPropertyBag::FindPropertyDescByName(const FName Name) const
{
	if (const UPropertyBag* BagStruct = GetPropertyBagStruct())
	{
		return BagStruct->FindPropertyDescByName(Name);
	}
	return nullptr;
}

TValueOrError<bool, EPropertyBagResult> FInstancedPropertyBag::GetValueBool(const FName Name) const
{
	TValueOrError<int64, EPropertyBagResult> Result = UE::StructUtils::Private::GetValueInt64<int64>(*this, Name);
	if (!Result.IsValid())
	{
		return MakeError(Result.GetError());
	}
	return MakeValue(Result.GetValue() != 0);
}

TValueOrError<uint8, EPropertyBagResult> FInstancedPropertyBag::GetValueByte(const FName Name) const
{
	return UE::StructUtils::Private::GetValueInt64<uint8>(*this, Name);
}

TValueOrError<int32, EPropertyBagResult> FInstancedPropertyBag::GetValueInt32(const FName Name) const
{
	return UE::StructUtils::Private::GetValueInt64<int32>(*this, Name);
}

TValueOrError<int64, EPropertyBagResult> FInstancedPropertyBag::GetValueInt64(const FName Name) const
{
	return UE::StructUtils::Private::GetValueInt64<int64>(*this, Name);
}

TValueOrError<float, EPropertyBagResult> FInstancedPropertyBag::GetValueFloat(const FName Name) const
{
	return UE::StructUtils::Private::GetValueDouble<float>(*this, Name);
}

TValueOrError<double, EPropertyBagResult> FInstancedPropertyBag::GetValueDouble(const FName Name) const
{
	return UE::StructUtils::Private::GetValueDouble<double>(*this, Name);
}

TValueOrError<FName, EPropertyBagResult> FInstancedPropertyBag::GetValueName(const FName Name) const
{
	return UE::StructUtils::Private::GetValue<FName, FNameProperty>(*this, Name);
}

TValueOrError<FString, EPropertyBagResult> FInstancedPropertyBag::GetValueString(const FName Name) const
{
	return UE::StructUtils::Private::GetValue<FString, FStrProperty>(*this, Name);
}

TValueOrError<FText, EPropertyBagResult> FInstancedPropertyBag::GetValueText(const FName Name) const
{
	return UE::StructUtils::Private::GetValue<FText, FTextProperty>(*this, Name);
}

TValueOrError<uint8, EPropertyBagResult> FInstancedPropertyBag::GetValueEnum(const FName Name, const UEnum* RequestedEnum) const
{
	const FPropertyBagPropertyDesc* Desc = FindPropertyDescByName(Name);
	if (Desc == nullptr || Desc->ValueType != EPropertyBagPropertyType::Enum)
	{
		return MakeError(Desc == nullptr ? EPropertyBagResult::PropertyNotFound : EPropertyBagResult::TypeMismatch);
	}
	check(Desc->CachedProperty);
	const FEnumProperty* EnumProperty = CastFieldChecked<FEnumProperty>(Desc->CachedProperty);
	const FNumericProperty* UnderlyingProperty = EnumProperty->GetUnderlyingProperty();
	check(UnderlyingProperty);
	
	if (RequestedEnum != EnumProperty->GetEnum())
	{
		return MakeError(EPropertyBagResult::TypeMismatch);
	}
	
	check(Value.IsValid());
	const void* Address = Value.GetMutableMemory() + Desc->CachedProperty->GetOffset_ForInternal();
	return MakeValue((uint8)UnderlyingProperty->GetUnsignedIntPropertyValue(Address));
}

TValueOrError<FStructView, EPropertyBagResult> FInstancedPropertyBag::GetValueStruct(const FName Name, const UScriptStruct* RequestedStruct) const
{
	const FPropertyBagPropertyDesc* Desc = FindPropertyDescByName(Name);
	if (Desc == nullptr || Desc->ValueType != EPropertyBagPropertyType::Struct)
	{
		return MakeError(Desc == nullptr ? EPropertyBagResult::PropertyNotFound : EPropertyBagResult::TypeMismatch);
	}
	check(Desc->CachedProperty);
	const FStructProperty* StructProperty = CastFieldChecked<FStructProperty>(Desc->CachedProperty);
	check(StructProperty->Struct);

	if (RequestedStruct != nullptr && UE::StructUtils::Private::CanCastTo(StructProperty->Struct, RequestedStruct) == false)
	{
		return MakeError(EPropertyBagResult::TypeMismatch);
	}
	
	check(Value.IsValid());
	void* Address = Value.GetMutableMemory() + Desc->CachedProperty->GetOffset_ForInternal();
	return MakeValue(FStructView(StructProperty->Struct, (uint8*)Address));
}

TValueOrError<UObject*, EPropertyBagResult> FInstancedPropertyBag::GetValueObject(const FName Name, const UClass* RequestedClass) const
{
	const FPropertyBagPropertyDesc* Desc = FindPropertyDescByName(Name);
	if (Desc == nullptr || (Desc->ValueType != EPropertyBagPropertyType::Object && Desc->ValueType != EPropertyBagPropertyType::SoftObject))
	{
		return MakeError(Desc == nullptr ? EPropertyBagResult::PropertyNotFound : EPropertyBagResult::TypeMismatch);
	}
	check(Desc->CachedProperty);
	const FObjectPropertyBase* ObjectProperty = CastFieldChecked<FObjectPropertyBase>(Desc->CachedProperty);
	check(ObjectProperty->PropertyClass);

	if (RequestedClass != nullptr && UE::StructUtils::Private::CanCastTo(ObjectProperty->PropertyClass, RequestedClass) == false)
	{
		return MakeError(EPropertyBagResult::TypeMismatch);
	}
	
	check(Value.IsValid());
	const void* Address = Value.GetMemory() + Desc->CachedProperty->GetOffset_ForInternal();
	return MakeValue(ObjectProperty->GetObjectPropertyValue(Address));
}

TValueOrError<UClass*, EPropertyBagResult> FInstancedPropertyBag::GetValueClass(const FName Name) const
{
	const FPropertyBagPropertyDesc* Desc = FindPropertyDescByName(Name);
	if (Desc == nullptr || (Desc->ValueType != EPropertyBagPropertyType::Class && Desc->ValueType != EPropertyBagPropertyType::SoftClass))
	{
		return MakeError(Desc == nullptr ? EPropertyBagResult::PropertyNotFound : EPropertyBagResult::TypeMismatch);
	}
	check(Desc->CachedProperty);
	const FObjectPropertyBase* ObjectProperty = CastFieldChecked<FObjectPropertyBase>(Desc->CachedProperty);
	check(Value.IsValid());
	const void* Address = Value.GetMemory() + Desc->CachedProperty->GetOffset_ForInternal();
	return MakeValue(CastChecked<UClass>(ObjectProperty->GetObjectPropertyValue(Address)));
}


EPropertyBagResult FInstancedPropertyBag::SetValueBool(const FName Name, const bool bInValue) const
{
	return UE::StructUtils::Private::SetValueInt64<int64>(*this, Name, bInValue ? 1 : 0);
}

EPropertyBagResult FInstancedPropertyBag::SetValueByte(const FName Name, const uint8 InValue) const
{
	return UE::StructUtils::Private::SetValueInt64<uint8>(*this, Name, InValue);
}

EPropertyBagResult FInstancedPropertyBag::SetValueInt32(const FName Name, const int32 InValue) const
{
	return UE::StructUtils::Private::SetValueInt64<int32>(*this, Name, InValue);
}

EPropertyBagResult FInstancedPropertyBag::SetValueInt64(const FName Name, const int64 InValue) const
{
	return UE::StructUtils::Private::SetValueInt64<int64>(*this, Name, InValue);
}

EPropertyBagResult FInstancedPropertyBag::SetValueFloat(const FName Name, const float InValue) const
{
	return UE::StructUtils::Private::SetValueDouble<float>(*this, Name, InValue);
}

EPropertyBagResult FInstancedPropertyBag::SetValueDouble(const FName Name, const double InValue) const
{
	return UE::StructUtils::Private::SetValueDouble<double>(*this, Name, InValue);
}

EPropertyBagResult FInstancedPropertyBag::SetValueName(const FName Name, const FName InValue) const
{
	return UE::StructUtils::Private::SetValue<FName, FNameProperty>(*this, Name, InValue);
}

EPropertyBagResult FInstancedPropertyBag::SetValueString(const FName Name, const FString& InValue) const
{
	return UE::StructUtils::Private::SetValue<FString, FStrProperty>(*this, Name, InValue);
}

EPropertyBagResult FInstancedPropertyBag::SetValueText(const FName Name, const FText& InValue) const
{
	return UE::StructUtils::Private::SetValue<FText, FTextProperty>(*this, Name, InValue);
}

EPropertyBagResult FInstancedPropertyBag::SetValueEnum(const FName Name, const uint8 InValue, const UEnum* Enum) const
{
	const FPropertyBagPropertyDesc* Desc = FindPropertyDescByName(Name);
	if (Desc == nullptr || Desc->ValueType != EPropertyBagPropertyType::Enum)
	{
		return Desc == nullptr ? EPropertyBagResult::PropertyNotFound : EPropertyBagResult::TypeMismatch;
	}
	check(Desc->CachedProperty);
	const FEnumProperty* EnumProperty = CastFieldChecked<FEnumProperty>(Desc->CachedProperty);
	const FNumericProperty* UnderlyingProperty = EnumProperty->GetUnderlyingProperty();
	check(UnderlyingProperty);

	if (Enum != EnumProperty->GetEnum())
	{
		return EPropertyBagResult::TypeMismatch;
	}
	
	check(Value.IsValid());
	void* Address = Value.GetMutableMemory() + Desc->CachedProperty->GetOffset_ForInternal();
	UnderlyingProperty->SetIntPropertyValue(Address, (uint64)InValue);
	
	return EPropertyBagResult::Success;
}

EPropertyBagResult FInstancedPropertyBag::SetValueStruct(const FName Name, FConstStructView InValue) const
{
	const FPropertyBagPropertyDesc* Desc = FindPropertyDescByName(Name);
	if (Desc == nullptr || Desc->ValueType != EPropertyBagPropertyType::Struct)
	{
		return Desc == nullptr ? EPropertyBagResult::PropertyNotFound : EPropertyBagResult::TypeMismatch;
	}
	check(Desc->CachedProperty);
	const FStructProperty* StructProperty = CastFieldChecked<FStructProperty>(Desc->CachedProperty);
	check(StructProperty->Struct);

	if (InValue.GetScriptStruct() && InValue.GetScriptStruct() != StructProperty->Struct)
	{
		return EPropertyBagResult::TypeMismatch;
	}

	check(Value.IsValid());
	void* Address = Value.GetMutableMemory() + Desc->CachedProperty->GetOffset_ForInternal();

	if (InValue.IsValid())
	{
		StructProperty->Struct->CopyScriptStruct(Address, InValue.GetMemory());
	}
	else
	{
		StructProperty->Struct->ClearScriptStruct(Address);
	}

	return EPropertyBagResult::Success;
}

EPropertyBagResult FInstancedPropertyBag::SetValueObject(const FName Name, UObject* InValue) const
{
	const FPropertyBagPropertyDesc* Desc = FindPropertyDescByName(Name);
	if (Desc == nullptr || (Desc->ValueType != EPropertyBagPropertyType::Object && Desc->ValueType != EPropertyBagPropertyType::SoftObject))
	{
		return Desc == nullptr ? EPropertyBagResult::PropertyNotFound : EPropertyBagResult::TypeMismatch;
	}
	check(Desc->CachedProperty);
	const FObjectPropertyBase* ObjectProperty = CastFieldChecked<FObjectPropertyBase>(Desc->CachedProperty);
	check(ObjectProperty->PropertyClass);

	if (InValue && UE::StructUtils::Private::CanCastTo(InValue->GetClass(), ObjectProperty->PropertyClass) == false)
	{
		return EPropertyBagResult::TypeMismatch;
	}
	
	check(Value.IsValid());
	void* Address = Value.GetMutableMemory() + Desc->CachedProperty->GetOffset_ForInternal();
	ObjectProperty->SetObjectPropertyValue(Address, InValue);

	return EPropertyBagResult::Success;
}

EPropertyBagResult FInstancedPropertyBag::SetValueClass(const FName Name, UClass* InValue) const
{
	const FPropertyBagPropertyDesc* Desc = FindPropertyDescByName(Name);
	if (Desc == nullptr || (Desc->ValueType != EPropertyBagPropertyType::Class && Desc->ValueType != EPropertyBagPropertyType::SoftClass))
	{
		return Desc == nullptr ? EPropertyBagResult::PropertyNotFound : EPropertyBagResult::TypeMismatch;
	}
	check(Desc->CachedProperty);
	
	check(Value.IsValid());
	void* Address = Value.GetMutableMemory() + Desc->CachedProperty->GetOffset_ForInternal();

	if (Desc->ValueType == EPropertyBagPropertyType::Class)
	{
		const FClassProperty* ClassProperty = CastFieldChecked<FClassProperty>(Desc->CachedProperty);
		if (InValue && InValue->IsChildOf(ClassProperty->MetaClass) == false)
		{
			return EPropertyBagResult::TypeMismatch;
		}
		ClassProperty->SetObjectPropertyValue(Address, InValue);
	}
	else
	{
		const FSoftClassProperty* ClassProperty = CastFieldChecked<FSoftClassProperty>(Desc->CachedProperty);
		if (InValue && InValue->IsChildOf(ClassProperty->MetaClass) == false)
		{
			return EPropertyBagResult::TypeMismatch;
		}
		ClassProperty->SetObjectPropertyValue(Address, InValue);
	}

	return EPropertyBagResult::Success;
}

EPropertyBagResult FInstancedPropertyBag::SetValue(const FName Name, const FProperty* InSourceProperty, const void* InSourceContainerAddress) const
{
	const FPropertyBagPropertyDesc* Desc = FindPropertyDescByName(Name);
	if (Desc == nullptr || InSourceProperty == nullptr || InSourceContainerAddress == nullptr)
	{
		return EPropertyBagResult::PropertyNotFound;
	}
	check(Desc->CachedProperty);

	void* TargetAddress = Value.GetMutableMemory() + Desc->CachedProperty->GetOffset_ForInternal();
	void const* SourceAddress = InSourceProperty->ContainerPtrToValuePtr<void>(InSourceContainerAddress);

	if (InSourceProperty->GetClass() == Desc->CachedProperty->GetClass())
	{
		Desc->CachedProperty->CopyCompleteValue(TargetAddress, SourceAddress);
	}
	else
	{
		return EPropertyBagResult::TypeMismatch;
	}

	return EPropertyBagResult::Success;
}

bool FInstancedPropertyBag::Serialize(FArchive& Ar)
{
	enum class EVersion : uint8
	{
		InitialVersion = 0,
		SerializeStructSize,
		// -----<new versions can be added above this line>-----
		VersionPlusOne,
		LatestVersion = VersionPlusOne - 1
	};

	EVersion Version = EVersion::LatestVersion;

	Ar << Version;

	UPropertyBag* BagStruct = Cast<UPropertyBag>(const_cast<UScriptStruct*>(Value.GetScriptStruct()));
	bool bHasData = (BagStruct != nullptr);
	
	Ar << bHasData;
	
	if (bHasData)
	{
		// The script struct class is not serialized, the properties are serialized and type is created based on that.
		if (Ar.IsLoading())
		{
			TArray<FPropertyBagPropertyDesc> PropertyDescs;
			Ar << PropertyDescs;

			BagStruct = const_cast<UPropertyBag*>(UPropertyBag::GetOrCreateFromDescs(PropertyDescs));
			Value.InitializeAs(BagStruct);

			// Size of the serialized memory
			int32 SerialSize = 0; 
			if (Version >= EVersion::SerializeStructSize)
			{
				Ar << SerialSize;
			}
			
			// BagStruct can be null if it contains structs, classes or enums that could not be found.
			if (BagStruct != nullptr && Value.GetMutableMemory() != nullptr)
			{
				BagStruct->SerializeItem(Ar, Value.GetMutableMemory(), /*Defaults*/nullptr);
			}
			else
			{
				UE_LOG(LogCore, Warning, TEXT("Unable to create serialized UPropertyBag -> Advance %u bytes in the archive and reset to empty FInstancedPropertyBag"), SerialSize);
				Ar.Seek(Ar.Tell() + SerialSize);
			}
		}
		else if (Ar.IsSaving())
		{
			check(BagStruct);
			Ar << BagStruct->PropertyDescs;

			const int64 SizeOffset = Ar.Tell(); // Position to write the actual size after struct serialization
			int32 SerialSize = 0;
			// Size of the serialized memory (reserve location)
			Ar << SerialSize;

			const int64 InitialOffset = Ar.Tell(); // Position before struct serialization to compute its serial size

			check(Value.GetMutableMemory() != nullptr);
			BagStruct->SerializeItem(Ar, Value.GetMutableMemory(), /*Defaults*/nullptr);
		
			const int64 FinalOffset = Ar.Tell(); // Keep current offset to reset the archive pos after write the serial size

			// Size of the serialized memory
			Ar.Seek(SizeOffset);	// Go back in the archive to write the actual size
			SerialSize = (int32)(FinalOffset - InitialOffset);
			Ar << SerialSize;
			Ar.Seek(FinalOffset);	// Reset archive to its position
		}
	}
	
	return true;
}

//----------------------------------------------------------------//
//  UPropertyBag
//----------------------------------------------------------------//

const UPropertyBag* UPropertyBag::GetOrCreateFromDescs(const TConstArrayView<FPropertyBagPropertyDesc> PropertyDescs)
{
	const uint64 BagHash = UE::StructUtils::Private::CalcPropertyDescArrayHash(PropertyDescs);
	const FString ScriptStructName = FString::Printf(TEXT("PropertyBag_%llx"), BagHash);

	if (const UPropertyBag* ExistingBag = FindObject<UPropertyBag>(GetTransientPackage(), *ScriptStructName))
	{
		return ExistingBag;
	}

	UPropertyBag* NewBag = NewObject<UPropertyBag>(GetTransientPackage(), *ScriptStructName, RF_Standalone | RF_Transient);

	NewBag->PropertyDescs = PropertyDescs;

	// Fix missing structs, enums, and objects.
	for (FPropertyBagPropertyDesc& Desc : NewBag->PropertyDescs)
	{
		if (Desc.ValueType == EPropertyBagPropertyType::Struct)
		{
			if (Desc.ValueTypeObject == nullptr || Desc.ValueTypeObject->GetClass()->IsChildOf(UScriptStruct::StaticClass()) == false)
			{
				UE_LOG(LogCore, Warning, TEXT("PropertyBag: Struct property '%s' is missing type."), *Desc.Name.ToString());
				Desc.ValueTypeObject = FPropertyBagMissingStruct::StaticStruct();
			}
		}
		else if (Desc.ValueType == EPropertyBagPropertyType::Enum)
		{
			if (Desc.ValueTypeObject == nullptr || Desc.ValueTypeObject->GetClass()->IsChildOf(UEnum::StaticClass()) == false)
			{
				UE_LOG(LogCore, Warning, TEXT("PropertyBag: Enum property '%s' is missing type."), *Desc.Name.ToString());
				Desc.ValueTypeObject = StaticEnum<EPropertyBagMissingEnum>();
			}
		}
		else if (Desc.ValueType == EPropertyBagPropertyType::Object || Desc.ValueType == EPropertyBagPropertyType::SoftObject)
		{
			if (Desc.ValueTypeObject == nullptr)
			{
				UE_LOG(LogCore, Warning, TEXT("PropertyBag: Object property '%s' is missing type."), *Desc.Name.ToString());
				Desc.ValueTypeObject = UPropertyBagMissingObject::StaticClass();
			}
		}
		else if (Desc.ValueType == EPropertyBagPropertyType::Class || Desc.ValueType == EPropertyBagPropertyType::SoftClass)
		{
			if (Desc.ValueTypeObject == nullptr || Desc.ValueTypeObject->GetClass()->IsChildOf(UClass::StaticClass()) == false)
			{
				UE_LOG(LogCore, Warning, TEXT("PropertyBag: Class property '%s' is missing type."), *Desc.Name.ToString());
				Desc.ValueTypeObject = UPropertyBagMissingObject::StaticClass();
			}
		}
	}
	
	// Remove properties with same name
	for (int32 Index = 0; Index < NewBag->PropertyDescs.Num() - 1; Index++)
	{
		UE::StructUtils::Private::RemovePropertyByName(NewBag->PropertyDescs, NewBag->PropertyDescs[Index].Name, Index + 1);
	}
	
	// Add properties (AddCppProperty() adds them backwards in the linked list)
	for (int32 DescIndex = NewBag->PropertyDescs.Num() - 1; DescIndex >= 0; DescIndex--)
	{
		FPropertyBagPropertyDesc& Desc = NewBag->PropertyDescs[DescIndex];

		if (!Desc.ID.IsValid())
		{
			Desc.ID = FGuid::NewGuid();
		}


		if (FProperty* NewProperty = UE::StructUtils::Private::CreatePropertyFromDesc(Desc, NewBag))
		{
#if WITH_EDITORONLY_DATA
			// Add metadata
			for (const FPropertyBagPropertyDescMetaData& PropertyDescMetaData : Desc.MetaData)
			{
				NewProperty->SetMetaData(*PropertyDescMetaData.Key.ToString(), *PropertyDescMetaData.Value);
			}
#endif
			
			NewProperty->SetPropertyFlags(CPF_Edit);
			NewBag->AddCppProperty(NewProperty);
			Desc.CachedProperty = NewProperty; 
		}
	}

	NewBag->Bind();
	NewBag->StaticLink(/*RelinkExistingProperties*/true);

	return NewBag;
}

void UPropertyBag::InitializeStruct(void* Dest, int32 ArrayDim) const
{
	Super::InitializeStruct(Dest, ArrayDim);

	// Do ref counting based on struct usage.
	// This ensures that if the UPropertyBag is still valid in the C++ destructor of
	// the last instance of the bag.
	UPropertyBag* NonConstThis = const_cast<UPropertyBag*>(this);
	const int32 OldCount = NonConstThis->RefCount.fetch_add(1, std::memory_order_acq_rel);
	if (OldCount == 0)
	{
		NonConstThis->AddToRoot();
	}
}

void UPropertyBag::DestroyStruct(void* Dest, int32 ArrayDim) const
{
	Super::DestroyStruct(Dest, ArrayDim);

	// Do ref counting based on struct usage.
	// This ensures that if the UPropertyBag is still valid in the C++ destructor of
	// the last instance of the bag.

	UPropertyBag* NonConstThis = const_cast<UPropertyBag*>(this);
	const int32 OldCount = NonConstThis->RefCount.fetch_sub(1, std::memory_order_acq_rel);
	if (OldCount == 1)
	{
		NonConstThis->RemoveFromRoot();
	}
	if (OldCount <= 0)
	{
		UE_LOG(LogCore, Error, TEXT("PropertyBag: DestroyStruct is called when RefCount is %d."), OldCount);
	}
}

void UPropertyBag::FinishDestroy()
{
	const int32 Count = RefCount.load(); 
	if (Count > 0)
	{
		UE_LOG(LogCore, Error, TEXT("PropertyBag: Expecting RefCount to be zero on destructor, but it is %d."), Count);
	}
	
	Super::FinishDestroy();
}

const FPropertyBagPropertyDesc* UPropertyBag::FindPropertyDescByID(const FGuid ID) const
{
	return PropertyDescs.FindByPredicate([&ID](const FPropertyBagPropertyDesc& Desc) { return Desc.ID == ID; });
}

const FPropertyBagPropertyDesc* UPropertyBag::FindPropertyDescByName(const FName Name) const
{
	return PropertyDescs.FindByPredicate([&Name](const FPropertyBagPropertyDesc& Desc) { return Desc.Name == Name; });
}
