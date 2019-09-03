// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Serialization/StructuredArchive.h"
#include "Containers/Set.h"
#include "Containers/UnrealString.h"

#if WITH_TEXT_ARCHIVE_SUPPORT

//////////// FStructuredArchive::FContainer ////////////

#if DO_STRUCTURED_ARCHIVE_CONTAINER_CHECKS

struct FStructuredArchive::FContainer
{
	int  Index                   = 0;
	int  Count                   = 0;
	bool bAttributedValueWritten = false;

#if DO_STRUCTURED_ARCHIVE_UNIQUE_FIELD_NAME_CHECKS
	TSet<FString> KeyNames;
#endif

	explicit FContainer(int InCount)
		: Count(InCount)
	{
	}
};
#endif

FStructuredArchiveChildReader::FStructuredArchiveChildReader(FStructuredArchive::FSlot InSlot)
	: OwnedFormatter(nullptr)
	, Archive(nullptr)
{
	FStructuredArchiveFormatter* Formatter = &InSlot.Ar.Formatter;
	if (InSlot.GetUnderlyingArchive().IsTextFormat())
	{
		Formatter = OwnedFormatter = InSlot.Ar.Formatter.CreateSubtreeReader();
	}

	Archive = new FStructuredArchive(*Formatter);
	Root.Emplace(Archive->Open());
	InSlot.EnterRecord();
}

FStructuredArchiveChildReader::~FStructuredArchiveChildReader()
{
	Root.Reset();
	Archive->Close();
	delete Archive;
	Archive = nullptr;

	// If this is a text archive, we'll have created a subtree reader that our contained archive is using as 
	// its formatter. We need to clean it up now.
	if (OwnedFormatter)
	{
		delete OwnedFormatter;
		OwnedFormatter = nullptr;
	}
}

//////////// FStructuredArchive ////////////

FStructuredArchive::FStructuredArchive(FArchiveFormatterType& InFormatter)
	: Formatter(InFormatter)
#if DO_STRUCTURED_ARCHIVE_CONTAINER_CHECKS
	, bRequiresStructuralMetadata(true)
#else
	, bRequiresStructuralMetadata(InFormatter.HasDocumentTree())
#endif
{
	CurrentScope.Reserve(32);
#if DO_STRUCTURED_ARCHIVE_CONTAINER_CHECKS
	CurrentContainer.Reserve(32);
#endif
}

FStructuredArchive::~FStructuredArchive()
{
	Close();
}

FStructuredArchive::FSlot FStructuredArchive::Open()
{
	check(CurrentScope.Num() == 0);
	check(!RootElementId.IsValid());
	check(!CurrentSlotElementId.IsValid());

	RootElementId = ElementIdGenerator.Generate();
	CurrentScope.Emplace(RootElementId, EElementType::Root);

	CurrentSlotElementId = ElementIdGenerator.Generate();

	return FSlot(*this, 0, CurrentSlotElementId);
}

void FStructuredArchive::Close()
{
	SetScope(FSlotPosition(0, RootElementId));
}

void FStructuredArchive::EnterSlot(FSlotPosition Slot)
{
	int32      ParentDepth = Slot.Depth;
	FElementId ElementId   = Slot.ElementId;

	// If the slot being entered has attributes, enter the value slot first.
	if (ParentDepth + 1 < CurrentScope.Num() && CurrentScope[ParentDepth + 1].Id == ElementId && CurrentScope[ParentDepth + 1].Type == EElementType::AttributedValue)
	{
#if DO_STRUCTURED_ARCHIVE_CONTAINER_CHECKS
		checkf(!CurrentSlotElementId.IsValid() && !CurrentContainer.Top()->bAttributedValueWritten, TEXT("Attempt to serialize data into an invalid slot"));
		CurrentContainer.Top()->bAttributedValueWritten = true;
#else
		checkf(!CurrentSlotElementId.IsValid(), TEXT("Attempt to serialize data into an invalid slot"));
#endif

		SetScope(FSlotPosition(ParentDepth + 1, ElementId));
		Formatter.EnterAttributedValueValue();
	}
	else
	{
		checkf(ElementId == CurrentSlotElementId, TEXT("Attempt to serialize data into an invalid slot"));
		CurrentSlotElementId.Reset();
	}

	CurrentEnteringAttributeState = EEnteringAttributeState::NotEnteringAttribute;
}

