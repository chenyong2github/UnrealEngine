// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Serialization/StructuredArchive.h"
#include "Containers/Set.h"
#include "Containers/UnrealString.h"

#if WITH_TEXT_ARCHIVE_SUPPORT

namespace 
{
	/**
	 * First element ID, which is assigned to the root element.
	 */
	static const int RootElementId = 1000;
}

//////////// FStructuredArchive::FContainer ////////////

#if DO_GUARD_SLOW

#define CHECK_UNIQUE_FIELD_NAMES 0

struct FStructuredArchive::FContainer
{
	int  Index                   = 0;
	int  Count                   = 0;
	bool bAttributedValueWritten = false;

#if CHECK_UNIQUE_FIELD_NAMES
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
#if DO_GUARD_SLOW
	, bRequiresStructuralMetadata(true)
#else
	, bRequiresStructuralMetadata(InFormatter.HasDocumentTree())
#endif
	, NextElementId(RootElementId + 1)
	, CurrentSlotElementId(INDEX_NONE)
{
	CurrentScope.Reserve(32);
#if DO_GUARD_SLOW
	CurrentContainer.Reserve(32);
#endif
}

FStructuredArchive::~FStructuredArchive()
{
	Close();

#if DO_GUARD_SLOW
	while(CurrentContainer.Num() > 0)
	{
		delete CurrentContainer.Pop();
	}
#endif
}

FStructuredArchive::FSlot FStructuredArchive::Open()
{
	check(CurrentScope.Num() == 0);
	check(NextElementId == RootElementId + 1);
	check(CurrentSlotElementId == INDEX_NONE);

	CurrentScope.Emplace(RootElementId, EElementType::Root);

	CurrentSlotElementId = NextElementId++;

	return FSlot(*this, 0, CurrentSlotElementId);
}

void FStructuredArchive::Close()
{
	SetScope(0, RootElementId);
}

void FStructuredArchive::EnterSlot(int32 ParentDepth, int32 ElementId)
{
	// If the slot being entered has attributes, enter the value slot first.
	if (ParentDepth + 1 < CurrentScope.Num() && CurrentScope[ParentDepth + 1].Id == ElementId && CurrentScope[ParentDepth + 1].Type == EElementType::AttributedValue)
	{
#if DO_GUARD_SLOW
		checkf(CurrentSlotElementId == INDEX_NONE && !CurrentContainer.Top()->bAttributedValueWritten, TEXT("Attempt to serialize data into an invalid slot"));
		CurrentContainer.Top()->bAttributedValueWritten = true;
#else
		checkf(CurrentSlotElementId == INDEX_NONE, TEXT("Attempt to serialize data into an invalid slot"));
#endif

		SetScope(ParentDepth + 1, ElementId);
		Formatter.EnterAttributedValueValue();
	}
	else
	{
		checkf(ElementId == CurrentSlotElementId, TEXT("Attempt to serialize data into an invalid slot"));
		CurrentSlotElementId = INDEX_NONE;
	}

	CurrentEnteringAttributeState = EEnteringAttributeState::NotEnteringAttribute;
}

int32 FStructuredArchive::EnterSlot(int32 ParentDepth, int32 ElementId, EElementType ElementType)
{
	EnterSlot(ParentDepth, ElementId);

	int32 Result = ParentDepth + 1;

	// If we're entering the value of an attributed slot, we need to return a depth one higher than usual, because we're
	// inside an attributed value container.
	//
	// We don't need to do adjust for attributes, because entering the attribute slot will bump the depth anyway.
	if (ParentDepth + 1 < CurrentScope.Num() &&
		CurrentScope[ParentDepth + 1].Type == EElementType::AttributedValue &&
		CurrentEnteringAttributeState == EEnteringAttributeState::NotEnteringAttribute)
	{
		++Result;
	}

	CurrentScope.Emplace(ElementId, ElementType);
	return Result;
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
#if DO_GUARD_SLOW
			CurrentContainer.Top()->Index++;
#endif
			break;
		case EElementType::Stream:
			Formatter.LeaveStreamElement();
			break;
		case EElementType::Map:
			Formatter.LeaveMapElement();
#if DO_GUARD_SLOW
			CurrentContainer.Top()->Index++;
#endif
			break;
		case EElementType::AttributedValue:
			Formatter.LeaveAttribute();
			break;
		}
	}
}

