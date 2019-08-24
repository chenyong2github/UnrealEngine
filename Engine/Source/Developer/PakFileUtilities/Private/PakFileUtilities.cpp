// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "PakFileUtilities.h"
#include "IPlatformFilePak.h"
#include "Misc/SecureHash.h"
#include "Math/BigInt.h"
#include "SignedArchiveWriter.h"
#include "Misc/AES.h"
#include "Templates/UniquePtr.h"
#include "Serialization/LargeMemoryWriter.h"
#include "ProfilingDebugging/DiagnosticTable.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/Base64.h"
#include "Misc/Compression.h"
#include "Features/IModularFeatures.h"
#include "Misc/CoreDelegates.h"
#include "Misc/FileHelper.h"
#include "Misc/ConfigCacheIni.h"
#include "HAL/PlatformFilemanager.h"
#include "Async/ParallelFor.h"
#include "Async/AsyncWork.h"
#include "Modules/ModuleManager.h"
#include "DerivedDataCacheInterface.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

IMPLEMENT_MODULE(FDefaultModuleImpl, PakFileUtilities);

#define GUARANTEE_UASSET_AND_UEXP_IN_SAME_PAK 0

#define USE_DDC_FOR_COMPRESSED_FILES 0
#define PAKCOMPRESS_DERIVEDDATA_VER TEXT("9493D2AB515048658AF7BE1342EC21FC")


#define SEEK_OPT_VERBOSITY Display

#define DETAILED_UNREALPAK_TIMING 0
#if DETAILED_UNREALPAK_TIMING
struct FUnrealPakScopeCycleCounter
{
	volatile int64& Counter;
	uint32 StartTime;
	FUnrealPakScopeCycleCounter(volatile int64& InCounter) : Counter(InCounter) { StartTime = FPlatformTime::Cycles(); }
	~FUnrealPakScopeCycleCounter() 
	{
		uint32 EndTime = FPlatformTime::Cycles();
		volatile int64 DeltaTime = EndTime;
		if (EndTime > StartTime)
		{
			DeltaTime = EndTime - StartTime;
		}
		FPlatformAtomics::InterlockedAdd(&Counter, DeltaTime);
	}
};
volatile int64 GCompressionTime = 0;
volatile int64 GDDCSyncReadTime = 0;
volatile int64 GDDCSyncWriteTime = 0;
int64 GDDCHits = 0;
int64 GDDCMisses = 0;
#endif

struct FNamedAESKey
{
	FString Name;
	FGuid Guid;
	FAES::FAESKey Key;

	bool IsValid() const
	{
		return Key.IsValid();
	}
};

struct FKeyChain
{
	FRSAKeyHandle SigningKey = InvalidRSAKeyHandle;
	TMap<FGuid, FNamedAESKey> EncryptionKeys;
	const FNamedAESKey* MasterEncryptionKey = nullptr;
};


class FThreadLocalScratchSpace
{
public:
#define MAX_SCRATCHSPACE_THREADS 64
#define ALLOC_BUFFER_SIZE (256 * 1024 * 1024)
	static FThreadLocalScratchSpace& Get()
	{
		static FThreadLocalScratchSpace Result;
		return Result;
	}
	struct FScratchSpace
	{
	public:
		FScratchSpace()
		{
			Size = 0;
			Buffer = nullptr;
		}
		int64 Size;
		uint8* Buffer;
		TArray<uint8> Array;

		void Embiggen(int64 NewSize)
		{
			if (NewSize > Size)
			{
				Buffer = (uint8*)FMemory::Realloc(Buffer, NewSize);
				Size = NewSize;
			}
		}
		void CleanUp()
		{
			FMemory::Free(Buffer);
			Buffer = nullptr;
			Size = 0;
			Array.Empty();
		}
	};

	void GetScratchSpace(FScratchSpace*& ScratchSpace)
	{
		ScratchSpace = (FScratchSpace*)FPlatformTLS::GetTlsValue(TLSSlot);
		if (ScratchSpace == nullptr)
		{
			int32 MyScratchSpace = FPlatformAtomics::InterlockedIncrement(&ThreadCounter);
			check(MyScratchSpace < (MAX_SCRATCHSPACE_THREADS - 1));
			ScratchSpace = &ScratchSpaces[MyScratchSpace];
			FPlatformTLS::SetTlsValue(TLSSlot, (void*)ScratchSpace);
		}
	}

	void CleanUp()
	{
		for (int32 I = 0; I < MAX_SCRATCHSPACE_THREADS; ++I)
		{
			ScratchSpaces[I].CleanUp();
		}
		ThreadCounter = 0;
	}
private:

	FThreadLocalScratchSpace()
	{
		TLSSlot = FPlatformTLS::AllocTlsSlot();
		ThreadCounter = 0;
	}

	uint32 TLSSlot;
	volatile int32 ThreadCounter;
	FScratchSpace ScratchSpaces[MAX_SCRATCHSPACE_THREADS];
};


class FMemoryCompressor;

/**
* AsyncTask for FMemoryCompressor
* Compress a memory block asynchronously
 */
class FBlockCompressTask : public FNonAbandonableTask
{
public:
	friend class FAsyncTask<FBlockCompressTask>;
	friend class FMemoryCompressor;
	FBlockCompressTask(void* InUncompressedBuffer, int32 InUncompressedSize, FName InFormat, int32 InBlockSize) :
		UncompressedBuffer(InUncompressedBuffer),
		UncompressedSize(InUncompressedSize),
		Format(InFormat),
		BlockSize(InBlockSize),
		Result(false)
	{
		// Store buffer size.
		CompressedSize = FCompression::CompressMemoryBound(Format, BlockSize);
		CompressedBuffer = FMemory::Malloc(CompressedSize);
	}

	~FBlockCompressTask()
	{
		FMemory::Free(CompressedBuffer);
	}

	/** Do compress */
	void DoWork()
	{
		// Compress memory block.
		// Actual size will be stored to CompressedSize.
		Result = FCompression::CompressMemory(Format, CompressedBuffer, CompressedSize, UncompressedBuffer, UncompressedSize);
	}

	
	FORCEINLINE TStatId GetStatId() const { RETURN_QUICK_DECLARE_CYCLE_STAT(ExampleAsyncTask, STATGROUP_ThreadPoolAsyncTasks); }
		
private:
	// Source buffer
	void* UncompressedBuffer;
	int32 UncompressedSize;

	// Compress parameters
	FName Format;
	int32 BlockSize;
	int32 BitWindow;

	// Compressed result
	void* CompressedBuffer;
	int32 CompressedSize;
	bool Result;
};

/**
* asynchronous memory compressor
*/
class FMemoryCompressor
{
public:
	/** Divide into blocks and start compress asynchronously */
	FMemoryCompressor(uint8* UncompressedBuffer, int32 UncompressedSize, FName Format, int32 CompressionBlockSize) :
		Index(0)
			{
		// Divide into blocks and start compression async tasks.
		// These blocks must be as same as followed CompressMemory callings.
		int64 UncompressedBytes = 0;
		while (UncompressedSize)
		{
			int32 BlockSize = (int32)FMath::Min<int64>(UncompressedSize, CompressionBlockSize);
			auto* AsyncTask = new FAsyncTask<FBlockCompressTask>(UncompressedBuffer + UncompressedBytes, BlockSize, Format, BlockSize);
			AsyncTask->StartBackgroundTask();
			BlockCompressAsyncTasks.Add(AsyncTask);
			UncompressedSize -= BlockSize;
			UncompressedBytes += BlockSize;
		}
	}

	~FMemoryCompressor()
	{
		for (auto* AsyncTask : BlockCompressAsyncTasks)
		{
			if (!AsyncTask->Cancel())
			{
				AsyncTask->EnsureCompletion();
			}
			delete AsyncTask;
		}
	}


	/** Fetch compressed result. Returns true and store CompressedSize if succeeded */
	bool CompressMemory(FName Format, void* CompressedBuffer, int32& CompressedSize, const void* UncompressedBuffer, int32 UncompressedSize)
	{
		// Fetch compressed result from task.
		// We assume this is called only once, same order, same parameters for
		// each task.
		auto* AsyncTask = BlockCompressAsyncTasks[Index++];
		AsyncTask->EnsureCompletion();

		FBlockCompressTask& Task = AsyncTask->GetTask();
		check(Task.Format == Format);
		check(Task.UncompressedBuffer == UncompressedBuffer);
		check(Task.UncompressedSize == UncompressedSize);
		check(CompressedSize >= Task.CompressedSize);
		if (!Task.Result)
		{
			return false;
		}
		FMemory::Memcpy(CompressedBuffer, Task.CompressedBuffer, Task.CompressedSize);
		CompressedSize = Task.CompressedSize;

		return true;
	}

private:
	TArray<FAsyncTask<FBlockCompressTask>*> BlockCompressAsyncTasks;

	// Fetched task index
	int32 Index;
};

/**
* Defines the order mapping for files within a pak
 */
class FPakOrderMap
{
public:
	FPakOrderMap()
		: MaxPrimaryOrderIndex(MAX_uint64)
	{}

	int32 Num() const
	{
		return OrderMap.Num();
	}

	void Add(const FString& Filename, uint64 Index)
	{
		OrderMap.Add(Filename, Index);
	}

	bool ProcessOrderFile(const TCHAR* ResponseFile, bool bSecondaryOrderFile = false)
	{
		int32 OrderOffset = 0;
		if (bSecondaryOrderFile)
		{
			OrderOffset = Num();
			MaxPrimaryOrderIndex = OrderOffset;
		}
		// List of all items to add to pak file
		FString Text;
		UE_LOG(LogPakFile, Display, TEXT("Loading pak order file %s..."), ResponseFile);
		if (FFileHelper::LoadFileToString(Text, ResponseFile))
		{
			// Read all lines
			TArray<FString> Lines;
			Text.ParseIntoArray(Lines, TEXT("\n"), true);
			for (int32 EntryIndex = 0; EntryIndex < Lines.Num(); EntryIndex++)
			{
				Lines[EntryIndex].ReplaceInline(TEXT("\r"), TEXT(""));
				Lines[EntryIndex].ReplaceInline(TEXT("\n"), TEXT(""));
				int32 OpenOrderNumber = EntryIndex;
				if (Lines[EntryIndex].FindLastChar('"', OpenOrderNumber))
				{
					FString ReadNum = Lines[EntryIndex].RightChop(OpenOrderNumber + 1);
					Lines[EntryIndex] = Lines[EntryIndex].Left(OpenOrderNumber + 1);
					ReadNum.TrimStartInline();
					if (ReadNum.IsNumeric())
					{
						OpenOrderNumber = FCString::Atoi(*ReadNum);
					}
				}
				Lines[EntryIndex] = Lines[EntryIndex].TrimQuotes();
				FString Path = FString::Printf(TEXT("%s"), *Lines[EntryIndex]);
				FPaths::NormalizeFilename(Path);
				Path = Path.ToLower();
				if (bSecondaryOrderFile && OrderMap.Contains(Path))
				{
					continue;
				}
				OrderMap.Add(Path, OpenOrderNumber + OrderOffset);
			}
			UE_LOG(LogPakFile, Display, TEXT("Finished loading pak order file %s."), ResponseFile);
			return true;
		}
		else
		{
			UE_LOG(LogPakFile, Error, TEXT("Unable to load pak order file %s."), ResponseFile);
			return false;
		}
	}

	uint64 GetFileOrder(const FString& Path, bool bAllowUexpUBulkFallback, bool* OutIsPrimary=nullptr) const
	{
		FString RegionStr;
		FString NewPath = RemapLocalizationPathIfNeeded(Path.ToLower(), RegionStr);
		const uint64* FoundOrder = OrderMap.Find(NewPath);
		uint64 ReturnOrder = MAX_uint64;
		if (FoundOrder != nullptr)
		{
			ReturnOrder = *FoundOrder;
			if (OutIsPrimary)
			{
				*OutIsPrimary = ( ReturnOrder < MaxPrimaryOrderIndex );
			}
		}
		else if (bAllowUexpUBulkFallback)
		{
			// if this is a cook order or an old order it will not have uexp files in it, so we put those in the same relative order after all of the normal files, but before any ubulk files
			if (Path.EndsWith(TEXT("uexp")) || Path.EndsWith(TEXT("ubulk")))
			{
				uint64 CounterpartOrder = GetFileOrder(FPaths::GetBaseFilename(Path, false) + TEXT(".uasset"), false);
				if (CounterpartOrder == MAX_uint64)
				{
					CounterpartOrder = GetFileOrder(FPaths::GetBaseFilename(Path, false) + TEXT(".umap"), false);
				}
				if (CounterpartOrder != MAX_uint64)
				{
					if (Path.EndsWith(TEXT("uexp")))
					{
						ReturnOrder = CounterpartOrder | (1 << 29);
					}
					else
					{
						ReturnOrder = CounterpartOrder | (1 << 30);
					}
				}
			}
		}

		// Optionally offset based on region, so multiple files in different regions don't get the same order.
		// I/O profiling suggests this is slightly worse, so leaving this disabled for now
#if 0
		if (ReturnOrder != MAX_uint64)
		{
			if (RegionStr.Len() > 0)
			{
				uint64 RegionOffset = 0;
				for (int i = 0; i < RegionStr.Len(); i++)
				{
					int8 Letter = (int8)(RegionStr[i] - TEXT('a'));
					RegionOffset |= (uint64(Letter) << (i * 5));
				}
				return ReturnOrder + (RegionOffset << 16);
			}
		}
#endif
		return ReturnOrder;
	}
private:
	FString RemapLocalizationPathIfNeeded(const FString& PathLower, FString& OutRegion) const
	{
		static const TCHAR* L10NPrefix = (const TCHAR*)TEXT("/content/l10n/");
		static const int32 L10NPrefixLength = FCString::Strlen(L10NPrefix);
		int32 FoundIndex = PathLower.Find(L10NPrefix, ESearchCase::CaseSensitive);
		if (FoundIndex > 0)
		{
			// Validate the content index is the first one
			int32 ContentIndex = PathLower.Find(TEXT("/content/"), ESearchCase::CaseSensitive);
			if (ContentIndex == FoundIndex)
			{
				int32 EndL10NOffset = ContentIndex + L10NPrefixLength;
				int32 NextSlashIndex = PathLower.Find(TEXT("/"), ESearchCase::CaseSensitive, ESearchDir::FromStart, EndL10NOffset);
				int32 RegionLength = NextSlashIndex - EndL10NOffset;
				if (RegionLength >= 2)
				{
					FString NonLocalizedPath = PathLower.Mid(0, ContentIndex) + TEXT("/content") + PathLower.Mid(NextSlashIndex);
					OutRegion = PathLower.Mid(EndL10NOffset, RegionLength);
					return NonLocalizedPath;
				}
			}
		}
		return PathLower;
	}

	TMap<FString, uint64> OrderMap;
	uint64 MaxPrimaryOrderIndex;
};


enum class ESeekOptMode : uint8
{
	None = 0,
	OnePass = 1,
	Incremental = 2,
	Incremental_OnlyPrimaryOrder = 3,
	Incremental_PrimaryThenSecondary = 4,
	COUNT
};

struct FPatchSeekOptParams
{
	FPatchSeekOptParams()
		: MaxGapSize(0)
		, MaxInflationPercent(0.0f)
		, Mode(ESeekOptMode::None)
		, MaxAdjacentOrderDiff(128)
	{}
	int64 MaxGapSize;
	float MaxInflationPercent; // For Incremental_ modes only
	ESeekOptMode Mode;
	int32 MaxAdjacentOrderDiff;
};

struct FPakCommandLineParameters
{
	FPakCommandLineParameters()
		: CompressionBlockSize(64 * 1024)
		, FileSystemBlockSize(0)
		, PatchFilePadAlign(0)
		, AlignForMemoryMapping(0)
		, GeneratePatch(false)
		, EncryptIndex(false)
		, UseCustomCompressor(false)
		, bSign(false)
		, bPatchCompatibilityMode421(false)
		, bFallbackOrderForNonUassetFiles(false)
		, bAsyncCompression(false)
	{
	}

	TArray<FName> CompressionFormats;
	FPatchSeekOptParams SeekOptParams;
	int32  CompressionBlockSize;
	int64  FileSystemBlockSize;
	int64  PatchFilePadAlign;
	int64  AlignForMemoryMapping;
	bool   GeneratePatch;
	FString SourcePatchPakFilename;
	FString SourcePatchDiffDirectory;
	FString InputFinalPakFilename; // This is the resulting pak file we want to end up with after we generate the pak patch.  This is used instead of passing in the raw content.
	FString ChangedFilesOutputFilename;
	bool EncryptIndex;
	bool UseCustomCompressor;
	FGuid EncryptionKeyGuid;
	bool bSign;
	bool bPatchCompatibilityMode421;
	bool bFallbackOrderForNonUassetFiles;
	bool bAsyncCompression;
};

struct FPakEntryPair
{
	FString Filename;
	FPakEntry Info;
};

struct FPakInputPair
{
	FString Source;
	FString Dest;
	uint64 SuggestedOrder; 
	bool bNeedsCompression;
	bool bNeedEncryption;
	bool bIsDeleteRecord;	// This is used for patch PAKs when a file is deleted from one patch to the next
	bool bIsInPrimaryOrder;

	FPakInputPair()
		: SuggestedOrder(MAX_uint64)
		, bNeedsCompression(false)
		, bNeedEncryption(false)
		, bIsDeleteRecord(false)
		, bIsInPrimaryOrder(false)
	{}

	FPakInputPair(const FString& InSource, const FString& InDest)
		: Source(InSource)
		, Dest(InDest)
		, bNeedsCompression(false)
		, bNeedEncryption(false)
		, bIsDeleteRecord(false)
	{}

	FORCEINLINE bool operator==(const FPakInputPair& Other) const
	{
		return Source == Other.Source;
	}
};

struct FPakEntryOrder
{
	FPakEntryOrder() : Order(MAX_uint64) {}
	FString Filename;
	uint64  Order;
};


struct FFileInfo
{
	uint64 FileSize;
	int32 PatchIndex;
	bool bIsDeleteRecord;
	bool bForceInclude;
	uint8 Hash[16];
};

bool ExtractFilesFromPak(const TCHAR* InPakFilename, TMap<FString, FFileInfo>& InFileHashes, 
	const TCHAR* InDestPath, bool bUseMountPoint, 
	const FKeyChain& InKeyChain, const FString* InFilter, 
	TArray<FPakInputPair>* OutEntries = nullptr, TArray<FPakInputPair>* OutDeletedEntries = nullptr, 
	FPakOrderMap* OutOrderMap = nullptr, TArray<FGuid>* OutUsedEncryptionKeys = nullptr, bool* OutAnyPakSigned = nullptr);

struct FCompressedFileBuffer
{
	FCompressedFileBuffer()
		: OriginalSize(0)
		,TotalCompressedSize(0)
		,FileCompressionBlockSize(0)
		,CompressedBufferSize(0)
	{

	}

	void Reinitialize(FArchive* File, FName CompressionMethod, int64 CompressionBlockSize)
	{
		OriginalSize = File->TotalSize();
		TotalCompressedSize = 0;
		FileCompressionBlockSize = 0;
		FileCompressionMethod = CompressionMethod;
		CompressedBlocks.Reset();
		CompressedBlocks.AddUninitialized((OriginalSize+CompressionBlockSize-1)/CompressionBlockSize);
	}

	void Empty()
	{
		OriginalSize = 0;
		TotalCompressedSize = 0;
		FileCompressionBlockSize = 0;
		FileCompressionMethod = NAME_None;
		CompressedBuffer = nullptr;
		CompressedBufferSize = 0; 
		CompressedBlocks.Empty();
	}

	void EnsureBufferSpace(int64 RequiredSpace)
	{
		if(RequiredSpace > CompressedBufferSize)
		{
			TUniquePtr<uint8[]> NewCompressedBuffer = MakeUnique<uint8[]>(RequiredSpace);
			FMemory::Memcpy(NewCompressedBuffer.Get(), CompressedBuffer.Get(), CompressedBufferSize);
			CompressedBuffer = MoveTemp(NewCompressedBuffer);
			CompressedBufferSize = RequiredSpace;
		}
	}

	bool CompressFileToWorkingBuffer(const FPakInputPair& InFile, FName CompressionMethod, const int32 CompressionBlockSize);

	void SerializeDDCData(FArchive &Ar)
	{
		Ar << OriginalSize;
		Ar << TotalCompressedSize;
		Ar << FileCompressionBlockSize;
		Ar << FileCompressionMethod;
		Ar << CompressedBlocks;
		if (Ar.IsLoading())
		{
			EnsureBufferSpace(TotalCompressedSize);
		}
		Ar.Serialize(CompressedBuffer.Get(), TotalCompressedSize);
	}

	int64 GetSerializedSizeEstimate() const
	{
		int64 Size = 0;
		Size += sizeof(this);
		Size += CompressedBlocks.Num() * sizeof(FPakCompressedBlock);
		Size += CompressedBufferSize;
		return Size;
	}

	FString GetDDCKeyString(const uint8* UncompressedFile, const int64& UncompressedFileSize, FName CompressionFormat, const int64& BlockSize);

	int64				OriginalSize;
	int64				TotalCompressedSize;
	int32				FileCompressionBlockSize;
	FName				FileCompressionMethod;
	TArray<FPakCompressedBlock>	CompressedBlocks;
	int64				CompressedBufferSize;
	TUniquePtr<uint8[]>		CompressedBuffer;
};

void LoadKeyChainFromFile(const FString& InFilename, FKeyChain& OutCryptoSettings);
void ApplyEncryptionKeys(const FKeyChain& KeyChain);

template <class T>
bool ReadSizeParam(const TCHAR* CmdLine, const TCHAR* ParamStr, T& SizeOut)
{
	FString ParamValueStr;
	if (FParse::Value(CmdLine, ParamStr, ParamValueStr) &&
		FParse::Value(CmdLine, ParamStr, SizeOut))
	{
		if (ParamValueStr.EndsWith(TEXT("GB")))
		{
			SizeOut *= 1024 * 1024 * 1024;
		}
		else if (ParamValueStr.EndsWith(TEXT("MB")))
		{
			SizeOut *= 1024 * 1024;
		}
		else if (ParamValueStr.EndsWith(TEXT("KB")))
		{
			SizeOut *= 1024;
		}
		return true;
	}
	return false;
}


FString GetLongestPath(TArray<FPakInputPair>& FilesToAdd)
{
	FString LongestPath;
	int32 MaxNumDirectories = 0;

	for (int32 FileIndex = 0; FileIndex < FilesToAdd.Num(); FileIndex++)
	{
		FString& Filename = FilesToAdd[FileIndex].Dest;
		int32 NumDirectories = 0;
		for (int32 Index = 0; Index < Filename.Len(); Index++)
		{
			if (Filename[Index] == '/')
			{
				NumDirectories++;
			}
		}
		if (NumDirectories > MaxNumDirectories)
		{
			LongestPath = Filename;
			MaxNumDirectories = NumDirectories;
		}
	}
	return FPaths::GetPath(LongestPath) + TEXT("/");
}

FString GetCommonRootPath(TArray<FPakInputPair>& FilesToAdd)
{
	FString Root = GetLongestPath(FilesToAdd);
	for (int32 FileIndex = 0; FileIndex < FilesToAdd.Num() && Root.Len(); FileIndex++)
	{
		FString Filename(FilesToAdd[FileIndex].Dest);
		FString Path = FPaths::GetPath(Filename) + TEXT("/");
		int32 CommonSeparatorIndex = -1;
		int32 SeparatorIndex = Path.Find(TEXT("/"), ESearchCase::CaseSensitive);
		while (SeparatorIndex >= 0)
		{
			if (FCString::Strnicmp(*Root, *Path, SeparatorIndex + 1) != 0)
			{
				break;
			}
			CommonSeparatorIndex = SeparatorIndex;
			if (CommonSeparatorIndex + 1 < Path.Len())
			{
				SeparatorIndex = Path.Find(TEXT("/"), ESearchCase::CaseSensitive, ESearchDir::FromStart, CommonSeparatorIndex + 1);
			}
			else
			{
				break;
			}
		}
		if ((CommonSeparatorIndex + 1) < Root.Len())
		{
			Root = Root.Mid(0, CommonSeparatorIndex + 1);
		}
	}
	return Root;
}


FString FCompressedFileBuffer::GetDDCKeyString(const uint8* UncompressedFile, const int64& UncompressedFileSize, FName CompressionFormat, const int64& BlockSize)
{
	FString KeyString;

	KeyString += FString::Printf(TEXT("_F:%s_C:%s_B:%d_"), *CompressionFormat.ToString(), *FCompression::GetCompressorDDCSuffix(CompressionFormat), BlockSize);
	
	FSHA1 HashState;
	HashState.Update(UncompressedFile, UncompressedFileSize);
	HashState.Final();
	FSHAHash FinalHash;
	HashState.GetHash(FinalHash.Hash);
	KeyString += FinalHash.ToString();;

	return FDerivedDataCacheInterface::BuildCacheKey(TEXT("PAKCOMPRESS_"), PAKCOMPRESS_DERIVEDDATA_VER, *KeyString);
}

