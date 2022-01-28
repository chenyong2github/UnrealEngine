// Copyright Epic Games, Inc. All Rights Reserved.


#include "Serialization/BulkData.h"
#include "HAL/FileManager.h"
#include "Misc/ScopeLock.h"
#include "Misc/PackageSegment.h"
#include "Serialization/LargeMemoryWriter.h"
#include "Serialization/LargeMemoryReader.h"
#include "HAL/IConsoleManager.h"
#include "UObject/Package.h"
#include "Templates/Casts.h"
#include "Async/Async.h"
#include "UObject/LinkerLoad.h"
#include "UObject/LinkerSave.h"
#include "Interfaces/ITargetPlatform.h"
#include "UObject/DebugSerializationFlags.h"
#include "UObject/PackageResourceManager.h"
#include "Serialization/AsyncLoadingPrivate.h"
#include "Async/MappedFileHandle.h"
#include "HAL/PlatformFileManager.h"
#include "UObject/UObjectThreadContext.h"
#include "ProfilingDebugging/LoadTimeTracker.h"
#include "IO/IoDispatcher.h"
#include "AsyncLoadingPrivate.h"

/*-----------------------------------------------------------------------------
	Constructors and operators
-----------------------------------------------------------------------------*/

/** Whether to track information of how bulk data is being used */
#define TRACK_BULKDATA_USE 0

DECLARE_STATS_GROUP(TEXT("Bulk Data"), STATGROUP_BulkData, STATCAT_Advanced);

#if TRACK_BULKDATA_USE

/** Simple wrapper for tracking the bulk data usage in the thread-safe way. */
struct FThreadSafeBulkDataToObjectMap
{
	static FThreadSafeBulkDataToObjectMap& Get()
	{
		static FThreadSafeBulkDataToObjectMap Instance;
		return Instance;
	}

	void Add( FUntypedBulkData* Key, UObject* Value )
	{
		FScopeLock ScopeLock(&CriticalSection);
		BulkDataToObjectMap.Add(Key,Value);
	}

	void Remove( FUntypedBulkData* Key )
	{
		FScopeLock ScopeLock(&CriticalSection);
		BulkDataToObjectMap.Remove(Key);
	}

	FCriticalSection& GetLock() 
	{
		return CriticalSection;
	}

	const TMap<FUntypedBulkData*,UObject*>::TConstIterator GetIterator() const
	{
		return BulkDataToObjectMap.CreateConstIterator();
	}

protected:
	/** Map from bulk data pointer to object it is contained by */
	TMap<FUntypedBulkData*,UObject*> BulkDataToObjectMap;

	/** CriticalSection. */
	FCriticalSection CriticalSection;
};

/**
 * Helper structure associating an object and a size for sorting purposes.
 */
struct FObjectAndSize
{
	FObjectAndSize( const UObject* InObject, int64 InSize )
	:	Object( InObject )
	,	Size( InSize )
	{}

	/** Object associated with size. */
	const UObject*	Object;
	/** Size associated with object. */
	int64			Size;
};

/** Hash function required for TMap support */
uint32 GetTypeHash( const FUntypedBulkData* BulkData )
{
	return PointerHash(BulkData);
}

#endif


FOwnedBulkDataPtr::~FOwnedBulkDataPtr()
{
	if (AllocatedData)
	{
		FMemory::Free(AllocatedData);
	}
	else
	{
		if (MappedRegion || MappedHandle)
		{
			delete MappedRegion;
			delete MappedHandle;
		}
	}
}

const void* FOwnedBulkDataPtr::GetPointer()
{
	// return the pointer that the caller can use
	return AllocatedData ? AllocatedData : (MappedRegion ? MappedRegion->GetMappedPtr() : nullptr);
}

bool FUntypedBulkData::FAllocatedPtr::MapFile(const FPackagePath& InPackagePath, EPackageSegment InPackageSegment, int64 Offset, int64 Size)
{
	check(!MappedHandle && !MappedRegion); // It doesn't make sense to do this twice, but if need be, not hard to do

	MappedHandle = IPackageResourceManager::Get().OpenMappedHandleToPackage(InPackagePath, InPackageSegment);

	if (!MappedHandle)
	{
		return false;
	}
	MappedRegion = MappedHandle->MapRegion(Offset, Size, true); //@todo we really don't want to hit the disk here to bring it into memory
	if (!MappedRegion)
	{
		delete MappedHandle;
		MappedHandle = nullptr;
		return false;
	}

	check(Size == MappedRegion->GetMappedSize());
	Ptr = (void*)(MappedRegion->GetMappedPtr()); //@todo mapped files should probably be const-correct
	check(IsAligned(Ptr, FPlatformProperties::GetMemoryMappingAlignment()));
	bAllocated = true;
	return true;
}

void FUntypedBulkData::FAllocatedPtr::UnmapFile()
{
	if (MappedRegion || MappedHandle)
	{
		delete MappedRegion;
		delete MappedHandle;
		MappedRegion = nullptr;
		MappedHandle = nullptr;
		Ptr = nullptr; // make sure we don't try to free this pointer
	}
}

/**
 * Constructor, initializing all member variables.
 */
FUntypedBulkData::FUntypedBulkData()
{
	InitializeMemberVariables();
}

/**
 * Copy constructor. Use the common routine to perform the copy.
 *
 * @param Other the source array to copy
 */
FUntypedBulkData::FUntypedBulkData( const FUntypedBulkData& Other )
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("FUntypedBulkData::FUntypedBulkData"), STAT_UBD_Constructor, STATGROUP_Memory);

	InitializeMemberVariables();
	BulkDataAlignment = Other.BulkDataAlignment;

	// Prepare bulk data pointer. Can't call any functions that would call virtual GetElementSize on "this" as
	// we're in the constructor of the base class and would hence call a pure virtual.
	ElementCount	= Other.ElementCount;
	BulkData.Reallocate( Other.GetBulkDataSize(), BulkDataAlignment );

	// Copy data over.
	Copy( Other );

#if TRACK_BULKDATA_USE
	FThreadSafeBulkDataToObjectMap::Get().Add( this, NULL );
#endif
}

/**
 * Virtual destructor, free'ing allocated memory.
 */
FUntypedBulkData::~FUntypedBulkData()
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("FUntypedBulkData::~FUntypedBulkData"), STAT_UBD_Destructor, STATGROUP_Memory);

	check( LockStatus == LOCKSTATUS_Unlocked );
	if (SerializeFuture.IsValid())
	{
		WaitForAsyncLoading();
	}

	// Free memory.
	BulkData     .Deallocate();
	BulkDataAsync.Deallocate();
	
#if WITH_EDITOR
	// Detach from archive.
	if( AttachedAr )
	{
		AttachedAr->DetachBulkData( this, false );
		check( AttachedAr == NULL );
	}
#endif // WITH_EDITOR

#if TRACK_BULKDATA_USE
	FThreadSafeBulkDataToObjectMap::Get().Remove( this );
#endif
}

/**
 * Copies the source array into this one after detaching from archive.
 *
 * @param Other the source array to copy
 */
FUntypedBulkData& FUntypedBulkData::operator=( const FUntypedBulkData& Other )
{
	// Remove bulk data, avoiding potential load in Lock call.
	RemoveBulkData();

	BulkDataAlignment = Other.BulkDataAlignment;

	if (Other.BulkData)
	{
		// Reallocate to size of src.
		Lock(LOCK_READ_WRITE);
		Realloc(Other.GetElementCount());

		// Copy data over.
		Copy( Other );

		// Unlock.
		Unlock();
	}
	else // Otherwise setup the bulk so that the data can be loaded through LoadBulkDataWithFileReader()
	{
		PackagePath = Other.PackagePath;
		PackageSegment = Other.PackageSegment;
		BulkDataFlags = Other.BulkDataFlags;
		ElementCount = Other.ElementCount;
		BulkDataOffsetInFile = Other.BulkDataOffsetInFile;
		BulkDataSizeOnDisk = Other.BulkDataSizeOnDisk;
	}

	return *this;
}


/*-----------------------------------------------------------------------------
	Static functions.
-----------------------------------------------------------------------------*/

/**
 * Dumps detailed information of bulk data usage.
 *
 * @param Log FOutputDevice to use for logging
 */
void FUntypedBulkData::DumpBulkDataUsage( FOutputDevice& Log )
{
#if TRACK_BULKDATA_USE
	// Arrays about to hold per object and per class size information.
	TArray<FObjectAndSize> PerObjectSizeArray;
	TArray<FObjectAndSize> PerClassSizeArray;

	{
		FScopeLock Lock(&FThreadSafeBulkDataToObjectMap::Get().GetLock());

		// Iterate over all "live" bulk data and add size to arrays if it is loaded.
		for( auto It(FThreadSafeBulkDataToObjectMap::Get().GetIterator()); It; ++It )
		{
			const FUntypedBulkData*	BulkData	= It.Key();
			const UObject*			Owner		= It.Value();
			// Only add bulk data that is consuming memory to array.
			if( Owner && BulkData->IsBulkDataLoaded() && BulkData->GetBulkDataSize() > 0 )
			{
				// Per object stats.
				PerObjectSizeArray.Add( FObjectAndSize( Owner, BulkData->GetBulkDataSize() ) );

				// Per class stats.
				bool bFoundExistingPerClassSize = false;
				// Iterate over array, trying to find existing entry.
				for( int32 PerClassIndex=0; PerClassIndex<PerClassSizeArray.Num(); PerClassIndex++ )
				{
					FObjectAndSize& PerClassSize = PerClassSizeArray[ PerClassIndex ];
					// Add to existing entry if found.
					if( PerClassSize.Object == Owner->GetClass() )
					{
						PerClassSize.Size += BulkData->GetBulkDataSize();
						bFoundExistingPerClassSize = true;
						break;
					}
				}
				// Add new entry if we didn't find an existing one.
				if( !bFoundExistingPerClassSize )
				{
					PerClassSizeArray.Add( FObjectAndSize( Owner->GetClass(), BulkData->GetBulkDataSize() ) );
				}
			}
		}
	}

	/** Compare operator, sorting by size in descending order */
	struct FCompareFObjectAndSize
	{
		FORCEINLINE bool operator()( const FObjectAndSize& A, const FObjectAndSize& B ) const
		{
			return B.Size < A.Size;
		}
	};

	// Sort by size.
	PerObjectSizeArray.Sort( FCompareFObjectAndSize() );
	PerClassSizeArray.Sort( FCompareFObjectAndSize() );

	// Log information.
	UE_LOG(LogSerialization, Log, TEXT(""));
	UE_LOG(LogSerialization, Log, TEXT("Per class summary of bulk data use:"));
	for( int32 PerClassIndex=0; PerClassIndex<PerClassSizeArray.Num(); PerClassIndex++ )
	{
		const FObjectAndSize& PerClassSize = PerClassSizeArray[ PerClassIndex ];
		Log.Logf( TEXT("  %5lld KByte of bulk data for Class %s"), PerClassSize.Size / 1024, *PerClassSize.Object->GetPathName() );
	}
	UE_LOG(LogSerialization, Log, TEXT(""));
	UE_LOG(LogSerialization, Log, TEXT("Detailed per object stats of bulk data use:"));
	for( int32 PerObjectIndex=0; PerObjectIndex<PerObjectSizeArray.Num(); PerObjectIndex++ )
	{
		const FObjectAndSize& PerObjectSize = PerObjectSizeArray[ PerObjectIndex ];
		Log.Logf( TEXT("  %5lld KByte of bulk data for %s"), PerObjectSize.Size / 1024, *PerObjectSize.Object->GetFullName() );
	}
	UE_LOG(LogSerialization, Log, TEXT(""));
#else
	UE_LOG(LogSerialization, Log, TEXT("Please recompiled with TRACK_BULKDATA_USE set to 1 in UnBulkData.cpp."));
#endif
}


