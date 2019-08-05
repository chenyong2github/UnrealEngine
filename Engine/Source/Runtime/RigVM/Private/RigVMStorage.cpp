// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "RigVMStorage.h"

FRigVMStorage::FRigVMStorage(bool bInUseNames)
	: bUseNameMap(bInUseNames)
	, bIsLiteralStorage(false)
{
}

FRigVMStorage::FRigVMStorage(const FRigVMStorage& Other)
{
	*this = Other;
}

FRigVMStorage::~FRigVMStorage()
{
	Reset();
}

FRigVMStorage& FRigVMStorage::operator= (const FRigVMStorage &InOther)
{
	Reset();

	bUseNameMap = InOther.bUseNameMap;
	Data.Append(InOther.Data);
	Registers.Append(InOther.Registers);
	ScriptStructs.Append(InOther.ScriptStructs);

	UpdateRegisters();

	for (int32 Index = 0; Index < Registers.Num(); Index++)
	{
		Construct(Index);
		Copy(Index, Index, &InOther);
	}

	return *this;
}

void FRigVMStorage::Reset()
{
	for (int32 Index = 0; Index < Registers.Num(); Index++)
	{
		Destroy(Index);
	}

	Data.Reset();
	Registers.Reset();
	ScriptStructs.Reset();
	NameMap.Reset();
}

bool FRigVMStorage::Copy(
	int32 InSourceRegisterIndex,
	int32 InTargetRegisterIndex,
	const FRigVMStorage* InSourceStorage,
	int32 InSourceByteOffset,
	int32 InTargetByteOffset,
	int32 InNumBytes)
{
	if (InSourceStorage == nullptr)
	{
		InSourceStorage = this;
	}

	ensure(InSourceStorage->Registers.IsValidIndex(InSourceRegisterIndex));
	ensure(Registers.IsValidIndex(InTargetRegisterIndex));

	if (InSourceRegisterIndex == InTargetRegisterIndex && InSourceByteOffset == InTargetByteOffset && this == InSourceStorage)
	{
		return false;
	}

	const FRigVMRegister& Source = InSourceStorage->Registers[InSourceRegisterIndex];
	const FRigVMRegister& Target = Registers[InTargetRegisterIndex];

	int32 SourceStartByte = Source.FirstByte();
	int32 SourceNumBytes = Source.NumBytes(false);
	if(InSourceByteOffset != INDEX_NONE)
	{
		ensure(InNumBytes != INDEX_NONE);
		ensure(InSourceByteOffset >= 0 && InSourceByteOffset < Source.NumBytes(false));
		ensure(InNumBytes > 0 && InSourceByteOffset + InNumBytes <= Source.NumBytes(false));

		SourceStartByte += InSourceByteOffset;
		SourceNumBytes = InNumBytes;
	}

	int32 TargetStartByte = Target.FirstByte();
	int32 TargetNumBytes = Target.NumBytes(false);
	if(InTargetByteOffset != INDEX_NONE)
	{
		ensure(InNumBytes != INDEX_NONE);
		ensure(InTargetByteOffset >= 0 && InTargetByteOffset < Target.NumBytes(false));
		ensure(InNumBytes > 0 && InTargetByteOffset + InNumBytes <= Target.NumBytes(false));

		TargetStartByte += InTargetByteOffset;
		TargetNumBytes = InNumBytes;
	}

	if (SourceNumBytes != TargetNumBytes)
	{
		return false;
	}

	switch (Target.Type)
	{
		case ERigVMRegisterType::Plain:
		{
			FMemory::Memcpy(&Data[TargetStartByte], &InSourceStorage->Data[SourceStartByte], TargetNumBytes);
			break;
		}
		case ERigVMRegisterType::Struct:
		{
			UScriptStruct* ScriptStruct = GetScriptStruct(InTargetRegisterIndex);
			int32 NumStructs = TargetNumBytes / ScriptStruct->GetStructureSize();
			ScriptStruct->CopyScriptStruct(&Data[TargetStartByte], &InSourceStorage->Data[SourceStartByte], NumStructs);
			break;
		}
		case ERigVMRegisterType::Name:
		{
			int32 NumNames = TargetNumBytes / sizeof(FName);
			TArrayView<FName> TargetNames((FName*)&Data[TargetStartByte], NumNames);
			TArrayView<FName> SourceNames((FName*)&InSourceStorage->Data[SourceStartByte], NumNames);
			for (int32 Index = 0; Index < NumNames; Index++)
			{
				TargetNames[Index] = SourceNames[Index];
			}
			break;
		}
		case ERigVMRegisterType::String:
		{
			int32 NumStrings = TargetNumBytes / sizeof(FString);
			TArrayView<FString> TargetStrings((FString*)&Data[TargetStartByte], NumStrings);
			TArrayView<FString> SourceStrings((FString*)&InSourceStorage->Data[SourceStartByte], NumStrings);
			for (int32 Index = 0; Index < NumStrings; Index++)
			{
				TargetStrings[Index] = SourceStrings[Index];
			}
			break;
		}
		case ERigVMRegisterType::Invalid:
		{
			return false;
		}
	}

	return true;
}

