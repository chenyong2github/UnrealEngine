// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "MultiplexStorage.h"

FMultiplexStorage::FMultiplexStorage(bool bInUseNames)
	:bUseNameMap(bInUseNames)
{
}

FMultiplexStorage::~FMultiplexStorage()
{
	Reset();
}

FMultiplexStorage& FMultiplexStorage::operator= (const FMultiplexStorage &InOther)
{
	Reset();

	bUseNameMap = InOther.bUseNameMap;
	Data.Append(InOther.Data);
	Addresses.Append(InOther.Addresses);
	ScriptStructs.Append(InOther.ScriptStructs);

	UpdateAddresses();

	for (int32 Index = 0; Index < Addresses.Num(); Index++)
	{
		Construct(Index);

		switch (Addresses[Index].Type)
		{
			case EMultiplexAddressType::Plain:
			{
				// the mem copy through the data array is enough!
				break;
			}
			case EMultiplexAddressType::String:
			{
				FString* TargetStrings = (FString*)GetData(Index);
				FString* SourceStrings = (FString*)InOther.GetData(Index);
				for (int32 ElementIndex = 0; ElementIndex < Addresses[Index].ElementCount; ElementIndex++)
				{
					TargetStrings[ElementIndex] = SourceStrings[ElementIndex];
				}
				break;
			}
			case EMultiplexAddressType::Name:
			{
				// is this right? Nothing to do since name can just be mem copied?
				break;
			}
			case EMultiplexAddressType::Struct:
			{
				UScriptStruct* ScriptStruct = GetScriptStruct(Index);
				ScriptStruct->CopyScriptStruct(GetData(Index), InOther.GetData(Index), Addresses[Index].ElementCount);
				break;
			}
			case EMultiplexAddressType::Invalid:
			{
				break;
			}
		}
	}

	return *this;
}

void FMultiplexStorage::Reset()
{
	for (int32 Index = 0; Index < Addresses.Num(); Index++)
	{
		Destroy(Index);
	}

	Data.Reset();
	Addresses.Reset();
	ScriptStructs.Reset();
	NameMap.Reset();
}

bool FMultiplexStorage::Copy(
	int32 InSourceAddressIndex,
	int32 InTargetAddressIndex,
	int32 InSourceByteOffset,
	int32 InTargetByteOffset,
	int32 InNumBytes)
{
	ensure(Addresses.IsValidIndex(InSourceAddressIndex));
	ensure(Addresses.IsValidIndex(InTargetAddressIndex));

	if (InSourceAddressIndex == InTargetAddressIndex && InSourceByteOffset == InTargetByteOffset)
	{
		return false;
	}

	const FMultiplexAddress& Source = Addresses[InSourceAddressIndex];
	const FMultiplexAddress& Target = Addresses[InTargetAddressIndex];

	int32 SourceStartByte = Source.ByteIndex;
	int32 SourceNumBytes = Source.NumBytes();
	if(InSourceByteOffset != INDEX_NONE)
	{
		ensure(InNumBytes != INDEX_NONE);
		ensure(InSourceByteOffset >= 0 && InSourceByteOffset < Source.NumBytes());
		ensure(InNumBytes > 0 && InSourceByteOffset + InNumBytes <= Source.NumBytes());

		SourceStartByte += InSourceByteOffset;
		SourceNumBytes = InNumBytes;
	}

	int32 TargetStartByte = Target.ByteIndex;
	int32 TargetNumBytes = Target.NumBytes();
	if(InTargetByteOffset != INDEX_NONE)
	{
		ensure(InNumBytes != INDEX_NONE);
		ensure(InTargetByteOffset >= 0 && InTargetByteOffset < Target.NumBytes());
		ensure(InNumBytes > 0 && InTargetByteOffset + InNumBytes <= Target.NumBytes());

		TargetStartByte += InTargetByteOffset;
		TargetNumBytes = InNumBytes;
	}

	if (SourceNumBytes != TargetNumBytes)
	{
		return false;
	}

	switch (Target.Type)
	{
		case EMultiplexAddressType::Plain:
		{
			FMemory::Memcpy(&Data[TargetStartByte], &Data[SourceStartByte], TargetNumBytes);
			break;
		}
		case EMultiplexAddressType::Struct:
		{
			UScriptStruct* ScriptStruct = GetScriptStruct(InTargetAddressIndex);
			int32 NumStructs = TargetNumBytes / ScriptStruct->GetStructureSize();
			ScriptStruct->CopyScriptStruct(&Data[TargetStartByte], &Data[SourceStartByte], NumStructs);
			break;
		}
		case EMultiplexAddressType::Name:
		{
			// is this right? can we just copy a name?
			FMemory::Memcpy(&Data[TargetStartByte], &Data[SourceStartByte], TargetNumBytes);
			break;
		}
		case EMultiplexAddressType::String:
		{
			int32 NumStrings = TargetNumBytes / sizeof(FString);
			FString* TargetStrings = (FString*)&Data[TargetStartByte];
			FString* SourceStrings = (FString*)&Data[SourceStartByte];
			for (int32 Index = 0; Index < NumStrings; Index++)
			{
				TargetStrings[Index] = SourceStrings[Index];
			}
			break;
		}
		case EMultiplexAddressType::Invalid:
		{
			return false;
		}
	}

	return true;
}