/*-----------------------------------------------------------------------------
	Accessors.
-----------------------------------------------------------------------------*/

/**
 * Returns the number of elements in this bulk data array.
 *
 * @return Number of elements in this bulk data array
 */
int64 FUntypedBulkData::GetElementCount() const
{
	return ElementCount;
}
/**
 * Returns the size of the bulk data in bytes.
 *
 * @return Size of the bulk data in bytes
 */
int64 FUntypedBulkData::GetBulkDataSize() const
{
	return GetElementCount() * GetElementSize();
}
/**
 * Returns the size of the bulk data on disk. This can differ from GetBulkDataSize if
 * BULKDATA_SerializeCompressed is set.
 *
 * @return Size of the bulk data on disk or INDEX_NONE in case there's no association
 */
int64 FUntypedBulkData::GetBulkDataSizeOnDisk() const
{
	return BulkDataSizeOnDisk;
}
/**
 * Returns the offset into the file the bulk data is located at.
 *
 * @return Offset into the file or INDEX_NONE in case there is no association
 */
int64 FUntypedBulkData::GetBulkDataOffsetInFile() const
{
	return BulkDataOffsetInFile;
}
/**
 * Returns whether the bulk data is stored compressed on disk.
 *
 * @return true if data is compressed on disk, false otherwise
 */
bool FUntypedBulkData::IsStoredCompressedOnDisk() const
{
	return (BulkDataFlags & BULKDATA_SerializeCompressed) ? true : false;
}

bool FUntypedBulkData::CanLoadFromDisk() const
{
#if WITH_EDITOR
#if WITH_IOSTORE_IN_EDITOR
	if (IsUsingIODispatcher())
	{
		return PackageId.IsValid();
	}
#endif //WITH_IOSTORE_IN_EDITOR

	return AttachedAr != NULL;
#else
	bool bCanLoadFromDisk = false;
	if (!PackagePath.IsEmpty())
	{
		bCanLoadFromDisk = true;
	}
	else if (UPackage* PackagePtr = Package.Get())
	{
		bCanLoadFromDisk = (PackagePtr->GetLinker() != nullptr);
	}
	return bCanLoadFromDisk;
#endif // WITH_EDITOR
}

bool FUntypedBulkData::DoesExist() const
{
#if WITH_IOSTORE_IN_EDITOR
	if (IsUsingIODispatcher())
	{
		return FBulkDataBase::GetIoDispatcher()->DoesChunkExist(CreateChunkId());
	}
#endif

	if (IsInExternalResource())
	{
		return IPackageResourceManager::Get().DoesExternalResourceExist(
			EPackageExternalResource::WorkspaceDomainFile, PackagePath.GetPackageName());
	}
	return IPackageResourceManager::Get().DoesPackageExist(PackagePath, PackageSegment);
}

/**
 * Returns flags usable to decompress the bulk data
 * 
 * @return COMPRESS_NONE if the data was not compressed on disk, or valid flags to pass to FCompression::UncompressMemory for this data
 */
FName FUntypedBulkData::GetDecompressionFormat() const
{
	return GetDecompressionFormat(this->BulkDataFlags);
}

FName FUntypedBulkData::GetDecompressionFormat(EBulkDataFlags InFlags)
{
	return (InFlags & BULKDATA_SerializeCompressedZLIB) ? NAME_Zlib : NAME_None;
}

/**
 * Returns whether the bulk data is currently loaded and resident in memory.
 *
 * @return true if bulk data is loaded, false otherwise
 */
bool FUntypedBulkData::IsBulkDataLoaded() const
{
	return !!BulkData;
}

bool FUntypedBulkData::IsAsyncLoadingComplete() const
{
	return SerializeFuture.IsValid() == false || SerializeFuture.WaitFor(FTimespan::Zero());
}

/**
* Returns whether this bulk data is used
* @return true if BULKDATA_Unused is not set
*/
bool FUntypedBulkData::IsAvailableForUse() const
{
	return (BulkDataFlags & BULKDATA_Unused) ? false : true;
}

/*-----------------------------------------------------------------------------
	Data retrieval and manipulation.
-----------------------------------------------------------------------------*/

void FUntypedBulkData::ResetAsyncData()
{
	// Async data should be released by the time we get here
	check(!BulkDataAsync);
	SerializeFuture = TFuture<bool>();
}

/**
 * Retrieves a copy of the bulk data.
 *
 * @param Dest [in/out] Pointer to pointer going to hold copy, can point to NULL pointer in which case memory is allocated
 * @param bDiscardInternalCopy Whether to discard/ free the potentially internally allocated copy of the data
 */
void FUntypedBulkData::GetCopy( void** Dest, bool bDiscardInternalCopy )
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("FUntypedBulkData::GetCopy"), STAT_UBD_GetCopy, STATGROUP_Memory);

	check( LockStatus == LOCKSTATUS_Unlocked );
	check( Dest );

	// Make sure any async loads have completed and moved the data into BulkData
	FlushAsyncLoading();

	// Passed in memory is going to be used.
	if( *Dest )
	{
		// The data is already loaded so we can simply use a mempcy.
		if( BulkData )
		{
			// Copy data into destination memory.
			FMemory::Memcpy( *Dest, BulkData.Get(), GetBulkDataSize() );
			// Discard internal copy if wanted and we're still attached to an archive or if we're
			// single use bulk data.
			if( bDiscardInternalCopy && (CanLoadFromDisk() || (BulkDataFlags & BULKDATA_SingleUse)) )
			{
				BulkData.Deallocate();
			}
		}
		// Data isn't currently loaded so we need to load it from disk.
		else
		{
			ensure(TryLoadDataIntoMemory(*Dest));
		}
	}
	// Passed in memory is NULL so we need to allocate some.
	else
	{
		// The data is already loaded so we can simply use a mempcy.
		if( BulkData )
		{
			// If the internal copy should be discarded and we are still attached to an archive we can
			// simply "return" the already existing copy and NULL out the internal reference. We can
			// also do this if the data is single use like e.g. when uploading texture data.
			if( bDiscardInternalCopy && (CanLoadFromDisk()|| (BulkDataFlags & BULKDATA_SingleUse)) )
			{
				*Dest = BulkData.ReleaseWithoutDeallocating();
				ResetAsyncData();
			}
			// Can't/ Don't discard so we need to allocate and copy.
			else
			{
				int64 BulkDataSize = GetBulkDataSize();
				if (BulkDataSize != 0)
				{
					// Allocate enough memory for data...
					*Dest = FMemory::Malloc( BulkDataSize, BulkDataAlignment );

					// ... and copy it into memory now pointed to by out parameter.
					FMemory::Memcpy( *Dest, BulkData.Get(), BulkDataSize );
				}
				else
				{
					*Dest = nullptr;
				}
			}
		}
		// Data isn't currently loaded so we need to load it from disk.
		else
		{
			int64 BulkDataSize = GetBulkDataSize();
			if (BulkDataSize != 0)
			{
				// Allocate enough memory for data...
				*Dest = FMemory::Malloc( BulkDataSize, BulkDataAlignment );

				// ... and directly load into it.
				ensure(TryLoadDataIntoMemory(*Dest));
			}
			else
			{
				*Dest = nullptr;
			}
		}
	}
}

/**
 * Locks the bulk data and returns a pointer to it.
 *
 * @param	LockFlags	Flags determining lock behavior
 */
void* FUntypedBulkData::Lock( uint32 LockFlags )
{
	check( LockStatus == LOCKSTATUS_Unlocked );
	
	// Make sure bulk data is loaded.
	MakeSureBulkDataIsLoaded();
		
	// Read-write operations are allowed on returned memory.
	if( LockFlags & LOCK_READ_WRITE )
	{
#if WITH_EDITOR
		// We need to detach from the archive to not be able to clobber changes by serializing
		// over them.
		if( AttachedAr )
		{
			// Detach bulk data. This will call DetachFromArchive which in turn will clear AttachedAr.
			AttachedAr->DetachBulkData( this, false );
			check( AttachedAr == NULL );
		}
#endif // WITH_EDITOR
		// This has to be set after the DetachBulkData because we can't detach a locked bulkdata
		LockStatus = LOCKSTATUS_ReadWriteLock;
		ClearBulkDataFlags(BULKDATA_LazyLoadable);
	}
	// Only read operations are allowed on returned memory.
	else if( LockFlags & LOCK_READ_ONLY )
	{
		LockStatus = LOCKSTATUS_ReadOnlyLock;
	}
	else
	{
		UE_LOG(LogSerialization, Fatal,TEXT("Unknown lock flag %i"),LockFlags);
	}

	return BulkData.Get();
}

const void* FUntypedBulkData::LockReadOnly() const
{
	check(LockStatus == LOCKSTATUS_Unlocked);
	
	FUntypedBulkData* mutable_this = const_cast<FUntypedBulkData*>(this);

	// Make sure bulk data is loaded.
	mutable_this->MakeSureBulkDataIsLoaded();

	// Only read operations are allowed on returned memory.
	mutable_this->LockStatus = LOCKSTATUS_ReadOnlyLock;

	check(BulkData);
	return BulkData.Get();
}

/**
 * Change size of locked bulk data. Only valid if locked via read-write lock.
 *
 * @param InElementCount	Number of elements array should be resized to
 */
void* FUntypedBulkData::Realloc( int64 InElementCount )
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("FUntypedBulkData::Realloc"), STAT_UBD_Realloc, STATGROUP_Memory);

	check( LockStatus == LOCKSTATUS_ReadWriteLock );
	// Progate element count and reallocate data based on new size.
	ElementCount	= InElementCount;
	BulkData.Reallocate( GetBulkDataSize(), BulkDataAlignment );
	return BulkData.Get();
}

/** 
 * Unlocks bulk data after which point the pointer returned by Lock no longer is valid.
 */
void FUntypedBulkData::Unlock() const
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("FUntypedBulkData::Unlock"), STAT_UBD_Unlock, STATGROUP_Memory);

	check(LockStatus != LOCKSTATUS_Unlocked);

	FUntypedBulkData* mutable_this = const_cast<FUntypedBulkData*>(this);

	mutable_this->LockStatus = LOCKSTATUS_Unlocked;

	// Free pointer if we're guaranteed to only to access the data once.
	if (BulkDataFlags & BULKDATA_SingleUse)
	{
		mutable_this->BulkData.Deallocate();
	}
}

/**
 * Clears/ removes the bulk data and resets element count to 0.
 */
void FUntypedBulkData::RemoveBulkData()
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("FUntypedBulkData::RemoveBulkData"), STAT_UBD_RemoveBulkData, STATGROUP_Memory);

	check( LockStatus == LOCKSTATUS_Unlocked );

#if WITH_EDITOR
	// Detach from archive without loading first.
	if( AttachedAr )
	{
		AttachedAr->DetachBulkData( this, false );
		check( AttachedAr == NULL );
	}
#endif // WITH_EDITOR
	
	// Resize to 0 elements.
	ElementCount	= 0;
	BulkData.Deallocate();
	ClearBulkDataFlags(BULKDATA_LazyLoadable);
}

/**
 * Deallocates bulk data without detaching the archive.
 */
bool FUntypedBulkData::UnloadBulkData()
{
#if WITH_EDITOR
	if (LockStatus == LOCKSTATUS_Unlocked)
	{
		FlushAsyncLoading();
		BulkData.Deallocate();
		return true;
	}
#endif
	return false;
}

// FutureState implementation that loads everything when created.
struct FStateComplete : public TFutureState<bool>
{
public:
	FStateComplete(TFunction<void()> CompletionCallback) : TFutureState<bool>(MoveTemp(CompletionCallback)) { MarkComplete(); }
};