bool FCompressedFileBuffer::CompressFileToWorkingBuffer(const FPakInputPair& InFile, FName CompressionMethod, const int32 CompressionBlockSize)
{

	FThreadLocalScratchSpace::FScratchSpace* ScratchSpace = nullptr;
	FThreadLocalScratchSpace::Get().GetScratchSpace(ScratchSpace);
	check(ScratchSpace);



	TUniquePtr<FArchive> FileHandle(IFileManager::Get().CreateFileReader(*InFile.Source));
	if(!FileHandle)
	{
		TotalCompressedSize = 0;
		return false;
	}

	Reinitialize(FileHandle.Get(), CompressionMethod, CompressionBlockSize);
	const int64 FileSize = OriginalSize;
	const int64 PaddedEncryptedFileSize = Align(FileSize,FAES::AESBlockSize);
	check(PaddedEncryptedFileSize >= FileSize);
	if(ScratchSpace->Size < PaddedEncryptedFileSize)
	{
		ScratchSpace->Embiggen(PaddedEncryptedFileSize);
	}


	uint8*& InOutPersistentBuffer = ScratchSpace->Buffer;
	int64& InOutBufferSize = ScratchSpace->Size;

	// Load to buffer
	FileHandle->Serialize(InOutPersistentBuffer,FileSize);


	FString DDCKey;
	TArray<uint8>& GetData = ScratchSpace->Array;
	const bool bShouldUseDDC = USE_DDC_FOR_COMPRESSED_FILES; // && (FileSize > 20 * 1024 ? true : false);
	if (bShouldUseDDC)
	{
#if DETAILED_UNREALPAK_TIMING
		FUnrealPakScopeCycleCounter Scope(GDDCSyncReadTime);
#endif
		DDCKey = GetDDCKeyString(InOutPersistentBuffer, FileSize, CompressionMethod, CompressionBlockSize);

		if (GetDerivedDataCacheRef().CachedDataProbablyExists(*DDCKey))
		{
			int32 AsyncHandle = GetDerivedDataCacheRef().GetAsynchronous(*DDCKey);
			GetDerivedDataCacheRef().WaitAsynchronousCompletion(AsyncHandle);
			GetData.Empty(GetData.Max());
			bool Result = false;
			GetDerivedDataCacheRef().GetAsynchronousResults(AsyncHandle, GetData, &Result);
			if (Result)
			{
				FMemoryReader Ar(GetData, true);
				SerializeDDCData(Ar);
				return true;
			}
		}
	}

	{
#if DETAILED_UNREALPAK_TIMING
		FUnrealPakScopeCycleCounter Scope(GCompressionTime);
#endif
		// Start parallel compress
		FMemoryCompressor MemoryCompressor(InOutPersistentBuffer, FileSize, CompressionMethod, CompressionBlockSize);

		// Build buffers for working
		int64 UncompressedSize = FileSize;
		int32 CompressionBufferSize = Align(FCompression::CompressMemoryBound(CompressionMethod, CompressionBlockSize, COMPRESS_NoFlags), FAES::AESBlockSize);
		EnsureBufferSpace(Align(FCompression::CompressMemoryBound(CompressionMethod, FileSize, COMPRESS_NoFlags), FAES::AESBlockSize));


		TotalCompressedSize = 0;
		int64 UncompressedBytes = 0;
		int32 CurrentBlock = 0;
		while (UncompressedSize)
		{
			int32 BlockSize = (int32)FMath::Min<int64>(UncompressedSize, CompressionBlockSize);
			int32 MaxCompressedBlockSize = FCompression::CompressMemoryBound(CompressionMethod, BlockSize, COMPRESS_NoFlags);
			int32 CompressedBlockSize = FMath::Max<int32>(CompressionBufferSize, MaxCompressedBlockSize);
			FileCompressionBlockSize = FMath::Max<uint32>(BlockSize, FileCompressionBlockSize);
			EnsureBufferSpace(Align(TotalCompressedSize + CompressedBlockSize, FAES::AESBlockSize));
			if (!MemoryCompressor.CompressMemory(CompressionMethod, CompressedBuffer.Get() + TotalCompressedSize, CompressedBlockSize, InOutPersistentBuffer + UncompressedBytes, BlockSize))
			{
				return false;
			}
			UncompressedSize -= BlockSize;
			UncompressedBytes += BlockSize;

			CompressedBlocks[CurrentBlock].CompressedStart = TotalCompressedSize;
			CompressedBlocks[CurrentBlock].CompressedEnd = TotalCompressedSize + CompressedBlockSize;
			++CurrentBlock;

			TotalCompressedSize += CompressedBlockSize;

			if (InFile.bNeedEncryption)
			{
				int32 EncryptionBlockPadding = Align(TotalCompressedSize, FAES::AESBlockSize);
				for (int64 FillIndex = TotalCompressedSize; FillIndex < EncryptionBlockPadding; ++FillIndex)
				{
					// Fill the trailing buffer with bytes from file. Note that this is now from a fixed location
					// rather than a random one so that we produce deterministic results
					CompressedBuffer.Get()[FillIndex] = CompressedBuffer.Get()[FillIndex % TotalCompressedSize];
				}
				TotalCompressedSize += EncryptionBlockPadding - TotalCompressedSize;
			}
		}

	}
	if (bShouldUseDDC)
	{
#if DETAILED_UNREALPAK_TIMING
		FUnrealPakScopeCycleCounter Scope(GDDCSyncWriteTime);
		++GDDCMisses;
#endif
		GetData.Empty(GetData.Max());
		FMemoryWriter Ar(GetData, true);
		SerializeDDCData(Ar);
		GetDerivedDataCacheRef().Put(*DDCKey, GetData);
	}


	return true;
}

bool PrepareCopyFileToPak(const FString& InMountPoint, const FPakInputPair& InFile, uint8*& InOutPersistentBuffer, int64& InOutBufferSize, FPakEntryPair& OutNewEntry, uint8*& OutDataToWrite, int64& OutSizeToWrite, const FKeyChain& InKeyChain)
{	
	TUniquePtr<FArchive> FileHandle(IFileManager::Get().CreateFileReader(*InFile.Source));
	bool bFileExists = FileHandle.IsValid();
	if (bFileExists)
	{
		const int64 FileSize = FileHandle->TotalSize();
		const int64 PaddedEncryptedFileSize = Align(FileSize, FAES::AESBlockSize); 
		OutNewEntry.Filename = InFile.Dest.Mid(InMountPoint.Len());
		OutNewEntry.Info.Offset = 0; // Don't serialize offsets here.
		OutNewEntry.Info.Size = FileSize;
		OutNewEntry.Info.UncompressedSize = FileSize;
		OutNewEntry.Info.CompressionMethodIndex = 0;
		OutNewEntry.Info.SetEncrypted( InFile.bNeedEncryption );
		OutNewEntry.Info.SetDeleteRecord(false);

		if (InOutBufferSize < PaddedEncryptedFileSize)
		{
			InOutPersistentBuffer = (uint8*)FMemory::Realloc(InOutPersistentBuffer, PaddedEncryptedFileSize);
			InOutBufferSize = FileSize;
		}

		// Load to buffer
		FileHandle->Serialize(InOutPersistentBuffer, FileSize);

		{
			OutSizeToWrite = FileSize;
			if (InFile.bNeedEncryption)
			{
				for(int64 FillIndex=FileSize; FillIndex < PaddedEncryptedFileSize && InFile.bNeedEncryption; ++FillIndex)
				{
					// Fill the trailing buffer with bytes from file. Note that this is now from a fixed location
					// rather than a random one so that we produce deterministic results
					InOutPersistentBuffer[FillIndex] = InOutPersistentBuffer[(FillIndex - FileSize)%FileSize];
				}

				//Encrypt the buffer before writing it to disk
				check(InKeyChain.MasterEncryptionKey);
				FAES::EncryptData(InOutPersistentBuffer, PaddedEncryptedFileSize, InKeyChain.MasterEncryptionKey->Key);
				// Update the size to be written
				OutSizeToWrite = PaddedEncryptedFileSize;
				OutNewEntry.Info.SetEncrypted( true );
			}

			// Calculate the buffer hash value
			FSHA1::HashBuffer(InOutPersistentBuffer,FileSize,OutNewEntry.Info.Hash);			
			OutDataToWrite = InOutPersistentBuffer;
		}
	}
	return bFileExists;
}

void FinalizeCopyCompressedFileToPak(FPakInfo& InPakInfo, const FCompressedFileBuffer& CompressedFile, FPakEntryPair& OutNewEntry)
{
	check(CompressedFile.TotalCompressedSize != 0);

	check(OutNewEntry.Info.CompressionBlocks.Num() == CompressedFile.CompressedBlocks.Num());
	check(OutNewEntry.Info.CompressionMethodIndex == InPakInfo.GetCompressionMethodIndex(CompressedFile.FileCompressionMethod));

	int64 TellPos = OutNewEntry.Info.GetSerializedSize(FPakInfo::PakFile_Version_Latest);
	const TArray<FPakCompressedBlock>& Blocks = CompressedFile.CompressedBlocks;
	for (int32 BlockIndex = 0, BlockCount = Blocks.Num(); BlockIndex < BlockCount; ++BlockIndex)
	{
		OutNewEntry.Info.CompressionBlocks[BlockIndex].CompressedStart = Blocks[BlockIndex].CompressedStart + TellPos;
		OutNewEntry.Info.CompressionBlocks[BlockIndex].CompressedEnd = Blocks[BlockIndex].CompressedEnd + TellPos;
	}
}

bool PrepareCopyCompressedFileToPak(const FString& InMountPoint, FPakInfo& Info, const FPakInputPair& InFile, const FCompressedFileBuffer& CompressedFile, FPakEntryPair& OutNewEntry, uint8*& OutDataToWrite, int64& OutSizeToWrite, const FKeyChain& InKeyChain)
{
	if (CompressedFile.TotalCompressedSize == 0)
	{
		return false;
	}

	OutNewEntry.Info.CompressionMethodIndex = Info.GetCompressionMethodIndex(CompressedFile.FileCompressionMethod);
	OutNewEntry.Info.CompressionBlocks.AddZeroed(CompressedFile.CompressedBlocks.Num());

	if (InFile.bNeedEncryption)
	{
		check(InKeyChain.MasterEncryptionKey);
		FAES::EncryptData(CompressedFile.CompressedBuffer.Get(), CompressedFile.TotalCompressedSize, InKeyChain.MasterEncryptionKey->Key);
	}

	//Hash the final buffer thats written
	FSHA1 Hash;
	Hash.Update(CompressedFile.CompressedBuffer.Get(), CompressedFile.TotalCompressedSize);
	Hash.Final();

	// Update file size & Hash
	OutNewEntry.Info.CompressionBlockSize = CompressedFile.FileCompressionBlockSize;
	OutNewEntry.Info.UncompressedSize = CompressedFile.OriginalSize;
	OutNewEntry.Info.Size = CompressedFile.TotalCompressedSize;
	Hash.GetHash(OutNewEntry.Info.Hash);

	//	Write the header, then the data
	OutNewEntry.Filename = InFile.Dest.Mid(InMountPoint.Len());
	OutNewEntry.Info.Offset = 0; // Don't serialize offsets here.
	OutNewEntry.Info.SetEncrypted( InFile.bNeedEncryption );
	OutNewEntry.Info.SetDeleteRecord(false);
	OutSizeToWrite = CompressedFile.TotalCompressedSize;
	OutDataToWrite = CompressedFile.CompressedBuffer.Get();
	//OutNewEntry.Info.Serialize(InPak,FPakInfo::PakFile_Version_Latest);	
	//InPak.Serialize(CompressedFile.CompressedBuffer.Get(), CompressedFile.TotalCompressedSize);

	return true;
}

void PrepareDeleteRecordForPak(const FString& InMountPoint, const FPakInputPair InDeletedFile, FPakEntryPair& OutNewEntry)
{
	OutNewEntry.Filename = InDeletedFile.Dest.Mid(InMountPoint.Len());
	OutNewEntry.Info.SetDeleteRecord(true);
}


static void CommandLineParseHelper(const TCHAR* InCmdLine, TArray<FString>& Tokens, TArray<FString>& Switches)
{
	FString NextToken;
	while(FParse::Token(InCmdLine,NextToken,false))
	{
		if((**NextToken == TCHAR('-')))
		{
			new(Switches)FString(NextToken.Mid(1));
		}
		else
		{
			new(Tokens)FString(NextToken);
		}
	}
}

void ProcessCommandLine(const TCHAR* CmdLine, const TArray<FString>& NonOptionArguments, TArray<FPakInputPair>& Entries, FPakCommandLineParameters& CmdLineParameters)
{
	// List of all items to add to pak file
	FString ResponseFile;
	FString ClusterSizeString;

	if (FParse::Param(CmdLine, TEXT("patchcompatibilitymode421")))
	{
		CmdLineParameters.bPatchCompatibilityMode421 = true;
	}

	if (FParse::Param(CmdLine, TEXT("asynccompression")))
	{
		CmdLineParameters.bAsyncCompression = true;
	}

	if (FParse::Param(CmdLine, TEXT("fallbackOrderForNonUassetFiles")))
	{
		CmdLineParameters.bFallbackOrderForNonUassetFiles = true;
	}

	if (FParse::Value(CmdLine, TEXT("-blocksize="), ClusterSizeString) && 
		FParse::Value(CmdLine, TEXT("-blocksize="), CmdLineParameters.FileSystemBlockSize))
		{
		if (ClusterSizeString.EndsWith(TEXT("MB")))
		{
			CmdLineParameters.FileSystemBlockSize *= 1024*1024;
		}
		else if (ClusterSizeString.EndsWith(TEXT("KB")))
		{
			CmdLineParameters.FileSystemBlockSize *= 1024;
		}
	}
	else
	{
		CmdLineParameters.FileSystemBlockSize = 0;
}

	FString CompBlockSizeString;
	if (FParse::Value(CmdLine, TEXT("-compressionblocksize="), CompBlockSizeString) &&
		FParse::Value(CmdLine, TEXT("-compressionblocksize="), CmdLineParameters.CompressionBlockSize))
{
		if (CompBlockSizeString.EndsWith(TEXT("MB")))
	{
			CmdLineParameters.CompressionBlockSize *= 1024 * 1024;
	}
		else if (CompBlockSizeString.EndsWith(TEXT("KB")))
	{
			CmdLineParameters.CompressionBlockSize *= 1024;
		}
	}

	if (!FParse::Value(CmdLine, TEXT("-patchpaddingalign="), CmdLineParameters.PatchFilePadAlign))
	{
		CmdLineParameters.PatchFilePadAlign = 0;
	}

	if (!FParse::Value(CmdLine, TEXT("-AlignForMemoryMapping="), CmdLineParameters.AlignForMemoryMapping))
	{
		CmdLineParameters.AlignForMemoryMapping = 0;
	}

	if (FParse::Param(CmdLine, TEXT("encryptindex")))
	{
		CmdLineParameters.EncryptIndex = true;
	}

	if (FParse::Param(CmdLine, TEXT("sign")))
	{
		CmdLineParameters.bSign = true;
	}

	FString DesiredCompressionFormats;
	// look for -compressionformats or -compressionformat on the commandline
	if (FParse::Value(CmdLine, TEXT("-compressionformats="), DesiredCompressionFormats) || FParse::Value(CmdLine, TEXT("-compressionformat="), DesiredCompressionFormats))
	{
		TArray<FString> Formats;
		DesiredCompressionFormats.ParseIntoArray(Formats, TEXT(","));
		for (FString& Format : Formats)
		{
			// look until we have a valid format
			FName FormatName = *Format;

			if (FCompression::IsFormatValid(FormatName))
	{
				CmdLineParameters.CompressionFormats.Add(FormatName);
				break;
			}
		}
	}

	// make sure we can always fallback to zlib, which is guaranteed to exist
	CmdLineParameters.CompressionFormats.AddUnique(NAME_Zlib);

	if (FParse::Value(CmdLine, TEXT("-create="), ResponseFile))
	{
		CmdLineParameters.GeneratePatch = FParse::Value(CmdLine, TEXT("-generatepatch="), CmdLineParameters.SourcePatchPakFilename);
		FParse::Value(CmdLine, TEXT("-outputchangedfiles="), CmdLineParameters.ChangedFilesOutputFilename);

		bool bCompress = FParse::Param(CmdLine, TEXT("compress"));
		bool bEncrypt = FParse::Param(CmdLine, TEXT("encrypt"));

		

		if (CmdLineParameters.GeneratePatch)
		{
			FParse::Value(CmdLine, TEXT("-patchSeekOptMaxInflationPercent="), CmdLineParameters.SeekOptParams.MaxInflationPercent);
			ReadSizeParam(CmdLine, TEXT("-patchSeekOptMaxGapSize="), CmdLineParameters.SeekOptParams.MaxGapSize);
			FParse::Value(CmdLine, TEXT("-patchSeekOptMaxAdjacentOrderDiff="), CmdLineParameters.SeekOptParams.MaxAdjacentOrderDiff);

			// For legacy reasons, if we specify a max gap size without a mode, we default to OnePass
			if (CmdLineParameters.SeekOptParams.MaxGapSize > 0)
			{
				CmdLineParameters.SeekOptParams.Mode = ESeekOptMode::OnePass;
			}
			FParse::Value(CmdLine, TEXT("-patchSeekOptMode="), (int32&)CmdLineParameters.SeekOptParams.Mode);

			
		}

		// if the response file is a pak file, then this is the pak file we want to use as the source
		if (ResponseFile.EndsWith(TEXT(".pak"), ESearchCase::IgnoreCase) && CmdLineParameters.GeneratePatch)
		{
			FString OutputPath;
			if (FParse::Value(CmdLine, TEXT("extractedpaktemp="), OutputPath) == false)
			{
				UE_LOG(LogPakFile, Error, TEXT("-extractedpaktemp= not specified.  Required when specifying pak file as the response file."), *ResponseFile);
			}

			FString ExtractedPakKeysFile;
			FKeyChain ExtractedPakKeys;
			if ( FParse::Value(CmdLine, TEXT("extractedpakcryptokeys="), ExtractedPakKeysFile) )
			{
				LoadKeyChainFromFile(ExtractedPakKeysFile, ExtractedPakKeys);
				ApplyEncryptionKeys(ExtractedPakKeys);
			}

			TMap<FString, FFileInfo> FileHashes;
			ExtractFilesFromPak(*ResponseFile, FileHashes, *OutputPath, true, ExtractedPakKeys, nullptr, &Entries, nullptr, nullptr, nullptr, nullptr);
		}
		else
		{
			TArray<FString> Lines;
			bool bParseLines = true;
			if (IFileManager::Get().DirectoryExists(*ResponseFile))
			{
				IFileManager::Get().FindFilesRecursive(Lines, *ResponseFile, TEXT("*"), true, false);
				bParseLines = false;
			}
			else
			{
				FString Text;
				UE_LOG(LogPakFile, Display, TEXT("Loading response file %s"), *ResponseFile);
				if (FFileHelper::LoadFileToString(Text, *ResponseFile))
				{
					// Remove all carriage return characters.
					Text.ReplaceInline(TEXT("\r"), TEXT(""));
					// Read all lines
					Text.ParseIntoArray(Lines, TEXT("\n"), true);
				}
				else
				{
					UE_LOG(LogPakFile, Error, TEXT("Failed to load %s"), *ResponseFile);
				}
			}

			for (int32 EntryIndex = 0; EntryIndex < Lines.Num(); EntryIndex++)
			{
				TArray<FString> SourceAndDest;
				TArray<FString> Switches;
				if (bParseLines)
				{
					Lines[EntryIndex].TrimStartInline();
					CommandLineParseHelper(*Lines[EntryIndex], SourceAndDest, Switches);
				}
				else
				{
					SourceAndDest.Add(Lines[EntryIndex]);
				}
				if (SourceAndDest.Num() == 0)
				{
					continue;
				}
				FPakInputPair Input;

				Input.Source = SourceAndDest[0];
				FPaths::NormalizeFilename(Input.Source);
				if (SourceAndDest.Num() > 1)
				{
					Input.Dest = FPaths::GetPath(SourceAndDest[1]);
				}
				else
				{
					Input.Dest = FPaths::GetPath(Input.Source);
				}
				FPaths::NormalizeFilename(Input.Dest);
				FPakFile::MakeDirectoryFromPath(Input.Dest);

				//check for compression switches
				for (int32 Index = 0; Index < Switches.Num(); ++Index)
				{
					if (Switches[Index] == TEXT("compress"))
					{
						Input.bNeedsCompression = true;
					}
					if (Switches[Index] == TEXT("encrypt"))
					{
						Input.bNeedEncryption = true;
					}
					if (Switches[Index] == TEXT("delete"))
					{
						Input.bIsDeleteRecord = true;
						Input.Dest = Input.Source;
					}
				}
				Input.bNeedsCompression |= bCompress;
				Input.bNeedEncryption |= bEncrypt;

				UE_LOG(LogPakFile, Log, TEXT("Added file Source: %s Dest: %s"), *Input.Source, *Input.Dest);

				bool bIsMappedBulk = Input.Source.EndsWith(TEXT(".m.ubulk"));
				if (bIsMappedBulk && CmdLineParameters.AlignForMemoryMapping > 0 && Input.bNeedsCompression
					&& !Input.bNeedEncryption) // if it is encrypted, we will compress it anyway since it won't be mapped at runtime
				{
					// no compression for bulk aligned files because they are memory mapped
					Input.bNeedsCompression = false;
					UE_LOG(LogPakFile, Log, TEXT("Stripped compression from %s for memory mapping."), *Input.Dest);
				}
				Entries.Add(Input);
			}
		}

		UE_LOG(LogPakFile, Display, TEXT("Added %d entries to add to pak file."), Entries.Num());
	}
	else
	{
		// Override destination path.
		FString MountPoint;
		FParse::Value(CmdLine, TEXT("-dest="), MountPoint);
		FPaths::NormalizeFilename(MountPoint);
		FPakFile::MakeDirectoryFromPath(MountPoint);

		// Parse command line params. The first param after the program name is the created pak name
		for (int32 Index = 1; Index < NonOptionArguments.Num(); Index++)
		{
			// Skip switches and add everything else to the Entries array
			FPakInputPair Input;
			Input.Source = *NonOptionArguments[Index];
			FPaths::NormalizeFilename(Input.Source);
			if (MountPoint.Len() > 0)
			{
				FString SourceDirectory( FPaths::GetPath(Input.Source) );
				FPakFile::MakeDirectoryFromPath(SourceDirectory);
				Input.Dest = Input.Source.Replace(*SourceDirectory, *MountPoint, ESearchCase::IgnoreCase);
			}
			else
			{
				Input.Dest = FPaths::GetPath(Input.Source);
				FPakFile::MakeDirectoryFromPath(Input.Dest);
			}
			FPaths::NormalizeFilename(Input.Dest);
			Entries.Add(Input);
		}
	}
}

void CollectFilesToAdd(TArray<FPakInputPair>& OutFilesToAdd, const TArray<FPakInputPair>& InEntries, const FPakOrderMap& OrderMap, const FPakCommandLineParameters& CmdLineParameters)
{
	UE_LOG(LogPakFile, Display, TEXT("Collecting files to add to pak file..."));
	const double StartTime = FPlatformTime::Seconds();

	// Start collecting files
	TSet<FString> AddedFiles;	
	for (int32 Index = 0; Index < InEntries.Num(); Index++)
	{
		const FPakInputPair& Input = InEntries[Index];
		const FString& Source = Input.Source;
		bool bCompression = Input.bNeedsCompression;
		bool bEncryption = Input.bNeedEncryption;

		if (Input.bIsDeleteRecord)
		{
			// just pass through any delete records found in the input
			OutFilesToAdd.Add(Input);
			continue;
		}

		FString Filename = FPaths::GetCleanFilename(Source);
		FString Directory = FPaths::GetPath(Source);
		FPaths::MakeStandardFilename(Directory);
		FPakFile::MakeDirectoryFromPath(Directory);

		if (Filename.IsEmpty())
		{
			Filename = TEXT("*.*");
		}
		if ( Filename.Contains(TEXT("*")) )
		{
			// Add multiple files
			TArray<FString> FoundFiles;
			IFileManager::Get().FindFilesRecursive(FoundFiles, *Directory, *Filename, true, false);

			for (int32 FileIndex = 0; FileIndex < FoundFiles.Num(); FileIndex++)
			{
				FPakInputPair FileInput;
				FileInput.Source = FoundFiles[FileIndex];
				FPaths::MakeStandardFilename(FileInput.Source);
				FileInput.Dest = FileInput.Source.Replace(*Directory, *Input.Dest, ESearchCase::IgnoreCase);
				
				uint64 FileOrder = OrderMap.GetFileOrder(FileInput.Dest, false, &FileInput.bIsInPrimaryOrder);
				if(FileOrder != MAX_uint64)
				{
					FileInput.SuggestedOrder = FileOrder;
				}
				else
				{
					// we will put all unordered files at 1 << 28 so that they are before any uexp or ubulk files we assign orders to here
					FileInput.SuggestedOrder = (1 << 28);
					// if this is a cook order or an old order it will not have uexp files in it, so we put those in the same relative order after all of the normal files, but before any ubulk files
					if (FileInput.Dest.EndsWith(TEXT("uexp")) || FileInput.Dest.EndsWith(TEXT("ubulk")))
					{
						FileOrder = OrderMap.GetFileOrder(FPaths::GetBaseFilename(FileInput.Dest, false) + TEXT(".uasset"), false, &FileInput.bIsInPrimaryOrder);
						if (FileOrder == MAX_uint64)
						{
							FileOrder = OrderMap.GetFileOrder(FPaths::GetBaseFilename(FileInput.Dest, false) + TEXT(".umap"),  false, &FileInput.bIsInPrimaryOrder);
						}
						if (FileInput.Dest.EndsWith(TEXT("uexp")))
						{
							FileInput.SuggestedOrder = ((FileOrder != MAX_uint64) ? FileOrder : 0) + (1 << 29);
						}
						else
						{
							FileInput.SuggestedOrder = ((FileOrder != MAX_uint64) ? FileOrder : 0) + (1 << 30);
						}
					}
				}
				FileInput.bNeedsCompression = bCompression;
				FileInput.bNeedEncryption = bEncryption;
				if (!AddedFiles.Contains(FileInput.Source))
				{
					OutFilesToAdd.Add(FileInput);
					AddedFiles.Add(FileInput.Source);
				}
				else
				{
					int32 FoundIndex;
					OutFilesToAdd.Find(FileInput,FoundIndex);
					OutFilesToAdd[FoundIndex].bNeedEncryption |= bEncryption;
					OutFilesToAdd[FoundIndex].bNeedsCompression |= bCompression;
					OutFilesToAdd[FoundIndex].SuggestedOrder = FMath::Min<uint64>(OutFilesToAdd[FoundIndex].SuggestedOrder, FileInput.SuggestedOrder);
				}
			}
		}
		else
		{
			// Add single file
			FPakInputPair FileInput;
			FileInput.Source = Input.Source;
			FPaths::MakeStandardFilename(FileInput.Source);
			FileInput.Dest = FileInput.Source.Replace(*Directory, *Input.Dest, ESearchCase::IgnoreCase);
			uint64 FileOrder = OrderMap.GetFileOrder(FileInput.Dest, CmdLineParameters.bFallbackOrderForNonUassetFiles, &FileInput.bIsInPrimaryOrder);
			if (FileOrder != MAX_uint64)
			{
				FileInput.SuggestedOrder = FileOrder;
			}
			FileInput.bNeedEncryption = bEncryption;
			FileInput.bNeedsCompression = bCompression;

			if (AddedFiles.Contains(FileInput.Source))
			{
				int32 FoundIndex;
				OutFilesToAdd.Find(FileInput, FoundIndex);
				OutFilesToAdd[FoundIndex].bNeedEncryption |= bEncryption;
				OutFilesToAdd[FoundIndex].bNeedsCompression |= bCompression;
				OutFilesToAdd[FoundIndex].SuggestedOrder = FMath::Min<uint64>(OutFilesToAdd[FoundIndex].SuggestedOrder, FileInput.SuggestedOrder);
			}
			else
			{
				OutFilesToAdd.Add(FileInput);
				AddedFiles.Add(FileInput.Source);
			}
		}
	}

	// Sort by suggested order then alphabetically
	struct FInputPairSort
	{
		FORCEINLINE bool operator()(const FPakInputPair& A, const FPakInputPair& B) const
		{
			return  A.bIsDeleteRecord == B.bIsDeleteRecord ? (A.SuggestedOrder == B.SuggestedOrder ? A.Dest < B.Dest : A.SuggestedOrder < B.SuggestedOrder) : A.bIsDeleteRecord < B.bIsDeleteRecord;
		}
	};
	OutFilesToAdd.Sort(FInputPairSort());
	UE_LOG(LogPakFile, Display, TEXT("Collected %d files in %.2lfs."), OutFilesToAdd.Num(), FPlatformTime::Seconds() - StartTime);
}