int32 FStructuredArchive::EnterSlotAsType(FSlotPosition Slot, EElementType ElementType)
{
	EnterSlot(Slot);

	int32 NewSlotDepth = Slot.Depth + 1;

	// If we're entering the value of an attributed slot, we need to return a depth one higher than usual, because we're
	// inside an attributed value container.
	//
	// We don't need to do adjust for attributes, because entering the attribute slot will bump the depth anyway.
	if (NewSlotDepth < CurrentScope.Num() &&
		CurrentScope[NewSlotDepth].Type == EElementType::AttributedValue &&
		CurrentEnteringAttributeState == EEnteringAttributeState::NotEnteringAttribute)
	{
		++NewSlotDepth;
	}

	CurrentScope.Emplace(Slot.ElementId, ElementType);
	return NewSlotDepth;
}

void FStructuredArchive::LeaveSlot()
{
	if (bRequiresStructuralMetadata)
	{
		switch (CurrentScope.Top().Type)
		{
		case EElementType::Record:
			Formatter.LeaveField();
			break;
		case EElementType::Array:
			Formatter.LeaveArrayElement();
#if DO_STRUCTURED_ARCHIVE_CONTAINER_CHECKS
			CurrentContainer.Top()->Index++;
#endif
			break;
		case EElementType::Stream:
			Formatter.LeaveStreamElement();
			break;
		case EElementType::Map:
			Formatter.LeaveMapElement();
#if DO_STRUCTURED_ARCHIVE_CONTAINER_CHECKS
			CurrentContainer.Top()->Index++;
#endif
			break;
		case EElementType::AttributedValue:
			Formatter.LeaveAttribute();
			break;
		}
	}
}

void FStructuredArchive::SetScope(FSlotPosition Slot)
{
	// Make sure the scope is valid
	checkf(Slot.Depth < CurrentScope.Num() && CurrentScope[Slot.Depth].Id == Slot.ElementId, TEXT("Invalid scope for writing to archive"));
	checkf(!CurrentSlotElementId.IsValid() || GetUnderlyingArchive().IsLoading(), TEXT("Cannot change scope until having written a value to the current slot"));

	// Roll back to the correct scope
	if (bRequiresStructuralMetadata)
	{
		for (int32 CurrentDepth = CurrentScope.Num() - 1; CurrentDepth > Slot.Depth; CurrentDepth--)
		{
			// Leave the current element
			const FElement& Element = CurrentScope[CurrentDepth];
			switch (Element.Type)
			{
			case EElementType::Record:
				Formatter.LeaveRecord();
#if DO_STRUCTURED_ARCHIVE_CONTAINER_CHECKS
				CurrentContainer.Pop(false);
#endif
				break;
			case EElementType::Array:
#if DO_STRUCTURED_ARCHIVE_CONTAINER_CHECKS
				checkf(GetUnderlyingArchive().IsLoading() || CurrentContainer.Top()->Index == CurrentContainer.Top()->Count, TEXT("Incorrect number of elements serialized in array"));
#endif
				Formatter.LeaveArray();
#if DO_STRUCTURED_ARCHIVE_CONTAINER_CHECKS
				CurrentContainer.Pop(false);
#endif
				break;
			case EElementType::Stream:
				Formatter.LeaveStream();
				break;
			case EElementType::Map:
#if DO_STRUCTURED_ARCHIVE_CONTAINER_CHECKS
				checkf(CurrentContainer.Top()->Index == CurrentContainer.Top()->Count, TEXT("Incorrect number of elements serialized in map"));
#endif
				Formatter.LeaveMap();
#if DO_STRUCTURED_ARCHIVE_CONTAINER_CHECKS
				CurrentContainer.Pop(false);
#endif
				break;
			case EElementType::AttributedValue:
				Formatter.LeaveAttributedValue();
#if DO_STRUCTURED_ARCHIVE_CONTAINER_CHECKS
				CurrentContainer.Pop(false);
#endif
				break;
			}

			// Remove the element from the stack
			CurrentScope.RemoveAt(CurrentDepth, 1, false);

			// Leave the slot containing it
			LeaveSlot();
		}
	}
	else
	{
		// Remove all the top elements from the stack
		CurrentScope.RemoveAt(Slot.Depth + 1, CurrentScope.Num() - (Slot.Depth + 1));
	}
}

//////////// FStructuredArchive::FSlot ////////////

FStructuredArchive::FRecord FStructuredArchive::FSlot::EnterRecord()
{
	int32 NewDepth = Ar.EnterSlotAsType(*this, EElementType::Record);

#if DO_STRUCTURED_ARCHIVE_CONTAINER_CHECKS
	Ar.CurrentContainer.Emplace(0);
#endif

	Ar.Formatter.EnterRecord();

	return FRecord(Ar, NewDepth, ElementId);
}

