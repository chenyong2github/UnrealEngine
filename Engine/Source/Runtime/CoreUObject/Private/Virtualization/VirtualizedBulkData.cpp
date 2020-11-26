// Copyright Epic Games, Inc. All Rights Reserved.

#include "Virtualization/VirtualizedBulkData.h"

#include "HAL/FileManager.h"
#include "Misc/PackageSegment.h"
#include "Misc/SecureHash.h"
#include "Misc/ScopeLock.h"
#include "Serialization/BulkData.h"
#include "UObject/LinkerLoad.h"
#include "UObject/LinkerSave.h"
#include "UObject/Object.h"

//#if WITH_EDITORONLY_DATA

FVirtualizedUntypedBulkData::~FVirtualizedUntypedBulkData()
{
	Reset();
}

void FVirtualizedUntypedBulkData::CreateFromBulkData(FUntypedBulkData& InBulkData, const FGuid& InGuid)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FVirtualizedUntypedBulkData::CreateFromBulkData);

	Reset();

	PackagePath = InBulkData.GetPackagePath();
	PackageSegment = InBulkData.GetPackageSegment();

	OffsetInFile = InBulkData.GetBulkDataOffsetInFile();
	PayloadLength = InBulkData.GetBulkDataSize();
	bIsDataStoredAsCompressed = InBulkData.IsStoredCompressedOnDisk();

	Key = InGuid;
}

void FVirtualizedUntypedBulkData::Serialize(FArchive& Ar, UObject* Owner)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FVirtualizedUntypedBulkData::Serialize);

	if (Ar.IsTransacting())
	{
		// Do not process the transaction if the owner is mid loading (see FUntypedBulkData::Serialize)
		bool bNeedsTransaction = Ar.IsSaving() && (!Owner || !Owner->HasAnyFlags(RF_NeedLoad));

		Ar << bNeedsTransaction;

		if (bNeedsTransaction)
		{
			Ar << Key;
			Ar << PayloadLength;
			Ar << bIsVirtualized;
			Ar << bShouldCompressData;
			Ar << bIsDataStoredAsCompressed;
			Ar << PackagePath;
			Ar << PackageSegment;
			Ar << OffsetInFile;

			bool bKeepBuffer = !Ar.IsLoading();
			if (!bIsVirtualized && PackagePath.IsEmpty())
			{
				// Data is held in memory so we need to serialize it to the archive
				bKeepBuffer = SerializeData(Ar, Payload);
			}
			// TODO:	Do we want to force data to be pulled/loaded and serialized to the archive?
			//			Assuming the virtualized back end does not discard the payload or the file with 
			//			the payload remains intact then it should not be needed?
			
			if (!bKeepBuffer)
			{
				Payload.Reset();
			}
		}
	}
	else if (Ar.IsPersistent() && !Ar.IsObjectReferenceCollector() && !Ar.ShouldSkipBulkData())
	{
		Ar << Key;
		Ar << PayloadLength;

		if (Ar.IsSaving())
		{
			FLinkerSave* LinkerSave = Cast<FLinkerSave>(Ar.GetLinker());

			const bool bCanAttemptVirtualization = LinkerSave != nullptr;

			if (bCanAttemptVirtualization)
			{
				PushData();
			}

			Ar << bIsVirtualized;

			if (!bIsVirtualized)
			{
				// Need to load the payload so that we can write it out
				FSharedBufferConstPtr PayloadToSerialize = GetDataInternal();

				// Need to update the flag before it is written out
				bIsDataStoredAsCompressed = !bIsVirtualized && bShouldCompressData;

				Ar << bIsDataStoredAsCompressed;

				// Write out a dummy value that we will write over once the payload is serialized
				int64 PlaceholderValue = INDEX_NONE;

				const int64 OffsetPos = Ar.Tell();
				Ar << PlaceholderValue; // OffsetInFile

				// The lambda is mutable so that PayloadToSerialize is not const (due to FArchive api not
				// accepting const values)
				auto SerializePayload = [this, OffsetPos, PayloadToSerialize](FArchive& Ar) mutable
				{
					int64 PayloadOffset = Ar.Tell();

					SerializeData(Ar, PayloadToSerialize);

					// Record the current archive offset (probably EOF but we cannot be sure)
					const int64 ReturnPos = Ar.Tell();

					// Update the offset/size entries that we set up during ::Serialize
					Ar.Seek(OffsetPos);
					Ar << PayloadOffset;

					// Restore the archive's offset
					Ar.Seek(ReturnPos);
				};

				// If we have a valid linker then we will defer serialization of the payload so that it will
				// be placed at the end of the output file so we don't have to seek past the payload on load.
				// If we do not have a linker then we might as well just serialize right away.
				if(LinkerSave != nullptr)
				{
					 LinkerSave->AdditionalDataToAppend.Add(MoveTemp(SerializePayload));
				}
				else
				{
					SerializePayload(Ar);
				}
			}
		}
		else if (Ar.IsLoading())
		{
			Ar << bIsVirtualized;
			
			if (bIsVirtualized)
			{
				// We aren't going to use these members so reset them
				bIsDataStoredAsCompressed = false;
				OffsetInFile = INDEX_NONE;

				PackagePath.Empty();
				PackageSegment = EPackageSegment::Header;
			}
			else
			{
				Ar << bIsDataStoredAsCompressed;

				// If we can lazy load then find the PackagePath, otherwise we will want
				// to serialize immediately.
				if (Ar.IsAllowingLazyLoading())
				{
					PackagePath = GetPackagePathFromOwner(Owner, PackageSegment);
				}
				else
				{
					PackagePath.Empty();
					PackageSegment = EPackageSegment::Header;
				}
				
				OffsetInFile = INDEX_NONE;
				Ar << OffsetInFile;

				if (PackagePath.IsEmpty())
				{
					// If we have no packagepath then we need to load the data immediately
					// as we will not be able to load it on demand.
					SerializeData(Ar, Payload);
				}
			}
		}
	}
}