/**
* Load the bulk data using a file reader. Works when no archive is attached to the bulk data.
* @return Whether the operation succeeded.
*/
bool FUntypedBulkData::LoadBulkDataWithFileReader()
{
#if WITH_EDITOR
	if (!BulkData && CanLoadBulkDataWithFileReader() && !SerializeFuture.IsValid())
	{
		SerializeFuture = TFuture<bool>(TSharedPtr<TFutureState<bool>, ESPMode::ThreadSafe>(new FStateComplete([=]() 
		{ 
			AsyncLoadBulkData();
			return true; 
		})));
		return (bool)BulkDataAsync;
	}
#endif
	return false;
}

bool FUntypedBulkData::CanLoadBulkDataWithFileReader() const
{
#if WITH_EDITOR
	return !PackagePath.IsEmpty();
#else
	return false;
#endif
}

/**
 * Forces the bulk data to be resident in memory and detaches the archive.
 */
void FUntypedBulkData::ForceBulkDataResident()
{
	// Make sure bulk data is loaded.
	MakeSureBulkDataIsLoaded();

#if WITH_EDITOR
	// Detach from the archive 
	if( AttachedAr )
	{
		// Detach bulk data. This will call DetachFromArchive which in turn will clear AttachedAr.
		AttachedAr->DetachBulkData( this, false );
		check( AttachedAr == NULL );
	}
#endif // WITH_EDITOR
}

bool FUntypedBulkData::StartAsyncLoading()
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("FUntypedBulkData::StartAsyncLoading"), STAT_UBD_StartSerializingBulkData, STATGROUP_Memory);

	if (!IsAsyncLoadingComplete())
	{
		return true; // Early out if an asynchronous load is already in progress.
	}

	if (IsBulkDataLoaded())
	{
		return false; // Early out if we do not need to actually load any data
	}

	if (!CanLoadFromDisk())
	{
		return false; // Early out if we cannot load from disk
	}

	check(SerializeFuture.IsValid() == false);

#if WITH_IOSTORE_IN_EDITOR
	if (IsUsingIODispatcher())
	{
		checkf(IsStoredCompressedOnDisk() == false, TEXT("BulkData in the IoStore should not have compression flags set!"));
		
		// TODO: We should be able to do this without the use of Async. We should be able to create a TPromise that is fulfilled
		// by the callback from the IoStore and not create this extra thread job with a wait but because the callback style is 
		// based on older code it is not a TUniqueFunction it makes it hard to pass a TPromise in.
		// Should fix this by fixing the callbacks.
		SerializeFuture = Async(EAsyncExecution::ThreadPool, [=]()
		{
			BulkDataAsync.Reallocate(GetBulkDataSize(), BulkDataAlignment);

			const FIoChunkId ChunkId = CreateChunkId();

			TUniquePtr<IBulkDataIORequest> Request = CreateBulkDataIoDispatcherRequest(ChunkId, BulkDataOffsetInFile, GetBulkDataSize(), nullptr, (uint8*)BulkDataAsync.Get());
			Request->WaitCompletion();

			return true;
		});

		return true;
	}
#endif //WITH_IOSTORE_IN_EDITOR

	SerializeFuture = Async(EAsyncExecution::ThreadPool, [=]()
	{
		AsyncLoadBulkData();
		return true;
	});

	return true;
}

/**
 * Sets the passed in bulk data flags.
 *
 * @param BulkDataFlagsToSet	Bulk data flags to set
 */
void FUntypedBulkData::SetBulkDataFlags( uint32 BulkDataFlagsToSet )
{
	BulkDataFlags = static_cast<EBulkDataFlags>(BulkDataFlags | BulkDataFlagsToSet);
}

void FUntypedBulkData::ResetBulkDataFlags(uint32 BulkDataFlagsToSet)
{
	BulkDataFlags = static_cast<EBulkDataFlags>(BulkDataFlagsToSet);
}

/**
* Gets the current bulk data flags.
*
* @return Bulk data flags currently set
*/
uint32 FUntypedBulkData::GetBulkDataFlags() const
{
	return BulkDataFlags;
}

/**
 * Sets the passed in bulk data alignment.
 *
 * @param BulkDataAlignmentToSet	Bulk data alignment to set
 */
void FUntypedBulkData::SetBulkDataAlignment(uint16 BulkDataAlignmentToSet)
{
	BulkDataAlignment = BulkDataAlignmentToSet;
}

/**
* Gets the current bulk data alignment.
*
* @return Bulk data alignment currently set
*/
uint32 FUntypedBulkData::GetBulkDataAlignment() const
{
	return BulkDataAlignment;
}

/**
 * Clears the passed in bulk data flags.
 *
 * @param BulkDataFlagsToClear	Bulk data flags to clear
 */
void FUntypedBulkData::ClearBulkDataFlags( uint32 BulkDataFlagsToClear )
{
	BulkDataFlags = static_cast<EBulkDataFlags>(BulkDataFlags & ~BulkDataFlagsToClear);
}

FIoChunkId FUntypedBulkData::CreateChunkId() const
{
#if WITH_IOSTORE_IN_EDITOR
	if(IsUsingIODispatcher())
	{ 
		const EIoChunkType ChunkType = BulkDataFlags & BULKDATA_OptionalPayload
			? EIoChunkType::OptionalBulkData
			: BulkDataFlags & BULKDATA_MemoryMappedPayload
				? EIoChunkType::MemoryMappedBulkData
				: EIoChunkType::BulkData;

		return CreateIoChunkId(PackageId.Value(), 0, ChunkType);
	}
	else
#endif // WITH_IOSTORE_IN_EDITOR
	{
		return FIoChunkId();
	}	
}

void FUntypedBulkData::AsyncLoadBulkData()
{
	BulkDataAsync.Reallocate(GetBulkDataSize(), BulkDataAlignment);

	TUniquePtr<FArchive> BulkArchive;
	if (IsInExternalResource())
	{
		BulkArchive = IPackageResourceManager::Get().OpenReadExternalResource(EPackageExternalResource::WorkspaceDomainFile,
			PackagePath.GetPackageName());
		checkf(BulkArchive.IsValid(), TEXT("Attempted to load bulk data from invalid WorkspaceDomain package '%s'."),
			*PackagePath.GetPackageName());
	}
	else
	{
		FOpenPackageResult Result = IPackageResourceManager::Get().OpenReadPackage(PackagePath, PackageSegment);
		checkf(Result.Archive.IsValid() && Result.Format == EPackageFormat::Binary, TEXT("Attempted to load bulk data from an invalid package '%s'%s."),
			*PackagePath.GetDebugName(PackageSegment), (Result.Archive == nullptr ? TEXT("") : TEXT(": Package Format is Text which is not supported")));
		BulkArchive = MoveTemp(Result.Archive);
	}

	// Seek to the beginning of the bulk data in the file.
	BulkArchive->Seek(BulkDataOffsetInFile);
	SerializeBulkData(*BulkArchive, BulkDataAsync.Get(), BulkDataFlags);
}

/*-----------------------------------------------------------------------------
	Serialization.
-----------------------------------------------------------------------------*/

void FUntypedBulkData::StartSerializingBulkData(FArchive& Ar, UObject* Owner, int32 Idx, bool bPayloadInline)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("FUntypedBulkData::StartSerializingBulkData"), STAT_UBD_StartSerializingBulkData, STATGROUP_Memory);
	check(SerializeFuture.IsValid() == false);	

	SerializeFuture = Async(EAsyncExecution::ThreadPool, [=]() 
	{ 
		UE_CLOG(GEventDrivenLoaderEnabled, LogSerialization, Error, TEXT("Attempt to stream bulk data with EDL enabled. This is not desireable. Package %s"),
			*PackagePath.GetDebugName(PackageSegment));
		AsyncLoadBulkData();

		return true;
	});

	// Skip bulk data in this archive
	if (bPayloadInline)
	{
		Ar.Seek(Ar.Tell() + BulkDataSizeOnDisk);
	}
}

int32 GMinimumBulkDataSizeForAsyncLoading = 131072;
static FAutoConsoleVariableRef CVarMinimumBulkDataSizeForAsyncLoading(
	TEXT("s.MinBulkDataSizeForAsyncLoading"),
	GMinimumBulkDataSizeForAsyncLoading,
	TEXT("Minimum time the time limit exceeded warning will be triggered by."),
	ECVF_Default
	);

bool FUntypedBulkData::ShouldStreamBulkData(FArchive& Ar)
{
	if (Ar.IsLoadingFromCookedPackage())
	{
#if WITH_EDITOR
		// Streaming not yet implemented
		return false;
#else
		if (!(BulkDataFlags & BULKDATA_PayloadAtEndOfFile))
		{
			return false; // if it is inline, it is already precached, so use it
		}

		if (!(BulkDataFlags & BULKDATA_PayloadInSeperateFile))
		{
			checkf(false, TEXT("Bulk data should either be inline or stored in a separate file for the new uobject loader."));
			return false; // if it is not in a separate file, then we can't easily find the correct offset in the uexp file; we don't want this case anyway!
		}
#endif
	}

	const bool bForceStream = !!(BulkDataFlags & BULKDATA_ForceStreamPayload);

	return (FPlatformProperties::RequiresCookedData() && !PackagePath.IsEmpty() &&
		FPlatformProcess::SupportsMultithreading() && IsInGameThread() &&
		(bForceStream || GetBulkDataSize() > GMinimumBulkDataSizeForAsyncLoading) &&
		GMinimumBulkDataSizeForAsyncLoading >= 0);
}

bool FUntypedBulkData::NeedsOffsetFixup() const
{
	return (BulkDataFlags & BULKDATA_NoOffsetFixUp) == 0;
}

void FUntypedBulkData::SetBulkDataFlagsOn(EBulkDataFlags& InOutAccumulator, EBulkDataFlags FlagsToSet)
{
	InOutAccumulator = static_cast<EBulkDataFlags>(InOutAccumulator | FlagsToSet);
}

void FUntypedBulkData::ClearBulkDataFlagsOn(EBulkDataFlags& InOutAccumulator, EBulkDataFlags FlagsToClear)
{
	InOutAccumulator = static_cast<EBulkDataFlags>(InOutAccumulator & ~FlagsToClear);
}