bool BufferedCopyFile(FArchive& Dest, FArchive& Source, const FPakFile& PakFile, const FPakEntry& Entry, void* Buffer, int64 BufferSize, const FKeyChain& InKeyChain)
{	
	// Align down
	BufferSize = BufferSize & ~(FAES::AESBlockSize-1);
	int64 RemainingSizeToCopy = Entry.Size;
	while (RemainingSizeToCopy > 0)
	{
		const int64 SizeToCopy = FMath::Min(BufferSize, RemainingSizeToCopy);
		// If file is encrypted so we need to account for padding
		int64 SizeToRead = Entry.IsEncrypted() ? Align(SizeToCopy,FAES::AESBlockSize) : SizeToCopy;

		Source.Serialize(Buffer,SizeToRead);
		if (Entry.IsEncrypted())
		{
			const FNamedAESKey* Key = InKeyChain.MasterEncryptionKey;
			check(Key);
			FAES::DecryptData((uint8*)Buffer, SizeToRead, Key->Key);
		}
		Dest.Serialize(Buffer, SizeToCopy);
		RemainingSizeToCopy -= SizeToRead;
	}
	return true;
}

bool UncompressCopyFile(FArchive& Dest, FArchive& Source, const FPakEntry& Entry, uint8*& PersistentBuffer, int64& BufferSize, const FKeyChain& InKeyChain, const FPakFile& PakFile)
{
	if (Entry.UncompressedSize == 0)
	{
		return false;
	}

	// The compression block size depends on the bit window that the PAK file was originally created with. Since this isn't stored in the PAK file itself,
	// we can use FCompression::CompressMemoryBound as a guideline for the max expected size to avoid unncessary reallocations, but we need to make sure
	// that we check if the actual size is not actually greater (eg. UE-59278).
	FName EntryCompressionMethod = PakFile.GetInfo().GetCompressionMethod(Entry.CompressionMethodIndex);
	int32 MaxCompressionBlockSize = FCompression::CompressMemoryBound(EntryCompressionMethod, Entry.CompressionBlockSize);
	for (const FPakCompressedBlock& Block : Entry.CompressionBlocks)
	{
		MaxCompressionBlockSize = FMath::Max<int32>(MaxCompressionBlockSize, Block.CompressedEnd - Block.CompressedStart);
	}

	int64 WorkingSize = Entry.CompressionBlockSize + MaxCompressionBlockSize;
	if (BufferSize < WorkingSize)
	{
		PersistentBuffer = (uint8*)FMemory::Realloc(PersistentBuffer, WorkingSize);
		BufferSize = WorkingSize;
	}

	uint8* UncompressedBuffer = PersistentBuffer+MaxCompressionBlockSize;

	for (uint32 BlockIndex=0, BlockIndexNum=Entry.CompressionBlocks.Num(); BlockIndex < BlockIndexNum; ++BlockIndex)
	{
		uint32 CompressedBlockSize = Entry.CompressionBlocks[BlockIndex].CompressedEnd - Entry.CompressionBlocks[BlockIndex].CompressedStart;
		uint32 UncompressedBlockSize = (uint32)FMath::Min<int64>(Entry.UncompressedSize - Entry.CompressionBlockSize*BlockIndex, Entry.CompressionBlockSize);
		Source.Seek(Entry.CompressionBlocks[BlockIndex].CompressedStart + (PakFile.GetInfo().HasRelativeCompressedChunkOffsets() ? Entry.Offset : 0));
		uint32 SizeToRead = Entry.IsEncrypted() ? Align(CompressedBlockSize, FAES::AESBlockSize) : CompressedBlockSize;
		Source.Serialize(PersistentBuffer, SizeToRead);

		if (Entry.IsEncrypted())
		{
			const FNamedAESKey* Key = InKeyChain.MasterEncryptionKey;
			check(Key);
			FAES::DecryptData(PersistentBuffer, SizeToRead, Key->Key);
		}

		if (!FCompression::UncompressMemory(EntryCompressionMethod, UncompressedBuffer, UncompressedBlockSize, PersistentBuffer, CompressedBlockSize))
		{
			return false;
		}
		Dest.Serialize(UncompressedBuffer,UncompressedBlockSize);
	}

	return true;
}

TEncryptionInt ParseEncryptionIntFromJson(TSharedPtr<FJsonObject> InObj, const TCHAR* InName)
{
	FString Base64;
	if (InObj->TryGetStringField(InName, Base64))
	{
		TArray<uint8> Bytes;
		FBase64::Decode(Base64, Bytes);
		check(Bytes.Num() == sizeof(TEncryptionInt));
		return TEncryptionInt((uint32*)&Bytes[0]);
	}
	else
	{
		return TEncryptionInt();
	}
}

FRSAKeyHandle ParseRSAKeyFromJson(TSharedPtr<FJsonObject> InObj)
{
	TSharedPtr<FJsonObject> PublicKey = InObj->GetObjectField(TEXT("PublicKey"));
	TSharedPtr<FJsonObject> PrivateKey = InObj->GetObjectField(TEXT("PrivateKey"));

	FString PublicExponentBase64, PrivateExponentBase64, PublicModulusBase64, PrivateModulusBase64;

	if (   PublicKey->TryGetStringField("Exponent", PublicExponentBase64)
		&& PublicKey->TryGetStringField("Modulus", PublicModulusBase64)
		&& PrivateKey->TryGetStringField("Exponent", PrivateExponentBase64)
		&& PrivateKey->TryGetStringField("Modulus", PrivateModulusBase64))
	{
		check(PublicModulusBase64 == PrivateModulusBase64);

		TArray<uint8> PublicExponent, PrivateExponent, Modulus;
		FBase64::Decode(PublicExponentBase64, PublicExponent);
		FBase64::Decode(PrivateExponentBase64, PrivateExponent);
		FBase64::Decode(PublicModulusBase64, Modulus);

		return FRSA::CreateKey(PublicExponent, PrivateExponent, Modulus);
	}
	else
	{
		return nullptr;
	}
}

void LoadKeyChainFromFile(const FString& InFilename, FKeyChain& OutCryptoSettings)
{
	FArchive* File = IFileManager::Get().CreateFileReader(*InFilename);
	UE_CLOG(File == nullptr, LogPakFile, Fatal, TEXT("Specified crypto keys cache '%s' does not exist!"), *InFilename);
	TSharedPtr<FJsonObject> RootObject;
	TSharedRef<TJsonReader<char>> Reader = TJsonReaderFactory<char>::Create(File);
	if (FJsonSerializer::Deserialize(Reader, RootObject))
	{
		const TSharedPtr<FJsonObject>* EncryptionKeyObject;
		if (RootObject->TryGetObjectField(TEXT("EncryptionKey"), EncryptionKeyObject))
		{
			FString EncryptionKeyBase64;
			if ((*EncryptionKeyObject)->TryGetStringField(TEXT("Key"), EncryptionKeyBase64))
			{
				if (EncryptionKeyBase64.Len() > 0)
				{
					TArray<uint8> Key;
					FBase64::Decode(EncryptionKeyBase64, Key);
					check(Key.Num() == sizeof(FAES::FAESKey::Key));
					FNamedAESKey NewKey;
					NewKey.Name = TEXT("Default");
					NewKey.Guid = FGuid();
					FMemory::Memcpy(NewKey.Key.Key, &Key[0], sizeof(FAES::FAESKey::Key));
					OutCryptoSettings.EncryptionKeys.Add(NewKey.Guid, NewKey);
				}
			}
		}

		const TSharedPtr<FJsonObject>* SigningKey = nullptr;
		if (RootObject->TryGetObjectField(TEXT("SigningKey"), SigningKey))
		{
			OutCryptoSettings.SigningKey = ParseRSAKeyFromJson(*SigningKey);
		}

		const TArray<TSharedPtr<FJsonValue>>* SecondaryEncryptionKeyArray = nullptr;
		if (RootObject->TryGetArrayField(TEXT("SecondaryEncryptionKeys"), SecondaryEncryptionKeyArray))
		{
			for (TSharedPtr<FJsonValue> EncryptionKeyValue : *SecondaryEncryptionKeyArray)
			{
				FNamedAESKey NewKey;
				TSharedPtr<FJsonObject> SecondaryEncryptionKeyObject = EncryptionKeyValue->AsObject();
				FGuid::Parse(SecondaryEncryptionKeyObject->GetStringField(TEXT("Guid")), NewKey.Guid);
				NewKey.Name = SecondaryEncryptionKeyObject->GetStringField(TEXT("Name"));
				FString KeyBase64 = SecondaryEncryptionKeyObject->GetStringField(TEXT("Key"));

				TArray<uint8> Key;
				FBase64::Decode(KeyBase64, Key);
				check(Key.Num() == sizeof(FAES::FAESKey::Key));
				FMemory::Memcpy(NewKey.Key.Key, &Key[0], sizeof(FAES::FAESKey::Key));

				check(!OutCryptoSettings.EncryptionKeys.Contains(NewKey.Guid) || OutCryptoSettings.EncryptionKeys[NewKey.Guid].Key == NewKey.Key);
				OutCryptoSettings.EncryptionKeys.Add(NewKey.Guid, NewKey);
			}
		}
	}
	delete File;
	FGuid EncryptionKeyOverrideGuid;
	OutCryptoSettings.MasterEncryptionKey = OutCryptoSettings.EncryptionKeys.Find(EncryptionKeyOverrideGuid);
}

void LoadKeyChain(const TCHAR* CmdLine, FKeyChain& OutCryptoSettings)
{
	OutCryptoSettings.SigningKey = InvalidRSAKeyHandle;
	OutCryptoSettings.EncryptionKeys.Empty();

	// First, try and parse the keys from a supplied crypto key cache file
	FString CryptoKeysCacheFilename;
	if (FParse::Value(CmdLine, TEXT("cryptokeys="), CryptoKeysCacheFilename))
	{
		UE_LOG(LogPakFile, Display, TEXT("Parsing crypto keys from a crypto key cache file"));
		LoadKeyChainFromFile(CryptoKeysCacheFilename, OutCryptoSettings);
	}
	else if (FParse::Param(CmdLine, TEXT("encryptionini")))
	{
		FString ProjectDir, EngineDir, Platform;

		if (FParse::Value(CmdLine, TEXT("projectdir="), ProjectDir, false)
			&& FParse::Value(CmdLine, TEXT("enginedir="), EngineDir, false)
			&& FParse::Value(CmdLine, TEXT("platform="), Platform, false))
		{
			UE_LOG(LogPakFile, Warning, TEXT("A legacy command line syntax is being used for crypto config. Please update to using the -cryptokey parameter as soon as possible as this mode is deprecated"));

			FConfigFile EngineConfig;

			FConfigCacheIni::LoadExternalIniFile(EngineConfig, TEXT("Engine"), *FPaths::Combine(EngineDir, TEXT("Config\\")), *FPaths::Combine(ProjectDir, TEXT("Config/")), true, *Platform);
			bool bDataCryptoRequired = false;
			EngineConfig.GetBool(TEXT("PlatformCrypto"), TEXT("PlatformRequiresDataCrypto"), bDataCryptoRequired);

			if (!bDataCryptoRequired)
			{
				return;
			}

			FConfigFile ConfigFile;
			FConfigCacheIni::LoadExternalIniFile(ConfigFile, TEXT("Crypto"), *FPaths::Combine(EngineDir, TEXT("Config\\")), *FPaths::Combine(ProjectDir, TEXT("Config/")), true, *Platform);
			bool bSignPak = false;
			bool bEncryptPakIniFiles = false;
			bool bEncryptPakIndex = false;
			bool bEncryptAssets = false;
			bool bEncryptPak = false;

			if (ConfigFile.Num())
			{
				UE_LOG(LogPakFile, Display, TEXT("Using new format crypto.ini files for crypto configuration"));

				static const TCHAR* SectionName = TEXT("/Script/CryptoKeys.CryptoKeysSettings");

				ConfigFile.GetBool(SectionName, TEXT("bEnablePakSigning"), bSignPak);
				ConfigFile.GetBool(SectionName, TEXT("bEncryptPakIniFiles"), bEncryptPakIniFiles);
				ConfigFile.GetBool(SectionName, TEXT("bEncryptPakIndex"), bEncryptPakIndex);
				ConfigFile.GetBool(SectionName, TEXT("bEncryptAssets"), bEncryptAssets);
				bEncryptPak = bEncryptPakIniFiles || bEncryptPakIndex || bEncryptAssets;

				if (bSignPak)
				{
					FString PublicExpBase64, PrivateExpBase64, ModulusBase64;
					ConfigFile.GetString(SectionName, TEXT("SigningPublicExponent"), PublicExpBase64);
					ConfigFile.GetString(SectionName, TEXT("SigningPrivateExponent"), PrivateExpBase64);
					ConfigFile.GetString(SectionName, TEXT("SigningModulus"), ModulusBase64);

					TArray<uint8> PublicExp, PrivateExp, Modulus;
					FBase64::Decode(PublicExpBase64, PublicExp);
					FBase64::Decode(PrivateExpBase64, PrivateExp);
					FBase64::Decode(ModulusBase64, Modulus);

					OutCryptoSettings.SigningKey = FRSA::CreateKey(PublicExp, PrivateExp, Modulus);

					UE_LOG(LogPakFile, Display, TEXT("Parsed signature keys from config files."));
				}

				if (bEncryptPak)
				{
					FString EncryptionKeyString;
					ConfigFile.GetString(SectionName, TEXT("EncryptionKey"), EncryptionKeyString);

					if (EncryptionKeyString.Len() > 0)
					{
						TArray<uint8> Key;
						FBase64::Decode(EncryptionKeyString, Key);
						check(Key.Num() == sizeof(FAES::FAESKey::Key));
						FNamedAESKey NewKey;
						NewKey.Name = TEXT("Default");
						NewKey.Guid = FGuid();
						FMemory::Memcpy(NewKey.Key.Key, &Key[0], sizeof(FAES::FAESKey::Key));
						OutCryptoSettings.EncryptionKeys.Add(NewKey.Guid, NewKey);
						UE_LOG(LogPakFile, Display, TEXT("Parsed AES encryption key from config files."));
					}
				}
			}
			else
			{
				static const TCHAR* SectionName = TEXT("Core.Encryption");

				UE_LOG(LogPakFile, Display, TEXT("Using old format encryption.ini files for crypto configuration"));

				FConfigCacheIni::LoadExternalIniFile(ConfigFile, TEXT("Encryption"), *FPaths::Combine(EngineDir, TEXT("Config\\")), *FPaths::Combine(ProjectDir, TEXT("Config/")), true, *Platform);
				ConfigFile.GetBool(SectionName, TEXT("SignPak"), bSignPak);
				ConfigFile.GetBool(SectionName, TEXT("EncryptPak"), bEncryptPak);

				if (bSignPak)
				{
					FString RSAPublicExp, RSAPrivateExp, RSAModulus;
					ConfigFile.GetString(SectionName, TEXT("rsa.publicexp"), RSAPublicExp);
					ConfigFile.GetString(SectionName, TEXT("rsa.privateexp"), RSAPrivateExp);
					ConfigFile.GetString(SectionName, TEXT("rsa.modulus"), RSAModulus);

					//TODO: Fix me!
					//OutSigningKey.PrivateKey.Exponent.Parse(RSAPrivateExp);
					//OutSigningKey.PrivateKey.Modulus.Parse(RSAModulus);
					//OutSigningKey.PublicKey.Exponent.Parse(RSAPublicExp);
					//OutSigningKey.PublicKey.Modulus = OutSigningKey.PrivateKey.Modulus;

					UE_LOG(LogPakFile, Display, TEXT("Parsed signature keys from config files."));
				}

				if (bEncryptPak)
				{
					FString EncryptionKeyString;
					ConfigFile.GetString(SectionName, TEXT("aes.key"), EncryptionKeyString);
					FNamedAESKey NewKey;
					NewKey.Name = TEXT("Default");
					NewKey.Guid = FGuid();
					if (EncryptionKeyString.Len() == 32 && TCString<TCHAR>::IsPureAnsi(*EncryptionKeyString))
					{
						for (int32 Index = 0; Index < 32; ++Index)
						{
							NewKey.Key.Key[Index] = (uint8)EncryptionKeyString[Index];
						}
						OutCryptoSettings.EncryptionKeys.Add(NewKey.Guid, NewKey);
						UE_LOG(LogPakFile, Display, TEXT("Parsed AES encryption key from config files."));
					}
				}
			}
		}
	}
	else
	{
		UE_LOG(LogPakFile, Display, TEXT("Using command line for crypto configuration"));

		FString EncryptionKeyString;
		FParse::Value(CmdLine, TEXT("aes="), EncryptionKeyString, false);

		if (EncryptionKeyString.Len() > 0)
		{
			UE_LOG(LogPakFile, Warning, TEXT("A legacy command line syntax is being used for crypto config. Please update to using the -cryptokey parameter as soon as possible as this mode is deprecated"));

			FNamedAESKey NewKey;
			NewKey.Name = TEXT("Default");
			NewKey.Guid = FGuid();
			const uint32 RequiredKeyLength = sizeof(NewKey.Key);

			// Error checking
			if (EncryptionKeyString.Len() < RequiredKeyLength)
			{
				UE_LOG(LogPakFile, Fatal, TEXT("AES encryption key must be %d characters long"), RequiredKeyLength);
			}

			if (EncryptionKeyString.Len() > RequiredKeyLength)
			{
				UE_LOG(LogPakFile, Warning, TEXT("AES encryption key is more than %d characters long, so will be truncated!"), RequiredKeyLength);
				EncryptionKeyString = EncryptionKeyString.Left(RequiredKeyLength);
			}

			if (!FCString::IsPureAnsi(*EncryptionKeyString))
			{
				UE_LOG(LogPakFile, Fatal, TEXT("AES encryption key must be a pure ANSI string!"));
			}

			ANSICHAR* AsAnsi = TCHAR_TO_ANSI(*EncryptionKeyString);
			check(TCString<ANSICHAR>::Strlen(AsAnsi) == RequiredKeyLength);
			FMemory::Memcpy(NewKey.Key.Key, AsAnsi, RequiredKeyLength);
			OutCryptoSettings.EncryptionKeys.Add(NewKey.Guid, NewKey);
			UE_LOG(LogPakFile, Display, TEXT("Parsed AES encryption key from command line."));
		}
	}

	FString EncryptionKeyOverrideGuidString;
	FGuid EncryptionKeyOverrideGuid;
	if (FParse::Value(CmdLine, TEXT("EncryptionKeyOverrideGuid="), EncryptionKeyOverrideGuidString))
	{
		FGuid::Parse(EncryptionKeyOverrideGuidString, EncryptionKeyOverrideGuid);
	}
	OutCryptoSettings.MasterEncryptionKey = OutCryptoSettings.EncryptionKeys.Find(EncryptionKeyOverrideGuid);
}

void ApplyEncryptionKeys(const FKeyChain& KeyChain)
{
	if (KeyChain.EncryptionKeys.Contains(FGuid()))
	{
		FAES::FAESKey DefaultKey = KeyChain.EncryptionKeys[FGuid()].Key;
		FCoreDelegates::GetPakEncryptionKeyDelegate().BindLambda([DefaultKey](uint8 OutKey[32]) { FMemory::Memcpy(OutKey, DefaultKey.Key, sizeof(DefaultKey.Key)); });
	}

	for (const TMap<FGuid, FNamedAESKey>::ElementType& Key : KeyChain.EncryptionKeys)
	{
		if (Key.Key.IsValid())
		{
			FCoreDelegates::GetRegisterEncryptionKeyDelegate().ExecuteIfBound(Key.Key, Key.Value.Key);
		}
	}
}

/**
 * Creates a pak file writer. This can be a signed writer if the encryption keys are specified in the command line
 */
FArchive* CreatePakWriter(const TCHAR* Filename, const FKeyChain& InKeyChain, bool bSign)
{
	FArchive* Writer = IFileManager::Get().CreateFileWriter(Filename);

	if (Writer)
	{
		if (bSign)
		{
			UE_LOG(LogPakFile, Display, TEXT("Creating signed pak %s."), Filename);
			Writer = new FSignedArchiveWriter(*Writer, Filename, InKeyChain.SigningKey);
		}
		else
		{
			UE_LOG(LogPakFile, Display, TEXT("Creating pak %s."), Filename);
		}
	}

	return Writer;
}


