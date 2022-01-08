// Copyright Epic Games, Inc. All Rights Reserved.

#include "PakFileDerivedDataBackend.h"

#include "Algo/Accumulate.h"
#include "Algo/StableSort.h"
#include "Algo/Transform.h"
#include "DerivedDataCachePrivate.h"
#include "DerivedDataCacheRecord.h"
#include "DerivedDataCacheUsageStats.h"
#include "DerivedDataChunk.h"
#include "DerivedDataValue.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFile.h"
#include "HAL/PlatformFileManager.h"
#include "HashingArchiveProxy.h"
#include "Misc/Compression.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/PathViews.h"
#include "Misc/ScopeRWLock.h"
#include "Serialization/CompactBinary.h"
#include "Serialization/CompactBinaryPackage.h"
#include "Serialization/CompactBinaryValidation.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Templates/Greater.h"

namespace UE::DerivedData::CacheStore::PakFile
{

FPakFileDerivedDataBackend::FPakFileDerivedDataBackend(const TCHAR* const InCachePath, const bool bInWriting)
	: bWriting(bInWriting)
	, bClosed(false)
	, CachePath(InCachePath)
{
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	if (bWriting)
	{
		PlatformFile.CreateDirectoryTree(*FPaths::GetPath(CachePath));
		FileHandle.Reset(PlatformFile.OpenWrite(*CachePath, /*bAppend*/ false, /*bAllowRead*/ true));
		if (!FileHandle)
		{
			UE_LOG(LogDerivedDataCache, Fatal, TEXT("%s: Failed to open pak cache for writing."), *CachePath);
			bClosed = true;
		}
		else
		{
			UE_LOG(LogDerivedDataCache, Display, TEXT("%s: Opened pak cache for writing."), *CachePath);
		}
	}
	else
	{
		FileHandle.Reset(PlatformFile.OpenRead(*CachePath));
		if (!FileHandle)
		{
			UE_LOG(LogDerivedDataCache, Warning, TEXT("%s: Failed to open pak cache for reading."), *CachePath);
		}
		else if (!LoadCache(*CachePath))
		{
			FileHandle.Reset();
			CacheItems.Empty();
			bClosed = true;
		}
		else
		{
			UE_LOG(LogDerivedDataCache, Display, TEXT("%s: Opened pak cache for reading. (%" INT64_FMT " MiB)"),
				*CachePath, FileHandle->Size() / 1024 / 1024);
		}
	}
}

FPakFileDerivedDataBackend::~FPakFileDerivedDataBackend()
{
	Close();
}

FString FPakFileDerivedDataBackend::GetName() const
{
	return CachePath;
}

void FPakFileDerivedDataBackend::Close()
{
	FDerivedDataBackend::Get().WaitForQuiescence();
	if (!bClosed)
	{
		if (bWriting)
		{
			SaveCache();
		}
		FWriteScopeLock ScopeLock(SynchronizationObject);
		FileHandle.Reset();
		CacheItems.Empty();
		bClosed = true;
	}
}

bool FPakFileDerivedDataBackend::IsWritable() const
{
	return bWriting && !bClosed;
}

/** Returns a class of speed for this interface **/
FDerivedDataBackendInterface::ESpeedClass FPakFileDerivedDataBackend::GetSpeedClass() const
{
	return ESpeedClass::Local;
}

bool FPakFileDerivedDataBackend::BackfillLowerCacheLevels() const
{
	return false;
}

bool FPakFileDerivedDataBackend::CachedDataProbablyExists(const TCHAR* CacheKey)
{
	COOK_STAT(auto Timer = UsageStats.TimeProbablyExists());
	FReadScopeLock ScopeLock(SynchronizationObject);
	bool Result = CacheItems.Contains(FString(CacheKey));
	if (Result)
	{
		COOK_STAT(Timer.AddHit(0));
	}
	return Result;
}

bool FPakFileDerivedDataBackend::GetCachedData(const TCHAR* CacheKey, TArray<uint8>& OutData)
{
	COOK_STAT(auto Timer = UsageStats.TimeGet());
	if (bClosed)
	{
		return false;
	}
	FWriteScopeLock ScopeLock(SynchronizationObject);
	if (FCacheValue* Item = CacheItems.Find(FString(CacheKey)))
	{
		check(FileHandle);
		ON_SCOPE_EXIT
		{
			if (bWriting)
			{
				FileHandle->SeekFromEnd();
			}
		};
		if (Item->Size >= MAX_int32)
		{
			UE_LOG(LogDerivedDataCache, Warning, TEXT("%s: Pak file, %s exceeds 2 GiB limit."), *CachePath, CacheKey);
		}
		else if (!FileHandle->Seek(Item->Offset))
		{
			UE_LOG(LogDerivedDataCache, Warning, TEXT("%s: Pak file, bad seek."), *CachePath);
		}
		else
		{
			check(Item->Size);
			check(!OutData.Num());
			OutData.AddUninitialized(Item->Size);
			if (!FileHandle->Read(OutData.GetData(), int64(Item->Size)))
			{
				UE_LOG(LogDerivedDataCache, Warning, TEXT("%s: Pak file, bad read."), *CachePath);
			}
			else if (uint32 TestCrc = FCrc::MemCrc_DEPRECATED(OutData.GetData(), Item->Size); TestCrc != Item->Crc)
			{
				UE_LOG(LogDerivedDataCache, Warning, TEXT("%s: Pak file, bad crc."), *CachePath);
			}
			else
			{
				UE_LOG(LogDerivedDataCache, Verbose, TEXT("%s: Cache hit on %s"), *CachePath, CacheKey);
				check(OutData.Num());
				COOK_STAT(Timer.AddHit(OutData.Num()));
				return true;
			}
		}
	}
	else
	{
		UE_LOG(LogDerivedDataCache, Verbose, TEXT("%s: Cache miss on %s"), *CachePath, CacheKey);
	}
	OutData.Empty();
	return false;
}

FDerivedDataBackendInterface::EPutStatus FPakFileDerivedDataBackend::PutCachedData(const TCHAR* CacheKey, TArrayView<const uint8> InData, bool bPutEvenIfExists)
{
	COOK_STAT(auto Timer = UsageStats.TimePut());
	if (!IsWritable())
	{
		return EPutStatus::NotCached;
	}
	{
		FWriteScopeLock ScopeLock(SynchronizationObject);
		FString Key(CacheKey);
		TOptional<uint32> Crc;
		check(InData.Num());
		check(Key.Len());
		check(FileHandle);

		if (bPutEvenIfExists)
		{
			if (FCacheValue* Item = CacheItems.Find(FString(CacheKey)))
			{
				// If there was an existing entry for this key, if it had the same contents, do nothing as the desired value is already stored.
				// If the contents differ, replace it if the size hasn't changed, but if the size has changed, 
				// remove the existing entry from the index but leave they actual data payload in place as it is too
				// costly to go back and attempt to rewrite all offsets and shift all bytes that follow it in the file.
				if (Item->Size == InData.Num())
				{
					COOK_STAT(Timer.AddHit(InData.Num()));
					Crc = FCrc::MemCrc_DEPRECATED(InData.GetData(), InData.Num());
					if (Crc.GetValue() != Item->Crc)
					{
						int64 Offset = FileHandle->Tell();
						FileHandle->Seek(Item->Offset);
						FileHandle->Write(InData.GetData(), InData.Num());
						Item->Crc = Crc.GetValue();
						FileHandle->Seek(Offset);
					}
					return EPutStatus::Cached;
				}

				UE_LOG(LogDerivedDataCache, Warning,
					TEXT("%s: Repeated put of %s with different sized contents. Multiple contents will be in the file, ")
					TEXT("but only the last will be in the index. This has wasted %" INT64_FMT " bytes in the file."),
					*CachePath, CacheKey, Item->Size);
				CacheItems.Remove(Key);
			}
		}

		int64 Offset = FileHandle->Tell();
		if (Offset < 0)
		{
			CacheItems.Empty();
			FileHandle.Reset();
			UE_LOG(LogDerivedDataCache, Fatal, TEXT("%s: Could not write pak file... out of disk space?"), *CachePath);
			return EPutStatus::NotCached;
		}
		else
		{
			COOK_STAT(Timer.AddHit(InData.Num()));
			if (!Crc.IsSet())
			{
				Crc = FCrc::MemCrc_DEPRECATED(InData.GetData(), InData.Num());
			}
			FileHandle->Write(InData.GetData(), InData.Num());
			UE_LOG(LogDerivedDataCache, Verbose, TEXT("%s: Put %s"), *CachePath, CacheKey);
			CacheItems.Add(Key, FCacheValue(Offset, InData.Num(), Crc.GetValue()));
			return EPutStatus::Cached;
		}
	}
}

void FPakFileDerivedDataBackend::RemoveCachedData(const TCHAR* CacheKey, bool bTransient)
{
	if (bClosed || bTransient)
	{
		return;
	}
	// strangish. We can delete from a pak, but it only deletes the index 
	// if this is a read cache, it will read it next time
	// if this is a write cache, we wasted space
	FWriteScopeLock ScopeLock(SynchronizationObject);
	FString Key(CacheKey);
	CacheItems.Remove(Key);
}

bool FPakFileDerivedDataBackend::SaveCache()
{
	FWriteScopeLock ScopeLock(SynchronizationObject);
	check(FileHandle);
	int64 IndexOffset = FileHandle->Tell();
	check(IndexOffset >= 0);
	uint32 NumItems = uint32(CacheItems.Num());
	check(IndexOffset > 0 || !NumItems);
	TArray<uint8> IndexBuffer;
	{
		FMemoryWriter Saver(IndexBuffer);
		uint32 NumProcessed = 0;
		for (TMap<FString, FCacheValue>::TIterator It(CacheItems); It; ++It )
		{
			FCacheValue& Value = It.Value();
			check(It.Key().Len());
			check(Value.Size);
			check(Value.Offset >= 0 && Value.Offset < IndexOffset);
			Saver << It.Key();
			Saver << Value.Offset;
			Saver << Value.Size;
			Saver << Value.Crc;
			NumProcessed++;
		}
		check(NumProcessed == NumItems);
	}
	uint32 IndexCrc = FCrc::MemCrc_DEPRECATED(IndexBuffer.GetData(), IndexBuffer.Num());
	uint32 SizeIndex = uint32(IndexBuffer.Num());

	uint32 Magic = PakCache_Magic;
	TArray<uint8> Buffer;
	FMemoryWriter Saver(Buffer);
	Saver << Magic;
	Saver << IndexCrc;
	Saver << NumItems;
	Saver << SizeIndex;
	Saver.Serialize(IndexBuffer.GetData(), IndexBuffer.Num());
	Saver << Magic;
	Saver << IndexOffset;
	FileHandle->Write(Buffer.GetData(), Buffer.Num());
	CacheItems.Empty();
	FileHandle.Reset();
	bClosed = true;
	return true;
}

bool FPakFileDerivedDataBackend::LoadCache(const TCHAR* InFilename)
{
	check(FileHandle);
	int64 FileSize = FileHandle->Size();
	check(FileSize >= 0);
	if (FileSize < sizeof(int64) + sizeof(uint32) * 5)
	{
		UE_LOG(LogDerivedDataCache, Error, TEXT("%s: Pak cache was corrupted (short)."), InFilename);
		return false;
	}
	int64 IndexOffset = -1;
	int64 Trailer = -1;
	{
		TArray<uint8> Buffer;
		const int64 SeekPos = FileSize - int64(sizeof(int64) + sizeof(uint32));
		FileHandle->Seek(SeekPos);
		Trailer = FileHandle->Tell();
		if (Trailer != SeekPos)
		{
			UE_LOG(LogDerivedDataCache, Error, TEXT("%s: Pak cache was corrupted (bad seek)."), InFilename);
			return false;
		}
		check(Trailer >= 0 && Trailer < FileSize);
		Buffer.AddUninitialized(sizeof(int64) + sizeof(uint32));
		FileHandle->Read(Buffer.GetData(), int64(sizeof(int64)+sizeof(uint32)));
		FMemoryReader Loader(Buffer);
		uint32 Magic = 0;
		Loader << Magic;
		Loader << IndexOffset;
		if (Magic != PakCache_Magic || IndexOffset < 0 || IndexOffset + int64(sizeof(uint32) * 4) > Trailer)
		{
			UE_LOG(LogDerivedDataCache, Error, TEXT("%s: Pak cache was corrupted (bad footer)."), InFilename);
			return false;
		}
	}
	uint32 IndexCrc = 0;
	uint32 NumIndex = 0;
	uint32 SizeIndex = 0;
	{
		TArray<uint8> Buffer;
		FileHandle->Seek(IndexOffset);
		if (FileHandle->Tell() != IndexOffset)
		{
			UE_LOG(LogDerivedDataCache, Error, TEXT("%s: Pak cache was corrupted (bad seek index)."), InFilename);
			return false;
		}
		Buffer.AddUninitialized(sizeof(uint32) * 4);
		FileHandle->Read(Buffer.GetData(), sizeof(uint32) * 4);
		FMemoryReader Loader(Buffer);
		uint32 Magic = 0;
		Loader << Magic;
		Loader << IndexCrc;
		Loader << NumIndex;
		Loader << SizeIndex;
		if (Magic != PakCache_Magic || (SizeIndex != 0 && NumIndex == 0) || (SizeIndex == 0 && NumIndex != 0)) 
		{
			UE_LOG(LogDerivedDataCache, Error, TEXT("%s: Pak cache was corrupted (bad index header)."), InFilename);
			return false;
		}
		if (IndexOffset + sizeof(uint32) * 4 + SizeIndex != Trailer) 
		{
			UE_LOG(LogDerivedDataCache, Error, TEXT("%s: Pak cache was corrupted (bad index size)."), InFilename);
			return false;
		}
	}
	{
		TArray<uint8> Buffer;
		Buffer.AddUninitialized(SizeIndex);
		FileHandle->Read(Buffer.GetData(), SizeIndex);
		FMemoryReader Loader(Buffer);
		while (Loader.Tell() < SizeIndex)
		{
			FString Key;
			int64 Offset;
			int64 Size;
			uint32 Crc;
			Loader << Key;
			Loader << Offset;
			Loader << Size;
			Loader << Crc;
			if (!Key.Len() || Offset < 0 || Offset >= IndexOffset || !Size)
			{
				UE_LOG(LogDerivedDataCache, Error, TEXT("%s: Pak cache was corrupted (bad index entry)."), InFilename);
				return false;
			}
			CacheItems.Add(Key, FCacheValue(Offset, Size, Crc));
		}
		if (CacheItems.Num() != NumIndex)
		{
			UE_LOG(LogDerivedDataCache, Error, TEXT("%s: Pak cache was corrupted (bad index count)."), InFilename);
			return false;
		}
	}
	return true;
}

void FPakFileDerivedDataBackend::MergeCache(FPakFileDerivedDataBackend* OtherPak)
{
	// Get all the existing keys
	TArray<FString> KeyNames;
	OtherPak->CacheItems.GenerateKeyArray(KeyNames);

	// Find all the keys to copy
	TArray<FString> CopyKeyNames;
	for(const FString& KeyName : KeyNames)
	{
		if(!CachedDataProbablyExists(*KeyName))
		{
			CopyKeyNames.Add(KeyName);
		}
	}
	UE_LOG(LogDerivedDataCache, Display, TEXT("Merging %d entries (%d skipped)."), CopyKeyNames.Num(), KeyNames.Num() - CopyKeyNames.Num());

	// Copy them all to the new cache. Don't use the overloaded get/put methods (which may compress/decompress); copy the raw data directly.
	TArray<uint8> Buffer;
	for(const FString& CopyKeyName : CopyKeyNames)
	{
		Buffer.Reset();
		if(OtherPak->FPakFileDerivedDataBackend::GetCachedData(*CopyKeyName, Buffer))
		{
			FPakFileDerivedDataBackend::PutCachedData(*CopyKeyName, Buffer, false);
		}
	}
}

bool FPakFileDerivedDataBackend::SortAndCopy(const FString &InputFilename, const FString &OutputFilename)
{
	// Open the input and output files
	FPakFileDerivedDataBackend InputPak(*InputFilename, false);
	if (InputPak.bClosed) return false;

	FPakFileDerivedDataBackend OutputPak(*OutputFilename, true);
	if (OutputPak.bClosed) return false;

	// Sort the key names
	TArray<FString> KeyNames;
	InputPak.CacheItems.GenerateKeyArray(KeyNames);
	KeyNames.Sort();

	// Copy all the DDC to the new cache
	TArray<uint8> Buffer;
	TArray<uint32> KeySizes;
	for (int KeyIndex = 0; KeyIndex < KeyNames.Num(); KeyIndex++)
	{
		Buffer.Reset();
		InputPak.GetCachedData(*KeyNames[KeyIndex], Buffer);
		OutputPak.PutCachedData(*KeyNames[KeyIndex], Buffer, false);
		KeySizes.Add(Buffer.Num());
	}

	// Write out a TOC listing for debugging
	FStringOutputDevice Output;
	Output.Logf(TEXT("Asset,Size") LINE_TERMINATOR);
	for(int KeyIndex = 0; KeyIndex < KeyNames.Num(); KeyIndex++)
	{
		Output.Logf(TEXT("%s,%d") LINE_TERMINATOR, *KeyNames[KeyIndex], KeySizes[KeyIndex]);
	}
	FFileHelper::SaveStringToFile(Output, *FPaths::Combine(*FPaths::GetPath(OutputFilename), *(FPaths::GetBaseFilename(OutputFilename) + TEXT(".csv"))));
	return true;
}

TSharedRef<FDerivedDataCacheStatsNode> FPakFileDerivedDataBackend::GatherUsageStats() const
{
	TSharedRef<FDerivedDataCacheStatsNode> Usage =
		MakeShared<FDerivedDataCacheStatsNode>(TEXT("PakFile"), CachePath, /*bIsLocal*/ true);
	Usage->Stats.Add(TEXT(""), UsageStats);
	return Usage;
}

void FPakFileDerivedDataBackend::Put(
	const TConstArrayView<FCachePutRequest> Requests,
	IRequestOwner& Owner,
	FOnCachePutComplete&& OnComplete)
{
	for (const FCachePutRequest& Request : Requests)
	{
		const FCacheRecord& Record = Request.Record;
		TRACE_CPUPROFILER_EVENT_SCOPE(PakFileDDC_Put);
		COOK_STAT(auto Timer = UsageStats.TimePut());
		if (PutCacheRecord(Request.Name, Record, Request.Policy))
		{
			UE_LOG(LogDerivedDataCache, Verbose, TEXT("%s: Cache put complete for %s from '%s'"),
				*CachePath, *WriteToString<96>(Record.GetKey()), *Request.Name);
			COOK_STAT(Timer.AddHit(Private::GetCacheRecordCompressedSize(Record)));
			if (OnComplete)
			{
				OnComplete({Request.Name, Record.GetKey(), Request.UserData, EStatus::Ok});
			}
		}
		else
		{
			COOK_STAT(Timer.AddMiss());
			if (OnComplete)
			{
				OnComplete({Request.Name, Record.GetKey(), Request.UserData, EStatus::Error});
			}
		}
	}
}

void FPakFileDerivedDataBackend::Get(
	const TConstArrayView<FCacheGetRequest> Requests,
	IRequestOwner& Owner,
	FOnCacheGetComplete&& OnComplete)
{
	for (const FCacheGetRequest& Request : Requests)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PakFileDDC_Get);
		COOK_STAT(auto Timer = UsageStats.TimeGet());
		EStatus Status = EStatus::Ok;
		if (FOptionalCacheRecord Record = GetCacheRecord(Request.Name, Request.Key, Request.Policy, Status))
		{
			UE_LOG(LogDerivedDataCache, Verbose, TEXT("%s: Cache hit for %s from '%s'"),
				*CachePath, *WriteToString<96>(Request.Key), *Request.Name);
			COOK_STAT(Timer.AddHit(Private::GetCacheRecordCompressedSize(Record.Get())));
			if (OnComplete)
			{
				OnComplete({Request.Name, MoveTemp(Record).Get(), Request.UserData, Status});
			}
		}
		else
		{
			if (OnComplete)
			{
				OnComplete({Request.Name, FCacheRecordBuilder(Request.Key).Build(), Request.UserData, Status});
			}
		}
	}
}