bool FMultiplexStorage::Copy(
	const FName& InSourceName,
	const FName& InTargetName,
	int32 InSourceByteOffset,
	int32 InTargetByteOffset,
	int32 InNumBytes)
{
	ensure(bUseNameMap);

	int32 SourceAddressIndex = GetIndex(InSourceName);
	int32 TargetAddressIndex = GetIndex(InTargetName);

	if(SourceAddressIndex == INDEX_NONE || TargetAddressIndex == INDEX_NONE)
	{
		return false;
	}

	return Copy(SourceAddressIndex, TargetAddressIndex, InSourceByteOffset, InTargetByteOffset, InNumBytes);
}

int32 FMultiplexStorage::Allocate(const FName& InNewName, int32 InElementSize, int32 InCount, const void* InDataPtr)
{
	FName Name = InNewName;
	if (bUseNameMap && InNewName == NAME_None)
	{
		const TCHAR* AddressPrefix = TEXT("Address");
		int32 AddressSuffix = 0;
		do
		{
			Name = FName(*FString::Printf(TEXT("%s_%d"), AddressPrefix, AddressSuffix++));
		} while (!IsNameAvailable(Name));
	}

	ensure(InElementSize > 0 && InCount > 0);

	if (bUseNameMap)
	{
		if (!IsNameAvailable(Name))
		{
			return INDEX_NONE;
		}
	}

	FMultiplexAddress NewAddress;
	NewAddress.ByteIndex = Data.Num();
	if (bUseNameMap)
	{
		NewAddress.Name = Name;
	}
	NewAddress.ElementSize = InElementSize;
	NewAddress.ElementCount = InCount;
	NewAddress.Type = EMultiplexAddressType::Plain;

	Data.AddZeroed(NewAddress.NumBytes());

	if (InDataPtr != nullptr)
	{
		FMemory::Memcpy(&Data[NewAddress.ByteIndex], InDataPtr, NewAddress.NumBytes());
	}

	int32 AddressIndex = Addresses.Num();
	Addresses.Add(NewAddress);
	UpdateAddresses();
	return AddressIndex;
}

int32 FMultiplexStorage::Allocate(int32 InElementSize, int32 InCount, const void* InDataPtr)
{
	return Allocate(NAME_None, InElementSize, InCount, InDataPtr);
}