bool CreatePakFile(const TCHAR* Filename, TArray<FPakInputPair>& FilesToAdd, const FPakCommandLineParameters& CmdLineParameters, const FKeyChain& InKeyChain)
{
	const double StartTime = FPlatformTime::Seconds();

	// Create Pak
	TUniquePtr<FArchive> PakFileHandle(CreatePakWriter(Filename, InKeyChain, CmdLineParameters.bSign));
	if (!PakFileHandle)
	{
		UE_LOG(LogPakFile, Error, TEXT("Unable to create pak file \"%s\"."), Filename);
		return false;
	}

	FPakInfo Info;
	Info.bEncryptedIndex = (InKeyChain.MasterEncryptionKey && CmdLineParameters.EncryptIndex);
	Info.EncryptionKeyGuid = InKeyChain.MasterEncryptionKey ? InKeyChain.MasterEncryptionKey->Guid : FGuid();


	if (CmdLineParameters.bPatchCompatibilityMode421)
	{
		// for old versions, put in some known names that we may have used
		Info.GetCompressionMethodIndex(NAME_None);
		Info.GetCompressionMethodIndex(NAME_Zlib);
		Info.GetCompressionMethodIndex(NAME_Gzip);
		Info.GetCompressionMethodIndex(TEXT("Bogus"));
		Info.GetCompressionMethodIndex(TEXT("Oodle"));

	}

	TArray<FName> UsedCompressionFormats; // List of compression formats we actually used in this pak file (used for logging only)

	if (InKeyChain.MasterEncryptionKey)
	{
		UE_LOG(LogPakFile, Display, TEXT("Using encryption key '%s' [%s]"), *InKeyChain.MasterEncryptionKey->Name, *InKeyChain.MasterEncryptionKey->Guid.ToString());
	}

	TArray<FPakEntryPair> Index;
	FString MountPoint = GetCommonRootPath(FilesToAdd);
	uint8* ReadBuffer = NULL;
	int64 BufferSize = 0;

	uint8* PaddingBuffer = nullptr;
	int64 PaddingBufferSize = 0;
	if (CmdLineParameters.PatchFilePadAlign > 0 || CmdLineParameters.AlignForMemoryMapping)
	{
		PaddingBufferSize = FMath::Max(CmdLineParameters.PatchFilePadAlign, CmdLineParameters.AlignForMemoryMapping);
		PaddingBuffer = (uint8*)FMemory::Malloc(PaddingBufferSize);
		FMemory::Memset(PaddingBuffer, 0, PaddingBufferSize);
	}

	// Some platforms provide patch download size reduction by diffing the patch files.  However, they often operate on specific block
	// sizes when dealing with new data within the file.  Pad files out to the given alignment to work with these systems more nicely.
	// We also want to combine smaller files into the same padding size block so we don't waste as much space. i.e. grouping 64 1k files together
	// rather than padding each out to 64k.
	const uint32 RequiredPatchPadding = CmdLineParameters.PatchFilePadAlign;

	uint64 ContiguousTotalSizeSmallerThanBlockSize = 0;
	uint64 ContiguousFilesSmallerThanBlockSize = 0;

	uint64 TotalUncompressedSize = 0;
	uint64 TotalCompressedSize = 0;

	uint64 TotalRequestedEncryptedFiles = 0;
	uint64 TotalEncryptedFiles = 0;
	uint64 TotalEncryptedDataSize = 0;

	TArray<FString> ExtensionsToNotUsePluginCompression;
	GConfig->GetArray(TEXT("Pak"), TEXT("ExtensionsToNotUsePluginCompression"), ExtensionsToNotUsePluginCompression, GEngineIni);
	TSet<FString> NoPluginCompressionExtensions;
	for (const FString& Ext : ExtensionsToNotUsePluginCompression)
	{
		NoPluginCompressionExtensions.Add(Ext);
	}

	struct FAsyncCompressor
	{
		
		// input
		const FPakInputPair* FileToAdd; 
		FPakEntryPair Entry;
		const TArray<FName>* CompressionFormats;
		const TSet<FString>* NoPluginCompressionExtensions;

		// output
		FCompressedFileBuffer CompressedFileBuffer;
		FName CompressionMethod; 
		int32 CompressionBlockSize;
		int64 RealFileSize;
		int64 OriginalFileSize;
		volatile bool bIsComplete;
		
		void Init(FPakInputPair* InFileToAdd, const TArray<FName>* InCompressionFormats, int32 InCompressionBlockSize, const TSet<FString>* InNoPluginCompressionExtensions) 
		{
			FileToAdd = InFileToAdd;
			NoPluginCompressionExtensions = InNoPluginCompressionExtensions;
			CompressionFormats= InCompressionFormats;
			CompressionBlockSize = InCompressionBlockSize;	
			bIsComplete = false;
		}
		
		bool IsComplete() const
		{
			return bIsComplete;
		}
		void Complete()
		{
			bIsComplete = true;
		}

		void Compress()
		{
			if (bIsComplete == true)
			{
				return;
			}

			//check if this file requested to be compression
			OriginalFileSize = IFileManager::Get().FileSize(*FileToAdd->Source);
			RealFileSize = OriginalFileSize + Entry.Info.GetSerializedSize(FPakInfo::PakFile_Version_Latest);

			if (OriginalFileSize <= 0)
			{
				CompressionMethod = NAME_None;
				return;
			}


			bool bSomeCompressionSucceeded = false;
			for (int32 MethodIndex = 0; MethodIndex < CompressionFormats->Num(); MethodIndex++)
			{
				CompressionMethod = (*CompressionFormats)[MethodIndex];

				// because compression is a plugin, certain files need to be loadable out of pak files before plugins are loadable
				// (like .uplugin files). for these, we enforce a non-plugin compression - zlib
				bool bForceCompressionFormat = false;
				if (NoPluginCompressionExtensions->Find(FPaths::GetExtension(FileToAdd->Source)) != nullptr)
				{
					CompressionMethod = NAME_Zlib;
					bForceCompressionFormat = true;
				}

				// attempt to compress the data
				if (CompressedFileBuffer.CompressFileToWorkingBuffer(*FileToAdd, CompressionMethod, CompressionBlockSize))
				{
					// Check the compression ratio, if it's too low just store uncompressed. Also take into account read size
					// if we still save 64KB it's probably worthwhile compressing, as that saves a file read operation in the runtime.
								// TODO: drive this threshold from the command line
					float PercentLess = ((float)CompressedFileBuffer.TotalCompressedSize / (OriginalFileSize / 100.f));
					if (PercentLess > 90.f && (OriginalFileSize - CompressedFileBuffer.TotalCompressedSize) < 65536)
					{
						// compression did not succeed, we can try the next format, so do nothing here
					}
					else
					{
						Entry.Info.CompressionBlocks.AddUninitialized(CompressedFileBuffer.CompressedBlocks.Num());
						RealFileSize = CompressedFileBuffer.TotalCompressedSize + Entry.Info.GetSerializedSize(FPakInfo::PakFile_Version_Latest);
						Entry.Info.CompressionBlocks.Reset();

						// at this point, we have successfully compressed the file, no need to continue
						bSomeCompressionSucceeded = true;
					}
				}

				// if we successfully compressed it, or we only wanted a single format, then we are done!
				if (bSomeCompressionSucceeded || bForceCompressionFormat)
				{
					break;
				}
			}

			// If no compression was able to make it small enough, or compress at all, don't compress it
			if (!bSomeCompressionSucceeded)
			{
				UE_LOG(LogPakFile, Log, TEXT("File \"%s\" did not get small enough from compression, or compression failed."), *FileToAdd->Source);
				CompressionMethod = NAME_None;
			}
			Complete();
		}

		void CleanUp()
		{
			CompressedFileBuffer.Empty();
		}
	};



	FThreadSafeCounter CompletionCounter;

	TArray<FAsyncCompressor> AsyncCompressors;
	AsyncCompressors.AddZeroed(FilesToAdd.Num());
	
	FThreadLocalScratchSpace::Get();

	class FRunCompressionTask : public FNonAbandonableTask
	{
		FAsyncCompressor* AsyncCompressor;
	public:
		FRunCompressionTask(FAsyncCompressor* InAsyncCompressor)
		{
			AsyncCompressor = InAsyncCompressor;
		}

		void DoWork()
		{
			AsyncCompressor->Compress();
		}

		FORCEINLINE TStatId GetStatId() const { RETURN_QUICK_DECLARE_CYCLE_STAT(ExampleAsyncTask, STATGROUP_ThreadPoolAsyncTasks); }
	};

	const bool bRunAsync = CmdLineParameters.bAsyncCompression;

	GetDerivedDataCacheRef();

	for (int32 FileIndex = 0; FileIndex < FilesToAdd.Num(); FileIndex++)
	{
		bool bDeleted = FilesToAdd[FileIndex].bIsDeleteRecord;
		//check if this file requested to be compression

		if (FilesToAdd[FileIndex].bNeedsCompression)
		{
			AsyncCompressors[FileIndex].Init(&FilesToAdd[FileIndex], &CmdLineParameters.CompressionFormats, CmdLineParameters.CompressionBlockSize, &NoPluginCompressionExtensions);
			if (bRunAsync)
			{
				(new FAutoDeleteAsyncTask<FRunCompressionTask>(&AsyncCompressors[FileIndex]))->StartBackgroundTask();
			}
		}
		else
		{
			AsyncCompressors[FileIndex].CompressionMethod = NAME_None;
			AsyncCompressors[FileIndex].Complete();
		}
	}

	for (int32 FileIndex = 0; FileIndex < FilesToAdd.Num(); FileIndex++)
	{
		bool bDeleted = FilesToAdd[FileIndex].bIsDeleteRecord;
		bool bIsUAssetUExpPairUAsset = false;
		bool bIsUAssetUExpPairUExp = false;
		bool bIsMappedBulk = FilesToAdd[FileIndex].Source.EndsWith(TEXT(".m.ubulk"));


		if (FileIndex)
		{
			if (FPaths::GetBaseFilename(FilesToAdd[FileIndex - 1].Dest, false) == FPaths::GetBaseFilename(FilesToAdd[FileIndex].Dest, false) &&
				FPaths::GetExtension(FilesToAdd[FileIndex - 1].Dest, true) == TEXT(".uasset") &&
				FPaths::GetExtension(FilesToAdd[FileIndex].Dest, true) == TEXT(".uexp")
				)
			{
				bIsUAssetUExpPairUExp = true;
			}
		}
		if (!bIsUAssetUExpPairUExp && FileIndex + 1 < FilesToAdd.Num())
		{
			if (FPaths::GetBaseFilename(FilesToAdd[FileIndex].Dest, false) == FPaths::GetBaseFilename(FilesToAdd[FileIndex + 1].Dest, false) &&
				FPaths::GetExtension(FilesToAdd[FileIndex].Dest, true) == TEXT(".uasset") &&
				FPaths::GetExtension(FilesToAdd[FileIndex + 1].Dest, true) == TEXT(".uexp")
				)
			{
				bIsUAssetUExpPairUAsset = true;
			}
		}

		//  Remember the offset but don't serialize it with the entry header.
		int64 NewEntryOffset = PakFileHandle->Tell();
		FPakEntryPair &NewEntry = AsyncCompressors[FileIndex].Entry;
		
		if (bRunAsync)
		{
			while (AsyncCompressors[FileIndex].IsComplete() != true)
			{
				FPlatformProcess::Sleep(0.001f);
			}
		}
		else
		{
			AsyncCompressors[FileIndex].Compress();
		}


		const FName& CompressionMethod = AsyncCompressors[FileIndex].CompressionMethod;
		const int32 CompressionBlockSize = AsyncCompressors[FileIndex].CompressionBlockSize;
		const int64 &RealFileSize = AsyncCompressors[FileIndex].RealFileSize;
		const int64 &OriginalFileSize = AsyncCompressors[FileIndex].OriginalFileSize;
		FCompressedFileBuffer& CompressedFileBuffer = AsyncCompressors[FileIndex].CompressedFileBuffer;
		NewEntry.Info.CompressionMethodIndex = Info.GetCompressionMethodIndex(CompressionMethod);

		if (!bDeleted)
		{

			// Account for file system block size, which is a boundary we want to avoid crossing.
			if (!bIsUAssetUExpPairUExp && // don't split uexp / uasset pairs
				CmdLineParameters.FileSystemBlockSize > 0 && OriginalFileSize != INDEX_NONE && RealFileSize <= CmdLineParameters.FileSystemBlockSize)
			{
				if ((NewEntryOffset / CmdLineParameters.FileSystemBlockSize) != ((NewEntryOffset + RealFileSize) / CmdLineParameters.FileSystemBlockSize))
				{
					//File crosses a block boundary, so align it to the beginning of the next boundary
					int64 OldOffset = NewEntryOffset;
					NewEntryOffset = AlignArbitrary(NewEntryOffset, CmdLineParameters.FileSystemBlockSize);
					int64 PaddingRequired = NewEntryOffset - OldOffset;

					if (PaddingRequired > 0)
					{
						// If we don't already have a padding buffer, create one
						if (PaddingBuffer == nullptr)
						{
							PaddingBufferSize = 64 * 1024;
							PaddingBuffer = (uint8*)FMemory::Malloc(PaddingBufferSize);
							FMemory::Memset(PaddingBuffer, 0, PaddingBufferSize);
						}

						UE_LOG(LogPakFile, Verbose, TEXT("%14llu - %14llu : %14llu padding."), PakFileHandle->Tell(), PakFileHandle->Tell() + PaddingRequired, PaddingRequired);
						while (PaddingRequired > 0)
						{
							int64 AmountToWrite = FMath::Min(PaddingRequired, PaddingBufferSize);
							PakFileHandle->Serialize(PaddingBuffer, AmountToWrite);
							PaddingRequired -= AmountToWrite;
						}

						check(PakFileHandle->Tell() == NewEntryOffset);
					}
				}
			}
			// Align bulk data
			if (bIsMappedBulk && CmdLineParameters.AlignForMemoryMapping > 0 && OriginalFileSize != INDEX_NONE && !bDeleted)
			{
				if (!IsAligned(NewEntryOffset + NewEntry.Info.GetSerializedSize(FPakInfo::PakFile_Version_Latest), CmdLineParameters.AlignForMemoryMapping))
				{
					int64 OldOffset = NewEntryOffset;
					NewEntryOffset = AlignArbitrary(NewEntryOffset + NewEntry.Info.GetSerializedSize(FPakInfo::PakFile_Version_Latest), CmdLineParameters.AlignForMemoryMapping) - NewEntry.Info.GetSerializedSize(FPakInfo::PakFile_Version_Latest);
					int64 PaddingRequired = NewEntryOffset - OldOffset;

					check(PaddingRequired > 0);
					check(PaddingBuffer && PaddingBufferSize >= PaddingRequired);

					{
						UE_LOG(LogPakFile, Verbose, TEXT("%14llu - %14llu : %14llu bulk padding."), PakFileHandle->Tell(), PakFileHandle->Tell() + PaddingRequired, PaddingRequired);
						PakFileHandle->Serialize(PaddingBuffer, PaddingRequired);
						check(PakFileHandle->Tell() == NewEntryOffset);
					}
				}
			}
		}

		bool bCopiedToPak;
		int64 SizeToWrite = 0;
		uint8* DataToWrite = nullptr;
		if (bDeleted)
		{
			PrepareDeleteRecordForPak(MountPoint, FilesToAdd[FileIndex], NewEntry);
			bCopiedToPak = false;

			// Directly add the new entry to the index, no more work to do
			Index.Add(NewEntry);
		}
		else if (FilesToAdd[FileIndex].bNeedsCompression && CompressionMethod != NAME_None)
		{
			bCopiedToPak = PrepareCopyCompressedFileToPak(MountPoint, Info, FilesToAdd[FileIndex], CompressedFileBuffer, NewEntry, DataToWrite, SizeToWrite, InKeyChain);
			DataToWrite = CompressedFileBuffer.CompressedBuffer.Get();
		}
		else
		{
			bCopiedToPak = PrepareCopyFileToPak(MountPoint, FilesToAdd[FileIndex], ReadBuffer, BufferSize, NewEntry, DataToWrite, SizeToWrite, InKeyChain);
			DataToWrite = ReadBuffer;
		}

		int64 TotalSizeToWrite = SizeToWrite + NewEntry.Info.GetSerializedSize(FPakInfo::PakFile_Version_Latest);
		if (bCopiedToPak)
		{
			if (RequiredPatchPadding > 0 &&
				!(bIsMappedBulk && CmdLineParameters.AlignForMemoryMapping > 0) // don't wreck the bulk padding with patch padding
				)
			{
				//if the next file is going to cross a patch-block boundary then pad out the current set of files with 0's
				//and align the next file up.
				bool bCrossesBoundary = AlignArbitrary(NewEntryOffset, RequiredPatchPadding) != AlignArbitrary(NewEntryOffset + TotalSizeToWrite - 1, RequiredPatchPadding);
				bool bPatchPadded = false;
				if (!bIsUAssetUExpPairUExp) // never patch-pad the uexp of a uasset/uexp pair
				{
					bool bPairProbablyCrossesBoundary = false; // we don't consider compression because we have not compressed the uexp yet.
					if (bIsUAssetUExpPairUAsset)
					{
						int64 UExpFileSize = IFileManager::Get().FileSize(*FilesToAdd[FileIndex + 1].Source) / 2; // assume 50% compression
						bPairProbablyCrossesBoundary = AlignArbitrary(NewEntryOffset, RequiredPatchPadding) != AlignArbitrary(NewEntryOffset + TotalSizeToWrite + UExpFileSize - 1, RequiredPatchPadding);
					}
					if (TotalSizeToWrite >= RequiredPatchPadding || // if it exactly the padding size and by luck does not cross a boundary, we still consider it "over" because it can't be packed with anything else
						bCrossesBoundary || bPairProbablyCrossesBoundary)
					{
						NewEntryOffset = AlignArbitrary(NewEntryOffset, RequiredPatchPadding);
						int64 CurrentLoc = PakFileHandle->Tell();
						int64 PaddingSize = NewEntryOffset - CurrentLoc;
						check(PaddingSize >= 0);
						if (PaddingSize)
						{
							UE_LOG(LogPakFile, Verbose, TEXT("%14llu - %14llu : %14llu patch padding."), PakFileHandle->Tell(), PakFileHandle->Tell() + PaddingSize, PaddingSize);
							check(PaddingSize <= PaddingBufferSize);

							//have to pad manually with 0's.  File locations skipped by Seek and never written are uninitialized which would defeat the whole purpose
							//of padding for certain platforms patch diffing systems.
							PakFileHandle->Serialize(PaddingBuffer, PaddingSize);
						}
						check(PakFileHandle->Tell() == NewEntryOffset);
						bPatchPadded = true;
					}
				}

				//if the current file is bigger than a patch block then we will always have to pad out the previous files.
				//if there were a large set of contiguous small files behind us then this will be the natural stopping point for a possible pathalogical patching case where growth in the small files causes a cascade 
				//to dirty up all the blocks prior to this one.  If this could happen let's warn about it.
				if (bPatchPadded ||
					FileIndex + 1 == FilesToAdd.Num()) // also check the last file, this won't work perfectly if we don't end up adding the last file for some reason
				{
					const uint64 ContiguousGroupedFilePatchWarningThreshhold = 50 * 1024 * 1024;
					if (ContiguousTotalSizeSmallerThanBlockSize > ContiguousGroupedFilePatchWarningThreshhold)
					{
						UE_LOG(LogPakFile, Display, TEXT("%i small files (%i) totaling %llu contiguous bytes found before first 'large' file.  Changes to any of these files could cause the whole group to be 'dirty' in a per-file binary diff based patching system."), ContiguousFilesSmallerThanBlockSize, RequiredPatchPadding, ContiguousTotalSizeSmallerThanBlockSize);
					}
					ContiguousTotalSizeSmallerThanBlockSize = 0;
					ContiguousFilesSmallerThanBlockSize = 0;
				}
				else
				{
					ContiguousTotalSizeSmallerThanBlockSize += TotalSizeToWrite;
					ContiguousFilesSmallerThanBlockSize++;
				}
			}
			if (FilesToAdd[FileIndex].bNeedsCompression && CompressionMethod != NAME_None)
			{
				UsedCompressionFormats.AddUnique(CompressionMethod); // used for logging only
				FinalizeCopyCompressedFileToPak(Info, CompressedFileBuffer, NewEntry);
			}

			// Write to file
			int64 Offset = PakFileHandle->Tell();
			NewEntry.Info.Serialize(*PakFileHandle, FPakInfo::PakFile_Version_Latest);
			int64 PayloadOffset = PakFileHandle->Tell();
			PakFileHandle->Serialize(DataToWrite, SizeToWrite);
			int64 EndOffset = PakFileHandle->Tell();

			UE_LOG(LogPakFile, Verbose, TEXT("%14llu [header] - %14llu - %14llu : %14llu header+file %s."), Offset, PayloadOffset, EndOffset, EndOffset - Offset, *NewEntry.Filename);

			// Update offset now and store it in the index (and only in index)
			NewEntry.Info.Offset = NewEntryOffset;
			Index.Add(NewEntry);
			const TCHAR* EncryptedString = TEXT("");

			if (FilesToAdd[FileIndex].bNeedEncryption)
			{
				TotalRequestedEncryptedFiles++;

				if (InKeyChain.MasterEncryptionKey)
				{
					TotalEncryptedFiles++;
					TotalEncryptedDataSize += SizeToWrite;
					EncryptedString = TEXT("encrypted ");
				}
			}

			if (FilesToAdd[FileIndex].bNeedsCompression && CompressionMethod != NAME_None)
			{
				TotalCompressedSize += NewEntry.Info.Size;
				TotalUncompressedSize += NewEntry.Info.UncompressedSize;
				float PercentLess = ((float)NewEntry.Info.Size / (NewEntry.Info.UncompressedSize / 100.f));
				if (FilesToAdd[FileIndex].SuggestedOrder < MAX_uint64)
				{
					UE_LOG(LogPakFile, Log, TEXT("Added compressed %sfile \"%s\", %.2f%% of original size. Compressed with %s, Size %lld bytes, Original Size %lld bytes (order %llu)."), EncryptedString, *NewEntry.Filename, PercentLess, *CompressionMethod.ToString(), NewEntry.Info.Size, NewEntry.Info.UncompressedSize, FilesToAdd[FileIndex].SuggestedOrder);
				}
				else
				{
					UE_LOG(LogPakFile, Log, TEXT("Added compressed %sfile \"%s\", %.2f%% of original size. Compressed with %s, Size %lld bytes, Original Size %lld bytes (no order given)."), EncryptedString, *NewEntry.Filename, PercentLess, *CompressionMethod.ToString(), NewEntry.Info.Size, NewEntry.Info.UncompressedSize);
				}
			}
			else
			{
				if (FilesToAdd[FileIndex].SuggestedOrder < MAX_uint64)
				{
					UE_LOG(LogPakFile, Log, TEXT("Added %sfile \"%s\", %lld bytes (order %llu)."), EncryptedString, *NewEntry.Filename, NewEntry.Info.Size, FilesToAdd[FileIndex].SuggestedOrder);
				}
				else
				{
					UE_LOG(LogPakFile, Log, TEXT("Added %sfile \"%s\", %lld bytes (no order given)."), EncryptedString, *NewEntry.Filename, NewEntry.Info.Size);
				}
			}
		}
		else
		{
			if (bDeleted)
			{
				UE_LOG(LogPakFile, Log, TEXT("Created delete record for file \"%s\"."), *FilesToAdd[FileIndex].Source);
			}
			else
			{
				UE_LOG(LogPakFile, Warning, TEXT("Missing file \"%s\" will not be added to PAK file."), *FilesToAdd[FileIndex].Source);
			}
		}
		AsyncCompressors[FileIndex].CleanUp();
	}

	FMemory::Free(PaddingBuffer);
	FMemory::Free(ReadBuffer);
	ReadBuffer = NULL;

	FThreadLocalScratchSpace::Get().CleanUp();

	// Remember IndexOffset
	Info.IndexOffset = PakFileHandle->Tell();

	// Serialize Pak Index at the end of Pak File
	TArray<uint8> IndexData;
	FMemoryWriter IndexWriter(IndexData);
	IndexWriter.SetByteSwapping(PakFileHandle->ForceByteSwapping());
	int32 NumEntries = Index.Num();
	IndexWriter << MountPoint;
	IndexWriter << NumEntries;
	for (int32 EntryIndex = 0; EntryIndex < Index.Num(); EntryIndex++)
	{
		FPakEntryPair& Entry = Index[EntryIndex];
		IndexWriter << Entry.Filename;
		Entry.Info.Serialize(IndexWriter, Info.Version);

		if (RequiredPatchPadding > 0)
		{
			int64 EntrySize = Entry.Info.GetSerializedSize(FPakInfo::PakFile_Version_Latest);
			int64 TotalSizeToWrite = Entry.Info.Size + EntrySize;
			if (TotalSizeToWrite >= RequiredPatchPadding)
			{
				int64 RealStart = Entry.Info.Offset;
				if ((RealStart % RequiredPatchPadding) != 0 &&
					!Entry.Filename.EndsWith(TEXT("uexp")) && // these are export sections of larger files and may be packed with uasset/umap and so we don't need a warning here
					!(Entry.Filename.EndsWith(TEXT(".m.ubulk")) && CmdLineParameters.AlignForMemoryMapping > 0)) // Bulk padding unaligns patch padding and so we don't need a warning here
				{
					UE_LOG(LogPakFile, Warning, TEXT("File at offset %lld of size %lld not aligned to patch size %i"), RealStart, Entry.Info.Size, RequiredPatchPadding);
				}
			}
		}
	}

	if (Info.bEncryptedIndex)
	{
		int32 OriginalSize = IndexData.Num();
		int32 AlignedSize = Align(OriginalSize, FAES::AESBlockSize);

		for (int32 PaddingIndex = IndexData.Num(); PaddingIndex < AlignedSize; ++PaddingIndex)
		{
			uint8 Byte = IndexData[PaddingIndex % OriginalSize];
			IndexData.Add(Byte);
		}
	}

	FSHA1::HashBuffer(IndexData.GetData(), IndexData.Num(), Info.IndexHash.Hash);

	if (Info.bEncryptedIndex)
	{
		check(InKeyChain.MasterEncryptionKey);
		FAES::EncryptData(IndexData.GetData(), IndexData.Num(), InKeyChain.MasterEncryptionKey->Key);
		TotalEncryptedDataSize += IndexData.Num();
	}

	PakFileHandle->Serialize(IndexData.GetData(), IndexData.Num());

	Info.IndexSize = IndexData.Num();

	// Save trailer (offset, size, hash value)
	Info.Serialize(*PakFileHandle, FPakInfo::PakFile_Version_Latest);

	UE_LOG(LogPakFile, Display, TEXT("Added %d files, %lld bytes total, time %.2lfs."), Index.Num(), PakFileHandle->TotalSize(), FPlatformTime::Seconds() - StartTime);
	if (TotalUncompressedSize)
	{
		float PercentLess = ((float)TotalCompressedSize / (TotalUncompressedSize / 100.f));
		UE_LOG(LogPakFile, Display, TEXT("Compression summary: %.2f%% of original size. Compressed Size %lld bytes, Original Size %lld bytes. "), PercentLess, TotalCompressedSize, TotalUncompressedSize);

		FString UsedCompressionFormatsString;
		for (FName CompressionFormat : UsedCompressionFormats)
		{
			UsedCompressionFormatsString.Append( CompressionFormat.ToString() + TEXT(", ") );
		}

		UE_LOG(LogPakFile, Display, TEXT("Used compression formats (in priority order) '%s'"), *UsedCompressionFormatsString);
	}

	if (TotalEncryptedDataSize)
	{
		UE_LOG(LogPakFile, Display, TEXT("Encryption - ENABLED"));
		UE_LOG(LogPakFile, Display, TEXT("  Files: %d"), TotalEncryptedFiles);

		if (Info.bEncryptedIndex)
		{
			UE_LOG(LogPakFile, Display, TEXT("  Index: Encrypted (%d bytes, %.2fMB)"), Info.IndexSize, (float)Info.IndexSize / 1024.0f / 1024.0f);
		}
		else
		{
			UE_LOG(LogPakFile, Display, TEXT("  Index: Unencrypted"));
		}


		UE_LOG(LogPakFile, Display, TEXT("  Total: %d bytes (%.2fMB)"), TotalEncryptedDataSize, (float)TotalEncryptedDataSize / 1024.0f / 1024.0f);
	}
	else
	{
		UE_LOG(LogPakFile, Display, TEXT("Encryption - DISABLED"));
	}

	if (TotalEncryptedFiles < TotalRequestedEncryptedFiles)
	{
		UE_LOG(LogPakFile, Display, TEXT("%d files requested encryption, but no AES key was supplied! Encryption was skipped for these files"), TotalRequestedEncryptedFiles);
	}

	PakFileHandle->Close();
	PakFileHandle.Reset();

#if DETAILED_UNREALPAK_TIMING
	UE_LOG(LogPakFile, Display, TEXT("Detailed timing stats"));
	UE_LOG(LogPakFile, Display, TEXT("Compression time: %lf"), ((double)GCompressionTime) * FPlatformTime::GetSecondsPerCycle());
	UE_LOG(LogPakFile, Display, TEXT("DDC Hits: %d"), GDDCHits);
	UE_LOG(LogPakFile, Display, TEXT("DDC Misses: %d"), GDDCMisses);
	UE_LOG(LogPakFile, Display, TEXT("DDC Sync Read Time: %lf"), ((double)GDDCSyncReadTime) * FPlatformTime::GetSecondsPerCycle());
	UE_LOG(LogPakFile, Display, TEXT("DDC Sync Write Time: %lf"), ((double)GDDCSyncWriteTime) * FPlatformTime::GetSecondsPerCycle());
#endif
	return true;
}

bool TestPakFile(const TCHAR* Filename)
{	
	FPakFile PakFile(&FPlatformFileManager::Get().GetPlatformFile(), Filename, false);
	if (PakFile.IsValid())
	{
		return PakFile.Check();
	}
	else
	{
		UE_LOG(LogPakFile, Error, TEXT("Unable to open pak file \"%s\"."), Filename);
		return false;
	}
}