void FPakFileDerivedDataBackend::GetChunks(
	const TConstArrayView<FCacheChunkRequest> Requests,
	IRequestOwner& Owner,
	FOnCacheChunkComplete&& OnComplete)
{
	TArray<FCacheChunkRequest, TInlineAllocator<16>> SortedRequests(Requests);
	SortedRequests.StableSort(TChunkLess());

	FOptionalCacheRecord Record;
	FCompressedBufferReader Reader;
	for (const FCacheChunkRequest& Request : SortedRequests)
	{
		const bool bExistsOnly = EnumHasAnyFlags(Request.Policy, ECachePolicy::SkipData);
		TRACE_CPUPROFILER_EVENT_SCOPE(PakFileDDC_Get);
		COOK_STAT(auto Timer = bExistsOnly ? UsageStats.TimeProbablyExists() : UsageStats.TimeGet());
		if (!Record || Record.Get().GetKey() != Request.Key)
		{
			FCacheRecordPolicyBuilder PolicyBuilder(ECachePolicy::None);
			PolicyBuilder.AddValuePolicy(Request.Id, Request.Policy);
			Record = GetCacheRecordOnly(Request.Name, Request.Key, PolicyBuilder.Build());
		}
		if (Record)
		{
			EStatus ValueStatus = EStatus::Ok;
			FValueWithId Value = Record.Get().GetValue(Request.Id);
			GetCacheContent(Request.Name, Request.Key, Request.Policy, Value, ValueStatus);
			if (Value)
			{
				const uint64 RawOffset = FMath::Min(Value.GetRawSize(), Request.RawOffset);
				const uint64 RawSize = FMath::Min(Value.GetRawSize() - RawOffset, Request.RawSize);
				UE_LOG(LogDerivedDataCache, Verbose, TEXT("%s: Cache hit for %s from '%s'"),
					*CachePath, *WriteToString<96>(Request.Key, '/', Request.Id), *Request.Name);
				COOK_STAT(Timer.AddHit(Value.HasData() ? RawSize : 0));
				if (OnComplete)
				{
					FSharedBuffer Buffer;
					if (Value.HasData() && !bExistsOnly)
					{
						FCompressedBufferReaderSourceScope Source(Reader, Value.GetData());
						Buffer = Reader.Decompress(RawOffset, RawSize);
					}
					OnComplete({Request.Name, Request.Key, Request.Id, Request.RawOffset,
						RawSize, Value.GetRawHash(), MoveTemp(Buffer), Request.UserData, ValueStatus});
				}
				continue;
			}
		}

		if (OnComplete)
		{
			OnComplete({Request.Name, Request.Key, Request.Id, Request.RawOffset,
				0, {}, {}, Request.UserData, EStatus::Error});
		}
	}
}

