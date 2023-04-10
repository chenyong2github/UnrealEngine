// Copyright Epic Games, Inc. All Rights Reserved.

#include <windows.h>
#include <assert.h>
#include <iostream>
#include "SharedMemoryBuffer.h"

enum class FSharedMemoryBuffer::EWriteState : int
{
	// Chunk is still being appended to
	Writing = 0,

	// Writer has moved to the next chunk
	MovedToNext = 2,

	// This chunk marks the end of the stream
	Complete = 3,
};

struct FSharedMemoryBuffer::FChunkState
{
	const long long Value;

	// Constructor
	FChunkState(long long InValue)
		: Value(InValue)
	{
	}

	// Constructor
	FChunkState(EWriteState WriteState, int ReaderFlags, int Length)
		: Value((unsigned long long)Length | ((unsigned long long)ReaderFlags << 31) | ((unsigned long long)WriteState << 62))
	{
	}

	// Written length of this chunk
	int GetLength() const { return (int)(Value & 0x7fffffff); }

	// Set of flags which are set for each reader that still has to read from a chunk
	int GetReaderFlags() const { return (int)((Value >> 31) & 0x7fffffff); }

	// State of the writer
	EWriteState GetWriteState() const { return (EWriteState)(Value >> 62); }

	// Test whether a particular reader is still referencing the chunk
	bool HasReaderFlag() const { return (Value & (1UL << 31)) != 0; }

	// Read the state value from memory
	static FChunkState Read(volatile long long* StateValue)
	{
		return FChunkState(InterlockedCompareExchange64(StateValue, 0, 0));
	}

	// Append data to the chunk
	static void Append(volatile long long* StateValue, long long length)
	{
		InterlockedAdd64(StateValue, length);
	}

	// Mark the chunk as being written to
	static void StartWriting(volatile long long* StateValue, int numReaders)
	{
		InterlockedExchange64(StateValue, FChunkState(EWriteState::Writing, (1 << numReaders) - 1, 0).Value);
	}

	// Move to the next chunk
	static void MoveToNext(volatile long long* StateValue)
	{
		InterlockedOr64(StateValue, FChunkState(EWriteState::MovedToNext, 0, 0).Value);
	}

	// Move to the next chunk
	static void MarkComplete(volatile long long* StateValue)
	{
		InterlockedOr64(StateValue, FChunkState(EWriteState::Complete, 0, 0).Value);
	}

	// Clear the reader flag
	static void FinishReading(volatile long long* StateValue)
	{
		InterlockedAnd64(StateValue, ~(1LL << (0 + 31)));
	}
};

struct FSharedMemoryBuffer::FHeader
{
	int NumChunks;
	int ChunkLength;
};

////////////////////////////

FSharedMemoryBuffer::FSharedMemoryBuffer()
	: MemoryMappedFile(nullptr)
	, Header(nullptr)
	, ReaderEvent(nullptr)
	, WriterEvent(nullptr)
	// Reader
	, ReadChunkIdx(-1)
	, ReadOffset(0)
	, ReadChunkStatePtr(nullptr)
	, ReadChunkDataPtr(nullptr)
	// Writer
	, WriteChunkIdx(-1)
	, WriteChunkStatePtr(nullptr)
	, WriteChunkDataPtr(nullptr)
{
}

FSharedMemoryBuffer::~FSharedMemoryBuffer()
{
	Close();
}

bool FSharedMemoryBuffer::OpenExisting(const char* Name)
{
	char NameBuffer[MAX_PATH];
	sprintf_s(NameBuffer, "%s_M", Name);

	MemoryMappedFile = OpenFileMappingA(FILE_MAP_ALL_ACCESS, TRUE, NameBuffer);
	if (MemoryMappedFile == nullptr)
	{
		std::cout << "No filemap" << NameBuffer << std::endl;
		Close();
		return false;
	}

	Header = (FHeader*)MapViewOfFile(MemoryMappedFile, FILE_MAP_ALL_ACCESS, 0, 0, 0);
	if (Header == nullptr)
	{
		std::cout << "No filemap view" << std::endl;
		Close();
		return false;
	}

	sprintf_s(NameBuffer, "%s_R", Name);
	ReaderEvent = OpenEventA(SYNCHRONIZE | EVENT_MODIFY_STATE, TRUE, NameBuffer);
	if (ReaderEvent == nullptr)
	{
		std::cout << "No read event" << std::endl;
		Close();
		return false;
	}

	sprintf_s(NameBuffer, "%s_W", Name);
	WriterEvent = OpenEventA(SYNCHRONIZE | EVENT_MODIFY_STATE, TRUE, NameBuffer);
	if (WriterEvent == nullptr)
	{
		std::cout << "No write event" << std::endl;
		Close();
		return false;
	}

	ReadChunkIdx = 0;
	ReadChunkDataPtr = GetChunkDataPtr(ReadChunkIdx);
	ReadChunkStatePtr = GetChunkStatePtr(ReadChunkIdx);

	WriteChunkIdx = 0;
	WriteChunkDataPtr = GetChunkDataPtr(WriteChunkIdx);
	WriteChunkStatePtr = GetChunkStatePtr(WriteChunkIdx);

	return true;
}

