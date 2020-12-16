// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/UnrealString.h"
#include "Virtualization/VirtualizedBulkData.h"
#include "Serialization/BufferReader.h"

#if WITH_EDITORONLY_DATA

class FVirtualizedBulkDataWriter : public FArchive
{
public:
	FVirtualizedBulkDataWriter(FVirtualizedUntypedBulkData& InBulkData, bool bIsPersistent = false)
		: BulkData(InBulkData)
	{
		SetIsSaving(true);
		SetIsPersistent(bIsPersistent);

		FSharedBuffer Payload = InBulkData.GetData().Get();

		if (Payload)
		{
			const int64 CurrentDataLength = Payload.GetSize();

			// Clone the payload so that we have a local copy that we can
			// append additional data to.
			Buffer = FMemory::Malloc(CurrentDataLength, DEFAULT_ALIGNMENT);
			FMemory::Memcpy(Buffer, Payload.GetData(), CurrentDataLength);

			BufferLength = CurrentDataLength;

			// Start at the end of the existing data
			CurPos = CurrentDataLength;
			DataLength = CurrentDataLength;
		}
		else
		{
			Buffer = nullptr;
			BufferLength = 0;

			CurPos = 0;
			DataLength = 0;
		}
	}

	~FVirtualizedBulkDataWriter()
	{
		// Remove the slack from the allocated bulk data
		Buffer = FMemory::Realloc(Buffer, DataLength, DEFAULT_ALIGNMENT);
		BulkData.UpdatePayload(FSharedBuffer::TakeOwnership(Buffer, DataLength, FMemory::Free));
	}

	virtual void Serialize(void* Data, int64 Num)
	{
		// Determine if we need to reallocate the buffer to fit the next item
		const int64 NewPos = CurPos + Num;
		checkf(NewPos >= CurPos, TEXT("Serialization has somehow gone backwards"));

		if (NewPos > BufferLength)
		{
			// If so, resize to the new size + 3/8 additional slack
			const int64 NewLength = NewPos + 3 * NewPos / 8 + 16;
			Buffer = FMemory::Realloc(Buffer, NewLength, DEFAULT_ALIGNMENT);
			BufferLength = NewLength;
		}

		FMemory::Memcpy(static_cast<unsigned char*>(Buffer) + CurPos, Data, Num);

		CurPos += Num;
		DataLength = FMath::Max(DataLength, CurPos);
	}

	using FArchive::operator<<; // For visibility of the overloads we don't override

	virtual FArchive& operator<<(class FName& Name) override
	{
		// FNames are serialized as strings in BulkData
		FString StringName = Name.ToString();
		*this << StringName;
		return *this;
	}

	virtual int64 Tell() { return CurPos; }
	virtual int64 TotalSize() { return DataLength; }

	virtual void Seek(int64 InPos)
	{
		check(InPos >= 0);
		check(InPos <= DataLength);
		CurPos = InPos;
	}

	virtual bool AtEnd()
	{
		return CurPos >= DataLength;
	}

	virtual FString GetArchiveName() const
	{
		return TEXT("FVirtualizedBulkDataWriter");
	}

protected:
	/** The target bulkdata object */
	FVirtualizedUntypedBulkData& BulkData;

	/** Pointer to the data buffer */
	void* Buffer;
	/** Length of the data buffer (stored as bytes) */
	int64 BufferLength;

	/** Current position in the buffer for serialization operations */
	int64 CurPos;

	/** The length of valid data in the data buffer (stored as bytes) */
	int64 DataLength;
};


namespace UE4VirtualizedBulkData_Private
{
	/** Wraps access to the FVirtualizedUntypedBulkData data for FVirtualizedBulkDataReader
	so that it can be done before the FBufferReaderBase constructor is called */
	class DataAccessWrapper
	{
	protected:
		DataAccessWrapper(FVirtualizedUntypedBulkData& InBulkData)
			: Payload(InBulkData.GetData().Get())

		{

		}

		virtual ~DataAccessWrapper() = default;

		void* GetData() const
		{
			// It's okay to remove the const qualifier here as it will only be passed
			// on to FBufferReaderBase, which will not change the data at all, but takes a non-const
			// pointer so that it can free the memory if requested, which we don't.
			return const_cast<void*>(Payload.GetData());
		}

		int64 GetDataLength() const
		{
			return Payload.GetSize();
		}

	private:
		FSharedBuffer Payload;
	};
}

class FVirtualizedBulkDataReader :	public UE4VirtualizedBulkData_Private::DataAccessWrapper,
	public FBufferReaderBase
{
public:
	FVirtualizedBulkDataReader(FVirtualizedUntypedBulkData& InBulkData, bool bIsPersistent = false)
		: DataAccessWrapper(InBulkData)
		, FBufferReaderBase(GetData(), GetDataLength(), false, bIsPersistent)
	{
	}

	virtual ~FVirtualizedBulkDataReader() = default;

	using FArchive::operator<<; // For visibility of the overloads we don't override

	virtual FArchive& operator<<(class FName& Name) override
	{
		// FNames are serialized as strings in BulkData
		FString StringName;
		*this << StringName;
		Name = FName(*StringName);
		return *this;
	}

	virtual FString GetArchiveName() const
	{
		return TEXT("FVirtualizedBulkDataReader");
	}	
};

#endif //WITH_EDITORONLY_DATA
