// Copyright Epic Games, Inc. All Rights Reserved.

#include "Serialization/StructuredArchiveSlots.h"
#include "Serialization/StructuredArchiveContainer.h"
#include "Containers/UnrealString.h"

#if WITH_TEXT_ARCHIVE_SUPPORT

//////////// FStructuredArchiveSlot ////////////

FStructuredArchiveRecord FStructuredArchiveSlot::EnterRecord()
{
	int32 NewDepth = Ar.EnterSlotAsType(*this, UE::StructuredArchive::Private::EElementType::Record);

#if DO_STRUCTURED_ARCHIVE_CONTAINER_CHECKS
	Ar.CurrentContainer.Emplace(0);
#endif

	Ar.Formatter.EnterRecord();

	return FStructuredArchiveRecord(FStructuredArchiveRecord::EPrivateToken{}, Ar, NewDepth, ElementId);
}

FStructuredArchiveArray FStructuredArchiveSlot::EnterArray(int32& Num)
{
	int32 NewDepth = Ar.EnterSlotAsType(*this, UE::StructuredArchive::Private::EElementType::Array);

	Ar.Formatter.EnterArray(Num);

#if DO_STRUCTURED_ARCHIVE_CONTAINER_CHECKS
	Ar.CurrentContainer.Emplace(Num);
#endif

	return FStructuredArchiveArray(FStructuredArchiveArray::EPrivateToken{}, Ar, NewDepth, ElementId);
}

FStructuredArchiveStream FStructuredArchiveSlot::EnterStream()
{
	int32 NewDepth = Ar.EnterSlotAsType(*this, UE::StructuredArchive::Private::EElementType::Stream);

	Ar.Formatter.EnterStream();

	return FStructuredArchiveStream(FStructuredArchiveStream::EPrivateToken{}, Ar, NewDepth, ElementId);
}

FStructuredArchiveMap FStructuredArchiveSlot::EnterMap(int32& Num)
{
	int32 NewDepth = Ar.EnterSlotAsType(*this, UE::StructuredArchive::Private::EElementType::Map);

	Ar.Formatter.EnterMap(Num);

#if DO_STRUCTURED_ARCHIVE_CONTAINER_CHECKS
	Ar.CurrentContainer.Emplace(Num);
#endif

	return FStructuredArchiveMap(FStructuredArchiveMap::EPrivateToken{}, Ar, NewDepth, ElementId);
}

FStructuredArchiveSlot FStructuredArchiveSlot::EnterAttribute(FArchiveFieldName AttributeName)
{
	check(Ar.CurrentScope.Num() > 0);

	int32 NewDepth = Depth + 1;
	if (NewDepth >= Ar.CurrentScope.Num() || Ar.CurrentScope[NewDepth].Id != ElementId || Ar.CurrentScope[NewDepth].Type != UE::StructuredArchive::Private::EElementType::AttributedValue)
	{
		int32 NewDepthCheck = Ar.EnterSlotAsType(*this, UE::StructuredArchive::Private::EElementType::AttributedValue);
		checkSlow(NewDepth == NewDepthCheck);

		Ar.Formatter.EnterAttributedValue();

#if DO_STRUCTURED_ARCHIVE_CONTAINER_CHECKS
		Ar.CurrentContainer.Emplace(0);
#endif
	}

	Ar.CurrentEnteringAttributeState = UE::StructuredArchive::Private::EEnteringAttributeState::NotEnteringAttribute;

	UE::StructuredArchive::Private::FElementId AttributedValueId = Ar.CurrentScope[NewDepth].Id;

	Ar.SetScope(UE::StructuredArchive::Private::FSlotPosition(NewDepth, AttributedValueId));

	Ar.CurrentSlotElementId = Ar.ElementIdGenerator.Generate();

#if DO_STRUCTURED_ARCHIVE_CONTAINER_CHECKS
#if DO_STRUCTURED_ARCHIVE_UNIQUE_FIELD_NAME_CHECKS
	if (!Ar.GetUnderlyingArchive().IsLoading())
	{
		FContainer& Container = *Ar.CurrentContainer.Top();
		checkf(!Container.KeyNames.Contains(Name.Name), TEXT("Multiple attributes called '%s' serialized into attributed value"), AttributeName.Name);
		Container.KeyNames.Add(Name.Name);
	}
#endif
#endif

	Ar.Formatter.EnterAttribute(AttributeName);

	return FStructuredArchiveSlot(FStructuredArchiveSlot::EPrivateToken{}, Ar, NewDepth, Ar.CurrentSlotElementId);
}

