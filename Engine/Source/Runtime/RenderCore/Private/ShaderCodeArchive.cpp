// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShaderCodeArchive.h"
#include "ShaderCodeLibrary.h"
#include "Shader.h"
#include "Stats/Stats.h"
#include "ProfilingDebugging/LoadTimeTracker.h"
#include "Misc/MemStack.h"

int32 GShaderCodeLibraryAsyncLoadingPriority = int32(AIOP_Normal);
static FAutoConsoleVariableRef CVarShaderCodeLibraryAsyncLoadingPriority(
	TEXT("r.ShaderCodeLibrary.DefaultAsyncIOPriority"),
	GShaderCodeLibraryAsyncLoadingPriority,
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

bool FSerializedShaderArchive::FindOrAddShaderMap(const FSHAHash& Hash, int32& OutIndex)
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
		bAdded = true;
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

FShaderCodeArchive* FShaderCodeArchive::Create(EShaderPlatform InPlatform, FArchive& Ar, const FString& InDestFilePath, const FString& InLibraryDir, const FString& InLibraryName)
{
	FShaderCodeArchive* Library = new FShaderCodeArchive(InPlatform, InLibraryDir, InLibraryName);
	Ar << Library->SerializedShaders;
	Library->LibraryCodeOffset = Ar.Tell();

#if TRACK_SHADER_PRELOADS
	Library->ShaderFramePreloaded.SetNumUninitialized(Library->SerializedShaders.GetNumShaders());
	for (uint32& Frame : Library->ShaderFramePreloaded)
	{
		Frame = ~0u;
	}
#endif // TRACK_SHADER_PRELOADS

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
}

IMemoryReadStreamRef FShaderCodeArchive::ReadShaderCode(int32 ShaderIndex)
{
	SCOPED_LOADTIMER(FShaderCodeArchive_ReadShaderCode);

	const FShaderCodeEntry& Entry = SerializedShaders.ShaderEntries[ShaderIndex];

	FGraphEventArray ReadCompleteEvents;
	IMemoryReadStreamRef LoadedCode = FileCacheHandle->ReadData(ReadCompleteEvents, LibraryCodeOffset + Entry.Offset, Entry.Size, AIOP_CriticalPath);
	if (ReadCompleteEvents.Num() > 0)
	{
#if TRACK_SHADER_PRELOADS
		if (ShaderFramePreloaded[ShaderIndex] < 0xffffffff)
		{
			UE_LOG(LogShaderLibrary, Warning, TEXT("** ShaderCode was preloaded on frame %d, unloaded by frame %d"), ShaderFramePreloaded[ShaderIndex], GFrameNumber);
		}
#endif // TRACK_SHADER_PRELOADS
		FTaskGraphInterface::Get().WaitUntilTasksComplete(ReadCompleteEvents);
	}

	return LoadedCode;
}

FGraphEventRef FShaderCodeArchive::PreloadShader(int32 ShaderIndex)
{
	const FShaderCodeEntry& ShaderEntry = SerializedShaders.ShaderEntries[ShaderIndex];
#if TRACK_SHADER_PRELOADS
	ShaderFramePreloaded[ShaderIndex] = FMath::Min(ShaderFramePreloaded[ShaderIndex], GFrameNumber);
#endif // TRACK_SHADER_PRELOADS
	const EAsyncIOPriorityAndFlags IOPriority = (EAsyncIOPriorityAndFlags)GShaderCodeLibraryAsyncLoadingPriority;
	const FFileCachePreloadEntry PreloadEntry(ShaderEntry.Offset, ShaderEntry.Size);
	return FileCacheHandle->PreloadData(&PreloadEntry, 1, LibraryCodeOffset, IOPriority);
}

FGraphEventRef FShaderCodeArchive::PreloadShaderMap(int32 ShaderMapIndex)
{
	const FShaderMapEntry& ShaderMapEntry = SerializedShaders.ShaderMapEntries[ShaderMapIndex];
#if TRACK_SHADER_PRELOADS
	const uint32 FrameNumber = GFrameNumber;
	for (uint32 i = 0u; i < ShaderMapEntry.NumShaders; ++i)
	{
		const uint32 ShaderIndex = SerializedShaders.ShaderIndices[ShaderMapEntry.ShaderIndicesOffset + i];
		ShaderFramePreloaded[ShaderIndex] = FMath::Min(ShaderFramePreloaded[ShaderIndex], FrameNumber);
	}
#endif // TRACK_SHADER_PRELOADS
	const EAsyncIOPriorityAndFlags IOPriority = (EAsyncIOPriorityAndFlags)GShaderCodeLibraryAsyncLoadingPriority;
	return FileCacheHandle->PreloadData(&SerializedShaders.PreloadEntries[ShaderMapEntry.FirstPreloadIndex], ShaderMapEntry.NumPreloadEntries, LibraryCodeOffset, IOPriority);
}

void FShaderCodeArchive::ReleasePreloadedShaderMap(int32 ShaderMapIndex)
{
	const FShaderMapEntry& ShaderMapEntry = SerializedShaders.ShaderMapEntries[ShaderMapIndex];
#if TRACK_SHADER_PRELOADS
	for (uint32 i = 0u; i < ShaderMapEntry.NumShaders; ++i)
	{
		const uint32 ShaderIndex = SerializedShaders.ShaderIndices[ShaderMapEntry.ShaderIndicesOffset + i];
		ShaderFramePreloaded[ShaderIndex] = ~0u;
	}
#endif // TRACK_SHADER_PRELOADS
	FileCacheHandle->ReleasePreloadedData(&SerializedShaders.PreloadEntries[ShaderMapEntry.FirstPreloadIndex], ShaderMapEntry.NumPreloadEntries, LibraryCodeOffset);
}

TRefCountPtr<FRHIShader> FShaderCodeArchive::CreateShader(int32 Index)
{
	TRefCountPtr<FRHIShader> Shader;

	IMemoryReadStreamRef Code = ReadShaderCode(Index);
	if (Code)
	{
		FMemStackBase& MemStack = FMemStack::Get();
		const FShaderCodeEntry& ShaderEntry = SerializedShaders.ShaderEntries[Index];
		check(ShaderEntry.Size == Code->GetSize());
		const uint8* ShaderCode = nullptr;

		FMemMark Mark(MemStack);
		if (ShaderEntry.UncompressedSize != ShaderEntry.Size)
		{
			void* UncompressedCode = MemStack.Alloc(ShaderEntry.UncompressedSize, 16);
			const bool bDecompressResult = FCompression::UncompressMemoryStream(ShaderLibraryCompressionFormat, UncompressedCode, ShaderEntry.UncompressedSize, Code, 0, ShaderEntry.Size);
			check(bDecompressResult);
			ShaderCode = (uint8*)UncompressedCode;
		}
		else
		{
			int64 ReadSize = 0;
			ShaderCode = (uint8*)Code->Read(ReadSize, 0, ShaderEntry.UncompressedSize);
			if (ReadSize != ShaderEntry.UncompressedSize)
			{
				// Unable to read contiguous block of code, need to copy to temp buffer
				void* UncompressedCode = MemStack.Alloc(ShaderEntry.UncompressedSize, 16);
				Code->CopyTo(UncompressedCode, 0, ShaderEntry.UncompressedSize);
				ShaderCode = (uint8*)UncompressedCode;
			}
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

		if (Shader)
		{
			Shader->SetHash(ShaderHash);
		}
	}
	return Shader;
}