void FVirtualizedUntypedBulkData::CalculateKey()
{
	if (Payload.IsValid())
	{
		uint32 Hash[5] = {};
		FSHA1::HashBuffer(Payload->GetData(), GetBulkDataSize(), (uint8*)Hash);

		Key = FGuid(Hash[0] ^ Hash[4], Hash[1], Hash[2], Hash[3]);
	}
	else
	{
		Key.Invalidate();
	}
}

FSharedBufferConstPtr FVirtualizedUntypedBulkData::LoadFromDisk() const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FVirtualizedUntypedBulkData::LoadFromDisk);

	FSharedBufferConstPtr PayloadFromDisk;

	// Open a reader to the file
	FOpenPackageResult Result = IPackageResourceManager::Get().OpenReadPackage(PackagePath, PackageSegment);
	if (Result.Archive.IsValid() && Result.Format == EPackageFormat::Binary)
	{
		// Move the correct location of the data in the file
		Result.Archive->Seek(OffsetInFile);

		// Now we can actually serialize it
		SerializeData(*Result.Archive, PayloadFromDisk);
	}
	else
	{
		UE_LOG(LogVirtualization, Error, TEXT("Could not open (%s) to read FVirtualizedUntypedBulkData: %s."), *PackagePath.GetDebugNameWithExtension(PackageSegment),
			(!Result.Archive.IsValid() ? TEXT("could not find package") : TEXT("package is a TextAsset which is not supported")));
	}

	return PayloadFromDisk;
}

bool FVirtualizedUntypedBulkData::SerializeData(FArchive& Ar, FSharedBufferConstPtr& InPayload) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FVirtualizedUntypedBulkData::SerializeData);

	const int64 PayloadSize = Ar.IsSaving() ? InPayload->GetSize() : GetBulkDataSize();

	if (PayloadSize > 0)
	{
		// Our serialization methods require a non-const pointer even when we are only reading from it
		// as the methods themselves are bi-directional. This means if we are saving we need to cast 
		// away the const qualifier.
		void* DataPtr = Ar.IsLoading() ?
						FMemory::Malloc(PayloadSize, DEFAULT_ALIGNMENT) :
						const_cast<void*>(InPayload->GetData());

		checkf(DataPtr != nullptr, TEXT("Attempting to serialize to/from an invalid shared buffer!"));

		if (bIsDataStoredAsCompressed)
		{
			Ar.SerializeCompressed(DataPtr, PayloadSize, NAME_Zlib, COMPRESS_NoFlags, false);
		}
		else
		{
			Ar.Serialize(DataPtr, PayloadSize);
		}

		if (Ar.IsLoading())
		{
			InPayload = FSharedBuffer::TakeOwnership(DataPtr, PayloadSize, FMemory::Free);
		}

		return true;
	}
	else
	{
		// If we are loading and we failed to serialize anything then we should reset the SharedBuffer.
		if (Ar.IsLoading())
		{
			InPayload.Reset();
		}

		return false;
	}
}