TOptional<FStructuredArchiveSlot> FStructuredArchiveSlot::TryEnterAttribute(FArchiveFieldName AttributeName, bool bEnterWhenWriting)
{
	check(Ar.CurrentScope.Num() > 0);

	int32 NewDepth = Depth + 1;
	if (NewDepth >= Ar.CurrentScope.Num() || Ar.CurrentScope[NewDepth].Id != ElementId || Ar.CurrentScope[NewDepth].Type != UE::StructuredArchive::Private::EElementType::AttributedValue)
	{
		int32 NewDepthCheck = Ar.EnterSlotAsType(*this, UE::StructuredArchive::Private::EElementType::AttributedValue);
		checkSlow(NewDepth == NewDepthCheck);

		Ar.Formatter.EnterAttributedValue();

#if DO_STRUCTURED_ARCHIVE_CONTAINER_CHECKS
		Ar.CurrentContainer.Emplace(0);
#endif
	}

#if DO_STRUCTURED_ARCHIVE_CONTAINER_CHECKS
#if DO_STRUCTURED_ARCHIVE_UNIQUE_FIELD_NAME_CHECKS
	if (!Ar.GetUnderlyingArchive().IsLoading())
	{
		FContainer& Container = *Ar.CurrentContainer.Top();
		checkf(!Container.KeyNames.Contains(Name.Name), TEXT("Multiple attributes called '%s' serialized into attributed value"), AttributeName.Name);
		Container.KeyNames.Add(Name.Name);
	}
#endif
#endif

	UE::StructuredArchive::Private::FElementId AttributedValueId = Ar.CurrentScope[NewDepth].Id;

	Ar.SetScope(UE::StructuredArchive::Private::FSlotPosition(NewDepth, AttributedValueId));

	if (Ar.Formatter.TryEnterAttribute(AttributeName, bEnterWhenWriting))
	{
		Ar.CurrentEnteringAttributeState = UE::StructuredArchive::Private::EEnteringAttributeState::NotEnteringAttribute;

		Ar.CurrentSlotElementId = Ar.ElementIdGenerator.Generate();

		return FStructuredArchiveSlot(FStructuredArchiveSlot::EPrivateToken{}, Ar, NewDepth, Ar.CurrentSlotElementId);
	}
	else
	{
		return {};
	}
}

void FStructuredArchiveSlot::operator<< (uint8& Value)
{
	Ar.EnterSlot(*this);
	Ar.Formatter.Serialize(Value);
	Ar.LeaveSlot();
}

void FStructuredArchiveSlot::operator<< (uint16& Value)
{
	Ar.EnterSlot(*this);
	Ar.Formatter.Serialize(Value);
	Ar.LeaveSlot();
}

void FStructuredArchiveSlot::operator<< (uint32& Value)
{
	Ar.EnterSlot(*this);
	Ar.Formatter.Serialize(Value);
	Ar.LeaveSlot();
}

void FStructuredArchiveSlot::operator<< (uint64& Value)
{
	Ar.EnterSlot(*this);
	Ar.Formatter.Serialize(Value);
	Ar.LeaveSlot();
}

void FStructuredArchiveSlot::operator<< (int8& Value)
{
	Ar.EnterSlot(*this);
	Ar.Formatter.Serialize(Value);
	Ar.LeaveSlot();
}

void FStructuredArchiveSlot::operator<< (int16& Value)
{
	Ar.EnterSlot(*this);
	Ar.Formatter.Serialize(Value);
	Ar.LeaveSlot();
}

void FStructuredArchiveSlot::operator<< (int32& Value)
{
	Ar.EnterSlot(*this);
	Ar.Formatter.Serialize(Value);
	Ar.LeaveSlot();
}

void FStructuredArchiveSlot::operator<< (int64& Value)
{
	Ar.EnterSlot(*this);
	Ar.Formatter.Serialize(Value);
	Ar.LeaveSlot();
}

void FStructuredArchiveSlot::operator<< (float& Value)
{
	Ar.EnterSlot(*this);
	Ar.Formatter.Serialize(Value);
	Ar.LeaveSlot();
}

