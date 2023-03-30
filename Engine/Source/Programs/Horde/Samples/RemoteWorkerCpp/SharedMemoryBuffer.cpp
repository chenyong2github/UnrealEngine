// Copyright Epic Games, Inc. All Rights Reserved.

#include <windows.h>
#include <assert.h>
#include <iostream>
#include "SharedMemoryBuffer.h"

enum class FSharedMemoryBuffer::EState : int
{
	Normal,
	ReaderWaitingForData, // Once new data is available, set _readerTcs
	WriterWaitingToFlush, // Once flush has been complete, set _writerTcs
	Finished
};

class FSharedMemoryBuffer::FHeader
{
public:
	EState GetState() const
	{
		return (EState)InterlockedCompareExchange((long*)&State, 0, 0);
	}

	bool UpdateState(EState OldState, EState NewState)
	{
		return InterlockedCompareExchange((long*)&State, (long)NewState, (long)OldState) == (long)OldState;
	}

	void Compact()
	{
		InterlockedExchange64(&ReadPosition, 0);
		InterlockedExchange64(&WritePosition, GetReadLength());
	}

	long long GetReadPosition() const
	{
		return InterlockedCompareExchange64(const_cast<long long*>(&ReadPosition), 0, 0);
	}

	long long AdvanceReadPosition(long long Value)
	{
		InterlockedAdd64(&ReadPosition, Value);
		return InterlockedAdd64(&ReadLength, -Value);
	}

	long long GetReadLength() const
	{
		return InterlockedCompareExchange64(const_cast<long long*>(&ReadLength), 0, 0);
	}

	long long GetWritePosition() const
	{
		return InterlockedCompareExchange64(const_cast<long long*>(&WritePosition), 0, 0);
	}

	long long AdvanceWritePosition(long long value)
	{
		InterlockedAdd64(&WritePosition, value);
		return InterlockedAdd64(&ReadLength, value);
	}

	void FinishWriting()
	{
		InterlockedExchange((long*)&State, (long)EState::Finished);
	}

private:
	EState State;
	unsigned char Padding[4];
	long long ReadPosition;
	long long ReadLength;
	long long WritePosition;
};

FSharedMemoryBuffer::FSharedMemoryBuffer()
	: MemoryMappedFile(nullptr)
	, Memory(nullptr)
	, Length(0)
	, Header(nullptr)
	, ReaderEvent(nullptr)
	, WriterEvent(nullptr)
{
}

FSharedMemoryBuffer::~FSharedMemoryBuffer()
{
	Close();
}

bool FSharedMemoryBuffer::CreateNew(long long Capacity)
{
	Close();

	LARGE_INTEGER CapacityLargeInt;
	CapacityLargeInt.QuadPart = Capacity;

	ReaderEvent = CreateEvent(nullptr, true, FALSE, nullptr);
	WriterEvent = CreateEvent(nullptr, true, FALSE, nullptr);
	MemoryMappedFile = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, CapacityLargeInt.HighPart, CapacityLargeInt.LowPart, nullptr);

	if (ReaderEvent == nullptr || WriterEvent == nullptr || MemoryMappedFile == nullptr || !Initialize())
	{
		Close();
		return false;
	}

	return true;
}

bool FSharedMemoryBuffer::OpenExisting(void* InMemoryMappedFile, void* InReaderEvent, void* InWriterEvent)
{
	MemoryMappedFile = InMemoryMappedFile;
	ReaderEvent = InReaderEvent;
	WriterEvent = InWriterEvent;
	return Initialize();
}

void FSharedMemoryBuffer::Close()
{
	Memory = nullptr;

	if (Header != nullptr)
	{
		UnmapViewOfFile(Header);
		Header = nullptr;
	}

	if (MemoryMappedFile != nullptr)
	{
		CloseHandle(MemoryMappedFile);
		MemoryMappedFile = nullptr;
	}

	if (WriterEvent != nullptr)
	{
		CloseHandle(WriterEvent);
		WriterEvent = nullptr;
	}

	if (ReaderEvent != nullptr)
	{
		CloseHandle(ReaderEvent);
		ReaderEvent = nullptr;
	}
}