bool FPakFileDerivedDataBackend::PutCacheRecord(
	const FStringView Name,
	const FCacheRecord& Record,
	const FCacheRecordPolicy& Policy)
{
	if (!IsWritable())
	{
		return false;
	}

	const FCacheKey& Key = Record.GetKey();

	// Skip the request if storing to the cache is disabled.
	if (!EnumHasAnyFlags(Policy.GetRecordPolicy(), ECachePolicy::StoreLocal))
	{
		UE_LOG(LogDerivedDataCache, VeryVerbose, TEXT("%s: Skipped put of %s from '%.*s' due to cache policy"),
			*CachePath, *WriteToString<96>(Key), Name.Len(), Name.GetData());
		return false;
	}

	//if (ShouldSimulateMiss(Key))
	//{
	//	UE_LOG(LogDerivedDataCache, Verbose, TEXT("%s: Simulated miss for put of %s from '%.*s'"),
	//		*CachePath, *WriteToString<96>(Key), Name.Len(), Name.GetData());
	//	return false;
	//}

	// Check if there is an existing record package.
	bool bRecordExists = false;
	FCbPackage ExistingPackage;
	TStringBuilder<256> Path;
	FPathViews::Append(Path, TEXT("Buckets"), Key);
	const ECachePolicy CombinedValuePolicy = Algo::TransformAccumulate(
		Policy.GetValuePolicies(), &FCacheValuePolicy::Policy, Policy.GetDefaultValuePolicy(), UE_PROJECTION(operator|));
	if (EnumHasAnyFlags(CombinedValuePolicy, ECachePolicy::SkipData))
	{
		bRecordExists = FileExists(Path);
	}
	else if (FSharedBuffer Buffer = LoadFile(Path, Name))
	{
		FCbFieldIterator It = FCbFieldIterator::MakeRange(MoveTemp(Buffer));
		bRecordExists = ExistingPackage.TryLoad(It);
	}

	// Save the record to a package and remove attachments that will be stored externally.
	FCbPackage Package = Record.Save();
	TArray<FCompressedBuffer, TInlineAllocator<8>> ExternalContent;
	if (ExistingPackage)
	{
		// Mirror the existing internal/external attachment storage.
		TArray<FCompressedBuffer, TInlineAllocator<8>> AllContent;
		Algo::Transform(Package.GetAttachments(), AllContent, &FCbAttachment::AsCompressedBinary);
		for (FCompressedBuffer& Content : AllContent)
		{
			const FIoHash RawHash = Content.GetRawHash();
			if (!ExistingPackage.FindAttachment(RawHash))
			{
				Package.RemoveAttachment(RawHash);
				ExternalContent.Add(MoveTemp(Content));
			}
		}
	}
	else
	{
		// Remove the largest attachments from the package until it fits within the size limits.
		TArray<FCompressedBuffer, TInlineAllocator<8>> AllContent;
		Algo::Transform(Package.GetAttachments(), AllContent, &FCbAttachment::AsCompressedBinary);
		uint64 TotalSize = Algo::TransformAccumulate(AllContent, &FCompressedBuffer::GetCompressedSize, uint64(0));
		const uint64 MaxSize = (AllContent.Num() == 1 ? MaxValueSizeKB : MaxRecordSizeKB) * 1024;
		if (TotalSize > MaxSize)
		{
			Algo::StableSortBy(AllContent, &FCompressedBuffer::GetCompressedSize, TGreater<>());
			for (FCompressedBuffer& Content : AllContent)
			{
				const uint64 CompressedSize = Content.GetCompressedSize();
				Package.RemoveAttachment(Content.GetRawHash());
				ExternalContent.Add(MoveTemp(Content));
				TotalSize -= CompressedSize;
				if (TotalSize <= MaxSize)
				{
					break;
				}
			}
		}
	}

	// Save the external content to storage.
	for (FCompressedBuffer& Content : ExternalContent)
	{
		PutCacheContent(Name, Content);
	}

	// Save the record package to storage.
	if (!bRecordExists && !SaveFile(Path, Name, [&Package](FArchive& Ar) { Package.Save(Ar); }))
	{
		return false;
	}

	return true;
}