void FUntypedBulkData::Serialize( FArchive& Ar, UObject* Owner, int32 Idx, bool bAttemptFileMapping, EFileRegionType FileRegionType)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("FUntypedBulkData::Serialize"), STAT_UBD_Serialize, STATGROUP_Memory);

	SCOPED_LOADTIMER(BulkData_Serialize);

	check( LockStatus == LOCKSTATUS_Unlocked );

	check(!bAttemptFileMapping || Ar.IsLoading()); // makes no sense to map unless we are loading

	if (Ar.IsTransacting())
	{
		// Special case for transacting bulk data arrays.

		// Constructing the object during load will save it to the transaction buffer.
		// We need to cancel that save because trying to load the bulk data now would break.
		bool bActuallySave = Ar.IsSaving() && (!Owner || !Owner->HasAnyFlags(RF_NeedLoad));

		Ar << bActuallySave;

		if (bActuallySave)
		{
			if (Ar.IsLoading())
			{
				// Flags for bulk data.
				Ar << BulkDataFlags;
				// Number of elements in array.
				Ar << ElementCount;

				// Allocate bulk data.
				BulkData.Reallocate( GetBulkDataSize(), BulkDataAlignment );

				// Deserialize bulk data.
				SerializeBulkData(Ar, BulkData.Get(), BulkDataFlags);
			}
			else if (Ar.IsSaving())
			{
				// Flags for bulk data.
				Ar << BulkDataFlags;
				// Number of elements in array.
				Ar << ElementCount;

				// Don't attempt to load or serialize BulkData if the current size is 0.
				// This could be a newly constructed BulkData that has not yet been loaded, 
				// and allocating 0 bytes now will cause a crash when we load.
				if (GetBulkDataSize() > 0)
				{
					// Make sure bulk data is loaded.
					MakeSureBulkDataIsLoaded();

					// Serialize bulk data.
					SerializeBulkData(Ar, BulkData.Get(), BulkDataFlags);
				}
			}
		}
	}
	else if ( Ar.IsPersistent() && !Ar.IsObjectReferenceCollector() && !Ar.ShouldSkipBulkData() )
	{
#if TRACK_BULKDATA_USE
		FThreadSafeBulkDataToObjectMap::Get().Add( this, Owner );
#endif
		// We're loading from the persistent archive.
		if (Ar.IsLoading())
		{
			EBulkDataFlags AddedFlags = static_cast<EBulkDataFlags>(0);
			EBulkDataFlags RemovedFlags = static_cast<EBulkDataFlags>(0);
			auto SetLocalBulkDataFlags = [this, &AddedFlags, &RemovedFlags](EBulkDataFlags BulkDataFlagsToSet)
			{
				AddedFlags = static_cast<EBulkDataFlags>(AddedFlags | BulkDataFlagsToSet);
				RemovedFlags = static_cast<EBulkDataFlags>(RemovedFlags & ~BulkDataFlagsToSet);
				SetBulkDataFlags(BulkDataFlagsToSet);
			};
			auto ClearLocalBulkDataFlags = [this, &AddedFlags, &RemovedFlags](EBulkDataFlags BulkDataFlagsToClear)
			{
				AddedFlags = static_cast<EBulkDataFlags>(AddedFlags & ~BulkDataFlagsToClear);
				RemovedFlags = static_cast<EBulkDataFlags>(RemovedFlags | BulkDataFlagsToClear);
				ClearBulkDataFlags(BulkDataFlagsToClear);
			};

			Ar << BulkDataFlags;
			SerializeBulkDataSizeInt(Ar, ElementCount, BulkDataFlags);

			// Size on disk, which in the case of compression is != GetBulkDataSize()
			SerializeBulkDataSizeInt(Ar, BulkDataSizeOnDisk, BulkDataFlags);
			Ar << BulkDataOffsetInFile;

			// Oct 2019: We erroneously wrote uint16 ChunkID during the saving of BulkData; it should've been written only when saving AND cooking to avoid writing it to the uasset or DDC.
			// To fix this we use a flag that was also set by those bad versions - BULKDATA_BadDataVersion - to identify them here when loading and load and discard the value.
			// TODO: Increase the DDC keys for the data types that write BulkData to the DDC and remove this check and BULKDATA_BadDataVersion.
			if ((BulkDataFlags & BULKDATA_BadDataVersion) != 0)
			{
				uint16 DummyValue;
				Ar << DummyValue;
				ClearLocalBulkDataFlags(BULKDATA_BadDataVersion);
			}

			EBulkDataFlags DuplicateDataFlags = static_cast<EBulkDataFlags>(0);
			int64 DuplicateSizeOnDisk = 0;
			int64 DuplicateDataOffsetInFile = 0;
			if (BulkDataFlags & BULKDATA_DuplicateNonOptionalPayload)
			{
				Ar << DuplicateDataFlags;
				SerializeBulkDataSizeInt(Ar, DuplicateSizeOnDisk, DuplicateDataFlags);
				Ar << DuplicateDataOffsetInFile;
			}
			
			// @todo when Landscape (and others?) only Lock/Unlock once, we can enable this
			if (false) // FPlatformProperties::RequiresCookedData())
			{
				// Bulk data that is being serialized via seekfree loading is single use only. This allows us 
				// to free the memory as e.g. the bulk data won't be attached to an archive in the case of
				// seek free loading.
				SetLocalBulkDataFlags(BULKDATA_SingleUse);
			}

			// Hacky fix for using cooked data in editor. Cooking sets BULKDATA_SingleUse for textures, but PIEing needs to keep bulk data around.
			if (GIsEditor)
			{
				ClearLocalBulkDataFlags(BULKDATA_SingleUse);
			}

			const bool bPayloadInline = !(BulkDataFlags & BULKDATA_PayloadAtEndOfFile);
			const bool bPayloadInSeparateFile = !bPayloadInline && (BulkDataFlags & BULKDATA_PayloadInSeperateFile);

			// GetLinker
			bool bUseIOStore = false;
#if WITH_EDITOR
			if (bPayloadInSeparateFile && Owner && IsPackageLoadingFromIoDispatcher(Owner->GetPackage(), Ar))
			{
				checkf(!(BulkDataFlags& BULKDATA_WorkspaceDomainPayload),
					TEXT("%s IsUsingEventDrivenLoader but has a bulkdata with BULKDATA_WorkspaceDomainPayload. ")
					TEXT("BULKDATA_WorkspaceDomainPayload is not supported with iostore."), *Ar.GetArchiveName());
				SetLocalBulkDataFlags(BULKDATA_UsesIoDispatcher);
				PackageId = Owner->GetPackage()->GetPackageId();
				bUseIOStore = true;
			}

			if (Owner != nullptr)
			{
				Linker = Owner->GetLinker();
			}
#else
			FLinkerLoad* Linker = nullptr;
			if (Owner != nullptr)
			{
				Package = Owner->GetOutermost();
				check(Package.IsValid());
				Linker = FLinkerLoad::FindExistingLinkerForPackage(Package.Get());
				check(!Owner->GetLinker() || Owner->GetLinker() == Linker);
			}
#endif

			// fix up the file offset if the offset is relative to the file's bulkdata section
			if (!bPayloadInline && NeedsOffsetFixup())
			{
				check(Linker);
				check(!bUseIOStore);
				BulkDataOffsetInFile += Linker->Summary.BulkDataStartOffset;
			}

			// Get the PackagePath and PackageSegment
			PackagePath = FPackagePath();
			if (Linker)
			{
				PackagePath = Linker->GetPackagePath();
			}
			PackageSegment = EPackageSegment::Header;
			if (bPayloadInSeparateFile)
			{
				if (BulkDataFlags & BULKDATA_DuplicateNonOptionalPayload)
				{
					// The (required) payload is stored in both the default segment and the optional segment.
					// Load it from the optional segment; this is preferable to loading it from the default segment
					// because it reduces seek times if we want to load optional bulk data elsewhere in this package.
					if (!bUseIOStore && IPackageResourceManager::Get().DoesPackageExist(PackagePath, EPackageSegment::BulkDataOptional))
					{
						PackageSegment = EPackageSegment::BulkDataOptional;
						BulkDataFlags = static_cast<EBulkDataFlags>((DuplicateDataFlags | AddedFlags) & ~RemovedFlags);
						SetBulkDataFlags(BULKDATA_OptionalPayload | BULKDATA_PayloadInSeperateFile | BULKDATA_PayloadAtEndOfFile);
						BulkDataOffsetInFile = DuplicateDataOffsetInFile;
						if (NeedsOffsetFixup())
						{
							check(Linker);
							BulkDataOffsetInFile += Linker->Summary.BulkDataStartOffset;
						}
					}
					else
					{
						PackageSegment = EPackageSegment::BulkDataDefault;
					}
				}
				else if (BulkDataFlags & BULKDATA_OptionalPayload)
				{
					PackageSegment = EPackageSegment::BulkDataOptional;
				}
				else if (BulkDataFlags & BULKDATA_MemoryMappedPayload)
				{
					PackageSegment = EPackageSegment::BulkDataMemoryMapped;
				}
				else if (BulkDataFlags & BULKDATA_WorkspaceDomainPayload)
				{
					// PackageSegment is set to BulkDataDefault for debug name purposes. The segment is not used for loading.
					PackageSegment = EPackageSegment::BulkDataDefault;
				}
				else
				{
					PackageSegment = EPackageSegment::BulkDataDefault;
				}
			}
			else
			{
				check(PackageSegment == EPackageSegment::Header)
				if (GEventDrivenLoaderEnabled)
				{
					BulkDataOffsetInFile -= IPackageResourceManager::Get().FileSize(PackagePath, PackageSegment);
					check(BulkDataOffsetInFile >= 0);
					PackageSegment = EPackageSegment::Exports;
				}
			}

			FArchive* CacheableArchive = Ar.GetCacheableArchive();
			if ((Ar.IsAllowingLazyLoading() && CacheableArchive) || bUseIOStore)
			{
				SetLocalBulkDataFlags(BULKDATA_LazyLoadable);

				// Deferred serialization is allowed by the archive/caller. Set flags for how to read
				// the data, but skip synchronous reading of it until TryLoadDataIntoMemory is called.
#if WITH_EDITOR
				if (CacheableArchive != nullptr)
				{
					CacheableArchive->AttachBulkData(Owner, this);
					check(!CacheableArchive->IsTextFormat());
					AttachedAr = CacheableArchive;
				}
#endif // WITH_EDITOR

				if (bPayloadInline)
				{
					// Inline payloads load immediately even though the archive/caller is allowing deferred loads.

					// Check whether we should load the data asynchronously before we proceed to synchronous or mapped load.
					if (ShouldStreamBulkData(Ar))
					{
						// Serialize the bulk data asynchronously, and start the task immediately
						StartSerializingBulkData(Ar, Owner, Idx, bPayloadInline);
					}
					// Check whether we should memory map the data before we proceed to synchronous load.
					else if (bAttemptFileMapping &&
						BulkData.MapFile(PackagePath, PackageSegment, BulkDataOffsetInFile, GetBulkDataSize()))
					{
						UE_LOG(LogSerialization, Error, TEXT("Attempt to file map inline bulk data. This is not desireable. File %s"),
							*PackagePath.GetDebugName(PackageSegment));
						// we need to seek past the inline bulk data 
						Ar.Seek(Ar.Tell() + GetBulkDataSize());
					}
					else
					{
						// Force non-lazy loading of inline bulk data to prevent PostLoad spikes.
						BulkData.Reallocate(GetBulkDataSize(), BulkDataAlignment);
						// if the payload is stored inline, just serialize it
						SerializeBulkData(Ar, BulkData.Get(), BulkDataFlags);
					}
				}
				else
				{
					if (bAttemptFileMapping)
					{
						if (IsInExternalResource())
						{
							// FileMapping not supported, and they they will be looking for the memory, do a sync load now.
							ForceBulkDataResident();
						}
						else
						{
							if (!BulkData.MapFile(PackagePath, PackageSegment, BulkDataOffsetInFile, GetBulkDataSize()))
							{
								// we failed to map when requested, so they will be looking for the memory, do a sync load now.
								ForceBulkDataResident();
							}
						}
					}
				}
			}
			else
			{
				ClearLocalBulkDataFlags(BULKDATA_LazyLoadable);
				// Serialize the bulk data right away.
				// bAttemptFileMapping is ignored in this case, since we are going to load the memory before returning anyway.

				// Check whether we should load the data asynchronously before we proceed to synchronous load.
				if (ShouldStreamBulkData(Ar))
				{
					StartSerializingBulkData(Ar, Owner, Idx, bPayloadInline);
				}
				else
				{
					BulkData.Reallocate( GetBulkDataSize(), BulkDataAlignment );

					if (bPayloadInline)
					{
						// if the payload is stored inline, just serialize it
						SerializeBulkData(Ar, BulkData.Get(), BulkDataFlags);
					}
					else
					{
						// if the payload is NOT stored inline ...
						if (bPayloadInSeparateFile)
						{
							SetLocalBulkDataFlags(BULKDATA_LazyLoadable); // Separate files are always lazy loadable

							// open separate bulk data file
							UE_CLOG(GEventDrivenLoaderEnabled, LogSerialization, Error, TEXT("Attempt to sync load bulk data with EDL enabled (separate file). This is not desireable. File %s"), *PackagePath.GetDebugName(PackageSegment));
							TUniquePtr<FArchive> TargetArchive;
							if (IsInExternalResource())
							{
								TargetArchive = IPackageResourceManager::Get().OpenReadExternalResource(EPackageExternalResource::WorkspaceDomainFile, PackagePath.GetPackageName());
								checkf(TargetArchive.IsValid(), TEXT("Attempted to load bulk data from invalid WorkspaceDomain package '%s'."),
									*PackagePath.GetPackageName());
							}
							else
							{
								FOpenPackageResult OpenResult = IPackageResourceManager::Get().OpenReadPackage(PackagePath, PackageSegment);
								checkf(OpenResult.Archive.IsValid() && OpenResult.Format == EPackageFormat::Binary, TEXT("Attempted to load bulk data from an invalid PackagePath '%s': %s."),
									*PackagePath.GetDebugName(PackageSegment), (!OpenResult.Archive.IsValid() ? TEXT("could not find package") : TEXT("package is a TextAsset which is not supported")));
								TargetArchive = MoveTemp(OpenResult.Archive);
							}

							// seek to the location in the file where the payload is stored
							TargetArchive->Seek(BulkDataOffsetInFile);
							// serialize the payload
							SerializeBulkData(*TargetArchive, BulkData.Get(), BulkDataFlags);
						}
						else
						{
							UE_CLOG(GEventDrivenLoaderEnabled, LogSerialization, Error, TEXT("Attempt to sync load bulk data with EDL enabled. This is not desireable. File %s"), *PackagePath.GetDebugName(PackageSegment));

							// store the current file offset
							int64 CurOffset = Ar.Tell();
							// seek to the location in the file where the payload is stored
							Ar.Seek(BulkDataOffsetInFile);
							// serialize the payload
							SerializeBulkData(Ar, BulkData.Get(), BulkDataFlags);
							// seek to the location we came from
							Ar.Seek(CurOffset);
						}
					}
  				}
			}
		}
		// We're saving to the persistent archive.
		else if ( Ar.IsSaving() )
		{
			// Make sure bulk data is loaded.
			MakeSureBulkDataIsLoaded();

			// Make mutable copies of the bulkdata location variables
			EBulkDataFlags LocalBulkDataFlags = BulkDataFlags;
			int64 LocalBulkDataSizeOnDisk = BulkDataSizeOnDisk;
			int64 LocalBulkDataOffsetInFile = BulkDataOffsetInFile;

			// If the bulk data size is greater than can be held in an int32, then potentially the ElementCount
			// and BulkDataSizeOnDisk need to be held as int64s, so set a flag indicating the new format.
			if (GetBulkDataSize() >= (1LL << 31))
			{
				SetBulkDataFlagsOn(LocalBulkDataFlags, BULKDATA_Size64Bit);
			}
			else
			{
				ClearBulkDataFlagsOn(LocalBulkDataFlags, BULKDATA_Size64Bit);
			}
			// Remove single element serialization requirement before saving out bulk data flags.
			ClearBulkDataFlagsOn(LocalBulkDataFlags, BULKDATA_ForceSingleElementSerialization);

			// Save offset where we are serializing BulkDataFlags and store a placeholder
			int64 SavedBulkDataFlagsPos = Ar.Tell();
			{
				Ar << LocalBulkDataFlags;
			}

			// Number of elements in array.
			SerializeBulkDataSizeInt(Ar, ElementCount, LocalBulkDataFlags);

			// Only serialize status information if wanted.
			int64 SavedBulkDataSizeOnDiskPos	= INDEX_NONE;
			int64 SavedBulkDataOffsetInFilePos	= INDEX_NONE;
			
			{
				// Save offset where we are serializing BulkDataSizeOnDisk and store a placeholder
				SavedBulkDataSizeOnDiskPos = Ar.Tell();
				LocalBulkDataSizeOnDisk = INDEX_NONE;
				SerializeBulkDataSizeInt(Ar, LocalBulkDataSizeOnDisk, LocalBulkDataFlags);

				// Save offset where we are serializing BulkDataOffsetInFile and store a placeholder
				SavedBulkDataOffsetInFilePos = Ar.Tell();
				LocalBulkDataOffsetInFile = INDEX_NONE;
				Ar << LocalBulkDataOffsetInFile;
			}

			// try to get the linkersave object
			FLinkerSave* LinkerSave = Cast<FLinkerSave>(Ar.GetLinker());

			// determine whether we are going to store the payload inline or not.
			bool bStoreInline = !!(LocalBulkDataFlags & BULKDATA_ForceInlinePayload) || !LinkerSave || Ar.IsTextFormat();
			if (Ar.IsCooking() && !(LocalBulkDataFlags & BULKDATA_Force_NOT_InlinePayload))
			{
				bStoreInline = true;
			}

			if (!bStoreInline)
			{
				// set the flag indicating where the payload is stored
				SetBulkDataFlagsOn(LocalBulkDataFlags, BULKDATA_PayloadAtEndOfFile);
				ClearBulkDataFlagsOn(LocalBulkDataFlags,
					static_cast<EBulkDataFlags>(BULKDATA_PayloadInSeperateFile | BULKDATA_WorkspaceDomainPayload)); // SavePackageUtilities::SaveBulkData will add these back if required

				// with no LinkerSave we have to store the data inline
				check(LinkerSave != NULL);

				// add the bulkdata storage info object to the linkersave
				FLinkerSave::FBulkDataStorageInfo& BulkStore = LinkerSave->BulkDataToAppend.AddZeroed_GetRef();

				BulkStore.BulkDataOffsetInFilePos = SavedBulkDataOffsetInFilePos;
				BulkStore.BulkDataSizeOnDiskPos = SavedBulkDataSizeOnDiskPos;
				BulkStore.BulkDataFlagsPos = SavedBulkDataFlagsPos;
				BulkStore.BulkDataFlags = LocalBulkDataFlags;
				BulkStore.BulkDataFileRegionType = FileRegionType;
				BulkStore.BulkData = this;

				// If having flag BULKDATA_DuplicateNonOptionalPayload, duplicate bulk data in optional storage (.uptnl)
				if (LocalBulkDataFlags & BULKDATA_DuplicateNonOptionalPayload)
				{
					int64 SavedDupeBulkDataFlagsPos = INDEX_NONE;
					int64 SavedDupeBulkDataSizeOnDiskPos = INDEX_NONE;
					int64 SavedDupeBulkDataOffsetInFilePos = INDEX_NONE;

					EBulkDataFlags SavedDupeBulkDataFlags = static_cast<EBulkDataFlags>(
						(LocalBulkDataFlags & ~BULKDATA_DuplicateNonOptionalPayload) | BULKDATA_OptionalPayload);
					{
						// Save offset where we are serializing SavedDupeBulkDataFlags and store a placeholder
						SavedDupeBulkDataFlagsPos = Ar.Tell();
						Ar << SavedDupeBulkDataFlags;

						// Save offset where we are serializing SavedDupeBulkDataSizeOnDisk and store a placeholder
						SavedDupeBulkDataSizeOnDiskPos = Ar.Tell();
						int64 DupeBulkDataSizeOnDisk = INDEX_NONE;
						SerializeBulkDataSizeInt(Ar, DupeBulkDataSizeOnDisk, SavedDupeBulkDataFlags);

						// Save offset where we are serializing SavedDupeBulkDataOffsetInFile and store a placeholder
						SavedDupeBulkDataOffsetInFilePos = Ar.Tell();
						int64 DupeBulkDataOffsetInFile = INDEX_NONE;
						Ar << DupeBulkDataOffsetInFile;
					}

					// add duplicate bulkdata with different flag
					FLinkerSave::FBulkDataStorageInfo& DupeBulkStore = LinkerSave->BulkDataToAppend.AddZeroed_GetRef();

					DupeBulkStore.BulkDataOffsetInFilePos = SavedDupeBulkDataOffsetInFilePos;
					DupeBulkStore.BulkDataSizeOnDiskPos = SavedDupeBulkDataSizeOnDiskPos;
					DupeBulkStore.BulkDataFlagsPos = SavedDupeBulkDataFlagsPos;
					DupeBulkStore.BulkDataFlags = SavedDupeBulkDataFlags;
					DupeBulkStore.BulkDataFileRegionType = FileRegionType;
					DupeBulkStore.BulkData = this;
				}
			}
			else
			{
				// set the flag indicating where the payload is stored
				ClearBulkDataFlagsOn(LocalBulkDataFlags,
					static_cast<EBulkDataFlags>(BULKDATA_PayloadAtEndOfFile | BULKDATA_PayloadInSeperateFile | BULKDATA_WorkspaceDomainPayload));

				int64 SavedBulkDataStartPos = Ar.Tell();

				// Serialize bulk data.
				if (FileRegionType != EFileRegionType::None)
				{
					Ar.PushFileRegionType(FileRegionType);
				}
				SerializeBulkData(Ar, BulkData.Get(), LocalBulkDataFlags);
				if (FileRegionType != EFileRegionType::None)
				{
					Ar.PopFileRegionType();
				}

				// store the payload endpos
				int64 SavedBulkDataEndPos = Ar.Tell();

				checkf(SavedBulkDataStartPos >= 0 && SavedBulkDataEndPos >= 0,
					TEXT("Bad archive positions for bulkdata. StartPos=%d EndPos=%d"),
					SavedBulkDataStartPos, SavedBulkDataEndPos);

				LocalBulkDataSizeOnDisk = SavedBulkDataEndPos - SavedBulkDataStartPos;
				LocalBulkDataOffsetInFile = SavedBulkDataStartPos;

				// Since we are storing inline we are not relying on SavePackageUtilities::SaveBulkData to update the placeholder
				// location data, so we need to do it here.

				// store current file offset before seeking back
				int64 CurrentFileOffset = Ar.Tell();
				{
					// Seek back and overwrite the flags 
					Ar.Seek(SavedBulkDataFlagsPos);
					Ar << LocalBulkDataFlags;

					// Seek back and overwrite placeholder for BulkDataSizeOnDisk
					Ar.Seek(SavedBulkDataSizeOnDiskPos);
					SerializeBulkDataSizeInt(Ar, LocalBulkDataSizeOnDisk, LocalBulkDataFlags);

					// Seek back and overwrite placeholder for BulkDataOffsetInFile
					Ar.Seek(SavedBulkDataOffsetInFilePos);
					Ar << LocalBulkDataOffsetInFile;
				}
				// Seek to the end of written data so we don't clobber any data in subsequent writes
				Ar.Seek(CurrentFileOffset);

#if WITH_EDITOR
				// If we are overwriting the LoadedPath for the current package, set the location variables on *this equal to the new values
				// that we are writing into the package on disk
				if (LinkerSave && LinkerSave->bUpdatingLoadedPath)
				{
					SetFlagsFromDiskWrittenValues(LocalBulkDataFlags, LocalBulkDataOffsetInFile, LocalBulkDataSizeOnDisk,
						INDEX_NONE /* LinkerSummaryBulkDataStartOffset, not applicable */);
				}
#endif
			}
		}
	}
}