void FVirtualizedUntypedBulkData::PushData()
{
	checkf(bIsVirtualized == false || Payload.IsValid() == false, TEXT("Cannot have a valid payload in memory if the payload is virtualized!")); // Sanity check

	// We only need to push if the payload is not currently virtualized (either we have an updated
	// payload in memory or the payload is currently non-virtualized and stored on disk)
	if (!bIsVirtualized)
	{ 
		TRACE_CPUPROFILER_EVENT_SCOPE(FVirtualizedUntypedBulkData::PushData);

		// We should only need to load from disk at this point if we are going from
		// a non-virtualized payload to a virtualized one. If the bulkdata is merely being
		// edited then we should have the payload in memory already and are just accessing a
		// reference to it.
		FSharedBufferConstPtr PayloadToPush = GetDataInternal();

		if (FVirtualizationManager::Get().PushData(PayloadToPush, Key))
		{
			bIsVirtualized = true;
		}
	}	
}

FSharedBufferConstPtr FVirtualizedUntypedBulkData::PullData() const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FVirtualizedUntypedBulkData::PullData);

	FSharedBufferConstPtr PulledPayload = FVirtualizationManager::Get().PullData(Key);

	if (PulledPayload.IsValid())
	{
		checkf(	PayloadLength == PulledPayload->GetSize(),	
				TEXT("Mismatch between serialized length (%d) and virtualized data length (%d)"), 
				PayloadLength,
				PulledPayload->GetSize());
	}
	else
	{
		UE_LOG(LogVirtualization, Warning, TEXT("Failed to pull virtual data with guid (%s)"), *Key.ToString());
	}

	return PulledPayload;
}

FPackagePath FVirtualizedUntypedBulkData::GetPackagePathFromOwner(UObject* Owner, EPackageSegment& OutPackageSegment) const
{
	OutPackageSegment = EPackageSegment::Header;
	if (Owner != nullptr)
	{
		UPackage* Package = Owner->GetOutermost();
		checkf(Package != nullptr, TEXT("Owner was not a valid UPackage!"));

		FLinkerLoad* Linker = FLinkerLoad::FindExistingLinkerForPackage(Package);
		checkf(Linker != nullptr, TEXT("UPackage did not have a valid FLinkerLoad!"));

		return Linker->GetPackagePath();
	}
	else
	{
		return FPackagePath();
	}
}

bool FVirtualizedUntypedBulkData::CanUnloadData() const
{
	// We cannot unload the data if are unable to reload it from a file
	return bIsVirtualized || PackagePath.IsEmpty() == false;
}

void FVirtualizedUntypedBulkData::Reset()
{
	PackagePath.Empty();
	PackageSegment = EPackageSegment::Header;
	OffsetInFile = INDEX_NONE;
	Key.Invalidate();
	Payload.Reset();
	PayloadLength = 0;
	bIsVirtualized = false;
	bIsDataStoredAsCompressed = false;
}

void FVirtualizedUntypedBulkData::UnloadData()
{
	Payload.Reset();
}

FSharedBufferConstPtr FVirtualizedUntypedBulkData::GetDataInternal() const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FVirtualizedUntypedBulkData::GetDataInternal);

	// Early out there isn't any data to actually load
	if (GetBulkDataSize() == 0)
	{
		return FSharedBufferConstPtr();
	}

	// Check if we already have the data in memory
	if (Payload.IsValid())
	{
		return Payload;
	}

	if (bIsVirtualized)
	{
		FSharedBufferConstPtr Buffer = PullData();
		check(!Payload.IsValid()); //Make sure that we did not assign the buffer internally
		return Buffer;
	}
	else
	{
		FSharedBufferConstPtr Buffer = LoadFromDisk();
		check(!Payload.IsValid()); //Make sure that we did not assign the buffer internally
		return Buffer;
	}
}

TFuture<FSharedBufferConstPtr> FVirtualizedUntypedBulkData::GetData() const
{
	TPromise<FSharedBufferConstPtr> Promise;

	FSharedBufferConstPtr SharedBuffer = GetDataInternal();

	// TODO: Not actually async yet!
	Promise.SetValue(SharedBuffer);

	return Promise.GetFuture();
}

void FVirtualizedUntypedBulkData::GetData(OnDataReadyCallback&& Callback) const
{
	Callback(GetDataInternal());
}

void FVirtualizedUntypedBulkData::UpdatePayload(const FSharedBufferConstRef& InPayload)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FVirtualizedUntypedBulkData::UpdatePayload);

	UnloadData();

	// Make sure that we own the memory in the shared buffer
	Payload = FSharedBuffer::MakeOwned(InPayload);
	PayloadLength = (int64)InPayload->GetSize();

	bIsVirtualized = false;
	bIsDataStoredAsCompressed = false;

	PackagePath.Empty();
	PackageSegment = EPackageSegment::Header;
	OffsetInFile = INDEX_NONE;

	CalculateKey();
}

//#endif //WITH_EDITORONLY_DATA
