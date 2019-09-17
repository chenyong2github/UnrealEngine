// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "RigVMCore/RigVMMemory.h"
#include "UObject/AnimObjectVersion.h"
#include "UObject/PropertyPortFlags.h"
#include "UObject/Package.h"

bool FRigVMRegister::Serialize(FArchive& Ar)
{
	Ar.UsingCustomVersion(FAnimObjectVersion::GUID);

	if (Ar.CustomVer(FAnimObjectVersion::GUID) < FAnimObjectVersion::StoreMarkerNamesOnSkeleton)
	{
		return false;
	}

	Ar << Type;
	Ar << ByteIndex;
	Ar << ElementSize;
	Ar << ElementCount;
	Ar << SliceIndex;
	Ar << SliceCount;
	Ar << AlignmentBytes;
	Ar << TrailingBytes;
	Ar << Name;
	Ar << ScriptStructIndex;

	return true;
}

bool FRigVMRegisterOffset::Serialize(FArchive& Ar)
{
	Ar.UsingCustomVersion(FAnimObjectVersion::GUID);

	if (Ar.CustomVer(FAnimObjectVersion::GUID) < FAnimObjectVersion::StoreMarkerNamesOnSkeleton)
	{
		return false;
	}

	Ar << Segments;
	Ar << Type;
	Ar << CPPType;
	Ar << ScriptStructPath;
	Ar << ElementSize;

	return true;
}

FRigVMRegisterOffset::FRigVMRegisterOffset(UScriptStruct* InScriptStruct, const FString& InSegmentPath, int32 InInitialOffset, uint16 InElementSize)
	: Segments()
	, Type(ERigVMRegisterType::Plain)
	, CPPType()
	, ScriptStruct(nullptr)
	, ScriptStructPath()
	, ElementSize(InElementSize)
{
	struct FRigVMRegisterOffsetBuilder
	{
		static void WalkStruct(UStruct* InStruct, const FString& InPath, FRigVMRegisterOffset& Offset)
		{
			FString Left, Right;
			if (!InPath.Split(TEXT("."), &Left, &Right))
			{
				Left = InPath;
			}

			UProperty* Property = InStruct->FindPropertyByName(*Left);
			check(Property)

			int32 SegmentIndex = Property->GetOffset_ReplaceWith_ContainerPtrToValuePtr();
			if (Offset.Segments.Num() > 0)
			{
				if (Offset.Segments.Last() >= 0)
				{
					Offset.Segments[Offset.Segments.Num() - 1] += SegmentIndex;
				}
				else
				{
					Offset.Segments.Add(SegmentIndex);
				}
			}
			else
			{
				Offset.Segments.Add(SegmentIndex);
			}

			if (!Right.IsEmpty())
			{
				if (UStructProperty* StructProperty = Cast<UStructProperty>(Property))
				{
					WalkStruct(StructProperty->Struct, Right, Offset);
				}
				else if (UArrayProperty* ArrayProperty = Cast<UArrayProperty>(Property))
				{
					WalkArray(ArrayProperty, Right, Offset);
				}
			}
			else
			{
				Offset.CPPType = *Property->GetCPPType();
				Offset.ElementSize = Property->ElementSize;

				if (UArrayProperty* ArrayProperty = Cast<UArrayProperty>(Property))
				{
					Offset.Segments.Add(-1);
					Property = ArrayProperty->Inner;
				}

				if (UStructProperty* StructProperty = Cast<UStructProperty>(Property))
				{
					Offset.ScriptStruct = StructProperty->Struct;
					Offset.Type = ERigVMRegisterType::Struct;
				}
				else
				{
					Offset.Type = ERigVMRegisterType::Plain;
				}
			}
		}

		static void WalkArray(UArrayProperty* InArrayProperty, const FString& InPath, FRigVMRegisterOffset& Offset)
		{
			FString Left, Right;
			if (!InPath.Split(TEXT("."), &Left, &Right))
			{
				Left = InPath;
			}

			int32 ArrayIndex = FCString::Atoi(*Left);
			int32 SegmentIndex = -1 - InArrayProperty->Inner->ElementSize * ArrayIndex;

			if (Offset.Segments.Num() > 0)
			{
				if (Offset.Segments.Last() == 0)
				{
					Offset.Segments[Offset.Segments.Num() - 1] = SegmentIndex;
				}
				else
				{
					Offset.Segments.Add(SegmentIndex);
				}
			}
			else
			{
				Offset.Segments.Add(SegmentIndex);
			}

			if (!Right.IsEmpty())
			{
				if (UStructProperty* StructProperty = Cast<UStructProperty>(InArrayProperty->Inner))
				{
					WalkStruct(StructProperty->Struct, Right, Offset);
				}
				else if (UArrayProperty* ArrayProperty = Cast<UArrayProperty>(InArrayProperty->Inner))
				{
					WalkArray(ArrayProperty, Right, Offset);
				}
			}
			else
			{
				Offset.CPPType = *InArrayProperty->Inner->GetCPPType();
				Offset.ElementSize = InArrayProperty->Inner->ElementSize;

				if (UArrayProperty* ArrayProperty = Cast<UArrayProperty>(InArrayProperty->Inner))
				{
					Offset.Segments.Add(-1);
					InArrayProperty = ArrayProperty;
				}

				if (UStructProperty* StructProperty = Cast<UStructProperty>(InArrayProperty->Inner))
				{
					Offset.ScriptStruct = StructProperty->Struct;
					Offset.Type = ERigVMRegisterType::Struct;
				}
				else
				{
					Offset.Type = ERigVMRegisterType::Plain;
				}
			}

		}
	};

	Segments.Add(InInitialOffset);

	if (!InSegmentPath.IsEmpty() || InScriptStruct != nullptr)
	{
		ensure(!InSegmentPath.IsEmpty());
		check(InScriptStruct)
		FString SegmentPath = InSegmentPath;
		SegmentPath = SegmentPath.Replace(TEXT("["), TEXT("."));
		SegmentPath = SegmentPath.Replace(TEXT("]"), TEXT("."));
		FRigVMRegisterOffsetBuilder::WalkStruct(InScriptStruct, SegmentPath, *this);
		if (ScriptStruct != nullptr)
		{
			ScriptStructPath = *ScriptStruct->GetPathName();
		}
		if (Type == ERigVMRegisterType::Plain)
		{
			if (CPPType == TEXT("FName"))
			{
				Type = ERigVMRegisterType::Name;
			}
			else if (CPPType == TEXT("FString"))
			{
				Type = ERigVMRegisterType::String;
			}
		}
	}

	ensure(ElementSize > 0);
}