#if WITH_EDITOR

void FUntypedBulkData::SetFlagsFromDiskWrittenValues(EBulkDataFlags InBulkDataFlags, int64 InBulkDataOffsetInFile, int64 InBulkDataSizeOnDisk,
	int64 LinkerSummaryBulkDataStartOffset)
{
	auto SetLocalBulkDataFlags = [&InBulkDataFlags](EBulkDataFlags BulkDataFlagsToSet)
	{
		InBulkDataFlags = static_cast<EBulkDataFlags>(InBulkDataFlags | BulkDataFlagsToSet);
	};
	auto ClearLocalBulkDataFlags = [&InBulkDataFlags](EBulkDataFlags BulkDataFlagsToClear)
	{
		InBulkDataFlags = static_cast<EBulkDataFlags>(InBulkDataFlags & ~BulkDataFlagsToClear);
	};

	check(!(InBulkDataFlags & BULKDATA_BadDataVersion)); // This is a legacy flag that should no longer be set when saving
	if (GIsEditor)
	{
		ClearLocalBulkDataFlags(BULKDATA_SingleUse); // SingleUse is not used in editor, see the notes in Serialize
	}
#if WITH_IOSTORE_IN_EDITOR
	// We are no longer loading from iostore, even if we were before; our data is now stored in the loose file we saved
	ClearLocalBulkDataFlags(BULKDATA_UsesIoDispatcher);
#endif // WITH_IOSTORE_IN_EDITOR

	const bool bPayloadInline = !(InBulkDataFlags & BULKDATA_PayloadAtEndOfFile);
	const bool bPayloadInSeparateFile = !bPayloadInline && (InBulkDataFlags & BULKDATA_PayloadInSeperateFile);
	// fix up the file offset if the offset is relative to the file's bulkdata section
	if (!bPayloadInline && NeedsOffsetFixup())
	{
		check(LinkerSummaryBulkDataStartOffset >= 0);
		InBulkDataOffsetInFile += LinkerSummaryBulkDataStartOffset;
	}

	// PackagePath does not change, but segment might
	PackageSegment = EPackageSegment::Header;
	if (bPayloadInSeparateFile)
	{
		if (InBulkDataFlags & BULKDATA_DuplicateNonOptionalPayload)
		{
			// Ignore the duplicate and use the non-optional data. The performance benefit of loading from optional data
			// is not necessary in this edge case of reloading bulk data after a save.
			PackageSegment = EPackageSegment::BulkDataDefault;
		}
		else if (InBulkDataFlags & BULKDATA_OptionalPayload)
		{
			PackageSegment = EPackageSegment::BulkDataOptional;
		}
		else if (InBulkDataFlags & BULKDATA_MemoryMappedPayload)
		{
			PackageSegment = EPackageSegment::BulkDataMemoryMapped;
		}
		else if (InBulkDataFlags & BULKDATA_WorkspaceDomainPayload)
		{
			// PackageSegment is set to BulkDataDefault for debug name purposes. The segment is not used for loading.
			PackageSegment = EPackageSegment::BulkDataDefault;
		}
		else
		{
			PackageSegment = EPackageSegment::BulkDataDefault;
		}
	}
	else
	{
		check(PackageSegment == EPackageSegment::Header);
		// Note that we're assuming the file we wrote on disk is not split into header and exports.
		// If we do need to support that, we will need to do a callback to update the BulkDataOffsetInFile once we know the header size
	}

	BulkDataFlags = InBulkDataFlags;
	BulkDataOffsetInFile = InBulkDataOffsetInFile;
	BulkDataSizeOnDisk = InBulkDataSizeOnDisk;
}
#endif