void FStructuredArchive::SetScope(int32 Depth, int32 ElementId)
{
	// Make sure the scope is valid
	checkf(Depth < CurrentScope.Num() && CurrentScope[Depth].Id == ElementId, TEXT("Invalid scope for writing to archive"));
	checkf(CurrentSlotElementId == INDEX_NONE || GetUnderlyingArchive().IsLoading(), TEXT("Cannot change scope until having written a value to the current slot"));

	// Roll back to the correct scope
	if (bRequiresStructuralMetadata)
	{
		for (int32 CurrentDepth = CurrentScope.Num() - 1; CurrentDepth > Depth; CurrentDepth--)
		{
			// Leave the current element
			const FElement& Element = CurrentScope[CurrentDepth];
			switch (Element.Type)
			{
			case EElementType::Record:
				Formatter.LeaveRecord();
#if DO_GUARD_SLOW
				delete CurrentContainer.Pop(false);
#endif
				break;
			case EElementType::Array:
#if DO_GUARD_SLOW
				checkf(GetUnderlyingArchive().IsLoading() || CurrentContainer.Top()->Index == CurrentContainer.Top()->Count, TEXT("Incorrect number of elements serialized in array"));
#endif
				Formatter.LeaveArray();
#if DO_GUARD_SLOW
				delete CurrentContainer.Pop(false);
#endif
				break;
			case EElementType::Stream:
				Formatter.LeaveStream();
				break;
			case EElementType::Map:
#if DO_GUARD_SLOW
				checkf(CurrentContainer.Top()->Index == CurrentContainer.Top()->Count, TEXT("Incorrect number of elements serialized in map"));
#endif
				Formatter.LeaveMap();
#if DO_GUARD_SLOW
				delete CurrentContainer.Pop(false);
#endif
				break;
			case EElementType::AttributedValue:
				Formatter.LeaveAttributedValue();
#if DO_GUARD_SLOW
				delete CurrentContainer.Pop(false);
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
		CurrentScope.RemoveAt(Depth + 1, CurrentScope.Num() - (Depth + 1));
	}
}

//////////// FStructuredArchive::FSlot ////////////

FStructuredArchive::FRecord FStructuredArchive::FSlot::EnterRecord()
{
	int32 NewDepth = Ar.EnterSlot(Depth, ElementId, EElementType::Record);

#if DO_GUARD_SLOW
	Ar.CurrentContainer.Add(new FContainer(0));
#endif

	Ar.Formatter.EnterRecord();

	return FRecord(Ar, NewDepth, ElementId);
}

FStructuredArchive::FRecord FStructuredArchive::FSlot::EnterRecord_TextOnly(TArray<FString>& OutFieldNames)
{
	int32 NewDepth = Ar.EnterSlot(Depth, ElementId, EElementType::Record);

#if DO_GUARD_SLOW
	Ar.CurrentContainer.Add(new FContainer(0));
#endif

	Ar.Formatter.EnterRecord_TextOnly(OutFieldNames);

	return FRecord(Ar, NewDepth, ElementId);
}

FStructuredArchive::FArray FStructuredArchive::FSlot::EnterArray(int32& Num)
{
	int32 NewDepth = Ar.EnterSlot(Depth, ElementId, EElementType::Array);

	Ar.Formatter.EnterArray(Num);

#if DO_GUARD_SLOW
	Ar.CurrentContainer.Add(new FContainer(Num));
#endif

	return FArray(Ar, NewDepth, ElementId);
}

FStructuredArchive::FStream FStructuredArchive::FSlot::EnterStream()
{
	int32 NewDepth = Ar.EnterSlot(Depth, ElementId, EElementType::Stream);

	Ar.Formatter.EnterStream();

	return FStream(Ar, NewDepth, ElementId);
}

FStructuredArchive::FStream FStructuredArchive::FSlot::EnterStream_TextOnly(int32& OutNumElements)
{
	int32 NewDepth = Ar.EnterSlot(Depth, ElementId, EElementType::Stream);

	Ar.Formatter.EnterStream_TextOnly(OutNumElements);

	return FStream(Ar, NewDepth, ElementId);
}

FStructuredArchive::FMap FStructuredArchive::FSlot::EnterMap(int32& Num)
{
	int32 NewDepth = Ar.EnterSlot(Depth, ElementId, EElementType::Map);

	Ar.Formatter.EnterMap(Num);

#if DO_GUARD_SLOW
	Ar.CurrentContainer.Add(new FContainer(Num));
#endif

	return FMap(Ar, NewDepth, ElementId);
}

FStructuredArchive::FSlot FStructuredArchive::FSlot::EnterAttribute(FArchiveFieldName AttributeName)
{
	check(Ar.CurrentScope.Num() > 0);

	int32 NewDepth = Depth + 1;
	if (NewDepth >= Ar.CurrentScope.Num() || Ar.CurrentScope[NewDepth].Id != ElementId || Ar.CurrentScope[NewDepth].Type != EElementType::AttributedValue)
	{
		int32 NewDepthCheck = Ar.EnterSlot(Depth, ElementId, EElementType::AttributedValue);
		checkSlow(NewDepth == NewDepthCheck);

		Ar.Formatter.EnterAttributedValue();

#if DO_GUARD_SLOW
		Ar.CurrentContainer.Add(new FContainer(0));
#endif
	}

	Ar.CurrentEnteringAttributeState = EEnteringAttributeState::NotEnteringAttribute;

	int32 AttributedValueId = Ar.CurrentScope[NewDepth].Id;

	Ar.SetScope(NewDepth, AttributedValueId);

	Ar.CurrentSlotElementId = Ar.NextElementId++;

#if DO_GUARD_SLOW
#if CHECK_UNIQUE_FIELD_NAMES
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
		int32 NewDepthCheck = Ar.EnterSlot(Depth, ElementId, EElementType::AttributedValue);
		checkSlow(NewDepth == NewDepthCheck);

		Ar.Formatter.EnterAttributedValue();

#if DO_GUARD_SLOW
		Ar.CurrentContainer.Add(new FContainer(0));
#endif
	}

#if DO_GUARD_SLOW
#if CHECK_UNIQUE_FIELD_NAMES
	if (!Ar.GetUnderlyingArchive().IsLoading())
	{
		FContainer& Container = *Ar.CurrentContainer.Top();
		checkf(!Container.KeyNames.Contains(Name.Name), TEXT("Multiple attributes called '%s' serialized into attributed value"), AttributeName.Name);
		Container.KeyNames.Add(Name.Name);
	}
#endif
#endif

	int32 AttributedValueId = Ar.CurrentScope[NewDepth].Id;

	Ar.SetScope(NewDepth, AttributedValueId);

	if (Ar.Formatter.TryEnterAttribute(AttributeName, bEnterWhenWriting))
	{
		Ar.CurrentEnteringAttributeState = EEnteringAttributeState::NotEnteringAttribute;

		Ar.CurrentSlotElementId = Ar.NextElementId++;

		return FSlot(Ar, NewDepth, Ar.CurrentSlotElementId);
	}
	else
	{
		return {};
	}
}

void FStructuredArchive::FSlot::operator<< (char& Value)
{
	Ar.EnterSlot(Depth, ElementId);

	int8 AsInt = Value;
	Ar.Formatter.Serialize(AsInt);
	Value = AsInt;

	Ar.LeaveSlot();
}

void FStructuredArchive::FSlot::operator<< (uint8& Value)
{
	Ar.EnterSlot(Depth, ElementId);
	Ar.Formatter.Serialize(Value);
	Ar.LeaveSlot();
}

void FStructuredArchive::FSlot::operator<< (uint16& Value)
{
	Ar.EnterSlot(Depth, ElementId);
	Ar.Formatter.Serialize(Value);
	Ar.LeaveSlot();
}

void FStructuredArchive::FSlot::operator<< (uint32& Value)
{
	Ar.EnterSlot(Depth, ElementId);
	Ar.Formatter.Serialize(Value);
	Ar.LeaveSlot();
}

void FStructuredArchive::FSlot::operator<< (uint64& Value)
{
	Ar.EnterSlot(Depth, ElementId);
	Ar.Formatter.Serialize(Value);
	Ar.LeaveSlot();
}

void FStructuredArchive::FSlot::operator<< (int8& Value)
{
	Ar.EnterSlot(Depth, ElementId);
	Ar.Formatter.Serialize(Value);
	Ar.LeaveSlot();
}

void FStructuredArchive::FSlot::operator<< (int16& Value)
{
	Ar.EnterSlot(Depth, ElementId);
	Ar.Formatter.Serialize(Value);
	Ar.LeaveSlot();
}

void FStructuredArchive::FSlot::operator<< (int32& Value)
{
	Ar.EnterSlot(Depth, ElementId);
	Ar.Formatter.Serialize(Value);
	Ar.LeaveSlot();
}

void FStructuredArchive::FSlot::operator<< (int64& Value)
{
	Ar.EnterSlot(Depth, ElementId);
	Ar.Formatter.Serialize(Value);
	Ar.LeaveSlot();
}

void FStructuredArchive::FSlot::operator<< (float& Value)
{
	Ar.EnterSlot(Depth, ElementId);
	Ar.Formatter.Serialize(Value);
	Ar.LeaveSlot();
}

void FStructuredArchive::FSlot::operator<< (double& Value)
{
	Ar.EnterSlot(Depth, ElementId);
	Ar.Formatter.Serialize(Value);
	Ar.LeaveSlot();
}

void FStructuredArchive::FSlot::operator<< (bool& Value)
{
	Ar.EnterSlot(Depth, ElementId);
	Ar.Formatter.Serialize(Value);
	Ar.LeaveSlot();
}

void FStructuredArchive::FSlot::operator<< (FString& Value)
{
	Ar.EnterSlot(Depth, ElementId);
	Ar.Formatter.Serialize(Value);
	Ar.LeaveSlot();
}

void FStructuredArchive::FSlot::operator<< (FName& Value)
{
	Ar.EnterSlot(Depth, ElementId);
	Ar.Formatter.Serialize(Value);
	Ar.LeaveSlot();
}

void FStructuredArchive::FSlot::operator<< (UObject*& Value)
{
	Ar.EnterSlot(Depth, ElementId);
	Ar.Formatter.Serialize(Value);
	Ar.LeaveSlot();
}

void FStructuredArchive::FSlot::operator<< (FText& Value)
{
	Ar.EnterSlot(Depth, ElementId);
	Ar.Formatter.Serialize(Value);
	Ar.LeaveSlot();
}

void FStructuredArchive::FSlot::operator<< (FWeakObjectPtr& Value)
{
	Ar.EnterSlot(Depth, ElementId);
	Ar.Formatter.Serialize(Value);
	Ar.LeaveSlot();
}

void FStructuredArchive::FSlot::operator<< (FLazyObjectPtr& Value)
{
	Ar.EnterSlot(Depth, ElementId);
	Ar.Formatter.Serialize(Value);
	Ar.LeaveSlot();
}

void FStructuredArchive::FSlot::operator<< (FSoftObjectPtr& Value)
{
	Ar.EnterSlot(Depth, ElementId);
	Ar.Formatter.Serialize(Value);
	Ar.LeaveSlot();
}

void FStructuredArchive::FSlot::operator<< (FSoftObjectPath& Value)
{
	Ar.EnterSlot(Depth, ElementId);
	Ar.Formatter.Serialize(Value);
	Ar.LeaveSlot();
}

void FStructuredArchive::FSlot::Serialize(TArray<uint8>& Data)
{
	Ar.EnterSlot(Depth, ElementId);
	Ar.Formatter.Serialize(Data);
	Ar.LeaveSlot();
}

void FStructuredArchive::FSlot::Serialize(void* Data, uint64 DataSize)
{
	Ar.EnterSlot(Depth, ElementId);
	Ar.Formatter.Serialize(Data, DataSize);
	Ar.LeaveSlot();
}

//////////// FStructuredArchive::FObject ////////////

FStructuredArchive::FSlot FStructuredArchive::FRecord::EnterField(FArchiveFieldName Name)
{
	Ar.SetScope(Depth, ElementId);

	Ar.CurrentSlotElementId = Ar.NextElementId++;

#if DO_GUARD_SLOW
#if CHECK_UNIQUE_FIELD_NAMES
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
	Ar.SetScope(Depth, ElementId);

#if DO_GUARD_SLOW
#if CHECK_UNIQUE_FIELD_NAMES
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
		Ar.CurrentSlotElementId = Ar.NextElementId++;
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
	Ar.SetScope(Depth, ElementId);

#if DO_GUARD_SLOW
	checkf(Ar.CurrentContainer.Top()->Index < Ar.CurrentContainer.Top()->Count, TEXT("Serialized too many array elements"));
#endif

	Ar.CurrentSlotElementId = Ar.NextElementId++;

	Ar.Formatter.EnterArrayElement();

	return FSlot(Ar, Depth, Ar.CurrentSlotElementId);
}

FStructuredArchive::FSlot FStructuredArchive::FArray::EnterElement_TextOnly(EArchiveValueType& OutType)
{
	Ar.SetScope(Depth, ElementId);

#if DO_GUARD_SLOW
	checkf(Ar.CurrentContainer.Top()->Index < Ar.CurrentContainer.Top()->Count, TEXT("Serialized too many array elements"));
#endif

	Ar.CurrentSlotElementId = Ar.NextElementId++;

	Ar.Formatter.EnterArrayElement_TextOnly(OutType);

	return FSlot(Ar, Depth, Ar.CurrentSlotElementId);
}

//////////// FStructuredArchive::FStream ////////////

FStructuredArchive::FSlot FStructuredArchive::FStream::EnterElement()
{
	Ar.SetScope(Depth, ElementId);

	Ar.CurrentSlotElementId = Ar.NextElementId++;

	Ar.Formatter.EnterStreamElement();

	return FSlot(Ar, Depth, Ar.CurrentSlotElementId);
}

FStructuredArchive::FSlot FStructuredArchive::FStream::EnterElement_TextOnly(EArchiveValueType& OutType)
{
	Ar.SetScope(Depth, ElementId);

	Ar.CurrentSlotElementId = Ar.NextElementId++;

	Ar.Formatter.EnterStreamElement_TextOnly(OutType);

	return FSlot(Ar, Depth, Ar.CurrentSlotElementId);
}

//////////// FStructuredArchive::FMap ////////////

FStructuredArchive::FSlot FStructuredArchive::FMap::EnterElement(FString& Name)
{
	Ar.SetScope(Depth, ElementId);

#if DO_GUARD_SLOW
	checkf(Ar.CurrentContainer.Top()->Index < Ar.CurrentContainer.Top()->Count, TEXT("Serialized too many map elements"));
#endif

	Ar.CurrentSlotElementId = Ar.NextElementId++;

#if DO_GUARD_SLOW
#if CHECK_UNIQUE_FIELD_NAMES
	if(Ar.GetUnderlyingArchive().IsSaving())
	{
		FContainer& Container = *Ar.CurrentContainer.Top();
		checkf(!Container.KeyNames.Contains(Name), TEXT("Multiple keys called '%s' serialized into record"), *Name);
		Container.KeyNames.Add(Name);
	}
#endif
#endif

	Ar.Formatter.EnterMapElement(Name);

#if DO_GUARD_SLOW
#if CHECK_UNIQUE_FIELD_NAMES
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
	Ar.SetScope(Depth, ElementId);

#if DO_GUARD_SLOW
	checkf(Ar.CurrentContainer.Top()->Index < Ar.CurrentContainer.Top()->Count, TEXT("Serialized too many map elements"));
#endif

	Ar.CurrentSlotElementId = Ar.NextElementId++;

#if DO_GUARD_SLOW
#if CHECK_UNIQUE_FIELD_NAMES
	if(Ar.GetUnderlyingArchive().IsSaving())
	{
		FContainer& Container = *Ar.CurrentContainer.Top();
		checkf(!Container.KeyNames.Contains(Name), TEXT("Multiple keys called '%s' serialized into record"), *Name);
		Container.KeyNames.Add(Name);
	}
#endif
#endif

	Ar.Formatter.EnterMapElement_TextOnly(Name, OutType);

#if DO_GUARD_SLOW
#if CHECK_UNIQUE_FIELD_NAMES
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