FOptionalCacheRecord FPakFileDerivedDataBackend::GetCacheRecordOnly(
	const FStringView Name,
	const FCacheKey& Key,
	const FCacheRecordPolicy& Policy)
{
	if (bClosed)
	{
		UE_LOG(LogDerivedDataCache, VeryVerbose,
			TEXT("%s: Skipped get of %s from '%.*s' because this cache store is not available"),
			*CachePath, *WriteToString<96>(Key), Name.Len(), Name.GetData());
		return FOptionalCacheRecord();
	}

	// Skip the request if querying the cache is disabled.
	if (!EnumHasAnyFlags(Policy.GetRecordPolicy(), ECachePolicy::QueryLocal))
	{
		UE_LOG(LogDerivedDataCache, VeryVerbose, TEXT("%s: Skipped get of %s from '%.*s' due to cache policy"),
			*CachePath, *WriteToString<96>(Key), Name.Len(), Name.GetData());
		return FOptionalCacheRecord();
	}

	//if (ShouldSimulateMiss(Key))
	//{
	//	UE_LOG(LogDerivedDataCache, Verbose, TEXT("%s: Simulated miss for get of %s from '%.*s'"),
	//		*CachePath, *WriteToString<96>(Key), Name.Len(), Name.GetData());
	//	return FOptionalCacheRecord();
	//}

	// Request the record from storage.
	TStringBuilder<256> Path;
	FPathViews::Append(Path, TEXT("Buckets"), Key);
	FSharedBuffer Buffer = LoadFile(Path, Name);
	if (Buffer.IsNull())
	{
		UE_LOG(LogDerivedDataCache, Verbose, TEXT("%s: Cache miss with missing record for %s from '%.*s'"),
			*CachePath, *WriteToString<96>(Key), Name.Len(), Name.GetData());
		return FOptionalCacheRecord();
	}

	// Validate that the record can be read as a compact binary package without crashing.
	if (ValidateCompactBinaryPackage(Buffer, ECbValidateMode::Default | ECbValidateMode::Package) != ECbValidateError::None)
	{
		UE_LOG(LogDerivedDataCache, Display, TEXT("%s: Cache miss with invalid package for %s from '%.*s'"),
			*CachePath, *WriteToString<96>(Key), Name.Len(), Name.GetData());
		return FOptionalCacheRecord();
	}

	// Load the record from the package.
	FOptionalCacheRecord Record;
	{
		FCbPackage Package;
		if (FCbFieldIterator It = FCbFieldIterator::MakeRange(Buffer); !Package.TryLoad(It))
		{
			UE_LOG(LogDerivedDataCache, Display, TEXT("%s: Cache miss with package load failure for %s from '%.*s'"),
				*CachePath, *WriteToString<96>(Key), Name.Len(), Name.GetData());
			return FOptionalCacheRecord();
		}
		Record = FCacheRecord::Load(Package);
		if (Record.IsNull())
		{
			UE_LOG(LogDerivedDataCache, Display, TEXT("%s: Cache miss with record load failure for %s from '%.*s'"),
				*CachePath, *WriteToString<96>(Key), Name.Len(), Name.GetData());
			return FOptionalCacheRecord();
		}
	}

	return Record.Get();
}