void FStructuredArchiveSlot::operator<< (double& Value)
{
	Ar.EnterSlot(*this);
	Ar.Formatter.Serialize(Value);
	Ar.LeaveSlot();
}

void FStructuredArchiveSlot::operator<< (bool& Value)
{
	Ar.EnterSlot(*this);
	Ar.Formatter.Serialize(Value);
	Ar.LeaveSlot();
}

void FStructuredArchiveSlot::operator<< (FString& Value)
{
	Ar.EnterSlot(*this);
	Ar.Formatter.Serialize(Value);
	Ar.LeaveSlot();
}

void FStructuredArchiveSlot::operator<< (FName& Value)
{
	Ar.EnterSlot(*this);
	Ar.Formatter.Serialize(Value);
	Ar.LeaveSlot();
}

void FStructuredArchiveSlot::operator<< (UObject*& Value)
{
	Ar.EnterSlot(*this);
	Ar.Formatter.Serialize(Value);
	Ar.LeaveSlot();
}

void FStructuredArchiveSlot::operator<< (FText& Value)
{
	Ar.EnterSlot(*this);
	Ar.Formatter.Serialize(Value);
	Ar.LeaveSlot();
}

void FStructuredArchiveSlot::operator<< (FWeakObjectPtr& Value)
{
	Ar.EnterSlot(*this);
	Ar.Formatter.Serialize(Value);
	Ar.LeaveSlot();
}

void FStructuredArchiveSlot::operator<< (FLazyObjectPtr& Value)
{
	Ar.EnterSlot(*this);
	Ar.Formatter.Serialize(Value);
	Ar.LeaveSlot();
}

void FStructuredArchiveSlot::operator<< (FObjectPtr& Value)
{
	Ar.EnterSlot(*this);
	Ar.Formatter.Serialize(Value);
	Ar.LeaveSlot();
}

void FStructuredArchiveSlot::operator<< (FSoftObjectPtr& Value)
{
	Ar.EnterSlot(*this);
	Ar.Formatter.Serialize(Value);
	Ar.LeaveSlot();
}

void FStructuredArchiveSlot::operator<< (FSoftObjectPath& Value)
{
	Ar.EnterSlot(*this);
	Ar.Formatter.Serialize(Value);
	Ar.LeaveSlot();
}

void FStructuredArchiveSlot::Serialize(TArray<uint8>& Data)
{
	Ar.EnterSlot(*this);
	Ar.Formatter.Serialize(Data);
	Ar.LeaveSlot();
}

void FStructuredArchiveSlot::Serialize(void* Data, uint64 DataSize)
{
	Ar.EnterSlot(*this);
	Ar.Formatter.Serialize(Data, DataSize);
	Ar.LeaveSlot();
}

//////////// FStructuredArchiveRecord ////////////

FStructuredArchiveSlot FStructuredArchiveRecord::EnterField(FArchiveFieldName Name)
{
	Ar.SetScope(*this);

	Ar.CurrentSlotElementId = Ar.ElementIdGenerator.Generate();

#if DO_STRUCTURED_ARCHIVE_CONTAINER_CHECKS
#if DO_STRUCTURED_ARCHIVE_UNIQUE_FIELD_NAME_CHECKS
	if (!Ar.GetUnderlyingArchive().IsLoading())
	{
		FContainer& Container = *Ar.CurrentContainer.Top();
		checkf(!Container.KeyNames.Contains(Name.Name), TEXT("Multiple keys called '%s' serialized into record"), Name.Name);
		Container.KeyNames.Add(Name.Name);
	}
#endif
#endif

	Ar.Formatter.EnterField(Name);

	return FStructuredArchiveSlot(FStructuredArchiveSlot::EPrivateToken{}, Ar, Depth, Ar.CurrentSlotElementId);
}

FStructuredArchiveRecord FStructuredArchiveRecord::EnterRecord(FArchiveFieldName Name)
{
	return EnterField(Name).EnterRecord();
}

FStructuredArchiveArray FStructuredArchiveRecord::EnterArray(FArchiveFieldName Name, int32& Num)
{
	return EnterField(Name).EnterArray(Num);
}

FStructuredArchiveStream FStructuredArchiveRecord::EnterStream(FArchiveFieldName Name)
{
	return EnterField(Name).EnterStream();
}