FStructuredArchive::FRecord FStructuredArchive::FSlot::EnterRecord_TextOnly(TArray<FString>& OutFieldNames)
{
	int32 NewDepth = Ar.EnterSlotAsType(*this, EElementType::Record);

#if DO_STRUCTURED_ARCHIVE_CONTAINER_CHECKS
	Ar.CurrentContainer.Emplace(0);
#endif

	Ar.Formatter.EnterRecord_TextOnly(OutFieldNames);

	return FRecord(Ar, NewDepth, ElementId);
}

FStructuredArchive::FArray FStructuredArchive::FSlot::EnterArray(int32& Num)
{
	int32 NewDepth = Ar.EnterSlotAsType(*this, EElementType::Array);

	Ar.Formatter.EnterArray(Num);

#if DO_STRUCTURED_ARCHIVE_CONTAINER_CHECKS
	Ar.CurrentContainer.Emplace(Num);
#endif

	return FArray(Ar, NewDepth, ElementId);
}

FStructuredArchive::FStream FStructuredArchive::FSlot::EnterStream()
{
	int32 NewDepth = Ar.EnterSlotAsType(*this, EElementType::Stream);

	Ar.Formatter.EnterStream();

	return FStream(Ar, NewDepth, ElementId);
}

FStructuredArchive::FStream FStructuredArchive::FSlot::EnterStream_TextOnly(int32& OutNumElements)
{
	int32 NewDepth = Ar.EnterSlotAsType(*this, EElementType::Stream);

	Ar.Formatter.EnterStream_TextOnly(OutNumElements);

	return FStream(Ar, NewDepth, ElementId);
}

FStructuredArchive::FMap FStructuredArchive::FSlot::EnterMap(int32& Num)
{
	int32 NewDepth = Ar.EnterSlotAsType(*this, EElementType::Map);

	Ar.Formatter.EnterMap(Num);

#if DO_STRUCTURED_ARCHIVE_CONTAINER_CHECKS
	Ar.CurrentContainer.Emplace(Num);
#endif

	return FMap(Ar, NewDepth, ElementId);
}

FStructuredArchive::FSlot FStructuredArchive::FSlot::EnterAttribute(FArchiveFieldName AttributeName)
{
	check(Ar.CurrentScope.Num() > 0);

	int32 NewDepth = Depth + 1;
	if (NewDepth >= Ar.CurrentScope.Num() || Ar.CurrentScope[NewDepth].Id != ElementId || Ar.CurrentScope[NewDepth].Type != EElementType::AttributedValue)
	{
		int32 NewDepthCheck = Ar.EnterSlotAsType(*this, EElementType::AttributedValue);
		checkSlow(NewDepth == NewDepthCheck);

		Ar.Formatter.EnterAttributedValue();

#if DO_STRUCTURED_ARCHIVE_CONTAINER_CHECKS
		Ar.CurrentContainer.Emplace(0);
#endif
	}

	Ar.CurrentEnteringAttributeState = EEnteringAttributeState::NotEnteringAttribute;

	FElementId AttributedValueId = Ar.CurrentScope[NewDepth].Id;

	Ar.SetScope(FSlotPosition(NewDepth, AttributedValueId));

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

	return FSlot(Ar, NewDepth, Ar.CurrentSlotElementId);
}