bool ListFilesInPak(const TCHAR * InPakFilename, int64 SizeFilter, bool bIncludeDeleted, const FString& CSVFilename, bool bExtractToMountPoint, const FKeyChain& InKeyChain)
{
	FPakFile PakFile(&FPlatformFileManager::Get().GetPlatformFile(), InPakFilename, false);
	int32 FileCount = 0;
	int64 FileSize = 0;
	int64 FilteredSize = 0;

	if (PakFile.IsValid())
	{
		UE_LOG(LogPakFile, Display, TEXT("Mount point %s"), *PakFile.GetMountPoint());

		TArray<FPakFile::FFileIterator> Records;

		for (FPakFile::FFileIterator It(PakFile,bIncludeDeleted); It; ++It)
		{
			Records.Add(It);
		}

		struct FOffsetSort
		{
			FORCEINLINE bool operator()(const FPakFile::FFileIterator& A, const FPakFile::FFileIterator& B) const
			{
				return A.Info().Offset < B.Info().Offset;
			}
		};

		Records.Sort(FOffsetSort());

		const FString MountPoint = bExtractToMountPoint ? PakFile.GetMountPoint() : TEXT("");

		if (CSVFilename.Len() > 0)
		{
			
			TArray<FString> Lines;
			Lines.Empty(Records.Num()+2);
			Lines.Add(TEXT("Filename, Offset, Size, Hash, Deleted, Compressed, CompressionMethod"));
			for (auto It : Records)
			{
				const FPakEntry& Entry = It.Info();

				bool bWasCompressed = Entry.CompressionMethodIndex != 0;

				Lines.Add( FString::Printf(
					TEXT("%s%s, %d, %d, %s, %s, %s, %d"),
					*MountPoint, *It.Filename(),
					Entry.Offset, Entry.Size,
					*BytesToHex(Entry.Hash, sizeof(Entry.Hash)),
					Entry.IsDeleteRecord() ? TEXT("true") : TEXT("false"),
					bWasCompressed ? TEXT("true") : TEXT("false"),
					Entry.CompressionMethodIndex) );
			}

			if (FFileHelper::SaveStringArrayToFile(Lines, *CSVFilename) == false)
			{
				UE_LOG(LogPakFile, Display, TEXT("Failed to save CSV file %s"), *CSVFilename);
			}
			else
			{
				UE_LOG(LogPakFile, Display, TEXT("Saved CSV file to %s"), *CSVFilename);
			}
		}

		TSet<int32> InspectChunks;
		FString InspectChunkString;
		FParse::Value(FCommandLine::Get(), TEXT("InspectChunk="), InspectChunkString, false);
		TArray<FString> InspectChunkRanges;
		if (InspectChunkString.TrimStartAndEnd().ParseIntoArray(InspectChunkRanges, TEXT(",")))
		{
			for (const FString& InspectChunkRangeString : InspectChunkRanges)
			{
				TArray<FString> RangeLimits;
				if (InspectChunkRangeString.TrimStartAndEnd().ParseIntoArray(RangeLimits, TEXT("-")))
				{
					if (RangeLimits.Num() == 1)
					{
						int32 Chunk = -1;
						LexFromString(Chunk, *InspectChunkRangeString);
						if (Chunk != -1)
						{
							InspectChunks.Add(Chunk);
						}
					}
					else if (RangeLimits.Num() == 2)
					{
						int32 FirstChunk = -1;
						int32 LastChunk = -1;
						LexFromString(FirstChunk, *RangeLimits[0]);
						LexFromString(LastChunk, *RangeLimits[1]);
						if (FirstChunk != -1 && LastChunk != -1)
						{
							for (int32 Chunk = FirstChunk; Chunk <= LastChunk; ++Chunk)
							{
								InspectChunks.Add(Chunk);
							}
						}
					}
					else
					{
						UE_LOG(LogPakFile, Error, TEXT("Error parsing inspect chunk range '%s'"), *InspectChunkRangeString);
					}
				}
			}
		}
		for (auto It : Records)
		{
			const FPakEntry& Entry = It.Info();
			if (Entry.Size >= SizeFilter)
			{
				if (InspectChunkRanges.Num() > 0)
				{
					int32 FirstChunk = Entry.Offset / (64 * 1024);
					int32 LastChunk = (Entry.Offset + Entry.Size) / (64 * 1024);

					for (int32 Chunk = FirstChunk; Chunk <= LastChunk; ++Chunk)
					{
						if (InspectChunks.Contains(Chunk))
						{
							UE_LOG(LogPakFile, Display, TEXT("[%d - %d] \"%s%s\" offset: %lld, size: %d bytes, sha1: %s, compression: %s."), FirstChunk, LastChunk, *MountPoint, *It.Filename(), Entry.Offset, Entry.Size, *BytesToHex(Entry.Hash, sizeof(Entry.Hash)), *PakFile.GetInfo().GetCompressionMethod(Entry.CompressionMethodIndex).ToString());
							break;
						}
					}
				}
				else
				{
					UE_LOG(LogPakFile, Display, TEXT("\"%s%s\" offset: %lld, size: %d bytes, sha1: %s, compression: %s."), *MountPoint, *It.Filename(), Entry.Offset, Entry.Size, *BytesToHex(Entry.Hash, sizeof(Entry.Hash)), *PakFile.GetInfo().GetCompressionMethod(Entry.CompressionMethodIndex).ToString());
				}
			}
			FileSize += Entry.Size;
			FileCount++;
		}
		UE_LOG(LogPakFile, Display, TEXT("%d files (%lld bytes), (%lld filtered bytes)."), FileCount, FileSize, FilteredSize);

		return true;
	}
	else
	{
		if (PakFile.GetInfo().EncryptionKeyGuid.IsValid() && !InKeyChain.EncryptionKeys.Contains(PakFile.GetInfo().EncryptionKeyGuid))
		{
			UE_LOG(LogPakFile, Fatal, TEXT("Missing encryption key %s for pak file \"%s\"."), *PakFile.GetInfo().EncryptionKeyGuid.ToString(), InPakFilename);
		}
		else
		{
			UE_LOG(LogPakFile, Fatal, TEXT("Unable to open pak file \"%s\"."), InPakFilename);
		}

		return false;
	}
}

int32 GetPakPriorityFromFilename( const FString& PakFilename )
{
	// Parse the pak file index, the base pak file is index -1
	int32 PakPriority = -1;
	if (PakFilename.EndsWith("_P.pak"))
	{
		FString PakIndexFromFilename = PakFilename.LeftChop(6);
		int32 PakIndexStart = INDEX_NONE;
		PakIndexFromFilename.FindLastChar('_', PakIndexStart);
		if (PakIndexStart != INDEX_NONE)
		{
			PakIndexFromFilename = PakIndexFromFilename.RightChop(PakIndexStart + 1);
			if (PakIndexFromFilename.IsNumeric())
			{
				PakPriority = FCString::Atoi(*PakIndexFromFilename);
			}
		}
	}

	return PakPriority;
}

int32 GetPakChunkIndexFromFilename( const FString& PakFilePath )
{
	const TCHAR* PakChunkPrefix = TEXT("pakchunk");
	const int32 PakChunkPrefixLength = 8;//FCString::Strlen(PakChunkPrefix);

	int32 PakChunkIndex = -1;
	FString PakFilename = FPaths::GetCleanFilename(PakFilePath);
	if (PakFilename.StartsWith(PakChunkPrefix))
	{
		int32 ChunkIndexStart = INDEX_NONE;
		if( PakFilename.FindChar(TEXT('-'), ChunkIndexStart ) )
		{
			FString PakChunkFromFilename = PakFilename.Mid( PakChunkPrefixLength, ChunkIndexStart - PakChunkPrefixLength );
			if( PakChunkFromFilename.IsNumeric() )
			{
				PakChunkIndex = FCString::Atoi(*PakChunkFromFilename);
			}
		}
	}

	return PakChunkIndex;
}

bool AuditPakFiles( const FString& InputPath, bool bOnlyDeleted, const FString& CSVFilename, const FPakOrderMap& OrderMap, bool bSortByOrdering )
{
	//collect all pak files
	FString PakFileDirectory;
	TArray<FString> PakFileList;
	if (FPaths::DirectoryExists(InputPath))
	{
		//InputPath is a directory
		IFileManager::Get().FindFiles(PakFileList, *InputPath, TEXT(".pak") );
		PakFileDirectory = InputPath;
	}
	else
	{
		//InputPath is a search wildcard (or a directory that doesn't exist...)
		IFileManager::Get().FindFiles(PakFileList, *InputPath, true, false);
		PakFileDirectory = FPaths::GetPath(InputPath);
	}
	if (PakFileList.Num() == 0)
	{
		UE_LOG(LogPakFile, Error, TEXT("No pak files found searching \"%s\"."), *InputPath);
		return false;
	}
	
	struct FFilePakRevision
	{
		FString PakFilename;
		int32 PakPriority;
		int32 Size;
	};
	TMap<FString, FFilePakRevision> FileRevisions;
	TMap<FString, FFilePakRevision> DeletedRevisions;
	TMap<FString, FString> PakFilenameToPatchDotChunk;
	int32 HighestPakPriority = -1;

	//build lookup tables for the newest revision of all files and all deleted files
	for (int32 PakFileIndex = 0; PakFileIndex < PakFileList.Num(); PakFileIndex++)
	{
		FString PakFilename = PakFileDirectory + "\\" + PakFileList[PakFileIndex];
		int32 PakPriority = GetPakPriorityFromFilename(PakFilename);
		HighestPakPriority = FMath::Max( HighestPakPriority, PakPriority );

		FPakFile PakFile(&FPlatformFileManager::Get().GetPlatformFile(), *PakFilename, false);
		if (PakFile.IsValid())
		{
			FString PakMountPoint = PakFile.GetMountPoint().Replace(TEXT("../../../"), TEXT(""));

			const bool bIncludeDeleted = true;
			for (FPakFile::FFileIterator It(PakFile,bIncludeDeleted); It; ++It)
			{
				FString AssetName = PakMountPoint;
				if (!AssetName.IsEmpty() && !AssetName.EndsWith("/"))
				{
					AssetName += "/";
				}
				AssetName += It.Filename();

				FFilePakRevision Revision;
				Revision.PakFilename = PakFileList[PakFileIndex];
				Revision.PakPriority = PakPriority;
				Revision.Size = It.Info().Size;

				//add or update the entry for the appropriate revision, depending on whether this is a delete record or not
				TMap<FString, FFilePakRevision>& AppropriateRevisions = (It.Info().IsDeleteRecord()) ? DeletedRevisions : FileRevisions;
				if (!AppropriateRevisions.Contains(AssetName))
				{
					AppropriateRevisions.Add(AssetName, Revision);
				}
				else if (AppropriateRevisions[AssetName].PakPriority < Revision.PakPriority)
				{
					AppropriateRevisions[AssetName] = Revision;
				}
			}


			//build "patch.chunk" string
			FString PatchDotChunk;
			PatchDotChunk += FString::Printf( TEXT("%d."), PakPriority+1 );
			int32 ChunkIndex = GetPakChunkIndexFromFilename( PakFilename );
			if( ChunkIndex != -1 )
			{
				PatchDotChunk += FString::Printf( TEXT("%d"), ChunkIndex );
			}
			PakFilenameToPatchDotChunk.Add( PakFileList[PakFileIndex], PatchDotChunk );
		}
		else
		{
			UE_LOG(LogPakFile, Error, TEXT("Unable to open pak file \"%s\"."), *PakFilename);
			return false;
		}
	}

	bool bHasOpenOrder = (OrderMap.Num() > 0);

	//open CSV file, if requested
	FArchive* CSVFileWriter = nullptr;
	if( !CSVFilename.IsEmpty() )
	{
		CSVFileWriter = IFileManager::Get().CreateFileWriter(*CSVFilename);
		if (CSVFileWriter == nullptr)
		{
			UE_LOG(LogPakFile, Error, TEXT("Unable to open csv file \"%s\"."), *CSVFilename);
			return false;
		}
	}

	//helper lambda for writing line depending on whether there's a CSV file or not
	auto WriteCSVLine = [CSVFileWriter]( const FString& Text )
	{
		if( CSVFileWriter )
		{
			CSVFileWriter->Logf( TEXT("%s"), *Text );
		}
		else
		{
			UE_LOG(LogPakFile, Display, TEXT("%s"), *Text );
		}
	};

	//cache open order for faster lookup
	TMap<FString,uint64> CachedOpenOrder;
	if( bHasOpenOrder )
	{
		UE_LOG(LogPakFile, Display, TEXT("Checking open order data") );
		for (auto Itr : FileRevisions)
		{
			const FString& AssetPath = Itr.Key;
			FString OpenOrderAssetName = FString::Printf( TEXT("../../../%s"), *AssetPath );
			FPaths::NormalizeFilename(OpenOrderAssetName);
			OpenOrderAssetName.ToLowerInline();

			uint64 OrderIndex = OrderMap.GetFileOrder(OpenOrderAssetName, false);
			if (OrderIndex != MAX_uint64)
			{
				CachedOpenOrder.Add( AssetPath, OrderIndex );
			}
		}
	}

	//helper lambda to look up cached open order
	auto FindOpenOrder = [&]( const FString& AssetPath )
	{
		if( const uint64* OrderIndexPtr = CachedOpenOrder.Find( AssetPath ) )
		{
			return (*OrderIndexPtr);
		}

		return uint64(UINT64_MAX);
	};

	//log every file, sorted alphabetically
	if( bSortByOrdering && bHasOpenOrder )
	{
		UE_LOG(LogPakFile, Display, TEXT("Sorting pak audit data by open order") );
		FileRevisions.KeySort([&]( const FString& A, const FString& B )
		{
			return FindOpenOrder(A) < FindOpenOrder(B);
		});
		DeletedRevisions.KeySort([&]( const FString& A, const FString& B )
		{
			return FindOpenOrder(A) < FindOpenOrder(B);
		});
	}
	else
	{
		UE_LOG(LogPakFile, Display, TEXT("Sorting pak audit data by name") );
		FileRevisions.KeySort([]( const FString& A, const FString& B )
		{
			return A.Compare(B, ESearchCase::IgnoreCase) < 0;
		});
		DeletedRevisions.KeySort([]( const FString& A, const FString& B )
		{
			return A.Compare(B, ESearchCase::IgnoreCase) < 0;
		});
	}

	FString PreviousPatchDotChunk;
	int NumSeeks = 0;
	int NumReads = 0;

	UE_CLOG((CSVFileWriter!=nullptr),LogPakFile, Display, TEXT("Writing pak audit CSV file %s..."), *CSVFilename );
	WriteCSVLine( TEXT("AssetName,State,Pak,Prev.Pak,Rev,Prev.Rev,Size,AssetPath,Patch.Chunk,OpenOrder" ) );
	for (auto Itr : FileRevisions)
	{
		const FString& AssetPath = Itr.Key;
		const FString AssetName = FPaths::GetCleanFilename(AssetPath);
		const FFilePakRevision* DeletedRevision = DeletedRevisions.Find(AssetPath);

		//look up the open order for this file
		FString OpenOrderText = "";
		uint64 OpenOrder = FindOpenOrder(AssetPath);
		if( OpenOrder != UINT64_MAX )
		{
			OpenOrderText = FString::Printf( TEXT("%llu"), OpenOrder );
		}

		//lookup patch.chunk value
		FString PatchDotChunk = "";
		if( const FString* PatchDotChunkPtr = PakFilenameToPatchDotChunk.Find(Itr.Value.PakFilename) )
		{
			PatchDotChunk = *PatchDotChunkPtr;
		}

		bool bFileExists = true;
		if (DeletedRevision == nullptr)
		{
			if (bOnlyDeleted)
			{
				//skip
			}
			else if (Itr.Value.PakPriority == HighestPakPriority)
			{
				WriteCSVLine( FString::Printf( TEXT("%s,Fresh,%s,,%d,,%d,%s,%s,%s"), *AssetName, *Itr.Value.PakFilename, Itr.Value.PakPriority, Itr.Value.Size, *AssetPath, *PatchDotChunk, *OpenOrderText ) );
			}
			else
			{
				WriteCSVLine( FString::Printf( TEXT("%s,Inherited,%s,,%d,,%d,%s,%s,%s"), *AssetName, *Itr.Value.PakFilename, Itr.Value.PakPriority, Itr.Value.Size, *AssetPath, *PatchDotChunk, *OpenOrderText  ) );
			}
		}
		else if (DeletedRevision->PakPriority == Itr.Value.PakPriority)
		{
			WriteCSVLine( FString::Printf( TEXT("%s,Moved,%s,%s,%d,,%d,%s,%s,%s"), *AssetName, *Itr.Value.PakFilename, *DeletedRevision->PakFilename, Itr.Value.PakPriority, Itr.Value.Size, *AssetPath, *PatchDotChunk, *OpenOrderText ) );
		}
		else if (DeletedRevision->PakPriority > Itr.Value.PakPriority)
		{
			WriteCSVLine( FString::Printf( TEXT("%s,Deleted,%s,%s,%d,%d,,%s,%s,%s"), *AssetName, *DeletedRevision->PakFilename, *Itr.Value.PakFilename, DeletedRevision->PakPriority, Itr.Value.PakPriority, *AssetPath, *PatchDotChunk, *OpenOrderText ) );
			bFileExists = false;
		}
		else if (DeletedRevision->PakPriority < Itr.Value.PakPriority)
		{
			WriteCSVLine( FString::Printf( TEXT("%s,Restored,%s,%s,%d,%d,%d,%s,%s,%s"), *AssetName, *Itr.Value.PakFilename, *DeletedRevision->PakFilename, Itr.Value.PakPriority, DeletedRevision->PakPriority, Itr.Value.Size, *AssetPath, *PatchDotChunk, *OpenOrderText ) );
		}

		if( bFileExists && bSortByOrdering && bHasOpenOrder )
		{
			NumReads++;
			if( PreviousPatchDotChunk != PatchDotChunk )
			{ 
				PreviousPatchDotChunk = PatchDotChunk;
				NumSeeks++;
			}
		}
	}

	//check for deleted assets where there is no previous revision (missing pak files?)
	for (auto Itr : DeletedRevisions)
	{
		const FString& AssetPath = Itr.Key;
		const FFilePakRevision* Revision = FileRevisions.Find(AssetPath);
		if (Revision == nullptr)
		{
			//look up the open order for this file
			FString OpenOrderText = "";
			uint64 OpenOrder = FindOpenOrder(AssetPath);
			if( OpenOrder != UINT64_MAX )
			{
				OpenOrderText = FString::Printf( TEXT("%llu"), OpenOrder );
			}

			//lookup patch.chunk value
			FString PatchDotChunk = "";
			if( const FString* PatchDotChunkPtr = PakFilenameToPatchDotChunk.Find(Itr.Value.PakFilename) )
			{
				PatchDotChunk = *PatchDotChunkPtr;
			}

			const FString AssetName = FPaths::GetCleanFilename(AssetPath);
			WriteCSVLine( FString::Printf( TEXT("%s,Deleted,%s,Error,%d,,,%s,%s,%s"), *AssetName, *Itr.Value.PakFilename, Itr.Value.PakPriority, *AssetPath, *PatchDotChunk, *OpenOrderText ) );
		}
	}

	//clean up CSV writer
	if (CSVFileWriter)
	{
		CSVFileWriter->Close();
		delete CSVFileWriter;
		CSVFileWriter = NULL;
	}


	//write seek summary
	if( bSortByOrdering && bHasOpenOrder && NumReads > 0 )
	{
		UE_LOG( LogPakFile, Display, TEXT("%d guaranteed seeks out of %d files read (%.2f%%) with the given open order"), NumSeeks, NumReads, (NumSeeks*100.0f)/NumReads );
	}

	return true;
}

bool ListFilesAtOffset( const TCHAR* InPakFileName, const TArray<int64>& InOffsets )
{
	if( InOffsets.Num() == 0 )
	{
		UE_LOG(LogPakFile, Error, TEXT("No offsets specified") );
		return false;
	}

	FPakFile PakFile(&FPlatformFileManager::Get().GetPlatformFile(), InPakFileName, false);
	if (!PakFile.IsValid())
	{
		UE_LOG(LogPakFile, Error, TEXT("Failed to open %s"), InPakFileName );
		return false;
	}

	UE_LOG( LogPakFile, Display, TEXT("%-12s%-12s%-12s%s"), TEXT("Offset"), TEXT("File Offset"), TEXT("File Size"), TEXT("File Name") );

	TArray<int64> OffsetsToCheck = InOffsets;
	FArchive& PakReader = *PakFile.GetSharedReader(NULL);
	for (FPakFile::FFileIterator It(PakFile); It; ++It)
	{
		const FPakEntry& Entry = It.Info();

		//see if this file is on of the ones in the offset range we want
		int64 FoundOffset = INDEX_NONE;
		for( int64 Offset : OffsetsToCheck )
		{
			if( Offset >= Entry.Offset && Offset <= Entry.Offset+Entry.Size )
			{
				UE_LOG( LogPakFile, Display, TEXT("%-12lld%-12lld%-12d%s"), Offset, Entry.Offset, Entry.Size, *It.Filename() );
				FoundOffset = Offset;
				break;
			}
		}

		//remove it from the list if we found a match
		if( FoundOffset != INDEX_NONE )
		{
			OffsetsToCheck.Remove(FoundOffset);
		}
	}

	//list out any that we didn't find a match for
	for( int64 InvalidOffset : OffsetsToCheck )
	{
		UE_LOG(LogPakFile, Display, TEXT("%-12lld - invalid offset"), InvalidOffset );
	}

	return true;
}

// used for diagnosing errors in FPakAsyncReadFileHandle::RawReadCallback
bool ShowCompressionBlockCRCs( const TCHAR* InPakFileName, TArray<int64>& InOffsets, const FKeyChain& InKeyChain )
{
	// open the pak file
	FPakFile PakFile(&FPlatformFileManager::Get().GetPlatformFile(), InPakFileName, false);
	if (!PakFile.IsValid())
	{
		UE_LOG(LogPakFile, Error, TEXT("Failed to open %s"), InPakFileName );
		return false;
	}

	// read the pak file and iterate over all given offsets
	FArchive& PakReader = *PakFile.GetSharedReader(NULL);
	UE_LOG(LogPakFile, Display, TEXT("") );
	for( int64 Offset : InOffsets )
	{
		//sanity check the offset
		if (Offset < 0 || Offset > PakFile.TotalSize() )
		{
			UE_LOG(LogPakFile, Error, TEXT("Offset: %lld - out of range (max size is %lld)"), Offset, PakFile.TotalSize() );
			continue;
		}

		//find the matching entry
		const FPakEntry* Entry = nullptr;
		for (FPakFile::FFileIterator It(PakFile); It; ++It)
		{
			const FPakEntry& ThisEntry = It.Info();
			if( Offset >= ThisEntry.Offset && Offset <= ThisEntry.Offset+ThisEntry.Size )
			{
				Entry = &ThisEntry;
				FString EntryFilename = It.Filename();
				FName EntryCompressionMethod = PakFile.GetInfo().GetCompressionMethod(Entry->CompressionMethodIndex);

				UE_LOG(LogPakFile, Display, TEXT("Offset: %lld  -> EntrySize: %lld  Encrypted: %-3s  Compression: %-8s  [%s]"), Offset, Entry->Size, Entry->IsEncrypted() ? TEXT("Yes") : TEXT("No"), *EntryCompressionMethod.ToString(), *EntryFilename );
				break;
			}
		}
		if (Entry == nullptr)
		{
			UE_LOG(LogPakFile, Error, TEXT("Offset: %lld - no entry found."), Offset );
			continue;
		}

		// sanity check
		if (Entry->CompressionMethodIndex == 0)
		{
			UE_LOG(LogPakFile, Error, TEXT("    Entry isn't compressed (not supported)") );
			continue;
		}
		if (Entry->IsDeleteRecord())
		{
			UE_LOG(LogPakFile, Error, TEXT("    Entry is deleted") );
			continue;
		}

		//iterate over all blocks, decoding them and computing the checksum

		//... adapted from UncompressCopyFile...
		FName EntryCompressionMethod = PakFile.GetInfo().GetCompressionMethod(Entry->CompressionMethodIndex);
		int32 MaxCompressionBlockSize = FCompression::CompressMemoryBound(EntryCompressionMethod, Entry->CompressionBlockSize);
		for (const FPakCompressedBlock& Block : Entry->CompressionBlocks)
		{
			MaxCompressionBlockSize = FMath::Max<int32>(MaxCompressionBlockSize, Block.CompressedEnd - Block.CompressedStart);
		}

		int64 WorkingSize = Entry->CompressionBlockSize + MaxCompressionBlockSize;
		uint8* PersistentBuffer = (uint8*)FMemory::Malloc(WorkingSize);

		for (uint32 BlockIndex=0, BlockIndexNum=Entry->CompressionBlocks.Num(); BlockIndex < BlockIndexNum; ++BlockIndex)
		{
			uint32 CompressedBlockSize = Entry->CompressionBlocks[BlockIndex].CompressedEnd - Entry->CompressionBlocks[BlockIndex].CompressedStart;
			uint32 UncompressedBlockSize = (uint32)FMath::Min<int64>(Entry->UncompressedSize - Entry->CompressionBlockSize*BlockIndex, Entry->CompressionBlockSize);
			PakReader.Seek(Entry->CompressionBlocks[BlockIndex].CompressedStart + (PakFile.GetInfo().HasRelativeCompressedChunkOffsets() ? Entry->Offset : 0));
			uint32 SizeToRead = Entry->IsEncrypted() ? Align(CompressedBlockSize, FAES::AESBlockSize) : CompressedBlockSize;
			PakReader.Serialize(PersistentBuffer, SizeToRead);

			if (Entry->IsEncrypted())
			{
				const FNamedAESKey* Key = InKeyChain.MasterEncryptionKey;
				check(Key);
				FAES::DecryptData(PersistentBuffer, SizeToRead, Key->Key);
			}

			// adapted from FPakAsyncReadFileHandle::RawReadCallback
			int32 ProcessedSize = Entry->CompressionBlockSize;
			if (BlockIndex == BlockIndexNum - 1)
			{
				ProcessedSize = Entry->UncompressedSize % Entry->CompressionBlockSize;
				if (!ProcessedSize)
				{
					ProcessedSize = Entry->CompressionBlockSize; // last block was a full block
				}
			}

			// compute checksum and log out the block information
			const uint32 BlockCrc32 = FCrc::MemCrc32( PersistentBuffer, CompressedBlockSize );
			const FString HexBytes = BytesToHex(PersistentBuffer,FMath::Min(CompressedBlockSize,32U));
			UE_LOG(LogPakFile, Display, TEXT("    Block:%-6d  ProcessedSize: %-6d  DecompressionRawSize: %-6d  Crc32: %-12u [%s...]"), BlockIndex, ProcessedSize, CompressedBlockSize, BlockCrc32, *HexBytes );
		}

		FMemory::Free(PersistentBuffer);
		UE_LOG(LogPakFile, Display, TEXT("") );

	}

	UE_LOG(LogPakFile, Display, TEXT("done") );
	return true;
}

bool GeneratePIXMappingFile(const TArray<FString> InPakFileList, const FString& OutputPath)
{
	if (!InPakFileList.Num())
	{
		UE_LOG(LogPakFile, Error, TEXT("Pak file list can not be empty."));
		return false;
	}

	if (!FPaths::DirectoryExists(OutputPath))
	{
		UE_LOG(LogPakFile, Error, TEXT("Output path doesn't exist.  Create %s."), *OutputPath);
		FPlatformFileManager::Get().GetPlatformFile().CreateDirectoryTree(*OutputPath);
	}

	for (const FString& PakFileName : InPakFileList)
	{
		// open CSV file, if requested
		FArchive* CSVFileWriter = nullptr;
		FString OutputMappingFilename = OutputPath / FPaths::GetBaseFilename(PakFileName) + TEXT(".csv");
		if (!OutputMappingFilename.IsEmpty())
		{
			CSVFileWriter = IFileManager::Get().CreateFileWriter(*OutputMappingFilename);
			if (CSVFileWriter == nullptr)
			{
				UE_LOG(LogPakFile, Error, TEXT("Unable to open csv file \"%s\"."), *OutputMappingFilename);
				return false;
			}
		}

		FPakFile PakFile(&FPlatformFileManager::Get().GetPlatformFile(), *PakFileName, false);
		if (!PakFile.IsValid())
		{
			UE_LOG(LogPakFile, Error, TEXT("Failed to open %s"), *PakFileName);
			return false;
		}

		CSVFileWriter->Logf(TEXT("%s"), *PakFileName);

		const FString PakFileMountPoint = PakFile.GetMountPoint();
		FArchive& PakReader = *PakFile.GetSharedReader(NULL);
		for (FPakFile::FFileIterator It(PakFile); It; ++It)
		{
			const FPakEntry& Entry = It.Info();

			CSVFileWriter->Logf(TEXT("0x%010llx,0x%08llx,%s"), Entry.Offset, Entry.Size, *(PakFileMountPoint / It.Filename()));
		}

		CSVFileWriter->Close();
		delete CSVFileWriter;
		CSVFileWriter = nullptr;
	}

	return true;
}