bool FSharedMemoryBuffer::DuplicateHandles(void* TargetProcess, void*& OutMemoryMappedFile, void*& OutReaderEvent, void*& OutWriterEvent)
{
	HANDLE CurrentProcess = GetCurrentProcess();

	HANDLE TargetMemoryMappedFile;
	if (DuplicateHandle(CurrentProcess, MemoryMappedFile, TargetProcess, &TargetMemoryMappedFile, 0, false, DUPLICATE_SAME_ACCESS))
	{
		HANDLE TargetWriterEvent;
		if (DuplicateHandle(CurrentProcess, WriterEvent, TargetProcess, &TargetWriterEvent, 0, false, DUPLICATE_SAME_ACCESS))
		{
			HANDLE TargetReaderEvent;
			if (DuplicateHandle(CurrentProcess, ReaderEvent, TargetProcess, &TargetReaderEvent, 0, false, DUPLICATE_SAME_ACCESS))
			{
				OutMemoryMappedFile = TargetMemoryMappedFile;
				OutReaderEvent = TargetReaderEvent;
				OutWriterEvent = TargetWriterEvent;
				return true;
			}
			CloseHandle(TargetWriterEvent);
		}
		CloseHandle(TargetMemoryMappedFile);
	}

	return false;
}

bool FSharedMemoryBuffer::HasFinishedReading() const
{
	return Header->GetState() == EState::Finished && Header->GetReadLength() == 0;
}

void FSharedMemoryBuffer::AdvanceReadPosition(long long Size)
{
	if (Header->AdvanceReadPosition(Size) == 0)
	{
		SetEvent(WriterEvent);
	}
}

const unsigned char* FSharedMemoryBuffer::GetReadMemory(long long& OutSize)
{
	OutSize = Header->GetReadLength();
	return Memory + Header->GetReadPosition();
}

void FSharedMemoryBuffer::WaitForData(long long Size)
{
	while (Header->GetReadLength() == Size)
	{
		EState State = Header->GetState();
		if (State == EState::Finished)
		{
			break;
		}
		else if (State == EState::Normal)
		{
			ResetEvent(ReaderEvent);
			Header->UpdateState(State, EState::ReaderWaitingForData);
		}
		else if (State == EState::ReaderWaitingForData)
		{
			WaitForSingleObject(ReaderEvent, INFINITE);
		}
		else if (State == EState::WriterWaitingToFlush)
		{
			Header->Compact();
			Header->UpdateState(State, EState::Normal);
			SetEvent(WriterEvent);
		}
		else
		{
			assert(false);
		}
	}
}

void FSharedMemoryBuffer::FinishWriting()
{
	Header->FinishWriting();
	SetEvent(ReaderEvent);
}

void FSharedMemoryBuffer::AdvanceWritePosition(long long size)
{
	assert(Header->GetState() != EState::Finished);

	Header->AdvanceWritePosition(size);

	if (Header->UpdateState(EState::ReaderWaitingForData, EState::Normal))
	{
		SetEvent(ReaderEvent);
	}
}

unsigned char* FSharedMemoryBuffer::GetWriteMemory(long long& OutSize)
{
	long long WritePosition = Header->GetWritePosition();
	OutSize = Length - WritePosition;
	return Memory + WritePosition;
}

void FSharedMemoryBuffer::Flush()
{
	while (Header->GetReadPosition() > 0)
	{
		EState State = Header->GetState();
		if (State == EState::Finished)
		{
			break;
		}
		else if (State == EState::Normal)
		{
			SetEvent(WriterEvent);
			Header->UpdateState(State, EState::WriterWaitingToFlush);
		}
		else if (State == EState::ReaderWaitingForData)
		{
			Header->Compact();
			break;
		}
		else if (State == EState::WriterWaitingToFlush)
		{
			WaitForSingleObject(WriterEvent, INFINITE);
		}
	}
}

bool FSharedMemoryBuffer::Initialize()
{
	if (ReaderEvent == nullptr || WriterEvent == nullptr || MemoryMappedFile == nullptr)
	{
		Close();
		return false;
	}

	Header = (FHeader*)MapViewOfFile(MemoryMappedFile, FILE_MAP_ALL_ACCESS, 0, 0, 0);
	if (Header == nullptr)
	{
		Close();
		return false;
	}

	Memory = (unsigned char*)(Header + 1);

	MEMORY_BASIC_INFORMATION MemInfo;
	if (VirtualQuery(Header, &MemInfo, sizeof(MemInfo)) == 0)
	{
		Close();
		return false;
	}

	Length = MemInfo.RegionSize;
	return true;
}