uint8* FRigVMRegisterOffset::GetData(uint8* InContainer) const
{
	uint8* Data = InContainer;
	for(int32 SegmentIndex : Segments)
	{
		if (SegmentIndex < 0)
		{
			int32 ArrayOffset = (-SegmentIndex) - 1;
			TArray<uint8>* ArrayPtr = (TArray<uint8>*)Data;
			Data = ArrayPtr->GetData() + ArrayOffset;
		}
		else
		{
			Data = Data + SegmentIndex;
		}
	}
	return Data;
}

bool FRigVMRegisterOffset::operator == (const FRigVMRegisterOffset& InOther) const
{
	if (Segments.Num() != InOther.Segments.Num())
	{
		return false;
	}
	if (ElementSize != InOther.ElementSize)
	{
		return false;
	}
	if (GetScriptStruct() != InOther.GetScriptStruct())
	{
		return false;
	}
	for (int32 Index = 0; Index < Segments.Num(); Index++)
	{
		if (Segments[Index] != InOther.Segments[Index])
		{
			return false;
		}
	}
	return true;
}

UScriptStruct* FRigVMRegisterOffset::GetScriptStruct() const
{
	if (ScriptStruct == nullptr)
	{
		if (ScriptStructPath != NAME_None)
		{
			FRigVMRegisterOffset* MutableThis = (FRigVMRegisterOffset*)this;
			MutableThis->ScriptStruct = FindObject<UScriptStruct>(ANY_PACKAGE, *ScriptStructPath.ToString());
		}
	}
	return ScriptStruct;
}

uint16 FRigVMRegisterOffset::GetElementSize() const
{
	return ElementSize;
}

