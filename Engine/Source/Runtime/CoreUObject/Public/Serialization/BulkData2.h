// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BulkDataBuffer.h"
#include "Async/AsyncFileHandle.h"
#include "IO/IoDispatcher.h"

// Any place marked with BULKDATA_NOT_IMPLEMENTED_FOR_RUNTIME marks a method that we do not support
// but needs to exist in order for the code to compile. This pretty much means code that is editor only
// but is not compiled out at runtime etc.
// It can also be found inside of methods to show code paths that the old path implements but I have
// not been able to find any way to actually execute/test.
#define BULKDATA_NOT_IMPLEMENTED_FOR_RUNTIME PLATFORM_BREAK()

struct FOwnedBulkDataPtr;

/**
 * Represents an IO request from the BulkData streaming API.
 *
 * It functions pretty much the same as IAsyncReadRequest expect that it also holds
 * the file handle as well.
 */
class COREUOBJECT_API IBulkDataIORequest
{
public:
	virtual ~IBulkDataIORequest() {}

	virtual bool PollCompletion() const = 0;
	virtual bool WaitCompletion(float TimeLimitSeconds = 0.0f) const = 0;

	virtual uint8* GetReadResults() = 0;
	virtual int64 GetSize() const = 0;

	virtual void Cancel() = 0;
};

/**
 * Callback to use when making streaming requests
 */
typedef TFunction<void(bool bWasCancelled, IBulkDataIORequest*)> FBulkDataIORequestCallBack;

/**
 * @documentation @todo documentation
 */
class COREUOBJECT_API FBulkDataBase
{
public:
	using BulkDataRangeArray = TArray<FBulkDataBase*, TInlineAllocator<8>>;

	static void				SetIoDispatcher(FIoDispatcher* InIoDispatcher) { IoDispatcher = InIoDispatcher; }
	static FIoDispatcher*	GetIoDispatcher() { return IoDispatcher; }
public:
	using FileToken = int32;
	static constexpr FileToken InvalidToken = INDEX_NONE;

	FBulkDataBase(const FBulkDataBase& Other) { *this = Other; }
	FBulkDataBase(FBulkDataBase&& Other);
	FBulkDataBase& operator=(const FBulkDataBase& Other);

	FBulkDataBase()
	{ 
		Fallback.BulkDataSize = 0;
		Fallback.Token = InvalidToken;
	}
	~FBulkDataBase();

protected:

	void Serialize(FArchive& Ar, UObject* Owner, int32 Index, bool bAttemptFileMapping, int32 ElementSize);

public:
	// Unimplemented:
	void* Lock(uint32 LockFlags);
	const void* LockReadOnly() const;
	void Unlock();
	bool IsLocked() const;

	void* Realloc(int64 SizeInBytes);

	/**
	 * Retrieves a copy of the bulk data.
	 *
	 * @param Dest [in/out] Pointer to pointer going to hold copy, can point to nullptr in which case memory is allocated
	 * @param bDiscardInternalCopy Whether to discard/ free the potentially internally allocated copy of the data
	 */
	void GetCopy(void** Dest, bool bDiscardInternalCopy);

	int64 GetBulkDataSize() const;

	void SetBulkDataFlags(uint32 BulkDataFlagsToSet);
	void ResetBulkDataFlags(uint32 BulkDataFlagsToSet);
	void ClearBulkDataFlags(uint32 BulkDataFlagsToClear);
	uint32 GetBulkDataFlags() const { return BulkDataFlags; }

	bool CanLoadFromDisk() const;
	
	/**
	 * Returns true if the data references a file that currently exists and can be referenced by the file system.
	 */
	bool DoesExist() const;

	bool IsStoredCompressedOnDisk() const;
	FName GetDecompressionFormat() const;

	bool IsBulkDataLoaded() const { return DataBuffer != nullptr; }

	// TODO: The flag tests could be inline if we fixed the header dependency issues (the flags are defined in Bulkdata.h at the moment)
	bool IsAvailableForUse() const;
	bool IsDuplicateNonOptional() const;
	bool IsOptional() const;
	bool IsInlined() const;
	UE_DEPRECATED(4.25, "Use ::IsInSeperateFile() instead")
	FORCEINLINE bool InSeperateFile() const { return IsInSeperateFile(); }
	bool IsInSeperateFile() const;
	bool IsSingleUse() const;
	bool IsMemoryMapped() const;
	bool IsUsingIODispatcher() const;

	IAsyncReadFileHandle* OpenAsyncReadHandle() const;

	IBulkDataIORequest* CreateStreamingRequest(EAsyncIOPriorityAndFlags Priority, FBulkDataIORequestCallBack* CompleteCallback, uint8* UserSuppliedMemory) const;
	IBulkDataIORequest* CreateStreamingRequest(int64 OffsetInBulkData, int64 BytesToRead, EAsyncIOPriorityAndFlags Priority, FBulkDataIORequestCallBack* CompleteCallback, uint8* UserSuppliedMemory) const;
	
	static IBulkDataIORequest* CreateStreamingRequestForRange(const BulkDataRangeArray& RangeArray, EAsyncIOPriorityAndFlags Priority, FBulkDataIORequestCallBack* CompleteCallback);

