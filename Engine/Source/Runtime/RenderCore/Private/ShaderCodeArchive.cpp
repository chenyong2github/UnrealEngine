// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShaderCodeArchive.h"
#include "ShaderCodeLibrary.h"
#include "Stats/Stats.h"
#include "ProfilingDebugging/LoadTimeTracker.h"
#include "Misc/ScopeRWLock.h"
#include "Misc/MemStack.h"
#include "Policies/PrettyJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"

int32 GShaderCodeLibraryAsyncLoadingPriority = int32(AIOP_Normal);
static FAutoConsoleVariableRef CVarShaderCodeLibraryAsyncLoadingPriority(
	TEXT("r.ShaderCodeLibrary.DefaultAsyncIOPriority"),
	GShaderCodeLibraryAsyncLoadingPriority,
	TEXT(""),
	ECVF_Default
);

int32 GShaderCodeLibraryAsyncLoadingAllowDontCache = 0;
static FAutoConsoleVariableRef CVarShaderCodeLibraryAsyncLoadingAllowDontCache(
	TEXT("r.ShaderCodeLibrary.AsyncIOAllowDontCache"),
	GShaderCodeLibraryAsyncLoadingAllowDontCache,
	TEXT(""),
	ECVF_Default
);


static const FName ShaderLibraryCompressionFormat = NAME_LZ4;