FStructuredArchiveMap FStructuredArchiveRecord::EnterMap(FArchiveFieldName Name, int32& Num)
{
	return EnterField(Name).EnterMap(Num);
}

TOptional<FStructuredArchiveSlot> FStructuredArchiveRecord::TryEnterField(FArchiveFieldName Name, bool bEnterWhenWriting)
{
	Ar.SetScope(*this);

#if DO_STRUCTURED_ARCHIVE_CONTAINER_CHECKS
#if DO_STRUCTURED_ARCHIVE_UNIQUE_FIELD_NAME_CHECKS
	if (!GetUnderlyingArchive().IsLoading())
	{
		FContainer& Container = *Ar.CurrentContainer.Top();
		checkf(!Container.KeyNames.Contains(Name.Name), TEXT("Multiple keys called '%s' serialized into record"), Name.Name);
		Container.KeyNames.Add(Name.Name);
	}
#endif
#endif

	if (Ar.Formatter.TryEnterField(Name, bEnterWhenWriting))
	{
		Ar.CurrentSlotElementId = Ar.ElementIdGenerator.Generate();
		return FStructuredArchiveSlot(FStructuredArchiveSlot::EPrivateToken{}, Ar, Depth, Ar.CurrentSlotElementId);
	}
	else
	{
		return {};
	}
}

//////////// FStructuredArchiveArray ////////////

FStructuredArchiveSlot FStructuredArchiveArray::EnterElement()
{
	Ar.SetScope(*this);

#if DO_STRUCTURED_ARCHIVE_CONTAINER_CHECKS
	checkf(Ar.CurrentContainer.Top()->Index < Ar.CurrentContainer.Top()->Count, TEXT("Serialized too many array elements"));
#endif

	Ar.CurrentSlotElementId = Ar.ElementIdGenerator.Generate();

	Ar.Formatter.EnterArrayElement();

	return FStructuredArchiveSlot(FStructuredArchiveSlot::EPrivateToken{}, Ar, Depth, Ar.CurrentSlotElementId);
}

//////////// FStructuredArchiveStream ////////////

FStructuredArchiveSlot FStructuredArchiveStream::EnterElement()
{
	Ar.SetScope(*this);

	Ar.CurrentSlotElementId = Ar.ElementIdGenerator.Generate();

	Ar.Formatter.EnterStreamElement();

	return FStructuredArchiveSlot(FStructuredArchiveSlot::EPrivateToken{}, Ar, Depth, Ar.CurrentSlotElementId);
}

//////////// FStructuredArchiveMap ////////////

FStructuredArchiveSlot FStructuredArchiveMap::EnterElement(FString& Name)
{
	Ar.SetScope(*this);

#if DO_STRUCTURED_ARCHIVE_CONTAINER_CHECKS
	checkf(Ar.CurrentContainer.Top()->Index < Ar.CurrentContainer.Top()->Count, TEXT("Serialized too many map elements"));
#endif

	Ar.CurrentSlotElementId = Ar.ElementIdGenerator.Generate();

#if DO_STRUCTURED_ARCHIVE_CONTAINER_CHECKS
#if DO_STRUCTURED_ARCHIVE_UNIQUE_FIELD_NAME_CHECKS
	if(Ar.GetUnderlyingArchive().IsSaving())
	{
		FContainer& Container = *Ar.CurrentContainer.Top();
		checkf(!Container.KeyNames.Contains(Name), TEXT("Multiple keys called '%s' serialized into record"), *Name);
		Container.KeyNames.Add(Name);
	}
#endif
#endif

	Ar.Formatter.EnterMapElement(Name);

#if DO_STRUCTURED_ARCHIVE_CONTAINER_CHECKS
#if DO_STRUCTURED_ARCHIVE_UNIQUE_FIELD_NAME_CHECKS
	if(Ar.GetUnderlyingArchive().IsLoading())
	{
		FContainer& Container = *Ar.CurrentContainer.Top();
		checkf(!Container.KeyNames.Contains(Name), TEXT("Multiple keys called '%s' serialized into record"), *Name);
		Container.KeyNames.Add(Name);
	}
#endif
#endif

	return FStructuredArchiveSlot(FStructuredArchiveSlot::EPrivateToken{}, Ar, Depth, Ar.CurrentSlotElementId);
}

#endif
