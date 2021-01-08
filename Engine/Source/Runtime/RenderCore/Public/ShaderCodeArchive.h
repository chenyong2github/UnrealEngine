// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RHI.h"
#include "FileCache/FileCache.h"
#include "Containers/HashTable.h"
#include "Shader.h"

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

	/** Hashes of all shadermaps in the library */
	TArray<FSHAHash> ShaderMapHashes;

	/** Output hashes of all shaders in the library */
	TArray<FSHAHash> ShaderHashes;

	/** An array of a shadermap descriptors. Each shadermap can reference an arbitrary number of shaders */
	TArray<FShaderMapEntry> ShaderMapEntries;

	/** An array of all shaders descriptors, deduplicated */
	TArray<FShaderCodeEntry> ShaderEntries;

	/** An array of preload entries*/
	TArray<FFileCachePreloadEntry> PreloadEntries;

	/** Flat array of shaders referenced by all shadermaps. Each shadermap has a range in this array, beginning of which is
	  * stored as ShaderIndicesOffset in the shadermap's descriptor (FShaderMapEntry).
	  */
	TArray<uint32> ShaderIndices;

	FHashTable ShaderMapHashTable;
	FHashTable ShaderHashTable;

#if WITH_EDITOR
	/** Mapping from shadermap hashes to an array of asset names - this is used for on-disk storage as it is shorter. */
	TMap<FSHAHash, FShaderMapAssetPaths> ShaderCodeToAssets;

	enum class EAssetInfoVersion : uint8
	{
		CurrentVersion = 2
	};

	struct FDebugStats
	{
		int32 NumAssets;
		int64 ShadersSize;
		int64 ShadersUniqueSize;
		int32 NumShaders;
		int32 NumUniqueShaders;
		int32 NumShaderMaps;
	};

	struct FExtendedDebugStats
	{
		/** Textual contents, should match the binary layout in terms of order */
		FString TextualRepresentation;

		/** Minimum number of shaders in any given shadermap */
		uint32 MinNumberOfShadersPerSM;

		/** Median number of shaders in shadermaps */
		uint32 MedianNumberOfShadersPerSM;

		/** Maximum number of shaders in any given shadermap */
		uint32 MaxNumberofShadersPerSM;

		/** For the top shaders (descending), number of shadermaps in which they are used. Expected to be limited to a small number (10) */
		TArray<int32> TopShaderUsages;
	};
#endif

	FSerializedShaderArchive()
	{
	}

	uint32 GetAllocatedSize() const
	{
		return ShaderHashes.GetAllocatedSize() +
			ShaderEntries.GetAllocatedSize() +
			ShaderMapHashes.GetAllocatedSize() +
			ShaderMapEntries.GetAllocatedSize() +
			PreloadEntries.GetAllocatedSize() +
			ShaderIndices.GetAllocatedSize()
#if WITH_EDITOR
			+ ShaderCodeToAssets.GetAllocatedSize()
#endif
			;
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
#if WITH_EDITOR
		ShaderCodeToAssets.Empty();
#endif
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
	bool FindOrAddShaderMap(const FSHAHash& Hash, int32& OutIndex, const FShaderMapAssetPaths* AssociatedAssets);

	int32 FindShaderWithKey(const FSHAHash& Hash, uint32 Key) const;
	int32 FindShader(const FSHAHash& Hash) const;
	bool FindOrAddShader(const FSHAHash& Hash, int32& OutIndex);

	void DecompressShader(int32 Index, const TArray<TArray<uint8>>& ShaderCode, TArray<uint8>& OutDecompressedShader) const;

	void Finalize();
	void Serialize(FArchive& Ar);
#if WITH_EDITOR
	void SaveAssetInfo(FArchive& Ar);
	bool LoadAssetInfo(const FString& Filename);
	void CreateAsChunkFrom(const FSerializedShaderArchive& Parent, const TSet<FName>& PackagesInChunk, TArray<int32>& OutShaderCodeEntriesNeeded);
	void CollectStatsAndDebugInfo(FDebugStats& OutDebugStats, FExtendedDebugStats* OutExtendedDebugStats);
	void DumpContentsInPlaintext(FString& OutText) const;
#endif

	friend FArchive& operator<<(FArchive& Ar, FSerializedShaderArchive& Ref)
	{
		Ref.Serialize(Ar);
		return Ar;
	}
};

class FShaderCodeArchive : public FRHIShaderLibrary
{
public:
	static FShaderCodeArchive* Create(EShaderPlatform InPlatform, FArchive& Ar, const FString& InDestFilePath, const FString& InLibraryDir, const FString& InLibraryName);

	virtual ~FShaderCodeArchive();

	virtual bool IsNativeLibrary() const override { return false; }

	uint32 GetSizeBytes() const
	{
		return sizeof(*this) +
			SerializedShaders.GetAllocatedSize() +
			ShaderPreloads.GetAllocatedSize();
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

	virtual bool PreloadShader(int32 ShaderIndex, FGraphEventArray& OutCompletionEvents) override;

	virtual bool PreloadShaderMap(int32 ShaderMapIndex, FGraphEventArray& OutCompletionEvents) override;

	virtual void ReleasePreloadedShader(int32 ShaderIndex) override;

	virtual TRefCountPtr<FRHIShader> CreateShader(int32 Index) override;
	virtual void Teardown() override;

	void OnShaderPreloadFinished(int32 ShaderIndex, const IMemoryReadStreamRef& PreloadData);

protected:
	FShaderCodeArchive(EShaderPlatform InPlatform, const FString& InLibraryDir, const FString& InLibraryName);

	FORCENOINLINE void CheckShaderCreation(void* ShaderPtr, int32 Index)
	{
	}

	struct FShaderPreloadEntry
	{
		FGraphEventRef PreloadEvent;
		void* Code = nullptr;
		uint32 FramePreloadStarted = ~0u;
		uint32 NumRefs = 0u;
	};

	bool WaitForPreload(FShaderPreloadEntry& ShaderPreloadEntry);

	// Library directory
	FString LibraryDir;

	// Offset at where shader code starts in a code library
	int64 LibraryCodeOffset;

	// Library file handle for async reads
	IFileCacheHandle* FileCacheHandle;

	// The shader code present in the library
	FSerializedShaderArchive SerializedShaders;

	TArray<FGraphEventRef> ShaderMapPreloadEvents;

	TArray<FShaderPreloadEntry> ShaderPreloads;
	FRWLock ShaderPreloadLock;
};