bool FMultiplexStorage::Construct(int32 InAddressIndex, int32 InElementIndex)
{
	ensure(Addresses.IsValidIndex(InAddressIndex));

	const FMultiplexAddress& Address = Addresses[InAddressIndex];
	switch (Address.Type)
	{
		case EMultiplexAddressType::Struct:
		{
			void* DataPtr = (void*)&Data[InElementIndex == INDEX_NONE ? Address.ByteIndex : Address.ByteIndex + InElementIndex * Address.ElementSize];
			int32 Count = InElementIndex == INDEX_NONE ? Address.ElementCount : 1;

			UScriptStruct* ScriptStruct = GetScriptStruct(InAddressIndex);
			ScriptStruct->InitializeStruct(DataPtr, Count);
			break;
		}
		case EMultiplexAddressType::String:
		{
			FString* DataPtr = (FString*)&Data[InElementIndex == INDEX_NONE ? Address.ByteIndex : Address.ByteIndex + InElementIndex * Address.ElementSize];
			int32 Count = InElementIndex == INDEX_NONE ? Address.ElementCount : 1;

			FMemory::Memzero(DataPtr, Address.NumBytes());
			for (int32 Index = 0; Index < Count; Index++)
			{
				DataPtr[Index] = FString();
			}
			break;
		}
		case EMultiplexAddressType::Name:
		{
			FName* DataPtr = (FName*)&Data[InElementIndex == INDEX_NONE ? Address.ByteIndex : Address.ByteIndex + InElementIndex * Address.ElementSize];
			int32 Count = InElementIndex == INDEX_NONE ? Address.ElementCount : 1;

			FMemory::Memzero(DataPtr, Address.NumBytes());
			for (int32 Index = 0; Index < Count; Index++)
			{
				DataPtr[Index] = FName();
			}
			break;
		}
		default:
		{
			return false;
		}
	}

	return true;
}

bool FMultiplexStorage::Destroy(int32 InAddressIndex, int32 InElementIndex)
{
	ensure(Addresses.IsValidIndex(InAddressIndex));

	const FMultiplexAddress& Address = Addresses[InAddressIndex];
	switch (Address.Type)
	{
		case EMultiplexAddressType::Struct:
		{
			void* DataPtr = (void*)&Data[InElementIndex == INDEX_NONE ? Address.ByteIndex : Address.ByteIndex + InElementIndex * Address.ElementSize];
			int32 Count = InElementIndex == INDEX_NONE ? Address.ElementCount : 1;

			UScriptStruct* ScriptStruct = GetScriptStruct(InAddressIndex);
			ScriptStruct->DestroyStruct(DataPtr, Count);
			break;
		}
		case EMultiplexAddressType::String:
		{
			FString* DataPtr = (FString*)&Data[InElementIndex == INDEX_NONE ? Address.ByteIndex : Address.ByteIndex + InElementIndex * Address.ElementSize];
			int32 Count = InElementIndex == INDEX_NONE ? Address.ElementCount : 1;

			for (int32 Index = 0; Index < Count; Index++)
			{
				DataPtr[Index].Empty();
			}
			break;
		}
		case EMultiplexAddressType::Name:
		{
			FName* DataPtr = (FName*)&Data[InElementIndex == INDEX_NONE ? Address.ByteIndex : Address.ByteIndex + InElementIndex * Address.ElementSize];
			int32 Count = InElementIndex == INDEX_NONE ? Address.ElementCount : 1;

			for (int32 Index = 0; Index < Count; Index++)
			{
				DataPtr[Index] = FName();
			}
			break;
		}
		default:
		{
			return false;
		}
	}

	return true;
}

bool FMultiplexStorage::Remove(int32 InAddressIndex)
{
	if (InAddressIndex < 0 || InAddressIndex >= Addresses.Num())
	{
		return false;
	}

	Destroy(InAddressIndex);

	FMultiplexAddress AddressToRemove = Addresses[InAddressIndex];
	Data.RemoveAt(AddressToRemove.ByteIndex, AddressToRemove.NumBytes());
	Addresses.RemoveAt(InAddressIndex);

	for (int32 Index = InAddressIndex; Index < Addresses.Num(); Index++)
	{
		Addresses[Index].ByteIndex -= AddressToRemove.NumBytes();
	}

	UpdateAddresses();
	return true;
}

bool FMultiplexStorage::Remove(const FName& InAddressName)
{
	ensure(bUseNameMap);
	return Remove(GetIndex(InAddressName));
}