FOptionalCacheRecord FPakFileDerivedDataBackend::GetCacheRecord(
	const FStringView Name,
	const FCacheKey& Key,
	const FCacheRecordPolicy& Policy,
	EStatus& OutStatus)
{
	FOptionalCacheRecord Record = GetCacheRecordOnly(Name, Key, Policy);
	if (Record.IsNull())
	{
		OutStatus = EStatus::Error;
		return Record;
	}

	OutStatus = EStatus::Ok;

	FCacheRecordBuilder RecordBuilder(Key);

	if (!EnumHasAnyFlags(Policy.GetRecordPolicy(), ECachePolicy::SkipMeta))
	{
		RecordBuilder.SetMeta(FCbObject(Record.Get().GetMeta()));
	}

	for (FValueWithId Value : Record.Get().GetValues())
	{
		const ECachePolicy ValuePolicy = Policy.GetValuePolicy(Value.GetId());
		GetCacheContent(Name, Key, ValuePolicy, Value, OutStatus);
		if (Value.IsNull())
		{
			return FOptionalCacheRecord();
		}
		RecordBuilder.AddValue(MoveTemp(Value));
	}

	return RecordBuilder.Build();
}

bool FPakFileDerivedDataBackend::PutCacheContent(const FStringView Name, const FCompressedBuffer& Content)
{
	const FIoHash& RawHash = Content.GetRawHash();
	TStringBuilder<256> Path;
	FPathViews::Append(Path, TEXT("Content"), RawHash);
	if (!FileExists(Path))
	{
		if (!SaveFile(Path, Name, [&Content](FArchive& Ar) { Content.Save(Ar); }))
		{
			return false;
		}
	}
	return true;
}

