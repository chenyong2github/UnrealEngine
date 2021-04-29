// Copyright Epic Games, Inc. All Rights Reserved.

#include "PakFileDerivedDataBackend.h"
#include "Misc/Compression.h"
#include "DerivedDataCacheRecord.h"
#include "DerivedDataCacheUsageStats.h"

namespace UE::DerivedData::Backends
{

FPakFileDerivedDataBackend::FPakFileDerivedDataBackend(ICacheFactory& InFactory, const TCHAR* InFilename, bool bInWriting)
	: Factory(InFactory)
	, bWriting(bInWriting)
	, bClosed(false)
	, Filename(InFilename)
{
	if (bWriting)
	{
		FileHandle.Reset(IFileManager::Get().CreateFileWriter(InFilename, FILEWRITE_NoReplaceExisting));
		if (!FileHandle)
		{
			UE_LOG(LogDerivedDataCache, Fatal, TEXT("Pak cache could not be opened for writing %s."), InFilename);
			bClosed = true;
		}
		else
		{
			UE_LOG(LogDerivedDataCache, Display, TEXT("Pak cache opened for writing %s."), InFilename);
		}
	}
	else
	{
		FileHandle.Reset(IFileManager::Get().CreateFileReader(InFilename));
		if (!FileHandle)
		{
			UE_LOG(LogDerivedDataCache, Warning, TEXT("Pak cache could not be opened for reading %s."), InFilename);
		}
		else if (!LoadCache(InFilename))
		{
			FileHandle.Reset();
			CacheItems.Empty();
			bClosed = true;
		}
		else
		{
			UE_LOG(LogDerivedDataCache, Display, TEXT("Pak cache opened for reading %s (%" INT64_FMT " MiB)."),
				InFilename, FileHandle->TotalSize() / 1024 / 1024);
		}
	}
}

FPakFileDerivedDataBackend::~FPakFileDerivedDataBackend()
{
	Close();
}

void FPakFileDerivedDataBackend::Close()
{
	FDerivedDataBackend::Get().WaitForQuiescence();
	FScopeLock ScopeLock(&SynchronizationObject);
	if (!bClosed)
	{
		if (bWriting)
		{
			SaveCache();
		}
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
	FScopeLock ScopeLock(&SynchronizationObject);
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
	if (bWriting || bClosed)
	{
		return false;
	}
	FScopeLock ScopeLock(&SynchronizationObject);
	FCacheValue* Item = CacheItems.Find(FString(CacheKey));
	if (Item)
	{
		check(FileHandle);
		FileHandle->Seek(Item->Offset);
		if (FileHandle->Tell() != Item->Offset)
		{
			UE_LOG(LogDerivedDataCache, Warning, TEXT("Pak file, bad seek."));
		}
		else
		{
			check(Item->Size);
			check(!OutData.Num());
			check(FileHandle->IsLoading());
			OutData.AddUninitialized(Item->Size);
			FileHandle->Serialize(OutData.GetData(), int64(Item->Size));
			uint32 TestCrc = FCrc::MemCrc_DEPRECATED(OutData.GetData(), Item->Size);
			if (TestCrc != Item->Crc)
			{
				UE_LOG(LogDerivedDataCache, Warning, TEXT("Pak file, bad crc."));
			}
			else
			{
				UE_LOG(LogDerivedDataCache, Verbose, TEXT("FPakFileDerivedDataBackend: Cache hit on %s"), CacheKey);
				check(OutData.Num());
				COOK_STAT(Timer.AddHit(OutData.Num()));
				return true;
			}
		}
	}
	else
	{
		UE_LOG(LogDerivedDataCache, Verbose, TEXT("FPakFileDerivedDataBackend: Miss on %s"), CacheKey);
	}
	OutData.Empty();
	return false;
}

FDerivedDataBackendInterface::EPutStatus FPakFileDerivedDataBackend::PutCachedData(const TCHAR* CacheKey, TArrayView<const uint8> InData, bool bPutEvenIfExists)
{
	COOK_STAT(auto Timer = UsageStats.TimePut());
	if (!bWriting || bClosed)
	{
		return EPutStatus::NotCached;
	}
	{
		FScopeLock ScopeLock(&SynchronizationObject);
		FString Key(CacheKey);
		TOptional<uint32> Crc;
		check(InData.Num());
		check(Key.Len());
		check(FileHandle);
		check(FileHandle->IsSaving());

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
						FileHandle->Serialize(const_cast<uint8*>(InData.GetData()), int64(InData.Num()));
						Item->Crc = Crc.GetValue();
						FileHandle->Seek(Offset);
					}
					return EPutStatus::Cached;
				}

				UE_LOG(LogDerivedDataCache, Warning, TEXT("FPakFileDerivedDataBackend: Repeated put of %s with different sized contents.  Multiple contents will be in the file, but only the last will be in the index.  This has wasted %" INT64_FMT " bytes in the file."), CacheKey, Item->Offset);
				CacheItems.Remove(Key);
			}
		}

		int64 Offset = FileHandle->Tell();
		if (Offset < 0)
		{
			CacheItems.Empty();
			FileHandle.Reset();
			UE_LOG(LogDerivedDataCache, Fatal, TEXT("Could not write pak file...out of disk space?"));
			return EPutStatus::NotCached;
		}
		else
		{
			COOK_STAT(Timer.AddHit(InData.Num()));
			if (!Crc.IsSet())
			{
				Crc = FCrc::MemCrc_DEPRECATED(InData.GetData(), InData.Num());
			}
			// NOTE: Gross that FArchive doesn't have a const version just for saving...
			FileHandle->Serialize(const_cast<uint8*>(InData.GetData()), int64(InData.Num()));
			UE_LOG(LogDerivedDataCache, Verbose, TEXT("FPakFileDerivedDataBackend: Put %s"), CacheKey);
			CacheItems.Add(Key,FCacheValue(Offset, InData.Num(), Crc.GetValue()));
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
	// if this is a write cache, we wated space
	FScopeLock ScopeLock(&SynchronizationObject);
	FString Key(CacheKey);
	CacheItems.Remove(Key);
}

