// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "MultiplexStorage.h"

FMultiplexStorage::FMultiplexStorage(bool bInUseNames)
	:bUseNameMap(bInUseNames)
{
}

FMultiplexStorage::~FMultiplexStorage()
{
}

FMultiplexStorage& FMultiplexStorage::operator= (const FMultiplexStorage &InOther)
{
	bUseNameMap = InOther.bUseNameMap;
	Data.Reset();
	Data.Append(InOther.Data);
	Addresses.Reset();
	Addresses.Append(InOther.Addresses);
	UpdateAddresses();
	return *this;
}

void FMultiplexStorage::Reset()
{
	Data.Reset();
	Addresses.Reset();
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

	FMemory::Memcpy(&Data[TargetStartByte], &Data[SourceStartByte], TargetNumBytes);

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

int32 FMultiplexStorage::Add(int32 InElementSize, int32 InCount, const void* InData)
{
	FName Name = NAME_None;

	if (bUseNameMap)
	{
		const TCHAR* AddressPrefix = TEXT("Address");
		int32 AddressSuffix = 0;
		do
		{
			Name = FName(*FString::Printf(TEXT("%s_%d"), AddressPrefix, AddressSuffix++));
		}
		while (!IsNameAvailable(Name));
	}

	return Add(Name, InElementSize, InCount, InData);
}

int32 FMultiplexStorage::Add(const FName& InNewName, int32 InElementSize, int32 InCount, const void* InData)
{
	if (bUseNameMap)
	{
		ensure(InNewName != NAME_None);
	}
	ensure(InElementSize > 0 && InCount > 0);

	if (bUseNameMap)
	{
		if (!IsNameAvailable(InNewName))
		{
			return INDEX_NONE;
		}
	}

	FMultiplexAddress NewAddress;
	NewAddress.ByteIndex = Data.Num();
	if (bUseNameMap)
	{
		NewAddress.Name = InNewName;
	}
	NewAddress.ElementSize = InElementSize;
	NewAddress.ElementCount = InCount;

	Data.AddZeroed(NewAddress.NumBytes());

	if (InData != nullptr)
	{
		FMemory::Memcpy(&Data[NewAddress.ByteIndex], InData, NewAddress.NumBytes());
	}

	int32 AddressIndex = Addresses.Num();
	Addresses.Add(NewAddress);
	UpdateAddresses();
	return AddressIndex;
}

bool FMultiplexStorage::Remove(int32 InAddressIndex)
{
	if (InAddressIndex < 0 || InAddressIndex >= Addresses.Num())
	{
		return false;
	}

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

	if (Addresses[InAddressIndex].ElementCount > InNewElementCount) // shrink
	{
		int32 ElementsToRemove = Addresses[InAddressIndex].ElementCount - InNewElementCount;
		int32 NumBytesToRemove = Addresses[InAddressIndex].ElementSize * ElementsToRemove;
		int32 FirstByteToRemove = Addresses[InAddressIndex].ByteIndex + Addresses[InAddressIndex].ElementSize * InNewElementCount;

		Data.RemoveAt(FirstByteToRemove, NumBytesToRemove);
		Addresses[InAddressIndex].ElementCount = InNewElementCount;

		for (int32 AddressIndex = InAddressIndex + 1; AddressIndex < Addresses.Num(); AddressIndex++)
		{
			Addresses[AddressIndex].ByteIndex -= NumBytesToRemove;
		}
	}
	else // grow
	{
		int32 ElementsToAdd = InNewElementCount - Addresses[InAddressIndex].ElementCount;
		int32 NumBytesToAdd = Addresses[InAddressIndex].ElementSize * ElementsToAdd;
		int32 FirstByteToAdd = Addresses[InAddressIndex].ByteIndex + Addresses[InAddressIndex].ElementSize * Addresses[InAddressIndex].ElementCount;

		Data.InsertZeroed(FirstByteToAdd, NumBytesToAdd);
		Addresses[InAddressIndex].ElementCount = InNewElementCount;

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