void FPakFileDerivedDataBackend::GetCacheContent(
	const FStringView Name,
	const FCacheKey& Key,
	const ECachePolicy Policy,
	FValueWithId& InOutValue,
	EStatus& InOutStatus)
{
	if (!EnumHasAnyFlags(Policy, ECachePolicy::Query) ||
		(EnumHasAnyFlags(Policy, ECachePolicy::SkipData) && InOutValue.HasData()))
	{
		InOutValue = InOutValue.RemoveData();
		return;
	}

	if (InOutValue.HasData())
	{
		return;
	}

	const FIoHash& RawHash = InOutValue.GetRawHash();

	TStringBuilder<256> Path;
	FPathViews::Append(Path, TEXT("Content"), RawHash);
	if (EnumHasAllFlags(Policy, ECachePolicy::SkipData))
	{
		if (FileExists(Path))
		{
			return;
		}
	}
	else
	{
		if (FSharedBuffer CompressedData = LoadFile(Path, Name))
		{
			if (FCompressedBuffer CompressedBuffer = FCompressedBuffer::FromCompressed(MoveTemp(CompressedData));
				CompressedBuffer && CompressedBuffer.GetRawHash() == RawHash)
			{
				InOutValue = FValueWithId(InOutValue.GetId(), MoveTemp(CompressedBuffer));
				return;
			}
			UE_LOG(LogDerivedDataCache, Display,
				TEXT("%s: Cache miss with corrupted value %s with hash %s for %s from '%.*s'"),
				*CachePath, *WriteToString<16>(InOutValue.GetId()), *WriteToString<48>(RawHash),
				*WriteToString<96>(Key), Name.Len(), Name.GetData());
			InOutStatus = EStatus::Error;
			if (!EnumHasAnyFlags(Policy, ECachePolicy::PartialOnError))
			{
				InOutValue = FValueWithId::Null;
			}
			return;
		}
	}

	UE_LOG(LogDerivedDataCache, Verbose,
		TEXT("%s: Cache miss with missing value %s with hash %s for %s from '%.*s'"),
		*CachePath, *WriteToString<16>(InOutValue.GetId()), *WriteToString<48>(RawHash), *WriteToString<96>(Key),
		Name.Len(), Name.GetData());
	InOutStatus = EStatus::Error;
	if (!EnumHasAnyFlags(Policy, ECachePolicy::PartialOnError))
	{
		InOutValue = FValueWithId::Null;
	}
}

