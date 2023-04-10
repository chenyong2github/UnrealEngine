// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

class FSharedMemoryBuffer
{
	enum class EWriteState : int;
	struct FChunkState;
	struct FHeader;

public:
	FSharedMemoryBuffer();
	~FSharedMemoryBuffer();

	// Opens an existing shared memory buffer (typically from handles created in another process)
	bool OpenExisting(const char* Name);

	// Close the current buffer and release all allocated resources
	void Close();


	/*** Reader Interface ***/

	// Test whether the buffer has finished being written to (ie. FinishWriting() has been called) and all data has been read from it.
	bool IsComplete() const;

	// Move the read cursor forwards by the given number of bytes
	void AdvanceReadPosition(size_t Size);

	// Gets the next data to be read (and the number of valid bytes accessible from the given pointer)
	const unsigned char* GetReadMemory(size_t& OutSize);

	// Wait for more data to be written to the buffer. The given parameter indicates the current size of the read buffer, used to exit immediately if it's changed since the value was fetched.
	void WaitToRead(size_t CurrentLength);


	/*** Writer Interface ***/

	// Signal that we've finished writing to this buffer
	void MarkComplete();

	// Move the write cursor forward by the given number of bytes
	void AdvanceWritePosition(size_t Size);

	// Gets the memory that can be written to, and the available space in it
	unsigned char* GetWriteMemory(size_t& OutSize);

	// Waits until all data has been read from the buffer (or more specifically, that the reader is stalled) and moves any unread data to the start of the buffer.
	void WaitToWrite(size_t currentLength);

private:
	unsigned char* GetChunkDataPtr(int chunkIdx) const;
	volatile long long* GetChunkStatePtr(int chunkIdx) const;

	const int ReaderIdx = 0;

	void* MemoryMappedFile;
	FHeader* Header;
	void* ReaderEvent;
	void* WriterEvent;

	int ReadChunkIdx;
	size_t ReadOffset;
	volatile long long* ReadChunkStatePtr;
	unsigned char* ReadChunkDataPtr;

	int WriteChunkIdx;
	volatile long long* WriteChunkStatePtr;
	unsigned char* WriteChunkDataPtr;
};