FName FMultiplexStorage::Rename(int32 InAddressIndex, const FName& InNewName)
{
	if (Addresses[InAddressIndex].Name == InNewName)
	{
		return Addresses[InAddressIndex].Name;
	}

	if (!IsNameAvailable(InNewName))
	{
		return Addresses[InAddressIndex].Name;
	}

	Addresses[InAddressIndex].Name = InNewName;
	UpdateAddresses();

	return InNewName;
}

FName FMultiplexStorage::Rename(const FName& InOldName, const FName& InNewName)
{
	ensure(bUseNameMap);

	int32 AddressIndex = GetIndex(InOldName);
	if (AddressIndex == INDEX_NONE)
	{
		return NAME_None;
	}

	return Rename(AddressIndex, InNewName);
}

bool FMultiplexStorage::Resize(int32 InAddressIndex, int32 InNewElementCount)
{
	if (InNewElementCount <= 0)
	{
		return Remove(InAddressIndex);
	}

	if (Addresses[InAddressIndex].ElementCount == InNewElementCount)
	{
		return false;
	}

	FMultiplexAddress& Address = Addresses[InAddressIndex];

	if (Address.ElementCount > InNewElementCount) // shrink
	{
		int32 ElementsToRemove = Address.ElementCount - InNewElementCount;
		int32 NumBytesToRemove = Address.ElementSize * ElementsToRemove;
		int32 FirstByteToRemove = Address.ByteIndex + Address.ElementSize * InNewElementCount;

		for (int32 ElementIndex = InNewElementCount; ElementIndex < Address.ElementCount; ElementIndex++)
		{
			Destroy(InAddressIndex, ElementIndex);
		}

		Data.RemoveAt(FirstByteToRemove, NumBytesToRemove);
		Address.ElementCount = InNewElementCount;

		for (int32 AddressIndex = InAddressIndex + 1; AddressIndex < Addresses.Num(); AddressIndex++)
		{
			Addresses[AddressIndex].ByteIndex -= NumBytesToRemove;
		}
	}
	else // grow
	{
		int32 OldElementCount = Address.ElementCount;
		int32 ElementsToAdd = InNewElementCount - Address.ElementCount;
		int32 NumBytesToAdd = Address.ElementSize * ElementsToAdd;
		int32 FirstByteToAdd = Address.ByteIndex + Address.ElementSize * Address.ElementCount;

		Data.InsertZeroed(FirstByteToAdd, NumBytesToAdd);
		Address.ElementCount = InNewElementCount;

		for (int32 ElementIndex = OldElementCount; ElementIndex < InNewElementCount; ElementIndex++)
		{
			Construct(InAddressIndex, ElementIndex);
		}

		for (int32 AddressIndex = InAddressIndex + 1; AddressIndex < Addresses.Num(); AddressIndex++)
		{
			Addresses[AddressIndex].ByteIndex += NumBytesToAdd;
		}
	}

	UpdateAddresses();
	return true;
}

bool FMultiplexStorage::Resize(const FName& InAddressName, int32 InNewElementCount)
{
	ensure(bUseNameMap);

	int32 AddressIndex = GetIndex(InAddressName);
	if (AddressIndex == INDEX_NONE)
	{
		return false;
	}

	return Resize(AddressIndex, InNewElementCount);
}

void FMultiplexStorage::UpdateAddresses()
{
	for (FMultiplexAddress& Address : Addresses)
	{
		Address.Pointer = &Data[Address.ByteIndex];
	}

	if (bUseNameMap)
	{
		NameMap.Reset();
		for (int32 Index = 0; Index < Addresses.Num(); Index++)
		{
			NameMap.Add(Addresses[Index].Name, Index);
		}
	}
}

void FMultiplexStorage::FillWithZeroes(int32 InAddressIndex)
{
	ensure(Addresses.IsValidIndex(InAddressIndex));
	FMemory::Memzero(GetData(InAddressIndex), Addresses[InAddressIndex].NumBytes());
}

int32 FMultiplexStorage::FindOrAddScriptStruct(UScriptStruct* InScriptStruct)
{
	int32 StructIndex = INDEX_NONE;
	if (ScriptStructs.Find(InScriptStruct, StructIndex))
	{
		return StructIndex;
	}
	return ScriptStructs.Add(InScriptStruct);
}