class FCrcBuilder
{
public:
	inline void Update(const void* Data, uint64 Size)
	{
		while (Size > 0)
		{
			const int32 CrcSize = int32(FMath::Min<uint64>(Size, MAX_int32));
			Crc = FCrc::MemCrc_DEPRECATED(Data, CrcSize, Crc);
			Size -= CrcSize;
		}
	}

	inline uint32 Finalize()
	{
		return Crc;
	}

private:
	uint32 Crc = 0;
};

class FPakWriterArchive final : public FArchive
{
public:
	inline FPakWriterArchive(IFileHandle& InHandle, FStringView InPath)
		: Handle(InHandle)
		, Path(InPath)
	{
		SetIsSaving(true);
		SetIsPersistent(true);
	}

	inline FString GetArchiveName() const final { return FString(Path); }
	inline int64 TotalSize() final { return Handle.Size(); }
	inline int64 Tell() final { unimplemented(); return 0; }
	inline void Seek(int64 InPos) final { unimplemented(); }
	inline void Flush() final { unimplemented(); }
	inline bool Close() final { unimplemented(); return false; }

	inline void Serialize(void* V, int64 Length) final
	{
		if (!Handle.Write(static_cast<uint8*>(V), Length))
		{
			SetError();
		}
	}

private:
	IFileHandle& Handle;
	FStringView Path;
};

class FPakReaderArchive final : public FArchive
{
public:
	inline FPakReaderArchive(IFileHandle& InHandle, FStringView InPath)
		: Handle(InHandle)
		, Path(InPath)
	{
		SetIsLoading(true);
		SetIsPersistent(true);
	}

	inline FString GetArchiveName() const final { return FString(Path); }
	inline int64 TotalSize() final { return Handle.Size(); }
	inline int64 Tell() final { unimplemented(); return 0; }
	inline void Seek(int64 InPos) final { unimplemented(); }
	inline void Flush() final { unimplemented(); }
	inline bool Close() final { unimplemented(); return false; }

	inline void Serialize(void* V, int64 Length) final
	{
		if (!Handle.Read(static_cast<uint8*>(V), Length))
		{
			SetError();
		}
	}

private:
	IFileHandle& Handle;
	FStringView Path;
};

