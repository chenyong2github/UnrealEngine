// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PlayerCore.h"


namespace Electra
{

	/**
	 * Byte ringbuffer
	**/
	class FPODRingbuffer : private TMediaNoncopyable<FPODRingbuffer>
	{
	public:
		FPODRingbuffer(int32 InNumBytes = 0)
			: Data(nullptr)
			, DataEnd(nullptr)
			, WritePos(nullptr)
			, ReadPos(nullptr)
			, DataSize(0)
			, NumIn(0)
			, WaitingForSize(0)
			, bEOD(false)
			, bWasAborted(false)
		{
			Allocate(InNumBytes);
			SizeAvailableSignal.Reset();
		}
		~FPODRingbuffer()
		{
			FMediaCriticalSection::ScopedLock Lock(AccessLock);
			Deallocate();
		}

		//! Allocates buffer of the specified capacity, destroying any previous buffer.
		bool Reserve(int32 InNumBytes)
		{
			FMediaCriticalSection::ScopedLock Lock(AccessLock);
			Deallocate();
			if (Allocate(InNumBytes))
			{
				Reset();
				return true;
			}
			return false;
		}

		//! Enlarges the buffer to the new capacity, retaining the current content.
		bool EnlargeTo(int32 InNewNumBytes)
		{
			FMediaCriticalSection::ScopedLock Lock(AccessLock);
			// Do we need a bigger capacity?
			if (InNewNumBytes > Capacity())
			{
				// Are we currently empty?
				if (IsEmpty())
				{
					return Reserve(InNewNumBytes);
				}
				else
				{
					return InternalGrowTo(InNewNumBytes);
				}
			}
			return true;
		}

		//! Clears the buffer
		void Reset()
		{
			FMediaCriticalSection::ScopedLock Lock(AccessLock);
			NumIn = 0;
			WritePos = ReadPos = Data;
			bEOD = false;
			bWasAborted = false;
			// Do not modify the "waiter" members. When waiting for data to arrive it needs to continue
			// doing so even when we are resetting the buffer.
				//WaitingForSize = 0;
				//SizeAvailableSignal.Signal();
		}

		//! Returns the ringbuffer capacity
		int32 Capacity() const
		{
			return DataSize;
		}

		//! Returns the number of bytes in the buffer (amount that can be popped)
		int32 Num() const
		{
			return NumIn;
		}

		//! Returns the number of free bytes in the buffer (amount that can be pushed)
		int32 Avail() const
		{
			return DataSize - Num();
		}

		//! Checks if the buffer is empty.
		bool IsEmpty() const
		{
			return Num() == 0;
		}

		//! Checks if the buffer is full.
		bool IsFull() const
		{
			return Avail() == 0;
		}

		//! Checks if the buffer has reached the end-of-data marker (marker is set and no more data is in the buffer).
		bool IsEndOfData() const
		{
			return IsEmpty() && bEOD;
		}

		//! Checks if the end-of-data flag has been set. There may still be data in the buffer though!
		bool GetEOD() const
		{
			return bEOD;
		}

		//! Waits until the specified number of bytes has arrived in the buffer.
		//! NOTE: This method is somewhat dangerous in that there is no guarantee the required amount will ever arrive.
		//!       You must also never wait for more data than is the capcacity of the buffer!
		bool WaitUntilSizeAvailable(int32 SizeNeeded, int32 TimeoutMicroseconds)
		{
			// Only wait if not at EOD and more data than presently available is asked for.
			// Otherwise return enough data to be present even if that is not actually the case.
			if (!bEOD && SizeNeeded > Num())
			{
				AccessLock.Lock();
				// Repeat the size check inside the mutex lock in case we enter this block
				// while new data is being pushed from another thread.
				if (SizeNeeded > Num())
				{
					SizeAvailableSignal.Reset();
					WaitingForSize = SizeNeeded;
				}
				else
				{
					SizeAvailableSignal.Signal();
					WaitingForSize = 0;
				}
				AccessLock.Unlock();
				if (TimeoutMicroseconds > 0)
				{
					return SizeAvailableSignal.WaitTimeout(TimeoutMicroseconds);
				}
				else
				{
					// No infinite waiting by specifying negative timeouts!
					check(TimeoutMicroseconds == 0);
					return SizeAvailableSignal.IsSignaled();
				}
			}
			return true;
		}


		//! Inserts elements into the buffer. Returns true if successful, false if there is no room.
		bool PushData(const uint8* InData, int32 NumElements)
		{
			FMediaCriticalSection::ScopedLock Lock(AccessLock);
			check(!bEOD);
			// Zero elements can always be pushed...
			if (NumElements == 0)
			{
				return true;
			}
			if (Avail() >= NumElements)
			{
				int32 NumBeforeWrap = DataEnd - WritePos;
				if (NumElements <= NumBeforeWrap)
				{
					CopyData(WritePos, InData, NumElements);
					if ((WritePos += NumElements) == DataEnd)
					{
						WritePos = Data;
					}
				}
				else
				{
					CopyData(WritePos, InData, NumBeforeWrap);
					CopyData(Data, InData + NumBeforeWrap, NumElements - NumBeforeWrap);
					WritePos = Data + (NumElements - NumBeforeWrap);
				}
				NumIn += NumElements;
				if (NumIn >= WaitingForSize)
				{
					SizeAvailableSignal.Signal();
				}
				return true;
			}
			else
			{
				return false;
			}
		}


