// Copyright Epic Games, Inc. All Rights Reserved.

#include "LocalizableMessage.h"

FLocalizableMessageParameterEntry::FLocalizableMessageParameterEntry() = default;
FLocalizableMessageParameterEntry::FLocalizableMessageParameterEntry(const FString& InKey, TUniquePtr<FLocalizableMessageParameter, FLocalizableMessageParameter::FCustomDeleter>&& InValue) : Key(InKey), Value(std::move(InValue)) {}
FLocalizableMessageParameterEntry::FLocalizableMessageParameterEntry(FLocalizableMessageParameterEntry&&) = default;
FLocalizableMessageParameterEntry::~FLocalizableMessageParameterEntry() = default;
FLocalizableMessageParameterEntry& FLocalizableMessageParameterEntry::operator=(FLocalizableMessageParameterEntry&&) = default;

bool FLocalizableMessageParameterEntry::operator==(const FLocalizableMessageParameterEntry& Other) const
{
	if (Key != Other.Key)
	{
		return false;
	}

	if (Value.IsValid() != Other.Value.IsValid())
	{
		return false;
	}

	if (Value.IsValid())
	{
		const UScriptStruct* TypeA = Value->GetScriptStruct();
		const UScriptStruct* TypeB = Other.Value->GetScriptStruct();

		if (TypeA != TypeB)
		{
			return false;
		}

		if (TypeA->CompareScriptStruct(Value.Get(), Other.Value.Get(), 0) == false)
		{
			return false;
		}
	}

	return true;
}

bool FLocalizableMessageParameterEntry::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess)
{
	TCheckedObjPtr<UScriptStruct> EntryType = Value.IsValid() ? Value->GetScriptStruct() : nullptr;

	Ar << Key;
	Ar << EntryType;

	if (Ar.IsLoading())
	{
		Value = nullptr;

		if (EntryType.IsValid())
		{
			FLocalizableMessageParameter* NewEntry = FLocalizableMessageParameter::AllocateType(EntryType.Get());
			Value = TUniquePtr<FLocalizableMessageParameter, FLocalizableMessageParameter::FCustomDeleter>(NewEntry);
		}
	}

	if (Value.IsValid())
	{
		if (EntryType->StructFlags & STRUCT_NetSerializeNative)
		{
			EntryType->GetCppStructOps()->NetSerialize(Ar, Map, bOutSuccess, Value.Get());
		}
		else
		{
			EntryType->SerializeItem(FStructuredArchiveFromArchive(Ar).GetSlot(), Value.Get(), NULL);
		}
	}

	bOutSuccess = true;

	return true;
}

void FLocalizableMessageParameterEntry::AddStructReferencedObjects(class FReferenceCollector& Collector)
{
	if (FLocalizableMessageParameter* Param = Value.Get())
	{
		Collector.AddPropertyReferencesWithStructARO(Param->GetScriptStruct(), Param);
	}
}

FLocalizableMessage::FLocalizableMessage() = default;
FLocalizableMessage::FLocalizableMessage(FLocalizableMessage&&) = default;
FLocalizableMessage::~FLocalizableMessage() = default;
FLocalizableMessage& FLocalizableMessage::operator=(FLocalizableMessage&&) = default;


bool FLocalizableMessage::operator==(const FLocalizableMessage& Other) const
{
	return Key == Other.Key &&
		DefaultText == Other.DefaultText &&
		Substitutions == Other.Substitutions;
}
