// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "MultiplexStorage.h"

FMultiplexStorage::FMultiplexStorage()
{
}

FMultiplexStorage::~FMultiplexStorage()
{
}

FMultiplexStorage& FMultiplexStorage::operator= (const FMultiplexStorage &InOther)
{
	Data.Reset();
	Data.Append(InOther.Data);
	Addresses.Reset();
	Addresses.Append(InOther.Addresses);
	UpdateAddresses();
	return *this;
}

#if WITH_EDITORONLY_DATA

void FMultiplexStorage::Reset()
{

}

int32 FMultiplexStorage::Add(const FName& InNewName, int32 InElementSize, int32 InCount, const void* InData)
{
	ensure(InElementSize > 0 && InCount > 0);

	if (!IsNameAvailable(InNewName))
	{
		return INDEX_NONE;
	}

	FMultiplexAddress NewAddress;
	NewAddress.ByteIndex = Data.Num();
	NewAddress.Name = InNewName;
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
	int32 AddressIndex = GetIndex(InAddressName);
	if (AddressIndex == INDEX_NONE)
	{
		return false;
	}

	return Resize(AddressIndex, InNewElementCount);
}

#endif

void FMultiplexStorage::UpdateAddresses()
{
	for (FMultiplexAddress& Address : Addresses)
	{
		Address.Pointer = &Data[Address.ByteIndex];
	}

#if WITH_EDITORONLY_DATA

	NameMap.Reset();
	for (int32 Index = 0; Index < Addresses.Num(); Index++)
	{
		NameMap.Add(Addresses[Index].Name, Index);
	}

#endif
}