FRigVMMemoryContainer::FRigVMMemoryContainer(bool bInUseNames)
	: bUseNameMap(bInUseNames)
	, MemoryType(ERigVMMemoryType::Work)
{
}

FRigVMMemoryContainer::FRigVMMemoryContainer(const FRigVMMemoryContainer& Other)
{
	*this = Other;
}

FRigVMMemoryContainer::~FRigVMMemoryContainer()
{
	Reset();
}

FRigVMMemoryContainer& FRigVMMemoryContainer::operator= (const FRigVMMemoryContainer &InOther)
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

bool FRigVMMemoryContainer::Serialize(FArchive& Ar)
{
	Ar.UsingCustomVersion(FAnimObjectVersion::GUID);

 	if (Ar.CustomVer(FAnimObjectVersion::GUID) < FAnimObjectVersion::StoreMarkerNamesOnSkeleton)
 	{
 		return false;
 	}

	if (Ar.IsLoading())
	{
		for (int32 RegisterIndex = 0; RegisterIndex < Registers.Num(); RegisterIndex++)
		{
			Registers[RegisterIndex].MoveToFirstSlice();
			Destroy(RegisterIndex);
		}
	}
	else
	{
		for (int32 RegisterIndex = 0; RegisterIndex < Registers.Num(); RegisterIndex++)
		{
			Registers[RegisterIndex].MoveToFirstSlice();
		}
	}

	Ar << bUseNameMap;
	Ar << MemoryType;
	Ar << Registers;
	Ar << RegisterOffsets;

	if (Ar.IsLoading())
	{
		ScriptStructs.Reset();
		TArray<FString> ScriptStructPaths;
		Ar << ScriptStructPaths;
		
		for (const FString& ScriptStructPath : ScriptStructPaths)
		{
			UScriptStruct* ScriptStruct = FindObject<UScriptStruct>(nullptr, *ScriptStructPath);
			ensure(ScriptStruct != nullptr);
			ScriptStructs.Add(ScriptStruct);
		}

		uint64 TotalBytes = 0;
		Ar << TotalBytes;
		
		Data.SetNumZeroed(TotalBytes);

		for (int32 RegisterIndex = 0; RegisterIndex < Registers.Num(); RegisterIndex++)
		{
			Registers[RegisterIndex].MoveToFirstSlice();
			Construct(RegisterIndex);
		}

		for (const FRigVMRegister& Register : Registers)
		{
			switch (Register.Type)
			{
				case ERigVMRegisterType::Plain:
				{
					TArray<uint8> View;
					Ar << View;
					ensure(View.Num() == Register.GetAllocatedBytes());
					FMemory::Memcpy(&Data[Register.GetFirstAllocatedByte()], View.GetData(), View.Num());
					break;
				}
				case ERigVMRegisterType::Name:
				{
					TArray<FName> View;
					Ar << View;
					ensure(View.Num() == Register.GetTotalElementCount());
					for (uint16 ElementIndex = 0; ElementIndex < Register.GetTotalElementCount(); ElementIndex++)
					{
						*((FName*)&Data[Register.GetWorkByteIndex() + Register.ElementSize * ElementIndex]) = View[ElementIndex];
					}
					break;
				}
				case ERigVMRegisterType::String:
				{
					TArray<FString> View;
					Ar << View;
					ensure(View.Num() == Register.GetTotalElementCount());
					for (uint16 ElementIndex = 0; ElementIndex < Register.GetTotalElementCount(); ElementIndex++)
					{
						*((FString*)&Data[Register.GetWorkByteIndex() + Register.ElementSize * ElementIndex]) = View[ElementIndex];
					}
					break;
				}
				case ERigVMRegisterType::Struct:
				{
					TArray<FString> View;
					Ar << View;
					ensure(View.Num() == Register.GetTotalElementCount());

					uint8* DataPtr = &Data[Register.GetWorkByteIndex()];
					UScriptStruct* ScriptStruct = ScriptStructs[Register.ScriptStructIndex];
					for (uint16 ElementIndex = 0; ElementIndex < Register.GetTotalElementCount(); ElementIndex++)
					{
						ScriptStruct->ImportText(*View[ElementIndex], DataPtr, nullptr, PPF_None, nullptr, ScriptStruct->GetName());
						DataPtr += Register.ElementSize;
					}
					break;
				}
			}
		}

		UpdateRegisters();
	}
	else
	{
		TArray<FString> ScriptStructPaths;
		for (UScriptStruct* ScriptStruct : ScriptStructs)
		{
			ScriptStructPaths.Add(ScriptStruct->GetPathName());
		}
		Ar << ScriptStructPaths;

		uint64 TotalBytes = Data.Num();
		Ar << TotalBytes;

		for (FRigVMRegister& Register : Registers)
		{
			Register.MoveToFirstSlice();

			switch(Register.Type)
			{
				case ERigVMRegisterType::Plain:
				{
					TArray<uint8> View;
					View.Append(&Data[Register.GetFirstAllocatedByte()], Register.GetAllocatedBytes());
					Ar << View;
					break;
				}
				case ERigVMRegisterType::Name:
				{
					TArray<FName> View;
					View.Append((FName*)&Data[Register.GetWorkByteIndex()], Register.GetTotalElementCount());
					Ar << View;
					break;
				}
				case ERigVMRegisterType::String:
				{
					TArray<FString> View;
					View.Append((FString*)&Data[Register.GetWorkByteIndex()], Register.GetTotalElementCount());
					Ar << View;
					break;
				}
				case ERigVMRegisterType::Struct:
				{
					uint8* DataPtr = &Data[Register.GetWorkByteIndex()];
					UScriptStruct* ScriptStruct = ScriptStructs[Register.ScriptStructIndex];

					TArray<FString> View;
					for (uint16 ElementIndex = 0; ElementIndex < Register.GetTotalElementCount(); ElementIndex++)
					{
						FString Value;
						ScriptStruct->ExportText(Value, DataPtr, nullptr, nullptr, PPF_None, nullptr);
						View.Add(Value);
						DataPtr += Register.ElementSize;
					}

					Ar << View;
					break;
				}
			}
		}
	}

	return true;
}

