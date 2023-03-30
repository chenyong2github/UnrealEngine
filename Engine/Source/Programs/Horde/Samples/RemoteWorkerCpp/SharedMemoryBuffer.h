// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

class FSharedMemoryBuffer
{
	enum class EState : int;
	class FHeader;

public:
	FSharedMemoryBuffer();
	~FSharedMemoryBuffer();

	// Initialize the object to a new shared memory buffer of the given size
	bool CreateNew(long long Capacity);

	// Opens an existing shared memory buffer (typically from handles created in another process)
	bool OpenExisting(void* MemoryMappedFile, void* ReaderEvent, void* WriterEvent);

	// Close the current buffer and release all allocated resources
	void Close();

	// Duplicates handles for this buffer into another process
	bool DuplicateHandles(void* TargetProcess, void*& OutMemoryMappedFile, void*& OutReaderEvent, void*& OutWriterEvent);


	/*** Reader Interface ***/

	// Test whether the buffer has finished being written to (ie. FinishWriting() has been called) and all data has been read from it.
	bool HasFinishedReading() const;

	// Move the read cursor forwards by the given number of bytes
	void AdvanceReadPosition(long long Size);

	// Gets the next data to be read (and the number of valid bytes accessible from the given pointer)
	const unsigned char* GetReadMemory(long long& OutSize);

	// Wait for more data to be written to the buffer. The given parameter indicates the current size of the read buffer, used to exit immediately if it's changed since the value was fetched.
	void WaitForData(long long CurrentLength);


	/*** Writer Interface ***/

	// Signal that we've finished writing to this buffer
	void FinishWriting();

	// Move the write cursor forward by the given number of bytes
	void AdvanceWritePosition(long long Size);

	// Gets the memory that can be written to, and the available space in it
	unsigned char* GetWriteMemory(long long& OutSize);

	// Waits until all data has been read from the buffer (or more specifically, that the reader is stalled) and moves any unread data to the start of the buffer.
	void Flush();

private:
	bool Initialize();

	void* MemoryMappedFile;
	long long Length;
	FHeader* Header;
	unsigned char* Memory;
	void* ReaderEvent;
	void* WriterEvent;
};