bool ExtractFilesFromPak(const TCHAR* InPakFilename, TMap<FString, FFileInfo>& InFileHashes, 
	const TCHAR* InDestPath, bool bUseMountPoint, const FKeyChain& InKeyChain, const FString* InFilter, 
	TArray<FPakInputPair>* OutEntries, TArray<FPakInputPair>* OutDeletedEntries, FPakOrderMap* OutOrderMap, 
	TArray<FGuid>* OutUsedEncryptionKeys, bool* OutAnyPakSigned)
{
	// Gather all patch versions of the requested pak file and run through each separately
	TArray<FString> PakFileList;
	FString PakFileDirectory = FPaths::GetPath(InPakFilename);
	// If file doesn't exist try using it as a search string, it may contain wild cards
	if (IFileManager::Get().FileExists(InPakFilename))
	{
		PakFileList.Add(*FPaths::GetCleanFilename(InPakFilename));
	}
	else
	{
		IFileManager::Get().FindFiles(PakFileList, *PakFileDirectory, *FPaths::GetCleanFilename(InPakFilename));
	}

	if (PakFileList.Num() == 0)
	{
		// No files found
		return false;
	}


	bool bIncludeDeleted = (OutDeletedEntries != nullptr);

	for (int32 PakFileIndex = 0; PakFileIndex < PakFileList.Num(); PakFileIndex++)
	{
		FString PakFilename = PakFileDirectory + "\\" + PakFileList[PakFileIndex];
		int32 PakPriority = GetPakPriorityFromFilename(PakFilename);

		FPakFile PakFile(&FPlatformFileManager::Get().GetPlatformFile(), *PakFilename, false);
		if (PakFile.IsValid())
		{
			FString DestPath(InDestPath);
			FArchive& PakReader = *PakFile.GetSharedReader(NULL);
			const int64 BufferSize = 8 * 1024 * 1024; // 8MB buffer for extracting
			void* Buffer = FMemory::Malloc(BufferSize);
			int64 CompressionBufferSize = 0;
			uint8* PersistantCompressionBuffer = NULL;
			int32 ErrorCount = 0;
			int32 FileCount = 0;
			int32 ExtractedCount = 0;

			if (OutUsedEncryptionKeys)
			{
				OutUsedEncryptionKeys->Add(PakFile.GetInfo().EncryptionKeyGuid);
			}

			if (OutAnyPakSigned && *OutAnyPakSigned == false)
			{
				FString SignatureFile(FPaths::ChangeExtension(PakFilename, TEXT(".sig")));
				if (FPaths::FileExists(SignatureFile))
				{
					*OutAnyPakSigned = true;
				}
			}

			FString PakMountPoint = bUseMountPoint ? PakFile.GetMountPoint().Replace(TEXT("../../../"), TEXT("")) : TEXT("");

			for (FPakFile::FFileIterator It(PakFile,bIncludeDeleted); It; ++It, ++FileCount)
			{
				// Extract only the most recent version of a file when present in multiple paks
				FFileInfo* HashFileInfo = InFileHashes.Find(It.Filename());
				if (HashFileInfo == nullptr || HashFileInfo->PatchIndex == PakPriority)
				{
					FString DestFilename(DestPath / PakMountPoint / It.Filename());

					const FPakEntry& Entry = It.Info();
					if (Entry.IsDeleteRecord())
					{
						UE_LOG(LogPakFile, Display, TEXT("Found delete record for \"%s\"."), *It.Filename() );

						FPakInputPair DeleteRecord;
						DeleteRecord.bIsDeleteRecord = true;
						DeleteRecord.Source = DestFilename;
						DeleteRecord.Dest = PakFile.GetMountPoint() / It.Filename();
						OutDeletedEntries->Add(DeleteRecord);
						continue;
					}

					if (InFilter && (!It.Filename().MatchesWildcard(*InFilter)))
					{
						continue;
					}

					PakReader.Seek(Entry.Offset);
					uint32 SerializedCrcTest = 0;
					FPakEntry EntryInfo;
					EntryInfo.Serialize(PakReader, PakFile.GetInfo().Version);
					if (EntryInfo == Entry)
					{
						TUniquePtr<FArchive> FileHandle(IFileManager::Get().CreateFileWriter(*DestFilename));
						if (FileHandle)
						{
							if (Entry.CompressionMethodIndex == 0)
							{
								BufferedCopyFile(*FileHandle, PakReader, PakFile, Entry, Buffer, BufferSize, InKeyChain);
							}
							else
							{
								UncompressCopyFile(*FileHandle, PakReader, Entry, PersistantCompressionBuffer, CompressionBufferSize, InKeyChain, PakFile);
							}
							UE_LOG(LogPakFile, Display, TEXT("Extracted \"%s\" to \"%s\"."), *It.Filename(), *DestFilename);
							ExtractedCount++;

							if (OutOrderMap != nullptr)
							{
								OutOrderMap->Add(DestFilename, OutOrderMap->Num());
							}

							if (OutEntries != nullptr)
							{
								FPakInputPair Input;

								Input.Source = DestFilename;
								FPaths::NormalizeFilename(Input.Source);

								Input.Dest = PakFile.GetMountPoint() + FPaths::GetPath(It.Filename());
								FPaths::NormalizeFilename(Input.Dest);
								FPakFile::MakeDirectoryFromPath(Input.Dest);

								Input.bNeedsCompression = Entry.CompressionMethodIndex != 0;
								Input.bNeedEncryption = Entry.IsEncrypted();
	
								OutEntries->Add(Input);
							}
						}
						else
						{
							UE_LOG(LogPakFile, Error, TEXT("Unable to create file \"%s\"."), *DestFilename);
							ErrorCount++;
						}
					}
					else
					{
						UE_LOG(LogPakFile, Error, TEXT("Serialized hash mismatch for \"%s\"."), *It.Filename());
						ErrorCount++;
					}
				}
			}
			FMemory::Free(Buffer);
			FMemory::Free(PersistantCompressionBuffer);

			UE_LOG(LogPakFile, Log, TEXT("Finished extracting %d (including %d errors)."), ExtractedCount, ErrorCount);
		}
		else
		{
			UE_LOG(LogPakFile, Error, TEXT("Unable to open pak file \"%s\"."), *PakFilename);
			return false;
		}
	}

	return true;
}

void CreateDiffRelativePathMap(TArray<FString>& FileNames, const FString& RootPath, TMap<FName, FString>& OutMap)
{
	for (int32 i = 0; i < FileNames.Num(); ++i)
	{
		const FString& FullPath = FileNames[i];
		FString RelativePath = FullPath.Mid(RootPath.Len());
		OutMap.Add(FName(*RelativePath), FullPath);
	}
}

bool DiffFilesInPaks(const FString& InPakFilename1, const FString& InPakFilename2, const bool bLogUniques1, const bool bLogUniques2, const FKeyChain& InKeyChain)
{
	int32 NumUniquePAK1 = 0;
	int32 NumUniquePAK2 = 0;
	int32 NumDifferentContents = 0;
	int32 NumEqualContents = 0;

	TGuardValue<ELogTimes::Type> DisableLogTimes(GPrintLogTimes, ELogTimes::None);
	UE_LOG(LogPakFile, Log, TEXT("FileEventType, FileName, Size1, Size2"));

	FPakFile PakFile1(&FPlatformFileManager::Get().GetPlatformFile(), *InPakFilename1, false);
	FPakFile PakFile2(&FPlatformFileManager::Get().GetPlatformFile(), *InPakFilename2, false);
	if (PakFile1.IsValid() && PakFile2.IsValid())
	{		
		FArchive& PakReader1 = *PakFile1.GetSharedReader(NULL);
		FArchive& PakReader2 = *PakFile2.GetSharedReader(NULL);

		const int64 BufferSize = 8 * 1024 * 1024; // 8MB buffer for extracting
		void* Buffer = FMemory::Malloc(BufferSize);
		int64 CompressionBufferSize = 0;
		uint8* PersistantCompressionBuffer = NULL;
		int32 ErrorCount = 0;
		int32 FileCount = 0;		
		
		//loop over pak1 entries.  compare against entry in pak2.
		for (FPakFile::FFileIterator It(PakFile1); It; ++It, ++FileCount)
		{
			const FString& PAK1FileName = It.Filename();

			//double check entry info and move pakreader into place
			const FPakEntry& Entry1 = It.Info();
			PakReader1.Seek(Entry1.Offset);

			FPakEntry EntryInfo1;
			EntryInfo1.Serialize(PakReader1, PakFile1.GetInfo().Version);

			if (EntryInfo1 != Entry1)
			{
				UE_LOG(LogPakFile, Log, TEXT("PakEntry1Invalid, %s, 0, 0"), *PAK1FileName);
				continue;
			}
			
			//see if entry exists in other pak							
			FPakEntry Entry2;
			FPakFile::EFindResult FoundEntry2 = PakFile2.Find(PakFile1.GetMountPoint() / PAK1FileName, &Entry2);
			if (FoundEntry2 != FPakFile::EFindResult::Found)
			{
				++NumUniquePAK1;
				if (bLogUniques1)
				{
					UE_LOG(LogPakFile, Log, TEXT("UniqueToFirstPak, %s, %i, 0"), *PAK1FileName, EntryInfo1.UncompressedSize);
				}
				continue;
			}

			//double check entry info and move pakreader into place
			PakReader2.Seek(Entry2.Offset);
			FPakEntry EntryInfo2;
			EntryInfo2.Serialize(PakReader2, PakFile2.GetInfo().Version);
			if (EntryInfo2 != Entry2)
			{
				UE_LOG(LogPakFile, Log, TEXT("PakEntry2Invalid, %s, 0, 0"), *PAK1FileName);
				continue;;
			}

			//check sizes first as quick compare.
			if (EntryInfo1.UncompressedSize != EntryInfo2.UncompressedSize)
			{
				UE_LOG(LogPakFile, Log, TEXT("FilesizeDifferent, %s, %i, %i"), *PAK1FileName, EntryInfo1.UncompressedSize, EntryInfo2.UncompressedSize);
				continue;
			}
			
			//serialize and memcompare the two entries
			{
				FLargeMemoryWriter PAKWriter1(EntryInfo1.UncompressedSize);
				FLargeMemoryWriter PAKWriter2(EntryInfo2.UncompressedSize);

				if (EntryInfo1.CompressionMethodIndex == 0)
				{
					BufferedCopyFile(PAKWriter1, PakReader1, PakFile1, Entry1, Buffer, BufferSize, InKeyChain);
				}
				else
				{
					UncompressCopyFile(PAKWriter1, PakReader1, Entry1, PersistantCompressionBuffer, CompressionBufferSize, InKeyChain, PakFile1);
				}

				if (EntryInfo2.CompressionMethodIndex == 0)
				{
					BufferedCopyFile(PAKWriter2, PakReader2, PakFile2, Entry2, Buffer, BufferSize, InKeyChain);
				}
				else
				{
					UncompressCopyFile(PAKWriter2, PakReader2, Entry2, PersistantCompressionBuffer, CompressionBufferSize, InKeyChain, PakFile2);
				}

				if (FMemory::Memcmp(PAKWriter1.GetData(), PAKWriter2.GetData(), EntryInfo1.UncompressedSize) != 0)
				{
					++NumDifferentContents;
					UE_LOG(LogPakFile, Log, TEXT("ContentsDifferent, %s, %i, %i"), *PAK1FileName, EntryInfo1.UncompressedSize, EntryInfo2.UncompressedSize);
				}
				else
				{
					++NumEqualContents;
				}
			}			
		}
		
		//check for files unique to the second pak.
		for (FPakFile::FFileIterator It(PakFile2); It; ++It, ++FileCount)
		{
			const FPakEntry& Entry2 = It.Info();
			PakReader2.Seek(Entry2.Offset);

			FPakEntry EntryInfo2;
			EntryInfo2.Serialize(PakReader2, PakFile2.GetInfo().Version);

			if (EntryInfo2 == Entry2)
			{
				const FString& PAK2FileName = It.Filename();
				FPakEntry Entry1;
				FPakFile::EFindResult FoundEntry1 = PakFile1.Find(PakFile2.GetMountPoint() / PAK2FileName, &Entry1);
				if (FoundEntry1 != FPakFile::EFindResult::Found)
				{
					++NumUniquePAK2;
					if (bLogUniques2)
					{
						UE_LOG(LogPakFile, Log, TEXT("UniqueToSecondPak, %s, 0, %i"), *PAK2FileName, Entry2.UncompressedSize);
					}
					continue;
				}
			}
		}

		FMemory::Free(Buffer);
		Buffer = nullptr;
	}

	UE_LOG(LogPakFile, Log, TEXT("Comparison complete"));
	UE_LOG(LogPakFile, Log, TEXT("Unique to first pak: %i, Unique to second pak: %i, Num Different: %i, NumEqual: %i"), NumUniquePAK1, NumUniquePAK2, NumDifferentContents, NumEqualContents);	
	return true;
}

void GenerateHashForFile(uint8* ByteBuffer, uint64 TotalSize, FFileInfo& FileHash)
{
	FMD5 FileHasher;
	FileHasher.Update(ByteBuffer, TotalSize);
	FileHasher.Final(FileHash.Hash);
	FileHash.FileSize = TotalSize;
}

bool GenerateHashForFile( FString Filename, FFileInfo& FileHash)
{
	FArchive* File = IFileManager::Get().CreateFileReader(*Filename);

	if ( File == NULL )
		return false;

	uint64 TotalSize = File->TotalSize();

	uint8* ByteBuffer = new uint8[TotalSize];

	File->Serialize(ByteBuffer, TotalSize);

	delete File;
	File = NULL;

	GenerateHashForFile(ByteBuffer, TotalSize, FileHash);
	
	delete[] ByteBuffer;
	return true;
}

bool GenerateHashesFromPak(const TCHAR* InPakFilename, const TCHAR* InDestPakFilename, TMap<FString, FFileInfo>& FileHashes, bool bUseMountPoint, const FKeyChain& InKeyChain, int32& OutLowestSourcePakVersion)
{
	OutLowestSourcePakVersion = FPakInfo::PakFile_Version_Invalid;

	// Gather all patch pak files and run through them one at a time
	TArray<FString> PakFileList;
	IFileManager::Get().FindFiles(PakFileList, InPakFilename, true, false);

	FString PakFileDirectory = FPaths::GetPath(InPakFilename);

	for (int32 PakFileIndex = 0; PakFileIndex < PakFileList.Num(); PakFileIndex++)
	{
		FString PakFilename = PakFileDirectory + "\\" + PakFileList[PakFileIndex];
		// Skip the destination pak file so we can regenerate an existing patch level
		if (PakFilename.Equals(InDestPakFilename))
		{
			continue;
		}
		int32 PakPriority = GetPakPriorityFromFilename(PakFilename);
		int32 PakChunkIndex = GetPakChunkIndexFromFilename(PakFilename);

		FPakFile PakFile(&FPlatformFileManager::Get().GetPlatformFile(), *PakFilename, false);
		if (PakFile.IsValid())
		{
			FArchive& PakReader = *PakFile.GetSharedReader(NULL);
			const int64 BufferSize = 8 * 1024 * 1024; // 8MB buffer for extracting
			void* Buffer = FMemory::Malloc(BufferSize);
			int64 CompressionBufferSize = 0;
			uint8* PersistantCompressionBuffer = NULL;
			int32 ErrorCount = 0;
			int32 FileCount = 0;

			//remember the lowest pak version for any patch paks
			if( PakPriority != -1 )
			{
				OutLowestSourcePakVersion = FMath::Min( OutLowestSourcePakVersion, PakFile.GetInfo().Version );
			}

			FString PakMountPoint = bUseMountPoint ? PakFile.GetMountPoint().Replace(TEXT("../../../"), TEXT("")) : TEXT("");

			const bool bIncludeDeleted = true;
			for (FPakFile::FFileIterator It(PakFile,bIncludeDeleted); It; ++It, ++FileCount)
			{
				const FPakEntry& Entry = It.Info();
				FFileInfo FileHash = {};
				bool bEntryValid = false;

				FString FullFilename = PakMountPoint;
				if (!FullFilename.IsEmpty() && !FullFilename.EndsWith("/"))
				{
					FullFilename += "/";
				}
				FullFilename += It.Filename();

				if (Entry.IsDeleteRecord())
				{
					FileHash.PatchIndex = PakPriority;
					FileHash.bIsDeleteRecord = true;
					FileHash.bForceInclude = false;
					bEntryValid = true;
				}
				else
				{
				    PakReader.Seek(Entry.Offset);
				    uint32 SerializedCrcTest = 0;
				    FPakEntry EntryInfo;
				    EntryInfo.Serialize(PakReader, PakFile.GetInfo().Version);
				    if (EntryInfo == Entry)
				    {
					    // TUniquePtr<FArchive> FileHandle(IFileManager::Get().CreateFileWriter(*DestFilename));
					    TArray<uint8> Bytes;
					    FMemoryWriter MemoryFile(Bytes);
					    FArchive* FileHandle = &MemoryFile;
					    // if (FileHandle.IsValid())
					    {
							if (Entry.CompressionMethodIndex == 0)
						    {
							    BufferedCopyFile(*FileHandle, PakReader, PakFile, Entry, Buffer, BufferSize, InKeyChain);
						    }
						    else
						    {
							    UncompressCopyFile(*FileHandle, PakReader, Entry, PersistantCompressionBuffer, CompressionBufferSize, InKeyChain, PakFile);
						    }
    
						    UE_LOG(LogPakFile, Display, TEXT("Generated hash for \"%s\""), *FullFilename);
						    GenerateHashForFile(Bytes.GetData(), Bytes.Num(), FileHash);
						    FileHash.PatchIndex = PakPriority;
						    FileHash.bIsDeleteRecord = false;
							FileHash.bForceInclude = false;
							bEntryValid = true;
						}
						/*else
						{
						UE_LOG(LogPakFile, Error, TEXT("Unable to create file \"%s\"."), *DestFilename);
						ErrorCount++;
						}*/

					}
					else
					{
						UE_LOG(LogPakFile, Error, TEXT("Serialized hash mismatch for \"%s\"."), *It.Filename());
						ErrorCount++;
					}
				}

				if (bEntryValid)
				{
					// Keep only the hash of the most recent version of a file (across multiple pak patch files)
					if (!FileHashes.Contains(FullFilename))
					{
						FileHashes.Add(FullFilename, FileHash);
					}
					else if (FileHashes[FullFilename].PatchIndex < FileHash.PatchIndex)
					{
						FileHashes[FullFilename] = FileHash;
					}
				}
			}
			FMemory::Free(Buffer);
			FMemory::Free(PersistantCompressionBuffer);

			UE_LOG(LogPakFile, Log, TEXT("Finished extracting %d files (including %d errors)."), FileCount, ErrorCount);
		}
		else
		{
			UE_LOG(LogPakFile, Error, TEXT("Unable to open pak file \"%s\"."), *PakFilename);
			return false;
		}
	}

	return true;
}

bool FileIsIdentical(FString SourceFile, FString DestFilename, const FFileInfo* Hash, int64* DestSizeOut = nullptr)
{
	int64 SourceTotalSize = Hash ? Hash->FileSize : IFileManager::Get().FileSize(*SourceFile);
	int64 DestTotalSize = IFileManager::Get().FileSize(*DestFilename);

	if (DestSizeOut != nullptr)
	{
		*DestSizeOut = DestTotalSize;
	}

	if (SourceTotalSize != DestTotalSize)
	{
		// file size doesn't match 
		UE_LOG(LogPakFile, Display, TEXT("Source file size for %s %d bytes doesn't match %s %d bytes, did find %d"), *SourceFile, SourceTotalSize, *DestFilename, DestTotalSize, Hash ? 1 : 0);
		return false;
	}

	FFileInfo SourceFileHash;
	if (!Hash)
	{
		if (GenerateHashForFile(SourceFile, SourceFileHash) == false)
		{
			// file size doesn't match 
			UE_LOG(LogPakFile, Display, TEXT("Source file size %s doesn't exist will be included in build"), *SourceFile);
			return false;;
		}
		else
		{
			UE_LOG(LogPakFile, Warning, TEXT("Generated hash for file %s but it should have been in the FileHashes array"), *SourceFile);
		}
	}
	else
	{
		SourceFileHash = *Hash;
	}

	FFileInfo DestFileHash;
	if (GenerateHashForFile(DestFilename, DestFileHash) == false)
	{
		// destination file was removed don't really care about it
		UE_LOG(LogPakFile, Display, TEXT("File was removed from destination cooked content %s not included in patch"), *DestFilename);
		return false;
	}

	int32 Diff = FMemory::Memcmp(&SourceFileHash.Hash, &DestFileHash.Hash, sizeof(DestFileHash.Hash));
	if (Diff != 0)
	{
		UE_LOG(LogPakFile, Display, TEXT("Source file hash for %s doesn't match dest file hash %s and will be included in patch"), *SourceFile, *DestFilename);
		return false;
	}

	return true;
}

float GetFragmentationPercentage(const TArray<FPakInputPair>& FilesToPak, const TBitArray<>& IncludeBitMask, int32 MaxAdjacentOrderDiff, bool bConsiderSecondaryFiles)
{
	uint64 PrevOrder = MAX_uint64;
	bool bPrevBit = false;
	int32 DiffCount = 0;
	int32 ConsideredCount = 0;
	for (int32 i = 0; i < IncludeBitMask.Num(); i++)
	{
		if (!bConsiderSecondaryFiles && !FilesToPak[i].bIsInPrimaryOrder)
		{
			PrevOrder = MAX_uint64;
			continue;
		}
		uint64 CurrentOrder = FilesToPak[i].SuggestedOrder;
		bool bCurrentBit = IncludeBitMask[i];
		uint64 OrderDiff = CurrentOrder - PrevOrder;
		if (OrderDiff > MaxAdjacentOrderDiff || bCurrentBit != bPrevBit)
		{
			DiffCount++;
		}
		ConsideredCount++;
		PrevOrder = CurrentOrder;
		bPrevBit = bCurrentBit;
	}
	// First always shows as different, so discount it
	DiffCount--;
	return 100.0f * float(DiffCount) / float(ConsideredCount);
}

int64 ComputePatchSize(const TArray<FPakInputPair>& FilesToPak, const TArray<int64>& FileSizes, TBitArray<>& IncludeFilesMask, int32& OutFileCount)
{
	int64 TotalPatchSize = 0;
	OutFileCount = 0;
	for (int i = 0; i < FilesToPak.Num(); i++)
	{
		if (IncludeFilesMask[i])
		{
			OutFileCount++;
			TotalPatchSize += FileSizes[i];
		}
	}
	return TotalPatchSize;
}

int32 AddOrphanedFiles(const TArray<FPakInputPair>& FilesToPak, const TArray<int64>& FileSizes, const TArray<int32>& UAssetToUexpMapping, TBitArray<>& IncludeFilesMask, int64& OutSizeIncrease)
{
	int32 UExpCount = 0;
	int32 UAssetCount = 0;
	int32 FilesAddedCount = 0;
	OutSizeIncrease = 0;
	// Add corresponding UExp/UBulk files to the patch if either is included but not the other
	for (int i = 0; i < FilesToPak.Num(); i++)
	{
		int32 CounterpartFileIndex = UAssetToUexpMapping[i];
		if (CounterpartFileIndex != -1)
		{
			const FPakInputPair& File = FilesToPak[i];
			const FPakInputPair& CounterpartFile = FilesToPak[CounterpartFileIndex];
			if (IncludeFilesMask[i] != IncludeFilesMask[CounterpartFileIndex])
			{
				if (!IncludeFilesMask[i])
				{
					//UE_LOG(LogPakFile, Display, TEXT("Added %s because %s is already included"), *FilesToPak[i].Source, *FilesToPak[CounterpartFileIndex].Source);
					IncludeFilesMask[i] = true;
					OutSizeIncrease += FileSizes[i];
				}
				else
				{
					//UE_LOG(LogPakFile, Display, TEXT("Added %s because %s is already included"), *FilesToPak[CounterpartFileIndex].Source, *FilesToPak[i].Source);
					IncludeFilesMask[CounterpartFileIndex] = true;
					OutSizeIncrease += FileSizes[CounterpartFileIndex];
				}
				FilesAddedCount++;
			}
		}
	}
	return FilesAddedCount;
}