bool FRigVMStorage::Copy(
	const FName& InSourceName,
	const FName& InTargetName,
	const FRigVMStorage* InSourceStorage,
	int32 InSourceByteOffset,
	int32 InTargetByteOffset,
	int32 InNumBytes)
{
	ensure(bUseNameMap);

	int32 SourceRegisterIndex = GetIndex(InSourceName);
	int32 TargetRegisterIndex = GetIndex(InTargetName);

	if(SourceRegisterIndex == INDEX_NONE || TargetRegisterIndex == INDEX_NONE)
	{
		return false;
	}

	return Copy(SourceRegisterIndex, TargetRegisterIndex, InSourceStorage, InSourceByteOffset, InTargetByteOffset, InNumBytes);
}

int32 FRigVMStorage::Allocate(const FName& InNewName, int32 InElementSize, int32 InCount, const void* InDataPtr, bool bUpdateRegisters)
{
	FName Name = InNewName;
	if (bUseNameMap && InNewName == NAME_None)
	{
		const TCHAR* RegisterPrefix = TEXT("Register");
		int32 RegisterSuffix = 0;
		do
		{
			Name = FName(*FString::Printf(TEXT("%s_%d"), RegisterPrefix, RegisterSuffix++));
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

	FRigVMRegister NewRegister;
	NewRegister.ByteIndex = Data.Num();
	if (bUseNameMap)
	{
		NewRegister.Name = Name;
	}
	NewRegister.ElementSize = InElementSize;
	NewRegister.ElementCount = InCount;
	NewRegister.Type = ERigVMRegisterType::Plain;

	Data.AddZeroed(NewRegister.NumBytes());

	if (InDataPtr != nullptr)
	{
		FMemory::Memcpy(&Data[NewRegister.FirstByte()], InDataPtr, NewRegister.NumBytes());
	}

	int32 RegisterIndex = Registers.Num();
	Registers.Add(NewRegister);

	if (bUpdateRegisters)
	{
		UpdateRegisters();
	}
	return RegisterIndex;
}

int32 FRigVMStorage::Allocate(int32 InElementSize, int32 InCount, const void* InDataPtr, bool bUpdateRegisters)
{
	return Allocate(NAME_None, InElementSize, InCount, InDataPtr, bUpdateRegisters);
}

bool FRigVMStorage::Construct(int32 InRegisterIndex, int32 InElementIndex)
{
	ensure(Registers.IsValidIndex(InRegisterIndex));

	const FRigVMRegister& Register = Registers[InRegisterIndex];
	switch (Register.Type)
	{
		case ERigVMRegisterType::Struct:
		{
			void* DataPtr = (void*)&Data[InElementIndex == INDEX_NONE ? Register.FirstByte() : Register.FirstByte() + InElementIndex * Register.ElementSize];
			int32 Count = InElementIndex == INDEX_NONE ? Register.ElementCount : 1;

			UScriptStruct* ScriptStruct = GetScriptStruct(InRegisterIndex);
			ScriptStruct->InitializeStruct(DataPtr, Count);
			break;
		}
		case ERigVMRegisterType::String:
		{
			FString* DataPtr = (FString*)&Data[InElementIndex == INDEX_NONE ? Register.FirstByte() : Register.FirstByte() + InElementIndex * Register.ElementSize];
			int32 Count = InElementIndex == INDEX_NONE ? Register.ElementCount : 1;

			FMemory::Memzero(DataPtr, Count * Register.ElementSize);
			for (int32 Index = 0; Index < Count; Index++)
			{
				DataPtr[Index] = FString();
			}
			break;
		}
		case ERigVMRegisterType::Name:
		{
			FName* DataPtr = (FName*)&Data[InElementIndex == INDEX_NONE ? Register.FirstByte() : Register.FirstByte() + InElementIndex * Register.ElementSize];
			int32 Count = InElementIndex == INDEX_NONE ? Register.ElementCount : 1;

			FMemory::Memzero(DataPtr, Count * Register.ElementSize);
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

bool FRigVMStorage::Destroy(int32 InRegisterIndex, int32 InElementIndex)
{
	ensure(Registers.IsValidIndex(InRegisterIndex));

	const FRigVMRegister& Register = Registers[InRegisterIndex];
	switch (Register.Type)
	{
		case ERigVMRegisterType::Struct:
		{
			void* DataPtr = (void*)&Data[InElementIndex == INDEX_NONE ? Register.FirstByte() : Register.FirstByte() + InElementIndex * Register.ElementSize];
			int32 Count = InElementIndex == INDEX_NONE ? Register.ElementCount : 1;

			UScriptStruct* ScriptStruct = GetScriptStruct(InRegisterIndex);
			ScriptStruct->DestroyStruct(DataPtr, Count);
			break;
		}
		case ERigVMRegisterType::String:
		{
			FString* DataPtr = (FString*)&Data[InElementIndex == INDEX_NONE ? Register.FirstByte() : Register.FirstByte() + InElementIndex * Register.ElementSize];
			int32 Count = InElementIndex == INDEX_NONE ? Register.ElementCount : 1;

			for (int32 Index = 0; Index < Count; Index++)
			{
				DataPtr[Index].Empty();
			}
			break;
		}
		case ERigVMRegisterType::Name:
		{
			FName* DataPtr = (FName*)&Data[InElementIndex == INDEX_NONE ? Register.FirstByte() : Register.FirstByte() + InElementIndex * Register.ElementSize];
			int32 Count = InElementIndex == INDEX_NONE ? Register.ElementCount : 1;

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

bool FRigVMStorage::Remove(int32 InRegisterIndex)
{
	if (InRegisterIndex < 0 || InRegisterIndex >= Registers.Num())
	{
		return false;
	}

	Destroy(InRegisterIndex);

	FRigVMRegister RegisterToRemove = Registers[InRegisterIndex];
	Data.RemoveAt(RegisterToRemove.ByteIndex, RegisterToRemove.NumBytes());
	Registers.RemoveAt(InRegisterIndex);

	for (int32 Index = InRegisterIndex; Index < Registers.Num(); Index++)
	{
		Registers[Index].ByteIndex -= RegisterToRemove.NumBytes();
	}

	UpdateRegisters();
	return true;
}

bool FRigVMStorage::Remove(const FName& InRegisterName)
{
	ensure(bUseNameMap);
	return Remove(GetIndex(InRegisterName));
}

FName FRigVMStorage::Rename(int32 InRegisterIndex, const FName& InNewName)
{
	if (Registers[InRegisterIndex].Name == InNewName)
	{
		return Registers[InRegisterIndex].Name;
	}

	if (!IsNameAvailable(InNewName))
	{
		return Registers[InRegisterIndex].Name;
	}

	Registers[InRegisterIndex].Name = InNewName;
	UpdateRegisters();

	return InNewName;
}

FName FRigVMStorage::Rename(const FName& InOldName, const FName& InNewName)
{
	ensure(bUseNameMap);

	int32 RegisterIndex = GetIndex(InOldName);
	if (RegisterIndex == INDEX_NONE)
	{
		return NAME_None;
	}

	return Rename(RegisterIndex, InNewName);
}

bool FRigVMStorage::Resize(int32 InRegisterIndex, int32 InNewElementCount)
{
	if (InNewElementCount <= 0)
	{
		return Remove(InRegisterIndex);
	}

	if (Registers[InRegisterIndex].ElementCount == InNewElementCount)
	{
		return false;
	}

	FRigVMRegister& Register = Registers[InRegisterIndex];

	if (Register.ElementCount > InNewElementCount) // shrink
	{
		int32 ElementsToRemove = Register.ElementCount - InNewElementCount;
		int32 NumBytesToRemove = Register.ElementSize * ElementsToRemove;
		int32 FirstByteToRemove = Register.FirstByte() + Register.ElementSize * InNewElementCount;

		for (int32 ElementIndex = InNewElementCount; ElementIndex < Register.ElementCount; ElementIndex++)
		{
			Destroy(InRegisterIndex, ElementIndex);
		}

		Data.RemoveAt(FirstByteToRemove, NumBytesToRemove);
		Register.ElementCount = InNewElementCount;

		for (int32 RegisterIndex = InRegisterIndex + 1; RegisterIndex < Registers.Num(); RegisterIndex++)
		{
			Registers[RegisterIndex].ByteIndex -= NumBytesToRemove;
		}
	}
	else // grow
	{
		int32 OldElementCount = Register.ElementCount;
		int32 ElementsToAdd = InNewElementCount - Register.ElementCount;
		int32 NumBytesToAdd = Register.ElementSize * ElementsToAdd;
		int32 FirstByteToAdd = Register.FirstByte() + Register.ElementSize * Register.ElementCount;

		Data.InsertZeroed(FirstByteToAdd, NumBytesToAdd);
		Register.ElementCount = InNewElementCount;

		for (int32 ElementIndex = OldElementCount; ElementIndex < InNewElementCount; ElementIndex++)
		{
			Construct(InRegisterIndex, ElementIndex);
		}

		for (int32 RegisterIndex = InRegisterIndex + 1; RegisterIndex < Registers.Num(); RegisterIndex++)
		{
			Registers[RegisterIndex].ByteIndex += NumBytesToAdd;
		}
	}

	UpdateRegisters();
	return true;
}

bool FRigVMStorage::Resize(const FName& InRegisterName, int32 InNewElementCount)
{
	ensure(bUseNameMap);

	int32 RegisterIndex = GetIndex(InRegisterName);
	if (RegisterIndex == INDEX_NONE)
	{
		return false;
	}

	return Resize(RegisterIndex, InNewElementCount);
}

void FRigVMStorage::UpdateRegisters()
{
	int32 AlignmentShift = 0;
	for (int32 RegisterIndex = 0; RegisterIndex < Registers.Num(); RegisterIndex++)
	{
		FRigVMRegister& Register = Registers[RegisterIndex];
		Register.ByteIndex += AlignmentShift;

		UScriptStruct* ScriptStruct = GetScriptStruct(RegisterIndex);
		if (ScriptStruct != nullptr)
		{
			UScriptStruct::ICppStructOps* TheCppStructOps = ScriptStruct->GetCppStructOps();
			if (TheCppStructOps != NULL)
			{
				if (!TheCppStructOps->HasZeroConstructor())
				{
					void* Pointer = GetData(RegisterIndex);

					if (Register.AlignmentBytes > 0)
					{
						if (!IsAligned(Pointer, TheCppStructOps->GetAlignment()))
						{
							Data.RemoveAt(Register.ByteIndex, Register.AlignmentBytes);
							AlignmentShift -= Register.AlignmentBytes;
							Register.AlignmentBytes = 0;
							Pointer = GetData(RegisterIndex);
						}
					}

					while (!IsAligned(Pointer, TheCppStructOps->GetAlignment()))
					{
						Data.InsertZeroed(Register.ByteIndex, 1);
						Register.AlignmentBytes++;
						AlignmentShift++;
						Pointer = GetData(RegisterIndex);
					}
				}
			}
		}
	}

	for (int32 RegisterIndex = 0; RegisterIndex < Registers.Num(); RegisterIndex++)
	{
		Registers[RegisterIndex].Pointer = GetData(RegisterIndex);
	}

	if (bUseNameMap)
	{
		NameMap.Reset();
		for (int32 Index = 0; Index < Registers.Num(); Index++)
		{
			NameMap.Add(Registers[Index].Name, Index);
		}
	}
}

void FRigVMStorage::FillWithZeroes(int32 InRegisterIndex)
{
	ensure(Registers.IsValidIndex(InRegisterIndex));
	FMemory::Memzero(GetData(InRegisterIndex), Registers[InRegisterIndex].NumBytes());
}

int32 FRigVMStorage::FindOrAddScriptStruct(UScriptStruct* InScriptStruct)
{
	int32 StructIndex = INDEX_NONE;
	if (ScriptStructs.Find(InScriptStruct, StructIndex))
	{
		return StructIndex;
	}
	return ScriptStructs.Add(InScriptStruct);
}