bool FPakFileDerivedDataBackend::SaveFile(
	const FStringView Path,
	const FStringView DebugName,
	TFunctionRef<void (FArchive&)> WriteFunction)
{
	FWriteScopeLock ScopeLock(SynchronizationObject);
	check(FileHandle);
	if (const int64 Offset = FileHandle->Tell(); Offset >= 0)
	{
		FPakWriterArchive Ar(*FileHandle, CachePath);
		THashingArchiveProxy<FCrcBuilder> HashAr(Ar);
		WriteFunction(HashAr);
		if (const int64 EndOffset = FileHandle->Tell(); EndOffset >= Offset && !Ar.IsError())
		{
			FCacheValue& Item = CacheItems.Emplace(Path, FCacheValue(Offset, EndOffset - Offset, HashAr.GetHash()));
			UE_LOG(LogDerivedDataCache, Log,
				TEXT("%s: File %.*s from '%.*s' written with offset %" INT64_FMT ", size %" INT64_FMT", CRC 0x%08x."),
				*CachePath, Path.Len(), Path.GetData(), DebugName.Len(), DebugName.GetData(), Item.Offset, Item.Size, Item.Crc);
			return true;
		}
	}
	return false;
}

FSharedBuffer FPakFileDerivedDataBackend::LoadFile(const FStringView Path, const FStringView DebugName)
{
	FWriteScopeLock ScopeLock(SynchronizationObject);
	if (const FCacheValue* Item = CacheItems.FindByHash(GetTypeHash(Path), Path))
	{
		check(FileHandle);
		ON_SCOPE_EXIT
		{
			if (bWriting)
			{
				FileHandle->SeekFromEnd();
			}
		};
		check(Item->Size);
		if (FileHandle->Seek(Item->Offset))
		{
			FPakReaderArchive Ar(*FileHandle, CachePath);
			THashingArchiveProxy<FCrcBuilder> HashAr(Ar);
			FUniqueBuffer MutableBuffer = FUniqueBuffer::Alloc(uint64(Item->Size));
			HashAr.Serialize(MutableBuffer.GetData(), Item->Size);
			if (Ar.IsError())
			{
				UE_LOG(LogDerivedDataCache, Display,
					TEXT("%s: File %.*s from '%.*s' failed to read %" INT64_FMT " bytes."),
					*CachePath, Path.Len(), Path.GetData(), DebugName.Len(), DebugName.GetData(), Item->Size);
			}
			else if (const uint32 TestCrc = HashAr.GetHash(); TestCrc != Item->Crc)
			{
				UE_LOG(LogDerivedDataCache, Display,
					TEXT("%s: File %.*s from '%.*s' is corrupted and has CRC 0x%08x when 0x%08x is expected."),
					*CachePath, Path.Len(), Path.GetData(), DebugName.Len(), DebugName.GetData(), TestCrc, Item->Crc);
			}
			else
			{
				return MutableBuffer.MoveToShared();
			}
		}
	}
	return FSharedBuffer();
}

bool FPakFileDerivedDataBackend::FileExists(const FStringView Path)
{
	FReadScopeLock ScopeLock(SynchronizationObject);
	const uint32 PathHash = GetTypeHash(Path);
	return CacheItems.ContainsByHash(PathHash, Path);
}

FCompressedPakFileDerivedDataBackend::FCompressedPakFileDerivedDataBackend(const TCHAR* InFilename, bool bInWriting)
	: FPakFileDerivedDataBackend(InFilename, bInWriting)
{
}

FDerivedDataBackendInterface::EPutStatus FCompressedPakFileDerivedDataBackend::PutCachedData(const TCHAR* CacheKey, TArrayView<const uint8> InData, bool bPutEvenIfExists)
{
	int32 UncompressedSize = InData.Num();
	int32 CompressedSize = FCompression::CompressMemoryBound(CompressionFormat, UncompressedSize, CompressionFlags);

	TArray<uint8> CompressedData;
	CompressedData.AddUninitialized(CompressedSize + sizeof(UncompressedSize));

	FMemory::Memcpy(&CompressedData[0], &UncompressedSize, sizeof(UncompressedSize));
	verify(FCompression::CompressMemory(CompressionFormat, CompressedData.GetData() + sizeof(UncompressedSize), CompressedSize, InData.GetData(), InData.Num(), CompressionFlags));
	CompressedData.SetNum(CompressedSize + sizeof(UncompressedSize), false);

	return FPakFileDerivedDataBackend::PutCachedData(CacheKey, CompressedData, bPutEvenIfExists);
}

bool FCompressedPakFileDerivedDataBackend::GetCachedData(const TCHAR* CacheKey, TArray<uint8>& OutData)
{
	TArray<uint8> CompressedData;
	if(!FPakFileDerivedDataBackend::GetCachedData(CacheKey, CompressedData))
	{
		return false;
	}

	int32 UncompressedSize;
	FMemory::Memcpy(&UncompressedSize, &CompressedData[0], sizeof(UncompressedSize));
	OutData.SetNum(UncompressedSize);
	verify(FCompression::UncompressMemory(CompressionFormat, OutData.GetData(), UncompressedSize, CompressedData.GetData() + sizeof(UncompressedSize), CompressedData.Num() - sizeof(UncompressedSize), CompressionFlags));

	return true;
}

} // UE::DerivedData::CacheStore::PakFile