int32 FSerializedShaderArchive::FindShaderMapWithKey(const FSHAHash& Hash, uint32 Key) const
{
	for (uint32 Index = ShaderMapHashTable.First(Key); ShaderMapHashTable.IsValid(Index); Index = ShaderMapHashTable.Next(Index))
	{
		if (ShaderMapHashes[Index] == Hash)
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

int32 FSerializedShaderArchive::FindShaderMap(const FSHAHash& Hash) const
{
	const uint32 Key = GetTypeHash(Hash);
	return FindShaderMapWithKey(Hash, Key);
}

bool FSerializedShaderArchive::FindOrAddShaderMap(const FSHAHash& Hash, int32& OutIndex, const FShaderMapAssetPaths* AssociatedAssets)
{
	const uint32 Key = GetTypeHash(Hash);
	int32 Index = FindShaderMapWithKey(Hash, Key);
	bool bAdded = false;
	if (Index == INDEX_NONE)
	{
		Index = ShaderMapHashes.Add(Hash);
		ShaderMapEntries.AddDefaulted();
		check(ShaderMapEntries.Num() == ShaderMapHashes.Num());
		ShaderMapHashTable.Add(Key, Index);
#if WITH_EDITOR
		if (AssociatedAssets && AssociatedAssets->Num() > 0)
		{
			ShaderCodeToAssets.Add(Hash, *AssociatedAssets);
		}
#endif
		bAdded = true;
	}
	else
	{
#if WITH_EDITOR
		// check if we need to replace or merge assets
		if (AssociatedAssets && AssociatedAssets->Num())
		{
			FShaderMapAssetPaths* PrevAssets = ShaderCodeToAssets.Find(Hash);
			if (PrevAssets)
			{
				int PrevAssetsNum = PrevAssets->Num();
				PrevAssets->Append(*AssociatedAssets);
			}
			else
			{
				ShaderCodeToAssets.Add(Hash, *AssociatedAssets);
			}
		}
#endif
	}

	OutIndex = Index;
	return bAdded;
}

int32 FSerializedShaderArchive::FindShaderWithKey(const FSHAHash& Hash, uint32 Key) const
{
	for (uint32 Index = ShaderHashTable.First(Key); ShaderHashTable.IsValid(Index); Index = ShaderHashTable.Next(Index))
	{
		if (ShaderHashes[Index] == Hash)
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

int32 FSerializedShaderArchive::FindShader(const FSHAHash& Hash) const
{
	const uint32 Key = GetTypeHash(Hash);
	return FindShaderWithKey(Hash, Key);
}

bool FSerializedShaderArchive::FindOrAddShader(const FSHAHash& Hash, int32& OutIndex)
{
	const uint32 Key = GetTypeHash(Hash);
	int32 Index = FindShaderWithKey(Hash, Key);
	bool bAdded = false;
	if (Index == INDEX_NONE)
	{
		Index = ShaderHashes.Add(Hash);
		ShaderEntries.AddDefaulted();
		check(ShaderEntries.Num() == ShaderHashes.Num());
		ShaderHashTable.Add(Key, Index);
		bAdded = true;
	}

	OutIndex = Index;
	return bAdded;
}

void FSerializedShaderArchive::DecompressShader(int32 Index, const TArray<TArray<uint8>>& ShaderCode, TArray<uint8>& OutDecompressedShader) const
{
	const FShaderCodeEntry& Entry = ShaderEntries[Index];
	OutDecompressedShader.SetNum(Entry.UncompressedSize, false);
	if (Entry.Size == Entry.UncompressedSize)
	{
		FMemory::Memcpy(OutDecompressedShader.GetData(), ShaderCode[Index].GetData(), Entry.UncompressedSize);
	}
	else
	{
		bool bSucceed = FCompression::UncompressMemory(ShaderLibraryCompressionFormat, OutDecompressedShader.GetData(), Entry.UncompressedSize, ShaderCode[Index].GetData(), Entry.Size);
		check(bSucceed);
	}
}

void FSerializedShaderArchive::Finalize()
{
	// Set the correct offsets
	{
		uint64 Offset = 0u;
		for (FShaderCodeEntry& Entry : ShaderEntries)
		{
			Entry.Offset = Offset;
			Offset += Entry.Size;
		}
	}

	PreloadEntries.Empty();
	for (FShaderMapEntry& ShaderMapEntry : ShaderMapEntries)
	{
		check(ShaderMapEntry.NumShaders > 0u);
		TArray<FFileCachePreloadEntry> SortedPreloadEntries;
		SortedPreloadEntries.Empty(ShaderMapEntry.NumShaders + 1);
		for (uint32 i = 0; i < ShaderMapEntry.NumShaders; ++i)
		{
			const int32 ShaderIndex = ShaderIndices[ShaderMapEntry.ShaderIndicesOffset + i];
			const FShaderCodeEntry& ShaderEntry = ShaderEntries[ShaderIndex];
			SortedPreloadEntries.Add(FFileCachePreloadEntry(ShaderEntry.Offset, ShaderEntry.Size));
		}
		SortedPreloadEntries.Sort([](const FFileCachePreloadEntry& Lhs, const FFileCachePreloadEntry& Rhs) { return Lhs.Offset < Rhs.Offset; });
		SortedPreloadEntries.Add(FFileCachePreloadEntry(INT64_MAX, 0));

		ShaderMapEntry.FirstPreloadIndex = PreloadEntries.Num();
		FFileCachePreloadEntry CurrentPreloadEntry = SortedPreloadEntries[0];
		for (uint32 PreloadIndex = 1; PreloadIndex <= ShaderMapEntry.NumShaders; ++PreloadIndex)
		{
			const FFileCachePreloadEntry& PreloadEntry = SortedPreloadEntries[PreloadIndex];
			const int64 Gap = PreloadEntry.Offset - CurrentPreloadEntry.Offset - CurrentPreloadEntry.Size;
			checkf(Gap >= 0, TEXT("Overlapping preload entries, [%lld-%lld), [%lld-%lld)"),
				CurrentPreloadEntry.Offset, CurrentPreloadEntry.Offset + CurrentPreloadEntry.Size, PreloadEntry.Offset, PreloadEntry.Offset + PreloadEntry.Size);
			if (Gap > 1024)
			{
				++ShaderMapEntry.NumPreloadEntries;
				PreloadEntries.Add(CurrentPreloadEntry);
				CurrentPreloadEntry = PreloadEntry;
			}
			else
			{
				CurrentPreloadEntry.Size = PreloadEntry.Offset + PreloadEntry.Size - CurrentPreloadEntry.Offset;
			}
		}
		check(ShaderMapEntry.NumPreloadEntries > 0u);
		check(CurrentPreloadEntry.Size == 0);
	}
}

void FSerializedShaderArchive::Serialize(FArchive& Ar)
{
	Ar << ShaderMapHashes;
	Ar << ShaderHashes;
	Ar << ShaderMapEntries;
	Ar << ShaderEntries;
	Ar << PreloadEntries;
	Ar << ShaderIndices;

	check(ShaderHashes.Num() == ShaderEntries.Num());
	check(ShaderMapHashes.Num() == ShaderMapEntries.Num());

	if (Ar.IsLoading())
	{
		{
			const uint32 HashSize = FMath::Min<uint32>(0x10000, 1u << FMath::CeilLogTwo(ShaderMapHashes.Num()));
			ShaderMapHashTable.Initialize(HashSize, ShaderMapHashes.Num());
			for (int32 Index = 0; Index < ShaderMapHashes.Num(); ++Index)
			{
				const uint32 Key = GetTypeHash(ShaderMapHashes[Index]);
				ShaderMapHashTable.Add(Key, Index);
			}
		}
		{
			const uint32 HashSize = FMath::Min<uint32>(0x10000, 1u << FMath::CeilLogTwo(ShaderHashes.Num()));
			ShaderHashTable.Initialize(HashSize, ShaderHashes.Num());
			for (int32 Index = 0; Index < ShaderHashes.Num(); ++Index)
			{
				const uint32 Key = GetTypeHash(ShaderHashes[Index]);
				ShaderHashTable.Add(Key, Index);
			}
		}
	}
}

#if WITH_EDITOR
void FSerializedShaderArchive::SaveAssetInfo(FArchive& Ar)
{
	if (Ar.IsSaving())
	{
		FString JsonTcharText;
		{
			TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&JsonTcharText);
			Writer->WriteObjectStart();

			Writer->WriteValue(TEXT("AssetInfoVersion"), static_cast<int32>(EAssetInfoVersion::CurrentVersion));

			Writer->WriteArrayStart(TEXT("ShaderCodeToAssets"));
			for (TMap<FSHAHash, FShaderMapAssetPaths>::TConstIterator Iter(ShaderCodeToAssets); Iter; ++Iter)
			{
				Writer->WriteObjectStart();
				const FSHAHash& Hash = Iter.Key();
				Writer->WriteValue(TEXT("ShaderMapHash"), Hash.ToString());
				const FShaderMapAssetPaths& Assets = Iter.Value();
				Writer->WriteArrayStart(TEXT("Assets"));
				for (FShaderMapAssetPaths::TConstIterator AssetIter(Assets); AssetIter; ++AssetIter)
				{
					Writer->WriteValue((*AssetIter));
				}
				Writer->WriteArrayEnd();
				Writer->WriteObjectEnd();
			}
			Writer->WriteArrayEnd();

			Writer->WriteObjectEnd();
			Writer->Close();
		}

		FTCHARToUTF8 JsonUtf8(*JsonTcharText);
		Ar.Serialize(const_cast<void *>(reinterpret_cast<const void*>(JsonUtf8.Get())), JsonUtf8.Length() * sizeof(UTF8CHAR));
	}
}

bool FSerializedShaderArchive::LoadAssetInfo(const FString& Filename)
{
	TArray<uint8> FileData;
	if (!FFileHelper::LoadFileToArray(FileData, *Filename))
	{
		return false;
	}

	FString JsonText;
	FFileHelper::BufferToString(JsonText, FileData.GetData(), FileData.Num());

	TSharedPtr<FJsonObject> JsonObject;
	TSharedRef<TJsonReader<TCHAR>> Reader = TJsonReaderFactory<TCHAR>::Create(JsonText);

	// Attempt to deserialize JSON
	if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
	{
		return false;
	}

	TSharedPtr<FJsonValue> AssetInfoVersion = JsonObject->Values.FindRef(TEXT("AssetInfoVersion"));
	if (!AssetInfoVersion.IsValid())
	{
		UE_LOG(LogShaderLibrary, Warning, TEXT("Rejecting asset info file %s: missing AssetInfoVersion (damaged file?)"), 
			*Filename);
		return false;
	}
	
	const EAssetInfoVersion FileVersion = static_cast<EAssetInfoVersion>(static_cast<int64>(AssetInfoVersion->AsNumber()));
	if (FileVersion != EAssetInfoVersion::CurrentVersion)
	{
		UE_LOG(LogShaderLibrary, Warning, TEXT("Rejecting asset info file %s: expected version %d, got unsupported version %d."),
			*Filename, static_cast<int32>(EAssetInfoVersion::CurrentVersion), static_cast<int32>(FileVersion));
		return false;
	}

	TSharedPtr<FJsonValue> AssetInfoArrayValue = JsonObject->Values.FindRef(TEXT("ShaderCodeToAssets"));
	if (!AssetInfoArrayValue.IsValid())
	{
		UE_LOG(LogShaderLibrary, Warning, TEXT("Rejecting asset info file %s: missing ShaderCodeToAssets array (damaged file?)"),
			*Filename);
		return false;
	}
	
	TArray<TSharedPtr<FJsonValue>> AssetInfoArray = AssetInfoArrayValue->AsArray();
	UE_LOG(LogShaderLibrary, Display, TEXT("Reading asset info file %s: found %d existing mappings"),
		*Filename, AssetInfoArray.Num());

	for (int32 IdxPair = 0, NumPairs = AssetInfoArray.Num(); IdxPair < NumPairs; ++IdxPair)
	{
		TSharedPtr<FJsonObject> Pair = AssetInfoArray[IdxPair]->AsObject();
		if (UNLIKELY(!Pair.IsValid()))
		{
			UE_LOG(LogShaderLibrary, Warning, TEXT("Rejecting asset info file %s: ShaderCodeToAssets array contains unreadable mapping #%d (damaged file?)"),
				*Filename,
				IdxPair
				);
			return false;
		}

		TSharedPtr<FJsonValue> ShaderMapHashJson = Pair->Values.FindRef(TEXT("ShaderMapHash"));
		if (UNLIKELY(!ShaderMapHashJson.IsValid()))
		{
			UE_LOG(LogShaderLibrary, Warning, TEXT("Rejecting asset info file %s: ShaderCodeToAssets array contains unreadable ShaderMapHash for mapping %d (damaged file?)"),
				*Filename,
				IdxPair
				);
			return false;
		}

		FSHAHash ShaderMapHash;
		ShaderMapHash.FromString(ShaderMapHashJson->AsString());

		TSharedPtr<FJsonValue> AssetPathsArrayValue = Pair->Values.FindRef(TEXT("Assets"));
		if (UNLIKELY(!AssetPathsArrayValue.IsValid()))
		{
			UE_LOG(LogShaderLibrary, Warning, TEXT("Rejecting asset info file %s: ShaderCodeToAssets array contains unreadable Assets array for mapping %d (damaged file?)"),
				*Filename,
				IdxPair
			);
			return false;
		}
			
		FShaderMapAssetPaths Paths;
		TArray<TSharedPtr<FJsonValue>> AssetPathsArray = AssetPathsArrayValue->AsArray();
		for (int32 IdxAsset = 0, NumAssets = AssetPathsArray.Num(); IdxAsset < NumAssets; ++IdxAsset)
		{
			Paths.Add(AssetPathsArray[IdxAsset]->AsString());
		}

		ShaderCodeToAssets.Add(ShaderMapHash, Paths);
	}

	return true;
}
#endif // WITH_EDITOR

FShaderCodeArchive* FShaderCodeArchive::Create(EShaderPlatform InPlatform, FArchive& Ar, const FString& InDestFilePath, const FString& InLibraryDir, const FString& InLibraryName)
{
	FShaderCodeArchive* Library = new FShaderCodeArchive(InPlatform, InLibraryDir, InLibraryName);
	Ar << Library->SerializedShaders;
	Library->ShaderPreloads.SetNum(Library->SerializedShaders.GetNumShaders());
	Library->LibraryCodeOffset = Ar.Tell();

	// Open library for async reads
	Library->FileCacheHandle = IFileCacheHandle::CreateFileCacheHandle(*InDestFilePath);

	UE_LOG(LogShaderLibrary, Display, TEXT("Using %s for material shader code. Total %d unique shaders."), *InDestFilePath, Library->SerializedShaders.ShaderEntries.Num());

	INC_DWORD_STAT_BY(STAT_Shaders_ShaderResourceMemory, Library->GetSizeBytes());

	return Library;
}

FShaderCodeArchive::FShaderCodeArchive(EShaderPlatform InPlatform, const FString& InLibraryDir, const FString& InLibraryName)
	: FRHIShaderLibrary(InPlatform, InLibraryName)
	, LibraryDir(InLibraryDir)
	, LibraryCodeOffset(0)
	, FileCacheHandle(nullptr)
{
}

FShaderCodeArchive::~FShaderCodeArchive()
{
	DEC_DWORD_STAT_BY(STAT_Shaders_ShaderResourceMemory, GetSizeBytes());
	Teardown();
}

void FShaderCodeArchive::Teardown()
{
	if (FileCacheHandle)
	{
		delete FileCacheHandle;
		FileCacheHandle = nullptr;
	}

	for (int32 ShaderIndex = 0; ShaderIndex < SerializedShaders.GetNumShaders(); ++ShaderIndex)
	{
		FShaderPreloadEntry& ShaderPreloadEntry = ShaderPreloads[ShaderIndex];
		if (ShaderPreloadEntry.Code)
		{
			const FShaderCodeEntry& ShaderEntry = SerializedShaders.ShaderEntries[ShaderIndex];
			FMemory::Free(ShaderPreloadEntry.Code);
			ShaderPreloadEntry.Code = nullptr;
			DEC_DWORD_STAT_BY(STAT_Shaders_ShaderPreloadMemory, ShaderEntry.Size);
		}
	}
}

void FShaderCodeArchive::OnShaderPreloadFinished(int32 ShaderIndex, const IMemoryReadStreamRef& PreloadData)
{
	FWriteScopeLock Lock(ShaderPreloadLock);
	const FShaderCodeEntry& ShaderEntry = SerializedShaders.ShaderEntries[ShaderIndex];
	FShaderPreloadEntry& ShaderPreloadEntry = ShaderPreloads[ShaderIndex];
	PreloadData->CopyTo(ShaderPreloadEntry.Code, 0, ShaderEntry.Size);
	ShaderPreloadEntry.PreloadEvent.SafeRelease();
}

struct FPreloadShaderTask
{
	explicit FPreloadShaderTask(FShaderCodeArchive* InArchive, int32 InShaderIndex, const IMemoryReadStreamRef& InData)
		: Archive(InArchive), Data(InData), ShaderIndex(InShaderIndex)
	{}

	FShaderCodeArchive* Archive;
	IMemoryReadStreamRef Data;
	int32 ShaderIndex;

	void DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
	{
		Archive->OnShaderPreloadFinished(ShaderIndex, Data);
		Data.SafeRelease();
	}

	FORCEINLINE static ESubsequentsMode::Type GetSubsequentsMode() { return ESubsequentsMode::TrackSubsequents; }
	FORCEINLINE ENamedThreads::Type GetDesiredThread() { return ENamedThreads::AnyNormalThreadNormalTask; }
	FORCEINLINE TStatId GetStatId() const { return TStatId(); }
};

bool FShaderCodeArchive::PreloadShader(int32 ShaderIndex, FGraphEventArray& OutCompletionEvents)
{
	LLM_SCOPE(ELLMTag::Shaders);

	FWriteScopeLock Lock(ShaderPreloadLock);

	FShaderPreloadEntry& ShaderPreloadEntry = ShaderPreloads[ShaderIndex];
	const uint32 ShaderNumRefs = ShaderPreloadEntry.NumRefs++;
	if (ShaderNumRefs == 0u)
	{
		check(!ShaderPreloadEntry.PreloadEvent);

		const FShaderCodeEntry& ShaderEntry = SerializedShaders.ShaderEntries[ShaderIndex];
		ShaderPreloadEntry.Code = FMemory::Malloc(ShaderEntry.Size);
		ShaderPreloadEntry.FramePreloadStarted = GFrameNumber;

		const EAsyncIOPriorityAndFlags IOPriority = (EAsyncIOPriorityAndFlags)GShaderCodeLibraryAsyncLoadingPriority;

		FGraphEventArray ReadCompletionEvents;

		EAsyncIOPriorityAndFlags DontCache = GShaderCodeLibraryAsyncLoadingAllowDontCache ? AIOP_FLAG_DONTCACHE : AIOP_MIN;
		IMemoryReadStreamRef PreloadData = FileCacheHandle->ReadData(ReadCompletionEvents, LibraryCodeOffset + ShaderEntry.Offset, ShaderEntry.Size, IOPriority | DontCache);
		auto Task = TGraphTask<FPreloadShaderTask>::CreateTask(&ReadCompletionEvents).ConstructAndHold(this, ShaderIndex, MoveTemp(PreloadData));
		ShaderPreloadEntry.PreloadEvent = Task->GetCompletionEvent();
		Task->Unlock();

		INC_DWORD_STAT_BY(STAT_Shaders_ShaderPreloadMemory, ShaderEntry.Size);
	}

	if (ShaderPreloadEntry.PreloadEvent)
	{
		OutCompletionEvents.Add(ShaderPreloadEntry.PreloadEvent);
	}
	return true;
}

bool FShaderCodeArchive::PreloadShaderMap(int32 ShaderMapIndex, FGraphEventArray& OutCompletionEvents)
{
	LLM_SCOPE(ELLMTag::Shaders);

	const FShaderMapEntry& ShaderMapEntry = SerializedShaders.ShaderMapEntries[ShaderMapIndex];
	const EAsyncIOPriorityAndFlags IOPriority = (EAsyncIOPriorityAndFlags)GShaderCodeLibraryAsyncLoadingPriority;
	const uint32 FrameNumber = GFrameNumber;
	uint32 PreloadMemory = 0u;
	
	FWriteScopeLock Lock(ShaderPreloadLock);

	for (uint32 i = 0u; i < ShaderMapEntry.NumShaders; ++i)
	{
		const int32 ShaderIndex = SerializedShaders.ShaderIndices[ShaderMapEntry.ShaderIndicesOffset + i];
		FShaderPreloadEntry& ShaderPreloadEntry = ShaderPreloads[ShaderIndex];
		const uint32 ShaderNumRefs = ShaderPreloadEntry.NumRefs++;
		if (ShaderNumRefs == 0u)
		{
			check(!ShaderPreloadEntry.PreloadEvent);
			const FShaderCodeEntry& ShaderEntry = SerializedShaders.ShaderEntries[ShaderIndex];
			ShaderPreloadEntry.Code = FMemory::Malloc(ShaderEntry.Size);
			ShaderPreloadEntry.FramePreloadStarted = FrameNumber;
			PreloadMemory += ShaderEntry.Size;

			FGraphEventArray ReadCompletionEvents;
			EAsyncIOPriorityAndFlags DontCache = GShaderCodeLibraryAsyncLoadingAllowDontCache ? AIOP_FLAG_DONTCACHE : AIOP_MIN;
			IMemoryReadStreamRef PreloadData = FileCacheHandle->ReadData(ReadCompletionEvents, LibraryCodeOffset + ShaderEntry.Offset, ShaderEntry.Size, IOPriority | DontCache);
			auto Task = TGraphTask<FPreloadShaderTask>::CreateTask(&ReadCompletionEvents).ConstructAndHold(this, ShaderIndex, MoveTemp(PreloadData));
			ShaderPreloadEntry.PreloadEvent = Task->GetCompletionEvent();
			Task->Unlock();
			OutCompletionEvents.Add(ShaderPreloadEntry.PreloadEvent);
		}
		else if (ShaderPreloadEntry.PreloadEvent)
		{
			OutCompletionEvents.Add(ShaderPreloadEntry.PreloadEvent);
		}
	}

	INC_DWORD_STAT_BY(STAT_Shaders_ShaderPreloadMemory, PreloadMemory);

	return true;
}

bool FShaderCodeArchive::WaitForPreload(FShaderPreloadEntry& ShaderPreloadEntry)
{
	FGraphEventRef Event;
	{
		FReadScopeLock Lock(ShaderPreloadLock);
		if(ShaderPreloadEntry.NumRefs > 0u)
		{
			Event = ShaderPreloadEntry.PreloadEvent;
		}
		else
		{
			check(!ShaderPreloadEntry.PreloadEvent);
		}
	}

	const bool bNeedToWait = Event && !Event->IsComplete();
	if (bNeedToWait)
	{
		FTaskGraphInterface::Get().WaitUntilTaskCompletes(Event);
	}
	return bNeedToWait;
}

void FShaderCodeArchive::ReleasePreloadedShader(int32 ShaderIndex)
{
	FShaderPreloadEntry& ShaderPreloadEntry = ShaderPreloads[ShaderIndex];

	WaitForPreload(ShaderPreloadEntry);

	FWriteScopeLock Lock(ShaderPreloadLock);

	ShaderPreloadEntry.PreloadEvent.SafeRelease();

	const uint32 ShaderNumRefs = ShaderPreloadEntry.NumRefs--;
	check(ShaderPreloadEntry.Code);
	check(ShaderNumRefs > 0u);
	if (ShaderNumRefs == 1u)
	{
		FMemory::Free(ShaderPreloadEntry.Code);
		ShaderPreloadEntry.Code = nullptr;
		const FShaderCodeEntry& ShaderEntry = SerializedShaders.ShaderEntries[ShaderIndex];
		DEC_DWORD_STAT_BY(STAT_Shaders_ShaderPreloadMemory, ShaderEntry.Size);
	}
}

TRefCountPtr<FRHIShader> FShaderCodeArchive::CreateShader(int32 Index)
{
	LLM_SCOPE(ELLMTag::Shaders);
	TRefCountPtr<FRHIShader> Shader;

	FMemStackBase& MemStack = FMemStack::Get();
	FMemMark Mark(MemStack);

	const FShaderCodeEntry& ShaderEntry = SerializedShaders.ShaderEntries[Index];
	FShaderPreloadEntry& ShaderPreloadEntry = ShaderPreloads[Index];

	void* PreloadedShaderCode = nullptr;
	{
		const bool bNeededToWait = WaitForPreload(ShaderPreloadEntry);
		if (bNeededToWait)
		{
			UE_LOG(LogShaderLibrary, Warning, TEXT("Blocking wait for shader preload, NumRefs: %d, FramePreloadStarted: %d"), ShaderPreloadEntry.NumRefs, ShaderPreloadEntry.FramePreloadStarted);
		}

		FWriteScopeLock Lock(ShaderPreloadLock);
		if (ShaderPreloadEntry.NumRefs > 0u)
		{
			check(!ShaderPreloadEntry.PreloadEvent || ShaderPreloadEntry.PreloadEvent->IsComplete());
			ShaderPreloadEntry.PreloadEvent.SafeRelease();

			ShaderPreloadEntry.NumRefs++; // Hold a reference to code while we're using it to create shader
			PreloadedShaderCode = ShaderPreloadEntry.Code;
			check(PreloadedShaderCode);
		}
	}

	const uint8* ShaderCode = (uint8*)PreloadedShaderCode;
	if (!ShaderCode)
	{
		UE_LOG(LogShaderLibrary, Warning, TEXT("Blocking shader load, NumRefs: %d, FramePreloadStarted: %d"), ShaderPreloadEntry.NumRefs, ShaderPreloadEntry.FramePreloadStarted);

		FGraphEventArray ReadCompleteEvents;
		EAsyncIOPriorityAndFlags DontCache = GShaderCodeLibraryAsyncLoadingAllowDontCache ? AIOP_FLAG_DONTCACHE : AIOP_MIN;
		IMemoryReadStreamRef LoadedCode = FileCacheHandle->ReadData(ReadCompleteEvents, LibraryCodeOffset + ShaderEntry.Offset, ShaderEntry.Size, AIOP_CriticalPath | DontCache);
		if (ReadCompleteEvents.Num() > 0)
		{
			FTaskGraphInterface::Get().WaitUntilTasksComplete(ReadCompleteEvents);
		}
		void* LoadedShaderCode = MemStack.Alloc(ShaderEntry.Size, 16);
		LoadedCode->CopyTo(LoadedShaderCode, 0, ShaderEntry.Size);
		ShaderCode = (uint8*)LoadedShaderCode;
	}

	if (ShaderEntry.UncompressedSize != ShaderEntry.Size)
	{
		void* UncompressedCode = MemStack.Alloc(ShaderEntry.UncompressedSize, 16);
		const bool bDecompressResult = FCompression::UncompressMemory(ShaderLibraryCompressionFormat, UncompressedCode, ShaderEntry.UncompressedSize, ShaderCode, ShaderEntry.Size);
		check(bDecompressResult);
		ShaderCode = (uint8*)UncompressedCode;
	}

	const auto ShaderCodeView = MakeArrayView(ShaderCode, ShaderEntry.UncompressedSize);
	const FSHAHash& ShaderHash = SerializedShaders.ShaderHashes[Index];
	switch (ShaderEntry.Frequency)
	{
	case SF_Vertex: Shader = RHICreateVertexShader(ShaderCodeView, ShaderHash); CheckShaderCreation(Shader, Index); break;
	case SF_Pixel: Shader = RHICreatePixelShader(ShaderCodeView, ShaderHash); CheckShaderCreation(Shader, Index); break;
	case SF_Geometry: Shader = RHICreateGeometryShader(ShaderCodeView, ShaderHash); CheckShaderCreation(Shader, Index); break;
	case SF_Hull: Shader = RHICreateHullShader(ShaderCodeView, ShaderHash); CheckShaderCreation(Shader, Index); break;
	case SF_Domain: Shader = RHICreateDomainShader(ShaderCodeView, ShaderHash); CheckShaderCreation(Shader, Index); break;
	case SF_Compute: Shader = RHICreateComputeShader(ShaderCodeView, ShaderHash); CheckShaderCreation(Shader, Index); break;
	case SF_RayGen: case SF_RayMiss: case SF_RayHitGroup: case SF_RayCallable:
#if RHI_RAYTRACING
		if (GRHISupportsRayTracing)
		{
			Shader = RHICreateRayTracingShader(ShaderCodeView, ShaderHash, ShaderEntry.GetFrequency());
			CheckShaderCreation(Shader, Index);
		}
#endif // RHI_RAYTRACING
		break;
	default: checkNoEntry(); break;
	}

	// Release the refernece we were holding
	if (PreloadedShaderCode)
	{
		FWriteScopeLock Lock(ShaderPreloadLock);
		check(ShaderPreloadEntry.NumRefs > 1u); // we shouldn't be holding the last ref here
		--ShaderPreloadEntry.NumRefs;
		PreloadedShaderCode = nullptr;
	}

	if (Shader)
	{
		Shader->SetHash(ShaderHash);
	}

	return Shader;
}