bool DoGapFillingIteration(const TArray<FPakInputPair>& FilesToPak, const TArray<int64>& FileSizes, const TArray<int32>& UAssetToUexpMapping, TBitArray<>& InOutIncludeFilesMask, int64 MaxGapSizeBytes, int32 MaxAdjacentOrderDiff, bool bForce, int64 MaxPatchSize = 0, bool bFillPrimaryOrderFiles = true, bool bFillSecondaryOrderFiles = true)
{
	TBitArray<> IncludeFilesMask = InOutIncludeFilesMask;

	float FragmentationPercentageOriginal = GetFragmentationPercentage(FilesToPak, IncludeFilesMask, MaxAdjacentOrderDiff, true);

	int64 SizeIncrease = 0;
	int64 CurrentOffset = 0;
	int64 CurrentPatchOffset = 0;
	int64 CurrentGapSize = 0;
	bool bPrevKeepFile = false;
	uint64 PrevOrder = MAX_uint64;
	int32 OriginalKeepCount = 0;
	int32 LastKeepIndex = -1;
	bool bCurrentGapIsUnbroken = true;
	int32 PatchFilesAddedCount = 0;
	for (int i = 0; i < FilesToPak.Num(); i++)
	{
		bool bKeepFile = IncludeFilesMask[i];
		const FPakInputPair& File = FilesToPak[i];
		uint64 Order = File.SuggestedOrder;

		// Skip unordered files or files outside the range we care about
		if (Order == MAX_uint64 || (File.bIsInPrimaryOrder && !bFillPrimaryOrderFiles) || (!File.bIsInPrimaryOrder && !bFillSecondaryOrderFiles))
		{
			continue;
		}
		CurrentOffset += FileSizes[i];
		if (bKeepFile)
		{
			OriginalKeepCount++;
			CurrentPatchOffset = CurrentOffset;
		}
		else
		{
			if (OriginalKeepCount > 0)
			{
				CurrentGapSize = CurrentOffset - CurrentPatchOffset;
			}
		}

		// Detect gaps in the file order. No point in removing those gaps because it won't affect seeks
		uint64 OrderDiff = Order - PrevOrder;
		if (bCurrentGapIsUnbroken && OrderDiff > uint64(MaxAdjacentOrderDiff))
		{
			bCurrentGapIsUnbroken = false;
		}

		// If we're keeping this file but not the last one, check if the gap size is small enough to bring over unchanged assets
		if (bKeepFile && !bPrevKeepFile && CurrentGapSize > 0)
		{
			if (CurrentGapSize <= MaxGapSizeBytes)
			{
				if (bCurrentGapIsUnbroken)
				{
					// Mark the files in the gap to keep, even though they're unchanged
					for (int j = LastKeepIndex + 1; j < i; j++)
					{
						IncludeFilesMask[j] = true;
						SizeIncrease += FileSizes[j];
						PatchFilesAddedCount++;
					}
				}
			}
			bCurrentGapIsUnbroken = true;
		}
		bPrevKeepFile = bKeepFile;
		if (bKeepFile)
		{
			LastKeepIndex = i;
		}
		PrevOrder = Order;
	}

#if GUARANTEE_UASSET_AND_UEXP_IN_SAME_PAK
	int64 OrphanedFilesSizeIncrease = 0;
	int32 OrphanedFileCount = AddOrphanedFiles(FilesToPak, FileSizes, UAssetToUexpMapping, IncludeFilesMask, OrphanedFilesSizeIncrease);
#endif

	int32 PatchFileCount = 0;
	int64 PatchSize = ComputePatchSize(FilesToPak, FileSizes, IncludeFilesMask, PatchFileCount);

	FString PrefixString = TEXT("");
	if (bFillPrimaryOrderFiles && !bFillSecondaryOrderFiles)
	{
		PrefixString = TEXT("[PRIMARY]");
	}
	else if (bFillSecondaryOrderFiles && !bFillPrimaryOrderFiles)
	{
		PrefixString = TEXT("[SECONDARY]");
	}
	if (PatchSize > MaxPatchSize && !bForce)
	{
		UE_LOG(LogPakFile, SEEK_OPT_VERBOSITY, TEXT("Patch seek optimization step %s - Gap size: %dKB - FAILED (patch too big)"), *PrefixString, MaxGapSizeBytes / 1024);
		return false;
	}

	// Stop if we didn't actually make patch size better
	float FragmentationPercentageAfterGapFill = GetFragmentationPercentage(FilesToPak, IncludeFilesMask, MaxAdjacentOrderDiff, true);
	if (FragmentationPercentageAfterGapFill >= FragmentationPercentageOriginal && !bForce)
	{
		UE_LOG(LogPakFile, SEEK_OPT_VERBOSITY, TEXT("Patch seek optimization step %s - Gap size: %dKB - FAILED (contiguous block count didn't improve)"), *PrefixString, MaxGapSizeBytes / 1024);
		return false;
	}
	InOutIncludeFilesMask = IncludeFilesMask;
	UE_LOG(LogPakFile, SEEK_OPT_VERBOSITY, TEXT("Patch seek optimization step %s - Gap size: %dKB - SUCCEEDED"), *PrefixString, MaxGapSizeBytes / 1024);
	return true;
}

bool DoIncrementalGapFilling(const TArray<FPakInputPair>& FilesToPak, const TArray<int64>& FileSizes, const TArray<int32>& UAssetToUexpMapping, TBitArray<>& IncludeFilesMask, int64 MinGapSize, int64 MaxGapSize, int64 MaxAdjacentOrderDiff, bool bFillPrimaryOrderFiles, bool bFillSecondaryOrderFiles, int64 MaxPatchSize)
{
	int64 GapSize = MinGapSize;
	bool bSuccess = false;
	while (GapSize <= MaxGapSize)
	{
		if (DoGapFillingIteration(FilesToPak, FileSizes, UAssetToUexpMapping, IncludeFilesMask, GapSize, MaxAdjacentOrderDiff, false, MaxPatchSize, bFillPrimaryOrderFiles, bFillSecondaryOrderFiles))
		{
			bSuccess = true;
		}
		else
		{
			// Try with 75% of the max gap size
			bSuccess |= DoGapFillingIteration(FilesToPak, FileSizes, UAssetToUexpMapping, IncludeFilesMask, GapSize*0.75, MaxAdjacentOrderDiff, false, MaxPatchSize, bFillPrimaryOrderFiles, bFillSecondaryOrderFiles);
			break;
		}
		GapSize *= 2;
	}
	return bSuccess;
}

void ApplyGapFilling(const TArray<FPakInputPair>& FilesToPak, const TArray<int64>& FileSizes, const TArray<int32>& UAssetToUexpMapping, TBitArray<>& IncludeFilesMask, const FPatchSeekOptParams& SeekOptParams)
{
	UE_CLOG(SeekOptParams.MaxGapSize == 0, LogPakFile, Fatal, TEXT("ApplyGapFilling requires MaxGapSize > 0"));
	check(SeekOptParams.MaxGapSize > 0);
	float FragmentationPercentageOriginal = GetFragmentationPercentage(FilesToPak, IncludeFilesMask, SeekOptParams.MaxAdjacentOrderDiff, true);
	float FragmentationPercentageOriginalPrimary = GetFragmentationPercentage(FilesToPak, IncludeFilesMask, SeekOptParams.MaxAdjacentOrderDiff, false);
	int32 OriginalPatchFileCount = 0;
	int64 OriginalPatchSize = ComputePatchSize(FilesToPak, FileSizes, IncludeFilesMask, OriginalPatchFileCount);
	int64 IncrementalMaxPatchSize = int64(double(OriginalPatchSize) + double(OriginalPatchSize) * double(SeekOptParams.MaxInflationPercent) * 0.01);
	int64 MinIncrementalGapSize = 4 * 1024;

	ESeekOptMode SeekOptMode = SeekOptParams.Mode;
	switch (SeekOptMode)
	{
	case ESeekOptMode::OnePass:
	{
		DoGapFillingIteration(FilesToPak, FileSizes, UAssetToUexpMapping, IncludeFilesMask, SeekOptParams.MaxGapSize, SeekOptParams.MaxAdjacentOrderDiff, true);
	}
	break;
	case ESeekOptMode::Incremental:
	case ESeekOptMode::Incremental_OnlyPrimaryOrder:
	{
		UE_CLOG(SeekOptParams.MaxInflationPercent == 0.0f, LogPakFile, Fatal, TEXT("ESeekOptMode::Incremental* requires MaxInflationPercent > 0.0"));
		bool bFillSecondaryOrderFiles = (SeekOptParams.Mode == ESeekOptMode::Incremental);
		DoIncrementalGapFilling(FilesToPak, FileSizes, UAssetToUexpMapping, IncludeFilesMask, MinIncrementalGapSize, SeekOptParams.MaxGapSize, SeekOptParams.MaxAdjacentOrderDiff, true, bFillSecondaryOrderFiles, IncrementalMaxPatchSize);
	}
	break;
	case ESeekOptMode::Incremental_PrimaryThenSecondary:
	{
		UE_CLOG(SeekOptParams.MaxInflationPercent == 0.0f, LogPakFile, Fatal, TEXT("ESeekOptMode::Incremental* requires MaxInflationPercent > 0.0"));
		int64 PassMaxPatchSize[3];
		PassMaxPatchSize[0] = OriginalPatchSize + (IncrementalMaxPatchSize - OriginalPatchSize) * 0.9f;
		PassMaxPatchSize[1] = IncrementalMaxPatchSize;
		PassMaxPatchSize[2] = IncrementalMaxPatchSize;
		for (int32 i = 0; i < 3; i++)
		{
			bool bFillPrimaryOrderFiles = (i == 0) || (i == 2);
			bool bFillSecondaryOrderFiles = (i == 1);
			DoIncrementalGapFilling(FilesToPak, FileSizes, UAssetToUexpMapping, IncludeFilesMask, MinIncrementalGapSize, SeekOptParams.MaxGapSize, SeekOptParams.MaxAdjacentOrderDiff, bFillPrimaryOrderFiles, bFillSecondaryOrderFiles, PassMaxPatchSize[i]);
		}
	}
	break;
	}

	int32 NewPatchFileCount = 0;
	int64 NewPatchSize = ComputePatchSize(FilesToPak, FileSizes, IncludeFilesMask, NewPatchFileCount);

	double OriginalSizeMB = double(OriginalPatchSize) / 1024.0 / 1024.0;
	double SizeIncreaseMB = double(NewPatchSize - OriginalPatchSize) / 1024.0 / 1024.0;
	double TotalSizeMB = OriginalSizeMB + SizeIncreaseMB;
	double SizeIncreasePercent = 100.0 * SizeIncreaseMB / OriginalSizeMB;
	if (NewPatchFileCount == OriginalPatchSize)
	{
		UE_LOG(LogPakFile, SEEK_OPT_VERBOSITY, TEXT("Patch seek optimization did not modify patch pak size (no additional files added)"), OriginalSizeMB, TotalSizeMB, SizeIncreasePercent);
	}
	else
	{
		UE_LOG(LogPakFile, SEEK_OPT_VERBOSITY, TEXT("Patch seek optimization increased estimated patch pak size from %.2fMB to %.2fMB (+%.1f%%)"), OriginalSizeMB, TotalSizeMB, SizeIncreasePercent);
		UE_LOG(LogPakFile, SEEK_OPT_VERBOSITY, TEXT("Total files added : %d (from %d)"), NewPatchFileCount - OriginalPatchFileCount, OriginalPatchFileCount);
	}


	float FragmentationPercentageNew = GetFragmentationPercentage(FilesToPak, IncludeFilesMask, SeekOptParams.MaxAdjacentOrderDiff, true);
	float FragmentationPercentageNewPrimary = GetFragmentationPercentage(FilesToPak, IncludeFilesMask, SeekOptParams.MaxAdjacentOrderDiff, false);
	UE_LOG(LogPakFile, SEEK_OPT_VERBOSITY, TEXT("Fragmentation pre-optimization: %.2f%%"), FragmentationPercentageOriginal);
	UE_LOG(LogPakFile, SEEK_OPT_VERBOSITY, TEXT("Fragmentation final: %.2f%%"), FragmentationPercentageNew);
	UE_LOG(LogPakFile, SEEK_OPT_VERBOSITY, TEXT("Fragmentation pre-optimization (primary files): %.2f%%"), FragmentationPercentageOriginalPrimary);
	UE_LOG(LogPakFile, SEEK_OPT_VERBOSITY, TEXT("Fragmentation final (primary files): %.2f%%"), FragmentationPercentageNewPrimary);
}

void RemoveIdenticalFiles( TArray<FPakInputPair>& FilesToPak, const FString& SourceDirectory, const TMap<FString, FFileInfo>& FileHashes, const FPatchSeekOptParams& SeekOptParams, const FString ChangedFilesOutputFilename)
{
	FArchive* ChangedFilesArchive = nullptr;
	if (ChangedFilesOutputFilename.IsEmpty() == false)
	{
		ChangedFilesArchive = IFileManager::Get().CreateFileWriter(*ChangedFilesOutputFilename);
	}

	FString HashFilename = SourceDirectory / TEXT("Hashes.txt");

	if (IFileManager::Get().FileExists(*HashFilename) )
	{
		FString EntireFile;
		FFileHelper::LoadFileToString(EntireFile, *HashFilename);
	}

	TBitArray<> IncludeFilesMask;
	IncludeFilesMask.Add(true, FilesToPak.Num());

	TMap<FString, int32> SourceFileToIndex;
	for (int i = 0; i < FilesToPak.Num(); i++)
	{
		SourceFileToIndex.Add(FilesToPak[i].Source, i);
	}

	// Generate the index mapping from UExp to corresponding UAsset (and vice versa)
	TArray<int32> UAssetToUexpMapping;
	UAssetToUexpMapping.Empty(FilesToPak.Num());
	for (int i = 0; i<FilesToPak.Num(); i++)
	{
		UAssetToUexpMapping.Add(-1);
	}
	for (int i = 0; i<FilesToPak.Num(); i++)
	{
		const auto& NewFile = FilesToPak[i];
		FString Ext(FPaths::GetExtension(FilesToPak[i].Source));
		if (Ext.Equals("uasset", ESearchCase::IgnoreCase) || Ext.Equals("umap", ESearchCase::IgnoreCase))
		{
			FString UexpDestFilename = FPaths::ChangeExtension(NewFile.Source, "uexp");
			int32 *UexpIndexPtr = SourceFileToIndex.Find(UexpDestFilename);
			if (UexpIndexPtr)
			{
				UAssetToUexpMapping[*UexpIndexPtr] = i;
				UAssetToUexpMapping[i] = *UexpIndexPtr;
			}
		}
	}
	TArray<int64> FileSizes;
	FileSizes.AddDefaulted(FilesToPak.Num());



    // Mark files to remove if they're unchanged
	for (int i= 0; i<FilesToPak.Num(); i++)
	{
		const auto& NewFile = FilesToPak[i];
		if( NewFile.bIsDeleteRecord )
		{
			continue;
		}
		FString SourceFileNoMountPoint =  NewFile.Dest.Replace(TEXT("../../../"), TEXT(""));
		FString SourceFilename = SourceDirectory / SourceFileNoMountPoint;
		
		const FFileInfo* FoundFileHash = FileHashes.Find(SourceFileNoMountPoint);
		if (!FoundFileHash)
		{
			FoundFileHash = FileHashes.Find(NewFile.Dest);
		}
		
 		if ( !FoundFileHash )
 		{
 			UE_LOG(LogPakFile, Display, TEXT("Didn't find hash for %s No mount %s"), *SourceFilename, *SourceFileNoMountPoint);
 		}
 
#if GUARANTEE_UASSET_AND_UEXP_IN_SAME_PAK
		// uexp files are always handled with their corresponding uasset file
		bool bForceInclude = false;
		if (!FPaths::GetExtension(SourceFilename).Equals("uexp", ESearchCase::IgnoreCase))
		{
			bForceInclude = FoundFileHash && FoundFileHash->bForceInclude;
		}
#else
		bool bForceInclude = FoundFileHash && FoundFileHash->bForceInclude;
#endif

		FString DestFilename = NewFile.Source;
		bool bFileIsIdentical = FileIsIdentical(SourceFilename, DestFilename, FoundFileHash, &FileSizes[i]);

		if (ChangedFilesArchive && !bFileIsIdentical)
		{
			ChangedFilesArchive->Logf(TEXT("%s\n"),*DestFilename);
		}

		if (!bForceInclude && bFileIsIdentical)
		{
			UE_LOG(LogPakFile, Display, TEXT("Source file %s matches dest file %s and will not be included in patch"), *SourceFilename, *DestFilename);
			// remove from the files to pak list
			IncludeFilesMask[i] = false;
		}
	}

	// Add corresponding UExp/UBulk files to the patch if one is included but not the other (uassets and uexp files must be in the same pak)
#if GUARANTEE_UASSET_AND_UEXP_IN_SAME_PAK
	for (int i = 0; i < FilesToPak.Num(); i++)
	{
		int32 CounterpartFileIndex= UAssetToUexpMapping[i];
		if (CounterpartFileIndex != -1)
		{
			if (IncludeFilesMask[i] != IncludeFilesMask[CounterpartFileIndex])
			{
				UE_LOG(LogPakFile, Display, TEXT("One of %s and %s is different from source, so both will be included in patch"), *FilesToPak[i].Source, *FilesToPak[CounterpartFileIndex].Source);
				IncludeFilesMask[i] = true;
				IncludeFilesMask[CounterpartFileIndex] = true;
			}
		}
	}
#endif

	if (SeekOptParams.Mode != ESeekOptMode::None)
	{
		ApplyGapFilling(FilesToPak, FileSizes, UAssetToUexpMapping, IncludeFilesMask, SeekOptParams);
	}

	// Compress the array while preserving the order, removing the files we marked to remove
	int32 WriteIndex = 0;
	for ( int ReadIndex=0; ReadIndex<IncludeFilesMask.Num(); ReadIndex++)
	{
		if (IncludeFilesMask[ReadIndex])
	{
			FilesToPak[WriteIndex++] = FilesToPak[ReadIndex];
	}
}
	int NumToRemove = FilesToPak.Num() - WriteIndex;
	FilesToPak.RemoveAt(WriteIndex, NumToRemove, true);

	if (ChangedFilesArchive)
	{
		ChangedFilesArchive->Close();
		delete ChangedFilesArchive;
		ChangedFilesArchive = nullptr;
	}
}

void ProcessLegacyFileMoves( TArray<FPakInputPair>& InDeleteRecords, TMap<FString, FFileInfo>& InExistingPackagedFileHashes, const FString& InInputPath, const TArray<FPakInputPair>& InFilesToPak, int32 CurrentPatchChunkIndex )
{
	double StartTime = FPlatformTime::Seconds();


	TArray<FString> PakFileList;
	IFileManager::Get().FindFiles(PakFileList, *InInputPath, TEXT(".pak") );
	if( PakFileList.Num() == 0 )
	{
		UE_LOG( LogPakFile, Error, TEXT("No pak files searching \"%s\""), *InInputPath );
		return;
	}

	struct FFileChunkRevisionInfo
	{
		FString PakFilename;
		int32 PakPriority;
		int32 PakChunkIndex;
		int32 PakVersion;
	};
	TMap<FString, FFileChunkRevisionInfo> DeletedFileRevisions;
	TMap<FString, FFileChunkRevisionInfo> RequiredFileRevisions;

	TSet<FString> DeleteRecordSourceNames;
	for (const FPakInputPair& DeleteRecord : InDeleteRecords)
	{
		DeleteRecordSourceNames.Add(DeleteRecord.Source);
	}

	TSet<FString> FilesToPakDestNames;
	for (const FPakInputPair& FileToPak : InFilesToPak)
	{
		FilesToPakDestNames.Add(FileToPak.Dest);
	}

	for (int32 PakFileIndex = 0; PakFileIndex < PakFileList.Num(); PakFileIndex++)
	{
		FString PakFilename = InInputPath + "\\" + PakFileList[PakFileIndex];
		int32 PakPriority = GetPakPriorityFromFilename(PakFilename);
		int32 PakChunkIndex = GetPakChunkIndexFromFilename(PakFilename);

		UE_LOG(LogPakFile, Display, TEXT("Checking old pak file \"%s\" Pri:%d Chunk:%d."), *PakFilename, PakPriority, PakChunkIndex );


		FPakFile PakFile(&FPlatformFileManager::Get().GetPlatformFile(), *PakFilename, false);
		if (PakFile.IsValid())
		{
			FString PakMountPoint = PakFile.GetMountPoint().Replace(TEXT("../../../"), TEXT(""));

			const bool bIncludeDeleted = true;
			for (FPakFile::FFileIterator It(PakFile,bIncludeDeleted); It; ++It)
			{
				FString AssetName = PakMountPoint;
				if (!AssetName.IsEmpty() && !AssetName.EndsWith("/"))
				{
					AssetName += "/";
				}
				AssetName += It.Filename();

				bool bHasNewDeleteRecord = DeleteRecordSourceNames.Contains(AssetName);

				FFileChunkRevisionInfo Revision;
				Revision.PakFilename = PakFileList[PakFileIndex];
				Revision.PakPriority = PakPriority;
				Revision.PakChunkIndex = PakChunkIndex;
				Revision.PakVersion = PakFile.GetInfo().Version;

				TMap<FString, FFileChunkRevisionInfo>* DestList = nullptr;

				if( bHasNewDeleteRecord )
				{
					DestList = &DeletedFileRevisions;
				}
				else if( InExistingPackagedFileHashes.Contains(AssetName) )
				{
					FString DestAssetName = TEXT("../../../") + AssetName;
					bool bRequiredFile = FilesToPakDestNames.Contains(DestAssetName);

					if(bRequiredFile)
					{
						DestList = &RequiredFileRevisions;
					}
				}

				if( DestList != nullptr )
				{
					if( !DestList->Contains(AssetName) )
					{
						DestList->Add(AssetName,Revision);
					}
					else if( (*DestList)[AssetName].PakPriority < PakPriority )
					{
						(*DestList)[AssetName] = Revision;
					}
				}

			}
		}
	}

	//prevent delete records being created for files that have historically been moved
	for (auto Itr : DeletedFileRevisions)
	{
		UE_LOG(LogPakFile, Display, TEXT("checking deleted revision %s chunk %d vs %d   pak version %d vs %d"), *Itr.Key, Itr.Value.PakChunkIndex, CurrentPatchChunkIndex, Itr.Value.PakVersion, FPakInfo::PakFile_Version_DeleteRecords );

		//asset hasn't been deleted in the latest version and the latest known version is in a different chunk to us from a previous version of unrealpak
		if( Itr.Value.PakChunkIndex != CurrentPatchChunkIndex )
		{
			int NumDeleted = InDeleteRecords.RemoveAll( [&]( const FPakInputPair& InPair )
			{
				return InPair.Source == Itr.Key;
			});
			if( NumDeleted > 0 )
			{
				UE_LOG( LogPakFile, Display, TEXT("Ignoring delete record for %s - it was moved to %s before delete records were created"), *Itr.Key, *FPaths::GetCleanFilename(Itr.Value.PakFilename) );
			}
		}
	}

	//make sure files who's latest revision was in a different chunk to the one we're building are added to the pak
	//#TODO: I think this RequiredFileRevision code is not needed
	for (auto Itr : RequiredFileRevisions)
	{
		if (Itr.Value.PakVersion < FPakInfo::PakFile_Version_DeleteRecords && Itr.Value.PakChunkIndex != CurrentPatchChunkIndex )
		{
			if( InExistingPackagedFileHashes.Contains(Itr.Key) )
			{
				UE_LOG( LogPakFile, Display, TEXT("Ensuring %s is included in the pak file - it was moved to %s before delete records were created"), *Itr.Key, *FPaths::GetCleanFilename(Itr.Value.PakFilename) );
				InExistingPackagedFileHashes[Itr.Key].bForceInclude = true;
			}
		}
	}

	UE_LOG(LogPakFile, Display, TEXT("...took %.2fs to manage legacy patch pak files"), FPlatformTime::Seconds() - StartTime );
}


TArray<FPakInputPair> GetNewDeleteRecords( const TArray<FPakInputPair>& InFilesToPak, const TMap<FString, FFileInfo>& InExistingPackagedFileHashes)
{
	double StartTime = FPlatformTime::Seconds();
	TArray<FPakInputPair> DeleteRecords;

	//build lookup table of files to pack
	TSet<FString> FilesToPack;
	for (const FPakInputPair& PakEntry : InFilesToPak)
	{
		FString PakFilename = PakEntry.Dest.Replace(TEXT("../../../"), TEXT(""));
		FilesToPack.Add(PakFilename);
	}

	//check all assets in the previous patch packs
	for (const TTuple<FString, FFileInfo>& Pair : InExistingPackagedFileHashes)
	{
		//ignore this file if the most recent revision is deleted already
		if (Pair.Value.bIsDeleteRecord)
		{
			continue;
		}

		//see if the file exists in the files to package
		FString SourceFileName = Pair.Key;
		bool bFound = FilesToPack.Contains(SourceFileName);

		if (bFound == false)
		{
			//file cannot be found now, and was not deleted in the most recent pak patch
			FPakInputPair DeleteRecord;
			DeleteRecord.bIsDeleteRecord = true;
			DeleteRecord.Source = SourceFileName;
			DeleteRecord.Dest = TEXT("../../../") + SourceFileName;
			DeleteRecords.Add(DeleteRecord);
			UE_LOG(LogPakFile, Display, TEXT("Existing pak entry %s not found in new pak asset list, so a delete record will be created in the patch pak."), *SourceFileName);
		}
 	}


	UE_LOG(LogPakFile, Display, TEXT("Took %.2fS for delete records"), FPlatformTime::Seconds()-StartTime );
	return DeleteRecords;
}

FString GetPakPath(const TCHAR* SpecifiedPath, bool bIsForCreation)
{
	FString PakFilename(SpecifiedPath);
	FPaths::MakeStandardFilename(PakFilename);
	
	// if we are trying to open (not create) it, but BaseDir relative doesn't exist, look in LaunchDir
	if (!bIsForCreation && !FPaths::FileExists(PakFilename))
	{
		PakFilename = FPaths::LaunchDir() + SpecifiedPath;

		if (!FPaths::FileExists(PakFilename))
		{
			UE_LOG(LogPakFile, Fatal, TEXT("Existing pak file %s could not be found (checked against binary and launch directories)"), SpecifiedPath);
			return TEXT("");
		}
	}
	
	return PakFilename;
}

bool Repack(const FString& InputPakFile, const FString& OutputPakFile, const FPakCommandLineParameters& CmdLineParameters, const FKeyChain& InKeyChain, bool bIncludeDeleted)
{
	bool bResult = false;

	// Extract the existing pak file
	TMap<FString, FFileInfo> Hashes;
	TArray<FPakInputPair> Entries;
	TArray<FPakInputPair> DeletedEntries;
	FPakOrderMap OrderMap;
	TArray<FGuid> EncryptionKeys;
	bool bAnySigned = false;
	FString TempDir = FPaths::EngineIntermediateDir() / TEXT("UnrealPak") / TEXT("Repack") / FPaths::GetBaseFilename(InputPakFile);
	if (ExtractFilesFromPak(*InputPakFile, Hashes, *TempDir, false, InKeyChain, nullptr, &Entries, &DeletedEntries, &OrderMap, &EncryptionKeys, &bAnySigned))
	{
		TArray<FPakInputPair> FilesToAdd;
		CollectFilesToAdd(FilesToAdd, Entries, OrderMap, CmdLineParameters);

		if (bIncludeDeleted)
		{
			for( const FPakInputPair& Entry : DeletedEntries )
			{
				FilesToAdd.Add(Entry);
			}
		}
		else if (DeletedEntries.Num() > 0)
		{
			UE_LOG(LogPakFile, Display, TEXT("%s has %d delete records - these will not be included in the repackage. Specify -IncludeDeleted to include them"), *InputPakFile, DeletedEntries.Num() );
		}

		// Get a temporary output filename. We'll only create/replace the final output file once successful.
		FString TempOutputPakFile = FPaths::CreateTempFilename(*FPaths::GetPath(OutputPakFile), *FPaths::GetCleanFilename(OutputPakFile));

		FPakCommandLineParameters ModifiedCmdLineParameters = CmdLineParameters;
		ModifiedCmdLineParameters.bSign = bAnySigned && (InKeyChain.SigningKey != InvalidRSAKeyHandle);
		
		FKeyChain ModifiedKeyChain = InKeyChain;
		ModifiedKeyChain.MasterEncryptionKey = InKeyChain.EncryptionKeys.Find(EncryptionKeys.Num() ? EncryptionKeys[0] : FGuid());

		// Create the new pak file
		UE_LOG(LogPakFile, Display, TEXT("Creating %s..."), *OutputPakFile);
		if (CreatePakFile(*TempOutputPakFile, FilesToAdd, ModifiedCmdLineParameters, ModifiedKeyChain))
		{
			IFileManager::Get().Move(*OutputPakFile, *TempOutputPakFile);

			FString OutputSigFile = FPaths::ChangeExtension(OutputPakFile, TEXT(".sig"));
			if (IFileManager::Get().FileExists(*OutputSigFile))
			{
				IFileManager::Get().Delete(*OutputSigFile);
			}

			FString TempOutputSigFile = FPaths::ChangeExtension(TempOutputPakFile, TEXT(".sig"));
			if (IFileManager::Get().FileExists(*TempOutputSigFile))
			{
				IFileManager::Get().Move(*OutputSigFile, *TempOutputSigFile);
			}

			bResult = true;
		}
	}
	IFileManager::Get().DeleteDirectory(*TempDir, false, true);

	return bResult;
}