FCustomVersionContainer FUntypedBulkData::GetCustomVersions(FArchive& InlineArchive)
{
	if (!IsInSeparateFile())
	{
		return InlineArchive.GetCustomVersions();
	}
	else if (!IsInExternalResource())
	{
		// The BulkData is in a sidecar file. These files were created with the same custom versions that
		// the package file containing the BulkData used
		return InlineArchive.GetCustomVersions();
	}
	else
	{
		// Read the CustomVersions out of the separate package file
		TUniquePtr<FArchive> ExternalArchive = IPackageResourceManager::Get().OpenReadExternalResource(
			EPackageExternalResource::WorkspaceDomainFile, PackagePath.GetPackageName());
		if (ExternalArchive.IsValid())
		{
			FPackageFileSummary PackageFileSummary;
			*ExternalArchive << PackageFileSummary;
			if (PackageFileSummary.Tag == PACKAGE_FILE_TAG && !ExternalArchive->IsError())
			{
				return PackageFileSummary.GetCustomVersionContainer();
			}
		}
		return FCustomVersionContainer();
	}
}

/*-----------------------------------------------------------------------------
	Class specific virtuals.
-----------------------------------------------------------------------------*/

/**
 * Returns whether single element serialization is required given an archive. This e.g.
 * can be the case if the serialization for an element changes and the single element
 * serialization code handles backward compatibility.
 */
bool FUntypedBulkData::RequiresSingleElementSerialization( FArchive& Ar )
{
	return false;
}

/*-----------------------------------------------------------------------------
	Accessors for friend classes FLinkerLoad and content cookers.
-----------------------------------------------------------------------------*/

#if WITH_EDITOR
/**
 * Detaches the bulk data from the passed in archive. Needs to match the archive we are currently
 * attached to.
 *
 * @param Ar						Archive to detach from
 * @param bEnsureBulkDataIsLoaded	whether to ensure that bulk data is load before detaching from archive
 */
void FUntypedBulkData::DetachFromArchive( FArchive* Ar, bool bEnsureBulkDataIsLoaded )
{
	check( Ar );
	check( Ar == AttachedAr || AttachedAr == nullptr || AttachedAr->IsProxyOf(Ar) );
	check( LockStatus == LOCKSTATUS_Unlocked );

	// Make sure bulk data is loaded.
	if( bEnsureBulkDataIsLoaded )
	{
		MakeSureBulkDataIsLoaded();
	}

	// Detach from archive.
	AttachedAr = nullptr;
	Linker = nullptr;
}
#endif // WITH_EDITOR

void FUntypedBulkData::StoreCompressedOnDisk(ECompressionFlags CompressionFlags)
{
	StoreCompressedOnDisk(FCompression::GetCompressionFormatFromDeprecatedFlags(CompressionFlags));
}

void FUntypedBulkData::StoreCompressedOnDisk( FName CompressionFormat )
{
	if( CompressionFormat != GetDecompressionFormat() )
	{
		//Need to force this to be resident so we don't try to load data as though it were compressed when it isn't.
		ForceBulkDataResident();

		if( CompressionFormat == NAME_None )
		{
			// clear all compression settings
			ClearBulkDataFlags(BULKDATA_SerializeCompressed);
		}
		else
		{
			// right BulkData only knows zlib
			check(CompressionFormat == NAME_Zlib);
			const uint32 FlagToSet = CompressionFormat == NAME_Zlib ? BULKDATA_SerializeCompressedZLIB : BULKDATA_None;
			SetBulkDataFlags(FlagToSet);

			// make sure we are not forcing the bulkdata to be stored inline if we use compression
			ClearBulkDataFlags(BULKDATA_ForceInlinePayload);
		}
	}
}


/*-----------------------------------------------------------------------------
	Internal helpers
-----------------------------------------------------------------------------*/

/**
 * Copies bulk data from passed in structure.
 *
 * @param	Other	Bulk data object to copy from.
 */
void FUntypedBulkData::Copy( const FUntypedBulkData& Other )
{
	// Only copy if there is something to copy.
	if( Other.GetElementCount() )
	{
		// Make sure src is loaded without calling Lock as the object is const.
		check(Other.BulkData);
		check(BulkData);
		check(ElementCount == Other.GetElementCount());
		// Copy from src to dest.
		FMemory::Memcpy( BulkData.Get(), Other.BulkData.Get(), Other.GetBulkDataSize() );
	}
}

/**
 * Helper function initializing all member variables.
 */
void FUntypedBulkData::InitializeMemberVariables()
{
	BulkDataFlags = BULKDATA_None;
	ElementCount = 0;
	BulkDataOffsetInFile = INDEX_NONE;
	BulkDataSizeOnDisk = INDEX_NONE;
	BulkDataAlignment = DEFAULT_ALIGNMENT;
	LockStatus = LOCKSTATUS_Unlocked;
	PackageSegment = EPackageSegment::Header;
#if WITH_EDITOR
	Linker = nullptr;
	AttachedAr = nullptr;
#else
	Package = nullptr;
#endif
}

void FUntypedBulkData::SerializeElements(FArchive& Ar, void* Data)
{
	// Serialize each element individually.				
	for (int64 ElementIndex = 0; ElementIndex < ElementCount; ElementIndex++)
	{
		SerializeElement(Ar, Data, ElementIndex);
	}
}

/**
 * Serialize just the bulk data portion to/ from the passed in memory.
 *
 * @param	Ar					Archive to serialize with
 * @param	Data				Memory to serialize either to or from
 * @param	InBulkDataFlags		Flags describing how the data was/shouldbe serialized
 */
void FUntypedBulkData::SerializeBulkData(FArchive& Ar, void* Data, EBulkDataFlags InBulkDataFlags)
{
	SCOPED_LOADTIMER(BulkData_SerializeBulkData);

	// skip serializing of unused data
	if (InBulkDataFlags & BULKDATA_Unused)
	{
		return;
	}

	// Skip serialization for bulk data of zero length
	const int64 BulkDataSize = GetBulkDataSize();
	if (BulkDataSize == 0)
	{
		return;
	}

	// Allow backward compatible serialization by forcing bulk serialization off if required. Saving also always uses single
	// element serialization so errors or oversight when changing serialization code is recoverable.
	bool bSerializeInBulk = true;
	if (RequiresSingleElementSerialization(Ar) 
	// Set when serialized like a lazy array.
	|| (InBulkDataFlags & BULKDATA_ForceSingleElementSerialization)
	// We use bulk serialization even when saving 1 byte types (texture & sound bulk data) as an optimization for those.
	|| (Ar.IsSaving() && (GetElementSize() > 1)))
	{
		bSerializeInBulk = false;
	}

	// Raw serialize the bulk data without any possibility for potential endian conversion.
	if (bSerializeInBulk)
	{
		// Serialize data compressed.
		if (InBulkDataFlags & BULKDATA_SerializeCompressed)
		{
			Ar.SerializeCompressed(Data, GetBulkDataSize(),
				GetDecompressionFormat(InBulkDataFlags), COMPRESS_NoFlags, false);
		}
		// Uncompressed/ regular serialization.
		else
		{
			Ar.Serialize( Data, GetBulkDataSize() );
		}
	}
	// Serialize an element at a time via the virtual SerializeElement function potentially allowing and dealing with 
	// endian conversion. Dealing with compression makes this a bit more complex as SerializeCompressed expects the 
	// full data to be compressed en block and not piecewise.
	else
	{
		// Serialize data compressed.
		if (InBulkDataFlags & BULKDATA_SerializeCompressed)
		{
			// Loading, data is compressed in archive and needs to be decompressed.
			if (Ar.IsLoading())
			{
				TUniquePtr<uint8[]> SerializedData = MakeUnique<uint8[]>( GetBulkDataSize() );

				// Serialize data with passed in archive and compress.
				Ar.SerializeCompressed(SerializedData.Get(), GetBulkDataSize(),
					GetDecompressionFormat(InBulkDataFlags), COMPRESS_NoFlags, false);

				// Initialize memory reader with uncompressed data array and propagate forced byte swapping
				FLargeMemoryReader MemoryReader(SerializedData.Get(), GetBulkDataSize(), ELargeMemoryReaderFlags::Persistent);
				MemoryReader.SetByteSwapping(Ar.ForceByteSwapping());

				// Serialize each element individually via memory reader.
				SerializeElements(MemoryReader, Data);
			}
			// Saving, data is uncompressed in memory and needs to be compressed.
			else if (Ar.IsSaving())
			{
				// Initialize memory writer with blank data array and propagate forced byte swapping
				FLargeMemoryWriter MemoryWriter(GetBulkDataSize(), true);
				MemoryWriter.SetByteSwapping(Ar.ForceByteSwapping());

				// Serialize each element individually via memory writer.
				SerializeElements(MemoryWriter, Data);

				// Serialize data with passed in archive and compress.
				Ar.SerializeCompressed(MemoryWriter.GetData(), GetBulkDataSize(),
					GetDecompressionFormat(InBulkDataFlags), COMPRESS_NoFlags, false);
			}
		}
		// Uncompressed/ regular serialization.
		else
		{
			// We can use the passed in archive if we're not compressing the data.
			SerializeElements(Ar, Data);
		}
	}
}

void FUntypedBulkData::SerializeBulkData(FArchive& Ar, void* Data)
{
	SerializeBulkData(Ar, Data, this->BulkDataFlags);
}