void FSharedMemoryBuffer::Close()
{
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

	ReadChunkIdx = -1;
	ReadOffset = 0;
	ReadChunkStatePtr = nullptr;
	ReadChunkDataPtr = nullptr;

	WriteChunkIdx = -1;
	WriteChunkStatePtr = nullptr;
	WriteChunkDataPtr = nullptr;
}

bool FSharedMemoryBuffer::IsComplete() const
{
	FChunkState State = FChunkState::Read(ReadChunkStatePtr);
	return State.GetWriteState() == EWriteState::Complete && ReadOffset == State.GetLength();
}

void FSharedMemoryBuffer::AdvanceReadPosition(size_t Size)
{
	ReadOffset += Size;
}

const unsigned char* FSharedMemoryBuffer::GetReadMemory(size_t& OutSize)
{
	FChunkState State = FChunkState::Read(ReadChunkStatePtr);
	if (State.HasReaderFlag())
	{
		OutSize = State.GetLength() - ReadOffset;
		return ReadChunkDataPtr + ReadOffset;
	}
	else
	{
		OutSize = 0;
		return nullptr;
	}
}

void FSharedMemoryBuffer::WaitToRead(size_t CurrentLength)
{
	for (; ; )
	{
		FChunkState State = FChunkState::Read(ReadChunkStatePtr);

		if (!State.HasReaderFlag())
		{
			// Wait until the current chunk is readable
			ResetEvent(ReaderEvent);
			if (!FChunkState::Read(ReadChunkStatePtr).HasReaderFlag())
			{
				WaitForSingleObject(ReaderEvent, INFINITE);
			}
		}
		else if (ReadOffset + CurrentLength < State.GetLength() || State.GetWriteState() == EWriteState::Complete)
		{
			// Still have data to read from this chunk
			break;
		}
		else if (ReadOffset + CurrentLength >= State.GetLength() && State.GetWriteState() == EWriteState::Writing)
		{
			// Wait until there is more data in the chunk
			ResetEvent(ReaderEvent);
			if (FChunkState::Read(ReadChunkStatePtr).Value == State.Value)
			{
				WaitForSingleObject(ReaderEvent, INFINITE);
			}
		}
		else if (State.GetWriteState() == EWriteState::MovedToNext)
		{
			// Move to the next chunk
			FChunkState::FinishReading(GetChunkStatePtr(ReadChunkIdx));
			SetEvent(WriterEvent);

			ReadChunkIdx++;
			if (ReadChunkIdx == Header->NumChunks)
			{
				ReadChunkIdx = 0;
			}
			ReadOffset = 0;
		}
		else
		{
			// Still need to read data from the current buffer
			break;
		}
	}
}


void FSharedMemoryBuffer::MarkComplete()
{
	FChunkState::MarkComplete(ReadChunkStatePtr);
	SetEvent(ReaderEvent);
}

void FSharedMemoryBuffer::AdvanceWritePosition(size_t Size)
{
	FChunkState::Append(ReadChunkStatePtr, Size);
	SetEvent(ReaderEvent);
}

unsigned char* FSharedMemoryBuffer::GetWriteMemory(size_t& OutSize)
{
	FChunkState State = FChunkState::Read(ReadChunkStatePtr);
	OutSize = State.GetLength() - ReadOffset;
	return ReadChunkDataPtr + ReadOffset;
}

void FSharedMemoryBuffer::WaitToWrite(size_t CurrentLength)
{
	for (; ; )
	{
		size_t Length;
		GetWriteMemory(Length);

		if (Length != CurrentLength)
		{
			break;
		}

		FChunkState::MoveToNext(WriteChunkStatePtr);

		WriteChunkIdx++;
		if (WriteChunkIdx == Header->NumChunks)
		{
			WriteChunkIdx = 0;
		}

		WriteChunkDataPtr = GetChunkDataPtr(WriteChunkIdx);
		WriteChunkStatePtr = GetChunkStatePtr(WriteChunkIdx);

		while (FChunkState::Read(WriteChunkStatePtr).GetReaderFlags() != 0)
		{
			WaitForSingleObject(WriterEvent, INFINITE);
			ResetEvent(WriterEvent);
		}

		FChunkState::StartWriting(WriteChunkStatePtr, 1);
	}
}

unsigned char* FSharedMemoryBuffer::GetChunkDataPtr(int chunkIdx) const
{
	return (unsigned char*)(Header + 1) + (sizeof(long long) * Header->NumChunks) + (chunkIdx * Header->ChunkLength);
}

volatile long long* FSharedMemoryBuffer::GetChunkStatePtr(int chunkIdx) const
{
	return (volatile long long*)(Header + 1) + chunkIdx;
}