void FRigVMMemoryContainer::Reset()
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

bool FRigVMMemoryContainer::Copy(
	int32 InSourceRegisterIndex,
	int32 InTargetRegisterIndex,
	const FRigVMMemoryContainer* InSourceMemory,
	int32 InSourceRegisterOffset,
	int32 InTargetRegisterOffset)
{
	if (InSourceMemory == nullptr)
	{
		InSourceMemory = this;
	}

	ensure(InSourceMemory->Registers.IsValidIndex(InSourceRegisterIndex));
	ensure(Registers.IsValidIndex(InTargetRegisterIndex));

	if (InSourceRegisterIndex == InTargetRegisterIndex && InSourceRegisterOffset == InTargetRegisterOffset && this == InSourceMemory)
	{
		return false;
	}

	const FRigVMRegister& Source = InSourceMemory->Registers[InSourceRegisterIndex];
	const FRigVMRegister& Target = Registers[InTargetRegisterIndex];

	const uint8* SourcePtr = InSourceMemory->GetData(InSourceRegisterIndex, InSourceRegisterOffset);
	uint8* TargetPtr = GetData(InTargetRegisterIndex, InTargetRegisterOffset);
	uint16 NumBytes = Target.GetNumBytesPerSlice();

	ERigVMRegisterType TargetType = Target.Type;
	if (InTargetRegisterOffset != INDEX_NONE)
	{
		TargetType = RegisterOffsets[InTargetRegisterOffset].GetType();
		NumBytes = RegisterOffsets[InTargetRegisterOffset].GetElementSize();
	}

	switch (TargetType)
	{
		case ERigVMRegisterType::Plain:
		{
			FMemory::Memcpy(TargetPtr, SourcePtr, NumBytes);
			break;
		}
		case ERigVMRegisterType::Struct:
		{
			UScriptStruct* ScriptStruct = GetScriptStruct(InTargetRegisterIndex, InTargetRegisterOffset);
			int32 NumStructs = NumBytes / ScriptStruct->GetStructureSize();
			ScriptStruct->CopyScriptStruct(TargetPtr, SourcePtr, NumStructs);
			break;
		}
		case ERigVMRegisterType::Name:
		{
			int32 NumNames = NumBytes / sizeof(FName);
			TArrayView<FName> TargetNames((FName*)TargetPtr, NumNames);
			TArrayView<FName> SourceNames((FName*)SourcePtr, NumNames);
			for (int32 Index = 0; Index < NumNames; Index++)
			{
				TargetNames[Index] = SourceNames[Index];
			}
			break;
		}
		case ERigVMRegisterType::String:
		{
			int32 NumStrings = NumBytes / sizeof(FString);
			TArrayView<FString> TargetStrings((FString*)TargetPtr, NumStrings);
			TArrayView<FString> SourceStrings((FString*)SourcePtr, NumStrings);
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

bool FRigVMMemoryContainer::Copy(
	const FName& InSourceName,
	const FName& InTargetName,
	const FRigVMMemoryContainer* InSourceMemory,
	int32 InSourceRegisterOffset,
	int32 InTargetRegisterOffset)
{
	ensure(bUseNameMap);

	int32 SourceRegisterIndex = GetIndex(InSourceName);
	int32 TargetRegisterIndex = GetIndex(InTargetName);

	if(SourceRegisterIndex == INDEX_NONE || TargetRegisterIndex == INDEX_NONE)
	{
		return false;
	}

	return Copy(SourceRegisterIndex, TargetRegisterIndex, InSourceMemory, InSourceRegisterOffset, InTargetRegisterOffset);
}

bool FRigVMMemoryContainer::Copy(
	const FRigVMOperand& InSourceOperand,
	const FRigVMOperand& InTargetOperand,
	const FRigVMMemoryContainer* InSourceMemory)
{
	return Copy(InSourceOperand.GetRegisterIndex(), InTargetOperand.GetRegisterIndex(), InSourceMemory, InSourceOperand.GetRegisterOffset(), InTargetOperand.GetRegisterOffset());
}

int32 FRigVMMemoryContainer::Allocate(const FName& InNewName, int32 InElementSize, int32 InElementCount, int32 InSliceCount, const uint8* InDataPtr, bool bUpdateRegisters)
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

	ensure(InElementSize > 0 && InElementCount > 0 && InSliceCount > 0);

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
	NewRegister.ElementSize = (uint16)InElementSize;
	NewRegister.ElementCount = (uint16)InElementCount;
	NewRegister.SliceCount = (uint16)InSliceCount;
	NewRegister.Type = ERigVMRegisterType::Plain;

	Data.AddZeroed(NewRegister.GetAllocatedBytes());

	if (InDataPtr != nullptr)
	{
		NewRegister.MoveToFirstSlice();
		for (uint16 SliceIndex = 0; SliceIndex < NewRegister.SliceCount; SliceIndex++)
		{
			FMemory::Memcpy(&Data[NewRegister.GetWorkByteIndex()], InDataPtr, NewRegister.GetNumBytesPerSlice());
			NewRegister.MoveToNextSlice();
		}
		NewRegister.MoveToFirstSlice();
	}

	int32 RegisterIndex = Registers.Num();
	Registers.Add(NewRegister);

	if (bUpdateRegisters)
	{
		UpdateRegisters();
	}
	return RegisterIndex;
}

int32 FRigVMMemoryContainer::Allocate(int32 InElementSize, int32 InElementCount, int32 InSliceCount, const uint8* InDataPtr, bool bUpdateRegisters)
{
	return Allocate(NAME_None, InElementSize, InElementCount, InSliceCount, InDataPtr, bUpdateRegisters);
}

bool FRigVMMemoryContainer::Construct(int32 InRegisterIndex, int32 InElementIndex)
{
	ensure(Registers.IsValidIndex(InRegisterIndex));

	const FRigVMRegister& Register = Registers[InRegisterIndex];
	switch (Register.Type)
	{
		case ERigVMRegisterType::Struct:
		{
			uint8* DataPtr = &Data[InElementIndex == INDEX_NONE ? Register.GetWorkByteIndex() : Register.GetWorkByteIndex() + InElementIndex * Register.ElementSize];
			int32 Count = InElementIndex == INDEX_NONE ? Register.GetTotalElementCount() : 1;

			UScriptStruct* ScriptStruct = GetScriptStruct(InRegisterIndex);
			ScriptStruct->InitializeStruct(DataPtr, Count);
			break;
		}
		case ERigVMRegisterType::String:
		{
			FString* DataPtr = (FString*)&Data[InElementIndex == INDEX_NONE ? Register.GetWorkByteIndex() : Register.GetWorkByteIndex() + InElementIndex * Register.ElementSize];
			int32 Count = InElementIndex == INDEX_NONE ? Register.GetTotalElementCount() : 1;

			FMemory::Memzero(DataPtr, Count * Register.ElementSize);
			for (int32 Index = 0; Index < Count; Index++)
			{
				DataPtr[Index] = FString();
			}
			break;
		}
		case ERigVMRegisterType::Name:
		{
			FName* DataPtr = (FName*)&Data[InElementIndex == INDEX_NONE ? Register.GetWorkByteIndex() : Register.GetWorkByteIndex() + InElementIndex * Register.ElementSize];
			int32 Count = InElementIndex == INDEX_NONE ? Register.GetTotalElementCount() : 1;

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

bool FRigVMMemoryContainer::Destroy(int32 InRegisterIndex, int32 InElementIndex)
{
	ensure(Registers.IsValidIndex(InRegisterIndex));

	FRigVMRegister& Register = Registers[InRegisterIndex];

	if (InElementIndex == INDEX_NONE)
	{
		Register.MoveToFirstSlice();
	}

	switch (Register.Type)
	{
		case ERigVMRegisterType::Struct:
		{
			uint8* DataPtr = &Data[InElementIndex == INDEX_NONE ? Register.GetWorkByteIndex() : Register.GetWorkByteIndex() + InElementIndex * Register.ElementSize];
			int32 Count = InElementIndex == INDEX_NONE ? Register.GetTotalElementCount() : 1;

			UScriptStruct* ScriptStruct = GetScriptStruct(InRegisterIndex);
			ScriptStruct->DestroyStruct(DataPtr, Count);
			break;
		}
		case ERigVMRegisterType::String:
		{
			FString* DataPtr = (FString*)&Data[InElementIndex == INDEX_NONE ? Register.GetWorkByteIndex() : Register.GetWorkByteIndex() + InElementIndex * Register.ElementSize];
			int32 Count = InElementIndex == INDEX_NONE ? Register.GetTotalElementCount() : 1;

			for (int32 Index = 0; Index < Count; Index++)
			{
				DataPtr[Index] = FString();
			}
			break;
		}
		case ERigVMRegisterType::Name:
		{
			FName* DataPtr = (FName*)&Data[InElementIndex == INDEX_NONE ? Register.GetWorkByteIndex() : Register.GetWorkByteIndex() + InElementIndex * Register.ElementSize];
			int32 Count = InElementIndex == INDEX_NONE ? Register.GetTotalElementCount() : 1;

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

bool FRigVMMemoryContainer::Remove(int32 InRegisterIndex)
{
	if (InRegisterIndex < 0 || InRegisterIndex >= Registers.Num())
	{
		return false;
	}

	Destroy(InRegisterIndex);

	FRigVMRegister RegisterToRemove = Registers[InRegisterIndex];
	Data.RemoveAt(RegisterToRemove.ByteIndex, RegisterToRemove.GetAllocatedBytes());
	Registers.RemoveAt(InRegisterIndex);

	for (int32 Index = InRegisterIndex; Index < Registers.Num(); Index++)
	{
		Registers[Index].ByteIndex -= RegisterToRemove.GetAllocatedBytes();
	}

	UpdateRegisters();
	return true;
}

bool FRigVMMemoryContainer::Remove(const FName& InRegisterName)
{
	ensure(bUseNameMap);
	return Remove(GetIndex(InRegisterName));
}

FName FRigVMMemoryContainer::Rename(int32 InRegisterIndex, const FName& InNewName)
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

FName FRigVMMemoryContainer::Rename(const FName& InOldName, const FName& InNewName)
{
	ensure(bUseNameMap);

	int32 RegisterIndex = GetIndex(InOldName);
	if (RegisterIndex == INDEX_NONE)
	{
		return NAME_None;
	}

	return Rename(RegisterIndex, InNewName);
}

bool FRigVMMemoryContainer::Resize(int32 InRegisterIndex, int32 InNewElementCount, int32 InNewSliceCount)
{
	ensure(Registers.IsValidIndex(InRegisterIndex));
	ensure(Registers[InRegisterIndex].TrailingBytes == 0);

	if (InNewElementCount <= 0 || InNewSliceCount < 0)
	{
		return Remove(InRegisterIndex);
	}

	uint32 NewTotalCount = (uint32)InNewElementCount * (uint32)InNewSliceCount;
	if (Registers[InRegisterIndex].GetTotalElementCount() == NewTotalCount)
	{
		return false;
	}

	FRigVMRegister& Register = Registers[InRegisterIndex];
	Register.MoveToFirstSlice();

	if (Register.GetTotalElementCount() > NewTotalCount) // shrink
	{
		int32 ElementsToRemove = Register.GetTotalElementCount() - NewTotalCount;
		int32 NumBytesToRemove = Register.ElementSize * ElementsToRemove;
		int32 FirstByteToRemove = Register.GetWorkByteIndex() + Register.ElementSize * NewTotalCount;

		for (uint32 ElementIndex = NewTotalCount; ElementIndex < Register.GetTotalElementCount(); ElementIndex++)
		{
			Destroy(InRegisterIndex, (uint32)ElementIndex);
		}

		Data.RemoveAt(FirstByteToRemove, NumBytesToRemove);
		Register.ElementCount = (uint32)InNewElementCount;
		Register.SliceCount = (uint32)InNewSliceCount;

		for (int32 RegisterIndex = InRegisterIndex + 1; RegisterIndex < Registers.Num(); RegisterIndex++)
		{
			Registers[RegisterIndex].ByteIndex -= NumBytesToRemove;
		}
	}
	else // grow
	{
		int32 OldElementCount = Register.GetTotalElementCount();
		int32 ElementsToAdd = NewTotalCount - Register.GetTotalElementCount();
		int32 NumBytesToAdd = Register.ElementSize * ElementsToAdd;
		int32 FirstByteToAdd = Register.GetWorkByteIndex() + Register.ElementSize * Register.GetTotalElementCount();

		Data.InsertZeroed(FirstByteToAdd, NumBytesToAdd);
		Register.ElementCount = (uint32)InNewElementCount;
		Register.SliceCount = (uint32)InNewSliceCount;

		for (uint32 ElementIndex = OldElementCount; ElementIndex < NewTotalCount; ElementIndex++)
		{
			Construct(InRegisterIndex, (int32)ElementIndex);
		}

		for (int32 RegisterIndex = InRegisterIndex + 1; RegisterIndex < Registers.Num(); RegisterIndex++)
		{
			Registers[RegisterIndex].ByteIndex += NumBytesToAdd;
		}
	}

	UpdateRegisters();
	return true;
}

bool FRigVMMemoryContainer::Resize(const FName& InRegisterName, int32 InNewElementCount, int32 InNewSliceCount)
{
	ensure(bUseNameMap);

	int32 RegisterIndex = GetIndex(InRegisterName);
	if (RegisterIndex == INDEX_NONE)
	{
		return false;
	}

	return Resize(RegisterIndex, InNewElementCount, InNewSliceCount);
}

bool FRigVMMemoryContainer::ChangeRegisterType(int32 InRegisterIndex, ERigVMRegisterType InNewType, int32 InElementSize, const uint8* InDataPtr, int32 InNewElementCount, int32 InNewSliceCount)
{
	ensure(Registers.IsValidIndex(InRegisterIndex));
	
	FRigVMRegister& Register = Registers[InRegisterIndex];
	ensure(Register.AlignmentBytes == 0);
	ensure(InNewType == ERigVMRegisterType::Name || InNewType == ERigVMRegisterType::String || InNewType == ERigVMRegisterType::Plain);

	Register.MoveToFirstSlice();

	if (Register.Type == InNewType && Register.ElementSize == InElementSize && Register.ElementCount == InNewElementCount && Register.SliceCount == InNewSliceCount)
	{
		return false;
	}

	Destroy(InRegisterIndex);

	uint16 OldAllocatedBytes = Register.GetAllocatedBytes();
	uint16 NewAllocatedBytes = (uint16)InElementSize * (uint16)InNewElementCount * (uint16)InNewSliceCount;
	ensure(NewAllocatedBytes <= OldAllocatedBytes);

	Register.Type = InNewType;
	Register.ElementSize = (uint16)InElementSize;
	Register.ElementCount = (uint16)InNewElementCount;
	Register.SliceCount = (uint16)InNewSliceCount;
	Register.TrailingBytes = OldAllocatedBytes - NewAllocatedBytes;

	Construct(InRegisterIndex);

	if (InDataPtr != nullptr)
	{
		for (int32 SliceIndex = 0; SliceIndex < Register.SliceCount; SliceIndex++)
		{
			FMemory::Memcpy(GetData(InRegisterIndex), InDataPtr, Register.GetNumBytesPerSlice());
			Register.MoveToNextSlice();
		}
		Register.MoveToFirstSlice();
	}

	return true;
}

int32 FRigVMMemoryContainer::GetOrAddRegisterOffset(int32 InRegisterIndex, UScriptStruct* InScriptStruct, const FString& InSegmentPath, int32 InInitialOffset, int32 InElementSize)
{
	if ((InScriptStruct == nullptr || InSegmentPath.IsEmpty()) && InInitialOffset == 0)
	{
		return INDEX_NONE;
	}

	ensure(Registers.IsValidIndex(InRegisterIndex));

	if (InElementSize == 0)
	{
		InElementSize = Registers[InRegisterIndex].GetNumBytesPerSlice();
	}

	FRigVMRegisterOffset Offset(InScriptStruct, InSegmentPath, InInitialOffset, InElementSize);
	int32 ExistingIndex = RegisterOffsets.Find(Offset);
	if (ExistingIndex == INDEX_NONE)
	{
		return RegisterOffsets.Add(Offset);
	}
	return ExistingIndex;
}

void FRigVMMemoryContainer::UpdateRegisters()
{
	int32 AlignmentShift = 0;
	for (int32 RegisterIndex = 0; RegisterIndex < Registers.Num(); RegisterIndex++)
	{
		FRigVMRegister& Register = Registers[RegisterIndex];
		Register.ByteIndex += AlignmentShift;
		Register.MoveToFirstSlice();

		UScriptStruct* ScriptStruct = GetScriptStruct(RegisterIndex);
		if (ScriptStruct != nullptr)
		{
			UScriptStruct::ICppStructOps* TheCppStructOps = ScriptStruct->GetCppStructOps();
			if (TheCppStructOps != NULL)
			{
				if (!TheCppStructOps->HasZeroConstructor())
				{
					uint8* Pointer = GetData(RegisterIndex);

					if (Register.AlignmentBytes > 0)
					{
						if (!IsAligned(Pointer, TheCppStructOps->GetAlignment()))
						{
							Data.RemoveAt(Register.GetFirstAllocatedByte(), Register.AlignmentBytes);
							AlignmentShift -= Register.AlignmentBytes;
							Register.ByteIndex -= Register.AlignmentBytes;
							Register.AlignmentBytes = 0;
							Pointer = GetData(RegisterIndex);
						}
					}

					while (!IsAligned(Pointer, TheCppStructOps->GetAlignment()))
					{
						Data.InsertZeroed(Register.GetFirstAllocatedByte(), 1);
						Register.AlignmentBytes++;
						Register.ByteIndex++;
						AlignmentShift++;
						Pointer = GetData(RegisterIndex);
					}
				}
			}
		}
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

void FRigVMMemoryContainer::FillWithZeroes(int32 InRegisterIndex)
{
	ensure(Registers.IsValidIndex(InRegisterIndex));
	FMemory::Memzero(GetData(InRegisterIndex), Registers[InRegisterIndex].GetNumBytesAllSlices());
}

int32 FRigVMMemoryContainer::FindOrAddScriptStruct(UScriptStruct* InScriptStruct)
{
	int32 StructIndex = INDEX_NONE;
	if (ScriptStructs.Find(InScriptStruct, StructIndex))
	{
		return StructIndex;
	}
	return ScriptStructs.Add(InScriptStruct);
}