IAsyncReadFileHandle* FUntypedBulkData::OpenAsyncReadHandle() const
{
#if WITH_IOSTORE_IN_EDITOR
	if (IsUsingIODispatcher())
	{
		return UE::BulkData::Private::CreateAsyncReadHandle(CreateChunkId());
	}
#endif //WITH_IOSTORE_IN_EDITOR
	
	FOpenAsyncPackageResult OpenResult;
	if (IsInExternalResource())
	{
		OpenResult = IPackageResourceManager::Get().OpenAsyncReadExternalResource(EPackageExternalResource::WorkspaceDomainFile,
			GetPackagePath().GetPackageName());
	}
	else
	{
		OpenResult = IPackageResourceManager::Get().OpenAsyncReadPackage(GetPackagePath(), GetPackageSegment());
	}
	return OpenResult.Handle.Release();
}

IBulkDataIORequest* FUntypedBulkData::CreateStreamingRequest(EAsyncIOPriorityAndFlags Priority, FBulkDataIORequestCallBack* CompleteCallback, uint8* UserSuppliedMemory) const
{
	const int64 DataSize = GetBulkDataSize();

	return CreateStreamingRequest(0, DataSize, Priority, CompleteCallback, UserSuppliedMemory);
}

IBulkDataIORequest* FUntypedBulkData::CreateStreamingRequest(int64 OffsetInBulkData, int64 BytesToRead, EAsyncIOPriorityAndFlags Priority, FBulkDataIORequestCallBack* CompleteCallback, uint8* UserSuppliedMemory) const
{
#if WITH_IOSTORE_IN_EDITOR
	if (IsUsingIODispatcher())
	{
		TUniquePtr<IBulkDataIORequest> Request = CreateBulkDataIoDispatcherRequest(CreateChunkId(), BulkDataOffsetInFile + OffsetInBulkData, BytesToRead, CompleteCallback, UserSuppliedMemory, ConvertToIoDispatcherPriority(Priority));
		return Request.Release();
	}
#endif
	check(PackagePath.IsEmpty() == false);

	if (GEventDrivenLoaderEnabled)
	{
		// Verify that the Serialize function converted a Header Segment and Offset to be relative to the Exports segment
		check(IsInExternalResource() || PackageSegment != EPackageSegment::Header);
		// Even the Exports segment is bad for performance
		UE_CLOG(PackageSegment == EPackageSegment::Exports,
			LogSerialization, Error, TEXT("Streaming from the .uexp file '%s' this MUST be in a ubulk instead for best performance."), *PackagePath.GetDebugName(PackageSegment));
	}

	UE_CLOG(IsStoredCompressedOnDisk(), LogSerialization, Fatal, TEXT("Package level compression is no longer supported (%s)."), *PackagePath.GetDebugName(PackageSegment));
	UE_CLOG(GetBulkDataSize() <= 0, LogSerialization, Error, TEXT("(%s) has invalid bulk data size."), *PackagePath.GetDebugName(PackageSegment));

	FOpenAsyncPackageResult OpenResult;
	if (IsInExternalResource())
	{
		OpenResult = IPackageResourceManager::Get().OpenAsyncReadExternalResource(
			EPackageExternalResource::WorkspaceDomainFile, PackagePath.GetPackageName());
	}
	else
	{
		OpenResult = IPackageResourceManager::Get().OpenAsyncReadPackage(PackagePath, PackageSegment);
	}
	IAsyncReadFileHandle* IORequestHandle = OpenResult.Handle.Release();
	check(IORequestHandle); // this generally cannot fail because it is async

	if (IORequestHandle == nullptr)
	{
		return nullptr;
	}

	const int64 OffsetInFile = BulkDataOffsetInFile + OffsetInBulkData;

	FBulkDataIORequest* IORequest = new FBulkDataIORequest(IORequestHandle);

	if (IORequest->MakeReadRequest(OffsetInFile, BytesToRead, Priority, CompleteCallback, UserSuppliedMemory))
	{
		return IORequest;
	}
	else
	{
		delete IORequest;
		return nullptr;
	}
}

IBulkDataIORequest* FUntypedBulkData::CreateStreamingRequestForRange(const BulkDataRangeArray& RangeArray, EAsyncIOPriorityAndFlags Priority, FBulkDataIORequestCallBack* CompleteCallback)
{	
	check(RangeArray.Num() > 0);

	const FUntypedBulkData& Start = *(RangeArray[0]);
	const FUntypedBulkData& End = *(RangeArray[RangeArray.Num()-1]);

	const FPackagePath& PackagePath = Start.GetPackagePath();
	const EPackageSegment PackageSegment = Start.GetPackageSegment();
	check(PackagePath.IsEmpty() == false);

	// TODO: The caller is assuming that their specified PackageSegments are the way in which bulkdata is stored for their package
	// To allow more flexible bulkdata storage, we will need to eliminate this interface and have them provide an array of bulkdatas.
	FOpenAsyncPackageResult OpenResult = IPackageResourceManager::Get().OpenAsyncReadPackage(PackagePath, PackageSegment);
	IAsyncReadFileHandle* IORequestHandle = OpenResult.Handle.Release();
	check(IORequestHandle); // this generally cannot fail because it is async

	if (IORequestHandle == nullptr)
	{
		return nullptr;
	}

	const int64 ReadOffset = Start.GetBulkDataOffsetInFile();
	const int64 ReadSize = (End.GetBulkDataOffsetInFile() + End.GetBulkDataSize()) - ReadOffset;

	check(ReadSize > 0);

	FBulkDataIORequest* IORequest = new FBulkDataIORequest(IORequestHandle);

	if (IORequest->MakeReadRequest(ReadOffset, ReadSize, Priority, CompleteCallback, nullptr))
	{
		return IORequest;
	}
	else
	{
		delete IORequest;
		return nullptr;
	}
}

FBulkDataIORequest::FBulkDataIORequest(IAsyncReadFileHandle* InFileHandle)
	: FileHandle(InFileHandle)
	, ReadRequest(nullptr)
	, Size(INDEX_NONE)
{
}

FBulkDataIORequest::FBulkDataIORequest(IAsyncReadFileHandle* InFileHandle, IAsyncReadRequest* InReadRequest, int64 BytesToRead)
	: FileHandle(InFileHandle)
	, ReadRequest(InReadRequest)
	, Size(BytesToRead)
{

}

FBulkDataIORequest::~FBulkDataIORequest()
{
	delete ReadRequest;
	delete FileHandle;
}

bool FBulkDataIORequest::MakeReadRequest(int64 Offset, int64 BytesToRead, EAsyncIOPriorityAndFlags PriorityAndFlags, FBulkDataIORequestCallBack* CompleteCallback, uint8* UserSuppliedMemory)
{
	check(ReadRequest == nullptr);

	FBulkDataIORequestCallBack LocalCallback = *CompleteCallback;
	FAsyncFileCallBack AsyncFileCallBack = [LocalCallback, BytesToRead, this](bool bWasCancelled, IAsyncReadRequest* InRequest)
	{
		// In some cases the call to ReadRequest can invoke the callback immediately (if the requested data is cached 
		// in the pak file system for example) which means that FBulkDataIORequest::ReadRequest might not actually be
		// set correctly, so we need to make sure it is assigned before we invoke LocalCallback!
		ReadRequest = InRequest;

		Size = BytesToRead;
		LocalCallback(bWasCancelled, this);
	};

	ReadRequest = FileHandle->ReadRequest(Offset, BytesToRead, PriorityAndFlags, &AsyncFileCallBack, UserSuppliedMemory);
	
	if (ReadRequest != nullptr)
	{
		return true;
	}
	else
	{
		return false;
	}
}

bool FBulkDataIORequest::PollCompletion() const
{
	return ReadRequest->PollCompletion();
}

bool FBulkDataIORequest::WaitCompletion(float TimeLimitSeconds)
{
	return ReadRequest->WaitCompletion(TimeLimitSeconds);
}

uint8* FBulkDataIORequest::GetReadResults()
{
	return ReadRequest->GetReadResults();
}

int64 FBulkDataIORequest::GetSize() const
{
	return Size;
}

void FBulkDataIORequest::Cancel()
{
	ReadRequest->Cancel();
}

/**
 * Loads the bulk data if it is not already loaded.
 */
void FUntypedBulkData::MakeSureBulkDataIsLoaded()
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("FUntypedBulkData::MakeSureBulkDataIsLoaded"), STAT_UBD_MakeSureBulkDataIsLoaded, STATGROUP_Memory);

	// Nothing to do if data is already loaded.
	if( !BulkData )
	{
		if (!IsInGameThread())
		{
			// BulkDatas in the same package share AttachedAr that they get from the LinkerLoad of the package.
			// To make calls to those BulkDatas threadsafe, we need to not use AttachedAr when called multithreaded.
			// LoadBulkDataWithFileReader does so by using a separate Archive to the PackagePath instead of AttachedAr.
			LoadBulkDataWithFileReader();
		}
		// Look for async request first
		if (SerializeFuture.IsValid())
		{
			WaitForAsyncLoading();
			BulkData = MoveTemp(BulkDataAsync);
			ResetAsyncData();
		}
		else
		{
			const int64 BytesNeeded = GetBulkDataSize();
			// Allocate memory for bulk data.
			BulkData.Reallocate(BytesNeeded, BulkDataAlignment);

			// Only load if there is something to load. E.g. we might have just created the bulk data array
			// in which case it starts out with a size of zero.
			if (BytesNeeded > 0)
			{
				if (!TryLoadDataIntoMemory(BulkData.Get()))
				{
					BulkData.Deallocate();
				}
			}
		}
	}
}

void FUntypedBulkData::WaitForAsyncLoading()
{
	check(SerializeFuture.IsValid());
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("FUntypedBulkData::WaitForAsyncLoading"), STAT_UBD_WaitForAsyncLoading, STATGROUP_Memory);
	while (!SerializeFuture.WaitFor(FTimespan::FromMilliseconds(1000.0)))
	{
		UE_LOG(LogSerialization, Warning, TEXT("Waiting for '%s' bulk data (size %" INT64_FMT ") to be loaded longer than 1000ms"), *PackagePath.GetDebugName(PackageSegment), GetBulkDataSizeOnDisk());
	}
	check(BulkDataAsync);
}

bool FUntypedBulkData::FlushAsyncLoading()
{
	bool bIsLoadingAsync = SerializeFuture.IsValid();
	if (bIsLoadingAsync)
	{
		WaitForAsyncLoading();
		check(!BulkData);
		BulkData = MoveTemp(BulkDataAsync);
		ResetAsyncData();
	}
	return bIsLoadingAsync;
}

/**
 * Loads the data from disk into the specified memory block. This requires us still being attached to an
 * archive we can use for serialization.
 *
 * @param Dest Memory to serialize data into
 */