bool FPakFileDerivedDataBackend::SaveCache()
{
	FScopeLock ScopeLock(&SynchronizationObject);
	check(FileHandle);
	check(FileHandle->IsSaving());
	int64 IndexOffset = FileHandle->Tell();
	check(IndexOffset >= 0);
	uint32 NumItems =  uint32(CacheItems.Num());
	check(IndexOffset > 0 || !NumItems);
	TArray<uint8> IndexBuffer;
	{
		FMemoryWriter Saver(IndexBuffer);
		uint32 NumProcessed = 0;
		for (TMap<FString, FCacheValue>::TIterator It(CacheItems); It; ++It )
		{
			check(It.Value().Offset >= 0 && It.Value().Offset < IndexOffset);
			check(It.Value().Size);
			check(It.Key().Len());
			Saver << It.Key();
			Saver << It.Value().Offset;
			Saver << It.Value().Size;
			Saver << It.Value().Crc;
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
	FileHandle->Serialize(Buffer.GetData(), Buffer.Num());
	CacheItems.Empty();
	FileHandle.Reset();
	bClosed = true;
	return true;
}

bool FPakFileDerivedDataBackend::LoadCache(const TCHAR* InFilename)
{
	check(FileHandle);
	check(FileHandle->IsLoading());
	int64 FileSize = FileHandle->TotalSize();
	check(FileSize >= 0);
	if (FileSize < sizeof(int64) + sizeof(uint32) * 5)
	{
		UE_LOG(LogDerivedDataCache, Error, TEXT("Pak cache was corrupted (short) %s."), InFilename);
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
			UE_LOG(LogDerivedDataCache, Error, TEXT("Pak cache was corrupted (bad seek) %s."), InFilename);
			return false;
		}
		check(Trailer >= 0 && Trailer < FileSize);
		Buffer.AddUninitialized(sizeof(int64) + sizeof(uint32));
		FileHandle->Serialize(Buffer.GetData(), int64(sizeof(int64)+sizeof(uint32)));
		FMemoryReader Loader(Buffer);
		uint32 Magic = 0;
		Loader << Magic;
		Loader << IndexOffset;
		if (Magic != PakCache_Magic || IndexOffset < 0 || IndexOffset + int64(sizeof(uint32) * 4) > Trailer)
		{
			UE_LOG(LogDerivedDataCache, Error, TEXT("Pak cache was corrupted (bad footer) %s."), InFilename);
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
			UE_LOG(LogDerivedDataCache, Error, TEXT("Pak cache was corrupted (bad seek index) %s."), InFilename);
			return false;
		}
		Buffer.AddUninitialized(sizeof(uint32) * 4);
		FileHandle->Serialize(Buffer.GetData(), int64(sizeof(uint32)* 4));
		FMemoryReader Loader(Buffer);
		uint32 Magic = 0;
		Loader << Magic;
		Loader << IndexCrc;
		Loader << NumIndex;
		Loader << SizeIndex;
		if (Magic != PakCache_Magic || (SizeIndex != 0 && NumIndex == 0) || (SizeIndex == 0 && NumIndex != 0)) 
		{
			UE_LOG(LogDerivedDataCache, Error, TEXT("Pak cache was corrupted (bad index header) %s."), InFilename);
			return false;
		}
		if (IndexOffset + sizeof(uint32) * 4 + SizeIndex != Trailer) 
		{
			UE_LOG(LogDerivedDataCache, Error, TEXT("Pak cache was corrupted (bad index size) %s."), InFilename);
			return false;
		}
	}
	{
		TArray<uint8> Buffer;
		Buffer.AddUninitialized(SizeIndex);
		FileHandle->Serialize(Buffer.GetData(), int64(SizeIndex));
		FMemoryReader Loader(Buffer);
		while (Loader.Tell() < (int32)SizeIndex)
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
				UE_LOG(LogDerivedDataCache, Error, TEXT("Pak cache was corrupted (bad index entry) %s."), InFilename);
				return false;
			}
			CacheItems.Add(Key, FCacheValue(Offset, Size, Crc));
		}
		if (CacheItems.Num() != NumIndex)
		{
			UE_LOG(LogDerivedDataCache, Error, TEXT("Pak cache was corrupted (bad index count) %s."), InFilename);
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

bool FPakFileDerivedDataBackend::SortAndCopy(ICacheFactory& InFactory, const FString &InputFilename, const FString &OutputFilename)
{
	// Open the input and output files
	FPakFileDerivedDataBackend InputPak(InFactory, *InputFilename, false);
	if (InputPak.bClosed) return false;

	FPakFileDerivedDataBackend OutputPak(InFactory, *OutputFilename, true);
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
	TSharedRef<FDerivedDataCacheStatsNode> Usage = MakeShared<FDerivedDataCacheStatsNode>(this, FString::Printf(TEXT("%s.%s"), TEXT("PakFile"), *Filename));
	Usage->Stats.Add(TEXT(""), UsageStats);

	return Usage;
}

FRequest FPakFileDerivedDataBackend::Put(
	TArrayView<FCacheRecord> Records,
	FStringView Context,
	ECachePolicy Policy,
	EPriority Priority,
	FOnCachePutComplete&& OnComplete)
{
	if (OnComplete)
	{
		for (const FCacheRecord& Record : Records)
		{
			OnComplete({Record.GetKey(), EStatus::Error});
		}
	}
	return FRequest();
}

FRequest FPakFileDerivedDataBackend::Get(
	TConstArrayView<FCacheKey> Keys,
	FStringView Context,
	ECachePolicy Policy,
	EPriority Priority,
	FOnCacheGetComplete&& OnComplete)
{
	if (OnComplete)
	{
		for (const FCacheKey& Key : Keys)
		{
			OnComplete({Factory.CreateRecord(Key).Build(), EStatus::Error});
		}
	}
	return FRequest();
}

FRequest FPakFileDerivedDataBackend::GetPayload(
	TConstArrayView<FCachePayloadKey> Keys,
	FStringView Context,
	ECachePolicy Policy,
	EPriority Priority,
	FOnCacheGetPayloadComplete&& OnComplete)
{
	if (OnComplete)
	{
		for (const FCachePayloadKey& Key : Keys)
		{
			OnComplete({Key.CacheKey, FPayload(Key.Id), EStatus::Error});
		}
	}
	return FRequest();
}

FCompressedPakFileDerivedDataBackend::FCompressedPakFileDerivedDataBackend(ICacheFactory& InFactory, const TCHAR* InFilename, bool bInWriting)
	: FPakFileDerivedDataBackend(InFactory, InFilename, bInWriting)
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

} // UE::DerivedData::Backends