	void RemoveBulkData();

	bool IsAsyncLoadingComplete() const { return true; }

	// Added for compatibility with the older BulkData system
	int64 GetBulkDataOffsetInFile() const;
	FString GetFilename() const;

public:
	// The following methods are for compatibility with SoundWave.cpp which assumes memory mapping.
	void ForceBulkDataResident(); // Is closer to MakeSureBulkDataIsLoaded in the old system but kept the name due to existing use
	FOwnedBulkDataPtr* StealFileMapping();

private:
	void LoadDataDirectly(void** DstBuffer);

	void SerializeDuplicateData(FArchive& Ar, uint32& OutBulkDataFlags, int64& OutBulkDataSizeOnDisk, int64& OutBulkDataOffsetInFile);
	void SerializeBulkData(FArchive& Ar, void* DstBuffer, int64 DataLength);

	void AllocateData(SIZE_T SizeInBytes);
	void FreeData();

	FString ConvertFilenameFromFlags(const FString& Filename) const;

private:

	static FIoDispatcher* IoDispatcher;

	union
	{
		// Inline data or fallback path
		struct
		{
			uint64 BulkDataSize;
			FileToken Token;
			
		} Fallback;

		// For IODispatcher
		FIoChunkId ChunkID;	
	}; // Note that the union will end up being 16 bytes with padding
	
	void* DataBuffer = nullptr;
	uint32 BulkDataFlags = 0;
	mutable uint8 LockStatus = 0; // Mutable so that the read only lock can be const
};

/**
 * @documentation @todo documentation
 */
template<typename ElementType>
class COREUOBJECT_API FUntypedBulkData2 : public FBulkDataBase
{
	// In the older Bulkdata system the data was being loaded as if it was POD with the option to opt out
	// but nothing actually opted out. This check should help catch if any non-POD data was actually being
	// used or not.
	static_assert(TIsPODType<ElementType>::Value, "FUntypedBulkData2 is limited to POD types!");
public:
	FORCEINLINE FUntypedBulkData2() {}

	void Serialize(FArchive& Ar, UObject* Owner, int32 Index, bool bAttemptFileMapping)
	{
		FBulkDataBase::Serialize(Ar, Owner, Index, bAttemptFileMapping, sizeof(ElementType));
	}
	
	// @TODO: The following two ::Serialize methods are a work around for the default parameters in the old 
	// BulkData api that are not used anywhere and to avoid causing code compilation issues for licensee code.
	// At some point in the future we should remove Index and bAttemptFileMapping from both the old and new 
	// BulkData api implementations of ::Serialize and then use UE_DEPRECATE to update existing code properly.
	FORCEINLINE void Serialize(FArchive& Ar, UObject* Owner)
	{	
		Serialize(Ar, Owner, INDEX_NONE, false);
	}

	// @TODO: See above
	FORCEINLINE void Serialize(FArchive& Ar, UObject* Owner, int32 Index)
	{
		Serialize(Ar, Owner, Index, false);
	}

	/**
	 * Returns the number of elements held by the BulkData object.
	 *
	 * @return Number of elements.
	 */
	int64 GetElementCount() const 
	{ 
		return GetBulkDataSize() / GetElementSize(); 
	}

	/**
	 * Returns size in bytes of single element.
	 *
	 * @return The size of the element.
	 */
	int32 GetElementSize() const
	{ 
		return sizeof(ElementType); 
	}

	ElementType* Lock(uint32 LockFlags)
	{
		return (ElementType*)FBulkDataBase::Lock(LockFlags);
	}

	const ElementType* LockReadOnly() const
	{
		return (const ElementType*)FBulkDataBase::LockReadOnly();
	}

	ElementType* Realloc(int64 InElementCount)
	{
		return (ElementType*)FBulkDataBase::Realloc(InElementCount * sizeof(ElementType));
	}

	/**
	 * Returns a copy encapsulated by a FBulkDataBuffer.
	 *
	 * @param RequestedElementCount If set to greater than 0, the returned FBulkDataBuffer will be limited to
	 * this number of elements. This will give an error if larger than the actual number of elements in the BulkData object.
	 * @param bDiscardInternalCopy If true then the BulkData object will free it's internal buffer once called.
	 *
	 * @return A FBulkDataBuffer that owns a copy of the BulkData, this might be a subset of the data depending on the value of RequestedSize.
	 */
	FORCEINLINE FBulkDataBuffer<ElementType> GetCopyAsBuffer(int64 RequestedElementCount, bool bDiscardInternalCopy)
	{
		const int64 MaxElementCount = GetElementCount();

		check(RequestedElementCount <= MaxElementCount);

		ElementType* Ptr = nullptr;
		GetCopy((void**)& Ptr, bDiscardInternalCopy);

		const int64 BufferSize = (RequestedElementCount > 0 ? RequestedElementCount : MaxElementCount);

		return FBulkDataBuffer<ElementType>(Ptr, BufferSize);
	}
};

// Commonly used types
using FByteBulkData2 = FUntypedBulkData2<uint8>;
using FWordBulkData2 = FUntypedBulkData2<uint16>;
using FIntBulkData2 = FUntypedBulkData2<int32>;
using FFloatBulkData2 = FUntypedBulkData2<float>;
