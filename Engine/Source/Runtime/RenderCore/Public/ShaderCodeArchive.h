// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RHI.h"
#include "FileCache/FileCache.h"
#include "Containers/HashTable.h"

struct FShaderMapEntry
{
	uint32 ShaderIndicesOffset = 0u;
	uint32 NumShaders = 0u;
	uint32 FirstPreloadIndex = 0u;
	uint32 NumPreloadEntries = 0u;

	friend FArchive& operator <<(FArchive& Ar, FShaderMapEntry& Ref)
	{
		return Ar << Ref.ShaderIndicesOffset << Ref.NumShaders << Ref.FirstPreloadIndex << Ref.NumPreloadEntries;
	}
};

static FArchive& operator <<(FArchive& Ar, FFileCachePreloadEntry& Ref)
{
	return Ar << Ref.Offset << Ref.Size;
}

struct FShaderCodeEntry
{
	uint64 Offset = 0;
	uint32 Size = 0;
	uint32 UncompressedSize = 0;
	uint8 Frequency;

	EShaderFrequency GetFrequency() const
	{
		return (EShaderFrequency)Frequency;
	}

	friend FArchive& operator <<(FArchive& Ar, FShaderCodeEntry& Ref)
	{
		return Ar << Ref.Offset << Ref.Size << Ref.UncompressedSize << Ref.Frequency;
	}
};

// Portion of shader code archive that's serialize to disk
class RENDERCORE_API FSerializedShaderArchive
{
public:
	TArray<FSHAHash> ShaderMapHashes;
	TArray<FSHAHash> ShaderHashes;
	TArray<FShaderMapEntry> ShaderMapEntries;
	TArray<FShaderCodeEntry> ShaderEntries;
	TArray<FFileCachePreloadEntry> PreloadEntries;
	TArray<uint32> ShaderIndices;
	FHashTable ShaderMapHashTable;
	FHashTable ShaderHashTable;

	FSerializedShaderArchive() : ShaderMapHashTable(0u), ShaderHashTable(0u) {}

	uint32 GetAllocatedSize() const
	{
		return ShaderHashes.GetAllocatedSize() +
			ShaderEntries.GetAllocatedSize() +
			ShaderMapHashes.GetAllocatedSize() +
			ShaderMapEntries.GetAllocatedSize() +
			PreloadEntries.GetAllocatedSize() +
			ShaderIndices.GetAllocatedSize();
	}

	void Empty()
	{
		ShaderHashes.Empty();
		ShaderEntries.Empty();
		ShaderMapHashes.Empty();
		ShaderMapEntries.Empty();
		PreloadEntries.Empty();
		ShaderIndices.Empty();
		ShaderMapHashTable.Clear();
		ShaderHashTable.Clear();
	}

	int32 GetNumShaderMaps() const
	{
		return ShaderMapEntries.Num();
	}

	int32 GetNumShaders() const
	{
		return ShaderEntries.Num();
	}

	int32 FindShaderMapWithKey(const FSHAHash& Hash, uint32 Key) const;
	int32 FindShaderMap(const FSHAHash& Hash) const;
	bool FindOrAddShaderMap(const FSHAHash& Hash, int32& OutIndex);

	int32 FindShaderWithKey(const FSHAHash& Hash, uint32 Key) const;
	int32 FindShader(const FSHAHash& Hash) const;
	bool FindOrAddShader(const FSHAHash& Hash, int32& OutIndex);

	void DecompressShader(int32 Index, const TArray<TArray<uint8>>& ShaderCode, TArray<uint8>& OutDecompressedShader) const;

	void Finalize();
	void Serialize(FArchive& Ar);

	friend FArchive& operator<<(FArchive& Ar, FSerializedShaderArchive& Ref)
	{
		Ref.Serialize(Ar);
		return Ar;
	}
};

#define TRACK_SHADER_PRELOADS STATS

class FShaderCodeArchive : public FRHIShaderLibrary
{
public:
	static FShaderCodeArchive* Create(EShaderPlatform InPlatform, FArchive& Ar, const FString& InDestFilePath, const FString& InLibraryDir, const FString& InLibraryName);

	virtual ~FShaderCodeArchive();

	virtual bool IsNativeLibrary() const override { return false; }

	uint32 GetSizeBytes() const
	{
		return sizeof(*this) +
			SerializedShaders.GetAllocatedSize();
	}

	virtual int32 GetNumShaders() const override { return SerializedShaders.ShaderEntries.Num(); }
	virtual int32 GetNumShaderMaps() const override { return SerializedShaders.ShaderMapEntries.Num(); }
	virtual int32 GetNumShadersForShaderMap(int32 ShaderMapIndex) const override { return SerializedShaders.ShaderMapEntries[ShaderMapIndex].NumShaders; }

	virtual int32 GetShaderIndex(int32 ShaderMapIndex, int32 i) const override
	{
		const FShaderMapEntry& ShaderMapEntry = SerializedShaders.ShaderMapEntries[ShaderMapIndex];
		return SerializedShaders.ShaderIndices[ShaderMapEntry.ShaderIndicesOffset + i];
	}

	virtual int32 FindShaderMapIndex(const FSHAHash& Hash) override
	{
		return SerializedShaders.FindShaderMap(Hash);
	}

	virtual int32 FindShaderIndex(const FSHAHash& Hash) override
	{
		return SerializedShaders.FindShader(Hash);
	}

	virtual FGraphEventRef PreloadShader(int32 ShaderIndex) override;

	virtual FGraphEventRef PreloadShaderMap(int32 ShaderMapIndex) override;

	virtual void ReleasePreloadedShaderMap(int32 ShaderMapIndex) override;

	virtual TRefCountPtr<FRHIShader> CreateShader(int32 Index) override;
	virtual void Teardown() override;

protected:
	FShaderCodeArchive(EShaderPlatform InPlatform, const FString& InLibraryDir, const FString& InLibraryName);

	IMemoryReadStreamRef ReadShaderCode(int32 Index);

	FORCENOINLINE void CheckShaderCreation(void* ShaderPtr, int32 Index)
	{
	}

	// Library directory
	FString LibraryDir;

	// Offset at where shader code starts in a code library
	int64 LibraryCodeOffset;

	// Library file handle for async reads
	IFileCacheHandle* FileCacheHandle;

	// The shader code present in the library
	FSerializedShaderArchive SerializedShaders;

#if TRACK_SHADER_PRELOADS
	TArray<uint32> ShaderFramePreloaded;
#endif
};