TOptional<FStructuredArchive::FSlot> FStructuredArchive::FSlot::TryEnterAttribute(FArchiveFieldName AttributeName, bool bEnterWhenWriting)
{
	check(Ar.CurrentScope.Num() > 0);

	int32 NewDepth = Depth + 1;
	if (NewDepth >= Ar.CurrentScope.Num() || Ar.CurrentScope[NewDepth].Id != ElementId || Ar.CurrentScope[NewDepth].Type != EElementType::AttributedValue)
	{
		int32 NewDepthCheck = Ar.EnterSlotAsType(*this, EElementType::AttributedValue);
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

	FElementId AttributedValueId = Ar.CurrentScope[NewDepth].Id;

	Ar.SetScope(FSlotPosition(NewDepth, AttributedValueId));

	if (Ar.Formatter.TryEnterAttribute(AttributeName, bEnterWhenWriting))
	{
		Ar.CurrentEnteringAttributeState = EEnteringAttributeState::NotEnteringAttribute;

		Ar.CurrentSlotElementId = Ar.ElementIdGenerator.Generate();

		return FSlot(Ar, NewDepth, Ar.CurrentSlotElementId);
	}
	else
	{
		return {};
	}
}

void FStructuredArchive::FSlot::operator<< (uint8& Value)
{
	Ar.EnterSlot(*this);
	Ar.Formatter.Serialize(Value);
	Ar.LeaveSlot();
}

void FStructuredArchive::FSlot::operator<< (uint16& Value)
{
	Ar.EnterSlot(*this);
	Ar.Formatter.Serialize(Value);
	Ar.LeaveSlot();
}

void FStructuredArchive::FSlot::operator<< (uint32& Value)
{
	Ar.EnterSlot(*this);
	Ar.Formatter.Serialize(Value);
	Ar.LeaveSlot();
}

void FStructuredArchive::FSlot::operator<< (uint64& Value)
{
	Ar.EnterSlot(*this);
	Ar.Formatter.Serialize(Value);
	Ar.LeaveSlot();
}

void FStructuredArchive::FSlot::operator<< (int8& Value)
{
	Ar.EnterSlot(*this);
	Ar.Formatter.Serialize(Value);
	Ar.LeaveSlot();
}

void FStructuredArchive::FSlot::operator<< (int16& Value)
{
	Ar.EnterSlot(*this);
	Ar.Formatter.Serialize(Value);
	Ar.LeaveSlot();
}

void FStructuredArchive::FSlot::operator<< (int32& Value)
{
	Ar.EnterSlot(*this);
	Ar.Formatter.Serialize(Value);
	Ar.LeaveSlot();
}

void FStructuredArchive::FSlot::operator<< (int64& Value)
{
	Ar.EnterSlot(*this);
	Ar.Formatter.Serialize(Value);
	Ar.LeaveSlot();
}

void FStructuredArchive::FSlot::operator<< (float& Value)
{
	Ar.EnterSlot(*this);
	Ar.Formatter.Serialize(Value);
	Ar.LeaveSlot();
}

void FStructuredArchive::FSlot::operator<< (double& Value)
{
	Ar.EnterSlot(*this);
	Ar.Formatter.Serialize(Value);
	Ar.LeaveSlot();
}

void FStructuredArchive::FSlot::operator<< (bool& Value)
{
	Ar.EnterSlot(*this);
	Ar.Formatter.Serialize(Value);
	Ar.LeaveSlot();
}

void FStructuredArchive::FSlot::operator<< (FString& Value)
{
	Ar.EnterSlot(*this);
	Ar.Formatter.Serialize(Value);
	Ar.LeaveSlot();
}

void FStructuredArchive::FSlot::operator<< (FName& Value)
{
	Ar.EnterSlot(*this);
	Ar.Formatter.Serialize(Value);
	Ar.LeaveSlot();
}

void FStructuredArchive::FSlot::operator<< (UObject*& Value)
{
	Ar.EnterSlot(*this);
	Ar.Formatter.Serialize(Value);
	Ar.LeaveSlot();
}

void FStructuredArchive::FSlot::operator<< (FText& Value)
{
	Ar.EnterSlot(*this);
	Ar.Formatter.Serialize(Value);
	Ar.LeaveSlot();
}

void FStructuredArchive::FSlot::operator<< (FWeakObjectPtr& Value)
{
	Ar.EnterSlot(*this);
	Ar.Formatter.Serialize(Value);
	Ar.LeaveSlot();
}

void FStructuredArchive::FSlot::operator<< (FLazyObjectPtr& Value)
{
	Ar.EnterSlot(*this);
	Ar.Formatter.Serialize(Value);
	Ar.LeaveSlot();
}

void FStructuredArchive::FSlot::operator<< (FSoftObjectPtr& Value)
{
	Ar.EnterSlot(*this);
	Ar.Formatter.Serialize(Value);
	Ar.LeaveSlot();
}

void FStructuredArchive::FSlot::operator<< (FSoftObjectPath& Value)
{
	Ar.EnterSlot(*this);
	Ar.Formatter.Serialize(Value);
	Ar.LeaveSlot();
}

void FStructuredArchive::FSlot::Serialize(TArray<uint8>& Data)
{
	Ar.EnterSlot(*this);
	Ar.Formatter.Serialize(Data);
	Ar.LeaveSlot();
}

void FStructuredArchive::FSlot::Serialize(void* Data, uint64 DataSize)
{
	Ar.EnterSlot(*this);
	Ar.Formatter.Serialize(Data, DataSize);
	Ar.LeaveSlot();
}

//////////// FStructuredArchive::FObject ////////////

FStructuredArchive::FSlot FStructuredArchive::FRecord::EnterField(FArchiveFieldName Name)
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

	return FSlot(Ar, Depth, Ar.CurrentSlotElementId);
}