bool FUntypedBulkData::TryLoadDataIntoMemory(void* Dest)
{
	// Try flushing async loading before attempting to load
	if (FlushAsyncLoading())
	{
		FMemory::Memcpy(Dest, BulkData.Get(), GetBulkDataSize());
		return true;
	}

#if WITH_IOSTORE_IN_EDITOR
	if (IsUsingIODispatcher())
	{
		checkf(IsStoredCompressedOnDisk() == false, TEXT("BulkData in the IoStore should not have compression flags set!"));
		const FIoChunkId ChunkId = CreateChunkId();

		TUniquePtr<IBulkDataIORequest> Request = CreateBulkDataIoDispatcherRequest(ChunkId, BulkDataOffsetInFile, GetBulkDataSize(), nullptr, (uint8*)Dest);
		Request->WaitCompletion();

		return true;
	}
#endif //WITH_IOSTORE_IN_EDITOR

#if WITH_EDITOR
	TUniquePtr<FArchive> BulkDataLoadedFile;
	FArchive* BulkDataArchive = nullptr;
	if (IsInSeparateFile())
	{
		if (IsInExternalResource())
		{
			BulkDataLoadedFile = IPackageResourceManager::Get().OpenReadExternalResource(
				EPackageExternalResource::WorkspaceDomainFile, PackagePath.GetPackageName());
			if (!BulkDataLoadedFile.IsValid())
			{
				UE_LOG(LogSerialization, Error, TEXT("Attempted to load bulk data from invalid WorkspaceDomain package '%s'."),
					*PackagePath.GetPackageName());
				return false;
			}
		}
		else
		{
			// The Serialize function set the PackageSegment, and since the payload is in a separate file, we
			// can assert that the PackageSegment that was set is one of the separate-file segments
			check(PackageSegment != EPackageSegment::Header && PackageSegment != EPackageSegment::Exports);
			FOpenPackageResult Result = IPackageResourceManager::Get().OpenReadPackage(PackagePath, PackageSegment);
			if (!Result.Archive.IsValid() || Result.Format != EPackageFormat::Binary)
			{
				UE_LOG(LogSerialization, Error, TEXT("Attempted to load bulk data from an invalid PackagePath '%s'%s."),
					*PackagePath.GetDebugName(PackageSegment), (!Result.Archive.IsValid() ? TEXT("") : TEXT("; package is in non-binary format and this is not supported.")));
				return false;
			}
			BulkDataLoadedFile = MoveTemp(Result.Archive);
		}
		BulkDataArchive = BulkDataLoadedFile.Get();
	}
	else
	{
		if (!AttachedAr)
		{
			UE_LOG(LogSerialization, Error, TEXT("Attempted to load bulk data without an attached archive. ")
				TEXT("Most likely the bulk data was loaded twice on console, which is not supported"));
			return false;
		}
		BulkDataArchive = AttachedAr;
	}

	// Keep track of current position in file so we can restore it later.
	int64 PushedPos = BulkDataArchive->Tell();
	// Seek to the beginning of the bulk data in the file.
	BulkDataArchive->Seek(BulkDataOffsetInFile);

	SerializeBulkData(*BulkDataArchive, Dest, BulkDataFlags);

	// Restore file pointer.
	BulkDataArchive->Seek(PushedPos);
	BulkDataArchive->FlushCache();
	return true;

#else
	bool bWasLoadedSuccessfully = false;
	if (!IsInSeparateFile() && IsInAsyncLoadingThread())
	{
		if (UPackage* PackagePtr = Package.Get())
		{
			if (PackagePtr->GetLinker() && PackagePtr->GetLinker()->GetOwnerThreadId() == FPlatformTLS::GetCurrentThreadId())
			{
				FLinkerLoad* LinkerLoad = PackagePtr->GetLinker();
				if (LinkerLoad && LinkerLoad->HasLoader())
				{
					FArchive* Ar = LinkerLoad;
					// keep track of current position in this archive
					int64 CurPos = Ar->Tell();

					// Seek to the beginning of the bulk data in the file.
					Ar->Seek( BulkDataOffsetInFile );

					// serialize the bulk data
					SerializeBulkData(*Ar, Dest, BulkDataFlags);

					// seek back to the position the archive was before
					Ar->Seek(CurPos);

					// note that we loaded it
					bWasLoadedSuccessfully = true;
				}
			}
		}
	}
	// if we weren't able to load via linker, load directly by PackagePath
	if (!bWasLoadedSuccessfully)
	{
		// load from the specified PackagePath when the linker has been cleared
		checkf(!PackagePath.IsEmpty(), TEXT( "Attempted to load bulk data without a proper PackagePath." ) );

#if PLATFORM_SUPPORTS_TEXTURE_STREAMING
		static auto CVarTextureStreamingEnabled = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.TextureStreaming"));
		check(CVarTextureStreamingEnabled);
		// Because "r.TextureStreaming" is driven by the project setting as well as the command line option "-NoTextureStreaming", 
		// is it possible for streaming mips to be loaded in non streaming ways.
		if (CVarTextureStreamingEnabled->GetValueOnAnyThread() != 0)
		{
			UE_CLOG(GEventDrivenLoaderEnabled && IsInSeparateFile() && (IsInGameThread() || IsInAsyncLoadingThread()), LogSerialization, Error,
				TEXT("Attempt to sync load bulk data with EDL enabled (LoadDataIntoMemory). This is not desireable. File %s"), *PackagePath.GetDebugName(PackageSegment));
		}
#endif

		// Verify that the Serialize function converted a Header Segment and Offset to be relative to the Exports segment, if GEventDrivenLoaderEnabled
		check(!GEventDrivenLoaderEnabled || IsInExternalResource() || PackageSegment != EPackageSegment::Header);

		TUniquePtr<FArchive> BulkArchive;
		if (IsInExternalResource())
		{
			BulkArchive = IPackageResourceManager::Get().OpenReadExternalResource(
				EPackageExternalResource::WorkspaceDomainFile, PackagePath.GetPackageName());
			if (!BulkArchive.IsValid())
			{
				UE_LOG(LogSerialization, Error, TEXT("Attempted to load bulk data from invalid WorkspaceDomain package '%s'."),
					*PackagePath.GetPackageName());
				return false;
			}
		}
		else
		{
			FOpenPackageResult Result = IPackageResourceManager::Get().OpenReadPackage(PackagePath, PackageSegment);
			if (!Result.Archive.IsValid() || Result.Format != EPackageFormat::Binary)
			{
				UE_LOG(LogSerialization, Error, TEXT("Attempted to load bulk data from an invalid PackagePath '%s'%s."),
					*PackagePath.GetDebugName(PackageSegment), (!Result.Archive.IsValid() ? TEXT("") : TEXT("; package is in non-binary format and this is not supported.")));
				return false;
			}
			BulkArchive = MoveTemp(Result.Archive);
		}
	
		// Seek to the beginning of the bulk data in the file.
		BulkArchive->Seek(BulkDataOffsetInFile);
		SerializeBulkData(*BulkArchive, Dest, BulkDataFlags);
	}
	return true;
#endif // WITH_EDITOR
}



/*-----------------------------------------------------------------------------
	uint8 version of bulk data.
-----------------------------------------------------------------------------*/

/**
 * Returns size in bytes of single element.
 *
 * @return Size in bytes of single element
 */
int32 FByteBulkDataOld::GetElementSize() const
{
	return sizeof(uint8);
}

/**
 * Serializes an element at a time allowing and dealing with endian conversion and backward compatiblity.
 * 
 * @param Ar			Archive to serialize with
 * @param Data			Base pointer to data
 * @param ElementIndex	Element index to serialize
 */
void FByteBulkDataOld::SerializeElement( FArchive& Ar, void* Data, int64 ElementIndex )
{
	uint8& ByteData = *((uint8*)Data + ElementIndex);
	Ar << ByteData;
}

/*-----------------------------------------------------------------------------
	uint16 version of bulk data.
-----------------------------------------------------------------------------*/

/**
 * Returns size in bytes of single element.
 *
 * @return Size in bytes of single element
 */
int32 FWordBulkDataOld::GetElementSize() const
{
	return sizeof(uint16);
}

/**
 * Serializes an element at a time allowing and dealing with endian conversion and backward compatiblity.
 * 
 * @param Ar			Archive to serialize with
 * @param Data			Base pointer to data
 * @param ElementIndex	Element index to serialize
 */
void FWordBulkDataOld::SerializeElement( FArchive& Ar, void* Data, int64 ElementIndex )
{
	uint16& WordData = *((uint16*)Data + ElementIndex);
	Ar << WordData;
}

/*-----------------------------------------------------------------------------
	int32 version of bulk data.
-----------------------------------------------------------------------------*/

/**
 * Returns size in bytes of single element.
 *
 * @return Size in bytes of single element
 */
int32 FIntBulkDataOld::GetElementSize() const
{
	return sizeof(int32);
}

/**
 * Serializes an element at a time allowing and dealing with endian conversion and backward compatiblity.
 * 
 * @param Ar			Archive to serialize with
 * @param Data			Base pointer to data
 * @param ElementIndex	Element index to serialize
 */
void FIntBulkDataOld::SerializeElement( FArchive& Ar, void* Data, int64 ElementIndex )
{
	int32& IntData = *((int32*)Data + ElementIndex);
	Ar << IntData;
}

/*-----------------------------------------------------------------------------
	float version of bulk data.
-----------------------------------------------------------------------------*/

/**
 * Returns size in bytes of single element.
 *
 * @return Size in bytes of single element
 */
int32 FFloatBulkDataOld::GetElementSize() const
{
	return sizeof(float);
}

/**
 * Serializes an element at a time allowing and dealing with endian conversion and backward compatiblity.
 * 
 * @param Ar			Archive to serialize with
 * @param Data			Base pointer to data
 * @param ElementIndex	Element index to serialize
 */
void FFloatBulkDataOld::SerializeElement( FArchive& Ar, void* Data, int64 ElementIndex )
{
	float& FloatData = *((float*)Data + ElementIndex);
	Ar << FloatData;
}

void FFormatContainer::Serialize(FArchive& Ar, UObject* Owner, const TArray<FName>* FormatsToSave, bool bSingleUse, uint16 InAlignment, bool bInline, bool bMapped)
{
	if (Ar.IsLoading())
	{
		int32 NumFormats = 0;
		Ar << NumFormats;
		for (int32 Index = 0; Index < NumFormats; Index++)
		{
			FName Name;
			Ar << Name;
			FByteBulkData& Bulk = GetFormat(Name);
#if !USE_NEW_BULKDATA
			Bulk.SetBulkDataAlignment(InAlignment);
#endif
			Bulk.Serialize(Ar, Owner, INDEX_NONE, false);
		}
	}
	else
	{
		check(Ar.IsCooking() && FormatsToSave); // this thing is for cooking only, and you need to provide a list of formats

		int32 NumFormats = 0;
		for (const TPair<FName, FByteBulkData*>& Format : Formats)
		{
			const FName Name = Format.Key;
			FByteBulkData* Bulk = Format.Value;
			check(Bulk);
			if (FormatsToSave->Contains(Name) && Bulk->GetBulkDataSize() > 0)
			{
				NumFormats++;
			}
		}
		Ar << NumFormats;
		for (const TPair<FName, FByteBulkData*>& Format : Formats)
		{
			FName Name = Format.Key;
			FByteBulkData* Bulk = Format.Value;
			if (FormatsToSave->Contains(Name) && Bulk->GetBulkDataSize() > 0)
			{
				NumFormats--;
				Ar << Name;
				// Force this kind of bulk data (physics, etc) to be stored inline for streaming
				const uint32 OldBulkDataFlags = Bulk->GetBulkDataFlags();
				if (bInline)
				{
					Bulk->SetBulkDataFlags(BULKDATA_ForceInlinePayload);
					Bulk->ClearBulkDataFlags(BULKDATA_Force_NOT_InlinePayload | BULKDATA_MemoryMappedPayload);
				}
				else
				{
					Bulk->SetBulkDataFlags(BULKDATA_Force_NOT_InlinePayload);
					if (bMapped)
					{
						Bulk->SetBulkDataFlags(BULKDATA_MemoryMappedPayload);
					}
					Bulk->ClearBulkDataFlags(BULKDATA_ForceInlinePayload);

				}
				if (bSingleUse)
				{
					Bulk->SetBulkDataFlags(BULKDATA_SingleUse);
				}
				Bulk->Serialize(Ar, Owner, INDEX_NONE, false);
				Bulk->ClearBulkDataFlags(0xFFFFFFFF);
				Bulk->SetBulkDataFlags(OldBulkDataFlags);
			}
		}
		check(NumFormats == 0);
	}
}

void FFormatContainer::SerializeAttemptMappedLoad(FArchive& Ar, UObject* Owner)
{
	check(Ar.IsLoading());
	int32 NumFormats = 0;
	Ar << NumFormats;
	for (int32 Index = 0; Index < NumFormats; Index++)
	{
		FName Name;
		Ar << Name;
		FByteBulkData& Bulk = GetFormat(Name);
		Bulk.Serialize(Ar, Owner, -1, true);
	}
}