int32 NumberOfWorkerThreadsDesired()
{
	const int32 MaxThreads = 64;
	const int32 NumberOfCores = FPlatformMisc::NumberOfCores();
	// need to spawn at least one worker thread (see FTaskGraphImplementation)
	return FMath::Max(FMath::Min(NumberOfCores - 1, MaxThreads), 1);
}

void CheckAndReallocThreadPool()
{
	if (FPlatformProcess::SupportsMultithreading())
	{
		const int32 ThreadsSpawned = GThreadPool->GetNumThreads();
		const int32 DesiredThreadCount = NumberOfWorkerThreadsDesired();
		if (ThreadsSpawned < DesiredThreadCount)
		{
			UE_LOG(LogPakFile, Log, TEXT("Engine only spawned %d worker threads, bumping up to %d!"), ThreadsSpawned, DesiredThreadCount);
			GThreadPool->Destroy();
			GThreadPool = FQueuedThreadPool::Allocate();
			verify(GThreadPool->Create(DesiredThreadCount, 128 * 1024));
		}
		else
		{
			UE_LOG(LogPakFile, Log, TEXT("Continuing with %d spawned worker threads."), ThreadsSpawned);
		}
	}
}



/**
 * Application entry point
 * Params:
 *   -Test test if the pak file is healthy
 *   -Extract extracts pak file contents (followed by a path, i.e.: -extract D:\ExtractedPak)
 *   -Create=filename response file to create a pak file with
 *
 * @param	ArgC	Command-line argument count
 * @param	ArgV	Argument strings
 */
bool ExecuteUnrealPak(const TCHAR* CmdLine)
{
	// Parse all the non-option arguments from the command line
	TArray<FString> NonOptionArguments;
	for (const TCHAR* CmdLineEnd = CmdLine; *CmdLineEnd != 0;)
	{
		FString Argument = FParse::Token(CmdLineEnd, false);
		if (Argument.Len() > 0 && !Argument.StartsWith(TEXT("-")))
		{
			NonOptionArguments.Add(Argument);
		}
	}

	FString BatchFileName;
	if (FParse::Value(CmdLine, TEXT("-Batch="), BatchFileName))
	{
		TArray<FString> Commands;
		if (!FFileHelper::LoadFileToStringArray(Commands, *BatchFileName))
		{
			UE_LOG(LogPakFile, Error, TEXT("Unable to read '%s'"), *BatchFileName);
			return false;
		}

		UE_LOG(LogPakFile, Display, TEXT("Running UnrealPak in batch mode with commands:"));
		for (int i = 0; i < Commands.Num(); i++)
		{
			UE_LOG(LogPakFile, Display, TEXT("[%d] : %s"), i, *Commands[i]);
		}

		TAtomic<bool> Result(true);
		ParallelFor(Commands.Num(), [&Commands, &Result](int32 Idx) { if (!ExecuteUnrealPak(*Commands[Idx])) { Result = false; } });
		return Result;
	}


	FKeyChain KeyChain;
	LoadKeyChain(CmdLine, KeyChain);
	ApplyEncryptionKeys(KeyChain);

	if (FParse::Param(CmdLine, TEXT("Test")))
	{
		if (NonOptionArguments.Num() != 1)
		{
			UE_LOG(LogPakFile, Error, TEXT("Incorrect arguments. Expected: -Test <PakFile>"));
			return false;
		}

		FString PakFilename = GetPakPath(*NonOptionArguments[0], false);
		return TestPakFile(*PakFilename);
	}

	if (FParse::Param(CmdLine, TEXT("TestMemoryOptimization")))
	{
		TArray<FPakInputPair> Entries;
		FPakCommandLineParameters CmdLineParameters;
		ProcessCommandLine(CmdLine, NonOptionArguments, Entries, CmdLineParameters);

		if (NonOptionArguments.Num() != 1)
		{
			UE_LOG(LogPakFile, Error, TEXT("Incorrect arguments. Expected: -TestMemoryOptimization <SourceFolder>"));
			return false;
		}

		FString SourceDir = NonOptionArguments[0];
		TArray<FString> PakFilenames;
		IFileManager::Get().FindFiles(PakFilenames, *SourceDir, TEXT("*.pak"));
		TArray<FPakFile*> PakFiles;
		PakFiles.Empty(PakFilenames.Num());

		for (const FString& PakFilename : PakFilenames)
		{
			PakFiles.Add(new FPakFile(&FPlatformFileManager::Get().GetPlatformFile(), *(FPaths::Combine(SourceDir, PakFilename)), false));
		}

		TMap<uint64, FPakEntry> CollisionChecker;
		
		for (FPakFile* PakFile : PakFiles)
		{
			if (!PakFile->UnloadPakEntryFilenames(CollisionChecker, nullptr, false))
			{
				UE_LOG(LogPakFile, Error, TEXT("Pak '%s' failed to unload filenames"), *PakFile->GetFilename());
			}

			if (!PakFile->ShrinkPakEntriesMemoryUsage())
			{
				UE_LOG(LogPakFile, Error, TEXT("Pak '%s' failed to shrink entries"), *PakFile->GetFilename());
			}
		}

		for (FPakFile* PakFile : PakFiles)
		{
			delete PakFile;
		}

		PakFiles.Empty();

		return true;
	}

	if (FParse::Param(CmdLine, TEXT("List")))
	{
		TArray<FPakInputPair> Entries;
		FPakCommandLineParameters CmdLineParameters;
		ProcessCommandLine(CmdLine, NonOptionArguments, Entries, CmdLineParameters);

		if (NonOptionArguments.Num() != 1)
		{
			UE_LOG(LogPakFile, Error, TEXT("Incorrect arguments. Expected: -List <PakFile> [-SizeFilter=N]"));
			return false;
		}

		int64 SizeFilter = 0;
		FParse::Value(CmdLine, TEXT("SizeFilter="), SizeFilter);

		bool bExcludeDeleted = FParse::Param( CmdLine, TEXT("ExcludeDeleted") );

		FString PakFilename = GetPakPath(*NonOptionArguments[0], false);

		FString CSVFilename;
		FParse::Value(CmdLine, TEXT("csv="), CSVFilename);

		bool bExtractToMountPoint = FParse::Param(CmdLine, TEXT("ExtractToMountPoint"));

		return ListFilesInPak(*PakFilename, SizeFilter, !bExcludeDeleted, *CSVFilename, bExtractToMountPoint, KeyChain);
	}

	if (FParse::Param(CmdLine, TEXT("Diff")))
	{
		if(NonOptionArguments.Num() != 2)
		{
			UE_LOG(LogPakFile,Error,TEXT("Incorrect arguments. Expected: -Diff <PakFile1> <PakFile2> [-NoUniques] [-NoUniquesFile1] [-NoUniquesFile2]"));
			return false;
		}

		FString PakFilename1 = GetPakPath(*NonOptionArguments[0], false);
		FString PakFilename2 = GetPakPath(*NonOptionArguments[1], false);

		// Allow the suppression of unique file logging for one or both files
		const bool bLogUniques = !FParse::Param(CmdLine, TEXT("nouniques"));
		const bool bLogUniques1 = bLogUniques && !FParse::Param(CmdLine, TEXT("nouniquesfile1"));
		const bool bLogUniques2 = bLogUniques && !FParse::Param(CmdLine, TEXT("nouniquesfile2"));

		return DiffFilesInPaks(PakFilename1, PakFilename2, bLogUniques1, bLogUniques2, KeyChain);
	}

	if (FParse::Param(CmdLine, TEXT("Extract")))
	{
		TArray<FPakInputPair> Entries;
		FPakCommandLineParameters CmdLineParameters;
		ProcessCommandLine(CmdLine, NonOptionArguments, Entries, CmdLineParameters);
	
		if (NonOptionArguments.Num() != 2)
		{
			UE_LOG(LogPakFile, Error, TEXT("Incorrect arguments. Expected: -Extract <PakFile> <OutputPath> [-responsefile=<filename>]"));
			return false;
		}

		FString PakFilename = GetPakPath(*NonOptionArguments[0], false);

		FString ResponseFileName;
		bool bGenerateResponseFile = FParse::Value(CmdLine, TEXT("responseFile="), ResponseFileName);

		bool bUseFilter = false;
		FString Filter;
		FString DestPath = NonOptionArguments[1];

		bUseFilter = FParse::Value(CmdLine, TEXT("Filter="), Filter);
		bool bExtractToMountPoint = FParse::Param(CmdLine, TEXT("ExtractToMountPoint"));
		TMap<FString, FFileInfo> EmptyMap;
		TArray<FPakInputPair> ResponseContent;
		TArray<FPakInputPair> DeletedContent;
		if (ExtractFilesFromPak(*PakFilename, EmptyMap, *DestPath, bExtractToMountPoint, KeyChain, bUseFilter ? &Filter : nullptr, &ResponseContent, &DeletedContent) == false)
		{
			return false;
		}

		if (bGenerateResponseFile)
		{
			FArchive* ResponseArchive = IFileManager::Get().CreateFileWriter(*ResponseFileName);
			ResponseArchive->SetIsTextFormat(true);
			// generate a response file
			if (FParse::Param(CmdLine, TEXT("includedeleted")))
			{
				for (int32 I = 0; I < DeletedContent.Num(); ++I)
				{
					ResponseArchive->Logf(TEXT("%s -delete"), *DeletedContent[I].Dest);
				}
			}

			for (int32 I = 0; I < ResponseContent.Num(); ++I)
			{
				const FPakInputPair& Response = ResponseContent[I];
				FString Line = FString::Printf(TEXT("%s %s"), *Response.Source, *Response.Dest);
				if (Response.bNeedEncryption)
				{
					Line += " -encrypt";
				}
				if (Response.bNeedsCompression)
				{
					Line += " -compress";
				}
				ResponseArchive->Logf(TEXT("%s"), *Line);
			}
			ResponseArchive->Close();
			delete ResponseArchive;
		}

		return true;
	}

	if (FParse::Param(CmdLine, TEXT("AuditFiles")))
	{
		if (NonOptionArguments.Num() != 1)
		{
			UE_LOG(LogPakFile, Error, TEXT("Incorrect arguments. Expected: -AuditFiles <PakFolder> -CSV=<OutputPath> [-OnlyDeleted] [-Order=<OrderingFile>] [-SortByOrdering]"));
			return false;
		}
		
		FString PakFilenames = *NonOptionArguments[0];
		FPaths::MakeStandardFilename(PakFilenames);
		
		FString CSVFilename;
		FParse::Value( CmdLine, TEXT("CSV="), CSVFilename );

		bool bOnlyDeleted = FParse::Param( CmdLine, TEXT("OnlyDeleted") );
		bool bSortByOrdering = FParse::Param(CmdLine, TEXT("SortByOrdering"));

		FPakOrderMap OrderMap;
		FString ResponseFile;
		if (FParse::Value(CmdLine, TEXT("-order="), ResponseFile) && !OrderMap.ProcessOrderFile(*ResponseFile))
		{
			return false;
		}
		FString SecondaryResponseFile;
		if (FParse::Value(CmdLine, TEXT("-secondaryOrder="), SecondaryResponseFile) && !OrderMap.ProcessOrderFile(*SecondaryResponseFile, true))
		{
			return false;
		}

		return AuditPakFiles(*PakFilenames, bOnlyDeleted, CSVFilename, OrderMap, bSortByOrdering );
	}
	
	if (FParse::Param(CmdLine, TEXT("WhatsAtOffset")))
	{
		if (NonOptionArguments.Num() < 2)
		{
			UE_LOG(LogPakFile, Error, TEXT("Incorrect arguments. Expected: -WhatsAtOffset <PakFile> [Offset...]"));
			return false;
		}
		
		FString PakFilename = GetPakPath(*NonOptionArguments[0], false);

		TArray<int64> Offsets;
		for( int ArgI = 1; ArgI < NonOptionArguments.Num(); ArgI++ )
		{
			if( FCString::IsNumeric(*NonOptionArguments[ArgI]) )
			{
				Offsets.Add( FCString::Strtoi64( *NonOptionArguments[ArgI], nullptr, 10 ) );
			}
		}

		return ListFilesAtOffset( *PakFilename, Offsets );
	}

	if (FParse::Param(CmdLine, TEXT("CalcCompressionBlockCRCs")))
	{
		if (NonOptionArguments.Num() < 2)
		{
			UE_LOG(LogPakFile, Error, TEXT("Incorrect arguments. Expected -CalcCompressionBlockCRCs <PakFile> [Offset...] ") );
			return false;
		}

		FString PakFilename = GetPakPath(*NonOptionArguments[0], false);

		TArray<int64> Offsets;
		for( int ArgI = 1; ArgI < NonOptionArguments.Num(); ArgI++ )
		{
			if( FCString::IsNumeric(*NonOptionArguments[ArgI]) )
			{
				Offsets.Add( FCString::Strtoi64( *NonOptionArguments[ArgI], nullptr, 10 ) );
			}
		}

		return ShowCompressionBlockCRCs( *PakFilename, Offsets, KeyChain );
	}
	
	if (FParse::Param(CmdLine, TEXT("GeneratePIXMappingFile")))
	{
		if (NonOptionArguments.Num() != 1)
		{
			UE_LOG(LogPakFile, Error, TEXT("Incorrect arguments. Expected: -GeneratePIXMappingFile <PakFile> [-OutputPath=<OutputPath>]"));
			return false;
		}

		TArray<FString> PakFileList;
		const FString& PakFolderName = NonOptionArguments[0];
		if (FPaths::DirectoryExists(PakFolderName))
		{
			TArray<FString> PakFilesInFolder;
			IFileManager::Get().FindFiles(PakFilesInFolder, *PakFolderName, TEXT(".pak"));
			for (const FString& PakFile : PakFilesInFolder)
			{
				FString FullPakFileName = PakFolderName / PakFile;
				FullPakFileName.ReplaceInline(TEXT("/"), TEXT("\\"));
				PakFileList.AddUnique(GetPakPath(*FullPakFileName, false));
			}
		}

		FString OutputPath;
		FParse::Value(CmdLine, TEXT("OutputPath="), OutputPath);
		return GeneratePIXMappingFile(PakFileList, OutputPath);
	}

	if (FParse::Param(CmdLine, TEXT("Repack")))
	{
		if (NonOptionArguments.Num() != 1)
		{
			UE_LOG(LogPakFile, Error, TEXT("Incorrect arguments. Expected: -Repack <PakFile> [-Output=<PakFile>]"));
			return false;
		}

		TArray<FPakInputPair> Entries;
		FPakCommandLineParameters CmdLineParameters;
		ProcessCommandLine(CmdLine, NonOptionArguments, Entries, CmdLineParameters);

		// Find all the input pak files
		FString InputDir = FPaths::GetPath(*NonOptionArguments[0]);

		TArray<FString> InputPakFiles;
		if (FPaths::FileExists(*NonOptionArguments[0]))
		{
			InputPakFiles.Add(*NonOptionArguments[0]);
		}
		else
		{
			IFileManager::Get().FindFiles(InputPakFiles, *InputDir, *FPaths::GetCleanFilename(*NonOptionArguments[0]));

			for (int Idx = 0; Idx < InputPakFiles.Num(); Idx++)
			{
				InputPakFiles[Idx] = InputDir / InputPakFiles[Idx];
			}
		}

		if (InputPakFiles.Num() == 0)
		{
			UE_LOG(LogPakFile, Error, TEXT("No files found matching '%s'"), *NonOptionArguments[0]);
			return false;
		}

		// Find all the output paths
		TArray<FString> OutputPakFiles;

		FString OutputPath;
		if (!FParse::Value(CmdLine, TEXT("Output="), OutputPath, false))
		{
			for (const FString& InputPakFile : InputPakFiles)
			{
				OutputPakFiles.Add(InputPakFile);
			}
		}
		else if (IFileManager::Get().DirectoryExists(*OutputPath))
		{
			for (const FString& InputPakFile : InputPakFiles)
			{
				OutputPakFiles.Add(FPaths::Combine(OutputPath, FPaths::GetCleanFilename(InputPakFile)));
			}
		}
		else
		{
			for (const FString& InputPakFile : InputPakFiles)
			{
				OutputPakFiles.Add(OutputPath);
			}
		}

		bool bExcludeDeleted = FParse::Param(CmdLine, TEXT("ExcludeDeleted"));

		// Repack them all
		for (int Idx = 0; Idx < InputPakFiles.Num(); Idx++)
		{
			UE_LOG(LogPakFile, Display, TEXT("Repacking %s into %s"), *InputPakFiles[Idx], *OutputPakFiles[Idx]);
			if (!Repack(InputPakFiles[Idx], OutputPakFiles[Idx], CmdLineParameters, KeyChain, !bExcludeDeleted))
			{
				return false;
			}
		}

		return true;
	}

	if(NonOptionArguments.Num() > 0)
	{

		CheckAndReallocThreadPool();



		// since this is for creation, we pass true to make it not look in LaunchDir
		FString PakFilename = GetPakPath(*NonOptionArguments[0], true);

		// List of all items to add to pak file
		TArray<FPakInputPair> Entries;
		FPakCommandLineParameters CmdLineParameters;
		ProcessCommandLine(CmdLine, NonOptionArguments, Entries, CmdLineParameters);

		FPakOrderMap OrderMap;
		FString ResponseFile;
		if (FParse::Value(CmdLine, TEXT("-order="), ResponseFile) && !OrderMap.ProcessOrderFile(*ResponseFile))
		{
			return false;
		}

		FString SecondaryResponseFile;
		if (FParse::Value(CmdLine, TEXT("-secondaryOrder="), SecondaryResponseFile) && !OrderMap.ProcessOrderFile(*SecondaryResponseFile, true))
		{
			return false;
		}

		if (Entries.Num() == 0)
		{
			UE_LOG(LogPakFile, Error, TEXT("No files specified to add to pak file."));
			return false;
		}

		int32 LowestSourcePakVersion = 0;
		TMap<FString, FFileInfo> SourceFileHashes;

		if ( CmdLineParameters.GeneratePatch )
		{
			FString OutputPath;
			if (!FParse::Value(CmdLine, TEXT("TempFiles="), OutputPath))
			{
				OutputPath = FPaths::GetPath(PakFilename) / FString(TEXT("TempFiles"));
			}

			IFileManager::Get().DeleteDirectory(*OutputPath);

			// Check command line for the "patchcryptokeys" param, which will tell us where to look for the encryption keys that
			// we need to access the patch reference data
			FString PatchReferenceCryptoKeysFilename;
			FKeyChain PatchKeyChain;

			if (FParse::Value(FCommandLine::Get(), TEXT("PatchCryptoKeys="), PatchReferenceCryptoKeysFilename))
			{
				LoadKeyChainFromFile(PatchReferenceCryptoKeysFilename, PatchKeyChain);
				ApplyEncryptionKeys(PatchKeyChain);
			}

			UE_LOG(LogPakFile, Display, TEXT("Generating patch from %s."), *CmdLineParameters.SourcePatchPakFilename, true );

			if (!GenerateHashesFromPak(*CmdLineParameters.SourcePatchPakFilename, *PakFilename, SourceFileHashes, true, PatchKeyChain, /*Out*/LowestSourcePakVersion))
			{
				if (ExtractFilesFromPak(*CmdLineParameters.SourcePatchPakFilename, SourceFileHashes, *OutputPath, true, PatchKeyChain, nullptr) == false)
				{
					UE_LOG(LogPakFile, Warning, TEXT("Unable to extract files from source pak file for patch"));
				}
				else
				{
					CmdLineParameters.SourcePatchDiffDirectory = OutputPath;
				}
			}

			ApplyEncryptionKeys(KeyChain);
		}


		// Start collecting files
		TArray<FPakInputPair> FilesToAdd;
		CollectFilesToAdd(FilesToAdd, Entries, OrderMap, CmdLineParameters);

		if ( CmdLineParameters.GeneratePatch )
		{
			// We need to get a list of files that were in the previous patch('s) Pak, but NOT in FilesToAdd
			TArray<FPakInputPair> DeleteRecords = GetNewDeleteRecords(FilesToAdd, SourceFileHashes);

			//if the patch is built using old source pak files, we need to handle the special case where a file has been moved between chunks but no delete record was created (this would cause a rogue delete record to be created in the latest pak), and also a case where the file was moved between chunks and back again without being changed (this would cause the file to not be included in this chunk because the file would be considered unchanged)
			if (LowestSourcePakVersion < FPakInfo::PakFile_Version_DeleteRecords)
			{
				int32 CurrentPatchChunkIndex = GetPakChunkIndexFromFilename(PakFilename);

				UE_LOG(LogPakFile, Display, TEXT("Some patch source paks were generated with an earlier version of UnrealPak that didn't support delete records. checking for historic assets that have moved between chunks to avoid creating invalid delete records"));
				FString SourcePakFolder = FPaths::GetPath(CmdLineParameters.SourcePatchPakFilename);

				//remove invalid items from DeleteRecords and set 'bForceInclude' on some SourceFileHashes
				ProcessLegacyFileMoves(DeleteRecords, SourceFileHashes, SourcePakFolder, FilesToAdd, CurrentPatchChunkIndex);
			}
			FilesToAdd.Append(DeleteRecords);

			// if we are generating a patch here we remove files which are already shipped...
			RemoveIdenticalFiles(FilesToAdd, CmdLineParameters.SourcePatchDiffDirectory, SourceFileHashes, CmdLineParameters.SeekOptParams, CmdLineParameters.ChangedFilesOutputFilename);
		}


		bool bResult = CreatePakFile(*PakFilename, FilesToAdd, CmdLineParameters, KeyChain);

		if (CmdLineParameters.GeneratePatch)
		{
			FString OutputPath = FPaths::GetPath(PakFilename) / FString(TEXT("TempFiles"));
			// delete the temporary directory
			IFileManager::Get().DeleteDirectory(*OutputPath, false, true);
		}

		GetDerivedDataCacheRef().WaitForQuiescence(true);

		

		return bResult;
	}

	UE_LOG(LogPakFile, Error, TEXT("No pak file name specified. Usage:"));
	UE_LOG(LogPakFile, Error, TEXT("  UnrealPak <PakFilename> -Test"));
	UE_LOG(LogPakFile, Error, TEXT("  UnrealPak <PakFilename> -List [-ExcludeDeleted]"));
	UE_LOG(LogPakFile, Error, TEXT("  UnrealPak <PakFilename> <GameUProjectName> <GameFolderName> -ExportDependencies=<OutputFileBase> -NoAssetRegistryCache -ForceDependsGathering"));
	UE_LOG(LogPakFile, Error, TEXT("  UnrealPak <PakFilename> -Extract <ExtractDir> [-Filter=<filename>]"));
	UE_LOG(LogPakFile, Error, TEXT("  UnrealPak <PakFilename> -Create=<ResponseFile> [Options]"));
	UE_LOG(LogPakFile, Error, TEXT("  UnrealPak <PakFilename> -Dest=<MountPoint>"));
	UE_LOG(LogPakFile, Error, TEXT("  UnrealPak <PakFilename> -Repack [-Output=Path] [-ExcludeDeleted] [Options]"));
	UE_LOG(LogPakFile, Error, TEXT("  UnrealPak <PakFilename1> <PakFilename2> -diff"));
	UE_LOG(LogPakFile, Error, TEXT("  UnrealPak <PakFolder> -AuditFiles [-OnlyDeleted] [-CSV=<filename>] [-order=<OrderingFile>] [-SortByOrdering]"));
	UE_LOG(LogPakFile, Error, TEXT("  UnrealPak <PakFilename> -WhatsAtOffset [offset1] [offset2] [offset3] [...]"));
	UE_LOG(LogPakFile, Error, TEXT("  UnrealPak <PakFolder> -GeneratePIXMappingFile -OutputPath=<Path>"));
	UE_LOG(LogPakFile, Error, TEXT("  Options:"));
	UE_LOG(LogPakFile, Error, TEXT("    -blocksize=<BlockSize>"));
	UE_LOG(LogPakFile, Error, TEXT("    -bitwindow=<BitWindow>"));
	UE_LOG(LogPakFile, Error, TEXT("    -compress"));
	UE_LOG(LogPakFile, Error, TEXT("    -encrypt"));
	UE_LOG(LogPakFile, Error, TEXT("    -order=<OrderingFile>"));
	UE_LOG(LogPakFile, Error, TEXT("    -diff (requires 2 filenames first)"));
	UE_LOG(LogPakFile, Error, TEXT("    -enginedir (specify engine dir for when using ini encryption configs)"));
	UE_LOG(LogPakFile, Error, TEXT("    -projectdir (specify project dir for when using ini encryption configs)"));
	UE_LOG(LogPakFile, Error, TEXT("    -encryptionini (specify ini base name to gather encryption settings from)"));
	UE_LOG(LogPakFile, Error, TEXT("    -extracttomountpoint (Extract to mount point path of pak file)"));
	UE_LOG(LogPakFile, Error, TEXT("    -encryptindex (encrypt the pak file index, making it unusable in unrealpak without supplying the key)"));
	UE_LOG(LogPakFile, Error, TEXT("    -compressionformat[s]=<Format[,format2,...]> (set the format(s) to compress with, falling back on failures)"));
	UE_LOG(LogPakFile, Error, TEXT("    -encryptionkeyoverrideguid (override the encryption key guid used for encrypting data in this pak file)"));
	UE_LOG(LogPakFile, Error, TEXT("    -sign (generate a signature (.sig) file alongside the pak)"));
	UE_LOG(LogPakFile, Error, TEXT("    -fallbackOrderForNonUassetFiles (if order is not specified for ubulk/uexp files, figure out implicit order based on the uasset order. Generally applies only to the cooker order)"));

	return false;
}