FStructuredArchive::FRecord FStructuredArchive::FRecord::EnterRecord(FArchiveFieldName Name)
{
	return EnterField(Name).EnterRecord();
}

FStructuredArchive::FRecord FStructuredArchive::FRecord::EnterRecord_TextOnly(FArchiveFieldName Name, TArray<FString>& OutFieldNames)
{
	return EnterField(Name).EnterRecord_TextOnly(OutFieldNames);
}

FStructuredArchive::FArray FStructuredArchive::FRecord::EnterArray(FArchiveFieldName Name, int32& Num)
{
	return EnterField(Name).EnterArray(Num);
}

FStructuredArchive::FStream FStructuredArchive::FRecord::EnterStream(FArchiveFieldName Name)
{
	return EnterField(Name).EnterStream();
}

FStructuredArchive::FStream FStructuredArchive::FRecord::EnterStream_TextOnly(FArchiveFieldName Name, int32& OutNumElements)
{
	return EnterField(Name).EnterStream_TextOnly(OutNumElements);
}

FStructuredArchive::FMap FStructuredArchive::FRecord::EnterMap(FArchiveFieldName Name, int32& Num)
{
	return EnterField(Name).EnterMap(Num);
}

TOptional<FStructuredArchive::FSlot> FStructuredArchive::FRecord::TryEnterField(FArchiveFieldName Name, bool bEnterWhenWriting)
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
		return FSlot(Ar, Depth, Ar.CurrentSlotElementId);
	}
	else
	{
		return {};
	}
}

//////////// FStructuredArchive::FArray ////////////

FStructuredArchive::FSlot FStructuredArchive::FArray::EnterElement()
{
	Ar.SetScope(*this);

#if DO_STRUCTURED_ARCHIVE_CONTAINER_CHECKS
	checkf(Ar.CurrentContainer.Top()->Index < Ar.CurrentContainer.Top()->Count, TEXT("Serialized too many array elements"));
#endif

	Ar.CurrentSlotElementId = Ar.ElementIdGenerator.Generate();

	Ar.Formatter.EnterArrayElement();

	return FSlot(Ar, Depth, Ar.CurrentSlotElementId);
}

FStructuredArchive::FSlot FStructuredArchive::FArray::EnterElement_TextOnly(EArchiveValueType& OutType)
{
	Ar.SetScope(*this);

#if DO_STRUCTURED_ARCHIVE_CONTAINER_CHECKS
	checkf(Ar.CurrentContainer.Top()->Index < Ar.CurrentContainer.Top()->Count, TEXT("Serialized too many array elements"));
#endif

	Ar.CurrentSlotElementId = Ar.ElementIdGenerator.Generate();

	Ar.Formatter.EnterArrayElement_TextOnly(OutType);

	return FSlot(Ar, Depth, Ar.CurrentSlotElementId);
}

//////////// FStructuredArchive::FStream ////////////

FStructuredArchive::FSlot FStructuredArchive::FStream::EnterElement()
{
	Ar.SetScope(*this);

	Ar.CurrentSlotElementId = Ar.ElementIdGenerator.Generate();

	Ar.Formatter.EnterStreamElement();

	return FSlot(Ar, Depth, Ar.CurrentSlotElementId);
}

FStructuredArchive::FSlot FStructuredArchive::FStream::EnterElement_TextOnly(EArchiveValueType& OutType)
{
	Ar.SetScope(*this);

	Ar.CurrentSlotElementId = Ar.ElementIdGenerator.Generate();

	Ar.Formatter.EnterStreamElement_TextOnly(OutType);

	return FSlot(Ar, Depth, Ar.CurrentSlotElementId);
}

//////////// FStructuredArchive::FMap ////////////

FStructuredArchive::FSlot FStructuredArchive::FMap::EnterElement(FString& Name)
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

	return FSlot(Ar, Depth, Ar.CurrentSlotElementId);
}

FStructuredArchive::FSlot FStructuredArchive::FMap::EnterElement_TextOnly(FString& Name, EArchiveValueType& OutType)
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

	Ar.Formatter.EnterMapElement_TextOnly(Name, OutType);

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

	return FSlot(Ar, Depth, Ar.CurrentSlotElementId);
}

#endif