		//! "Opens" a push-block by returning 2 sets of pointers and block sizes where data to be pushed can be stored directly.
		int32 PushBlockOpen(uint8*& Block1, int32& Block1Size, uint8*& Block2, int32& Block2Size)
		{
			FMediaCriticalSection::ScopedLock Lock(AccessLock);
			int32 av = Avail();
			int32 NumBeforeWrap = DataEnd - WritePos;
			Block1 = WritePos;
			if (av > NumBeforeWrap)
			{
				Block1Size = NumBeforeWrap;
				Block2 = Data;
				Block2Size = av - NumBeforeWrap;
			}
			else
			{
				Block1Size = av;
				Block2 = nullptr;
				Block2Size = 0;
			}
			return av;
		}

		//! "Closes" a previously opened push block and advances the write pointer by as many bytes as were actually stored.
		void PushBlockClose(int32 NumElements)
		{
			if (NumElements)
			{
				FMediaCriticalSection::ScopedLock Lock(AccessLock);
				if ((WritePos += NumElements) >= DataEnd)
				{
					WritePos -= DataSize;
				}
				NumIn += NumElements;
				if (NumIn >= WaitingForSize)
				{
					SizeAvailableSignal.Signal();
				}
			}
		}


		//! "Pushes" an end-of-data marker signaling that no further data will be pushed. May be called more than once. Ringbuffer must be Reset() before next use.
		void SetEOD()
		{
			FMediaCriticalSection::ScopedLock Lock(AccessLock);
			bEOD = true;
			// Signal that data is present to wake any waiters on WaitForData() even though there may be no data in the buffer anymore.
			SizeAvailableSignal.Signal();
		}

		//! "Pops" data from the buffer to a destination. At most the specified number of bytes are popped, or fewer if not as many are available.
		int32 PopData(uint8* OutData, int32 MaxElementsWanted)
		{
			FMediaCriticalSection::ScopedLock Lock(AccessLock);
			int32 size = Num();
			int32 NumBeforeWrap = DataEnd - ReadPos;

			if (MaxElementsWanted > size)
			{
				MaxElementsWanted = size;
			}
			// Copy out or skip over?
			if (OutData)
			{
				// Can copy out without wrap?
				if (MaxElementsWanted <= NumBeforeWrap)
				{
					CopyData(OutData, ReadPos, MaxElementsWanted);
				}
				else
				{
					CopyData(OutData, ReadPos, NumBeforeWrap);
					CopyData(OutData + NumBeforeWrap, Data, MaxElementsWanted - NumBeforeWrap);
				}
			}

			if ((ReadPos += MaxElementsWanted) >= DataEnd)
			{
				ReadPos -= DataSize;
			}
			NumIn -= MaxElementsWanted;
			return MaxElementsWanted;
		}

		void Lock()
		{
			AccessLock.Lock();
		}

		void Unlock()
		{
			AccessLock.Unlock();
		}

		int32 GetLinearReadSize() const
		{
			FMediaCriticalSection::ScopedLock Lock(AccessLock);
			int32 NumBeforeWrap = DataEnd - ReadPos;
			return NumBeforeWrap;
		}

		// NOTE: Must control Lock()/Unlock() externally!
		const uint8* GetLinearReadData() const
		{
			return ReadPos;
		}
		uint8* GetLinearReadData()
		{
			return ReadPos;
		}


		void Abort()
		{
			FMediaCriticalSection::ScopedLock Lock(AccessLock);
			bWasAborted = true;
			SizeAvailableSignal.Signal();
		}

		bool WasAborted() const
		{
			return bWasAborted;
		}

	protected:
		bool Allocate(int32 InSize)
		{
			if (InSize)
			{
				DataSize = InSize;
				Data = (uint8*)FMemory::Malloc(InSize);
				DataEnd = Data + DataSize;
				WritePos = Data;
				ReadPos = Data;
				return Data != nullptr;
			}
			return true;
		}

		void Deallocate()
		{
			FMemory::Free(Data);
			Data = nullptr;
			DataEnd = nullptr;
			DataSize = 0;
			NumIn = 0;
			WritePos = nullptr;
			ReadPos = nullptr;
		}

		bool InternalGrowTo(int32 InNewNumBytes)
		{
			// Note: The access mutex must already be held here!
			check(Data && InNewNumBytes);
			// Resize the buffer
			uint8* NewData = (uint8*)FMemory::Realloc(Data, InNewNumBytes);
			if (NewData)
			{
				// Get current read & write offsets
				int32 ReadOffset = ReadPos - Data;
				int32 WriteOffset = WritePos - Data;
				// Set the data pointers up in the new buffer.
				DataSize = InNewNumBytes;
				Data = NewData;
				DataEnd = Data + DataSize;
				WritePos = Data + WriteOffset;
				ReadPos = Data + ReadOffset;
				return true;
			}
			return false;
		}

		void CopyData(uint8* CopyTo, const uint8* CopyFrom, int32 NumElements)
		{
			if (NumElements && CopyTo && CopyFrom)
			{
				FMemory::Memcpy(CopyTo, CopyFrom, NumElements);
			}
		}

		FMediaCriticalSection	AccessLock;
		FMediaEvent				SizeAvailableSignal;	//!< signaled when WaitingForSize data is present
		uint8*					Data;					//!< Base address of buffer
		uint8*					DataEnd;				//!< End address of buffer
		uint8*					WritePos;				//!< Current write position
		uint8*					ReadPos;				//!< Current read position
		int32					DataSize;				//!< Maximum number of bytes in the buffer
		int32					NumIn;					//!< Current number of bytes in the buffer
		int32					WaitingForSize;			//!< If waiting for a certain number of bytes to become available
		bool					bEOD;					//!< true when the last packet of data was pushed into the buffer.
		bool					bWasAborted;
	};



} // namespace Electra


