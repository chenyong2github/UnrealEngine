// Copyright Epic Games, Inc. All Rights Reserved.

/*==============================================================================
	TextureDerivedDataTask.h: Tasks to update texture DDC.
==============================================================================*/

#pragma once

#include "CoreMinimal.h"

#if WITH_EDITOR

#include "Async/AsyncWork.h"
#include "Engine/Texture2D.h"
#include "IImageWrapperModule.h"
#include "ImageCore.h"
#include "Misc/EnumClassFlags.h"
#include "TextureCompressorModule.h"

#endif // WITH_EDITOR

enum
{
	/** The number of mips to store inline. */
	NUM_INLINE_DERIVED_MIPS = 7,
};

#if WITH_EDITOR

namespace UE::DerivedData { class FBuildOutput; }

void GetTextureDerivedDataKeySuffix(const UTexture& Texture, const FTextureBuildSettings* BuildSettingsPerLayer, FString& OutKeySuffix);
uint32 PutDerivedDataInCache(FTexturePlatformData* DerivedData, const FString& DerivedDataKeySuffix, const FStringView& TextureName, bool bForceAllMipsToBeInlined, bool bReplaceExistingDDC);

enum class ETextureCacheFlags : uint32
{
	None			= 0x00,
	Async			= 0x01,
	ForceRebuild	= 0x02,
	InlineMips		= 0x08,
	AllowAsyncBuild	= 0x10,
	ForDDCBuild		= 0x20,
	RemoveSourceMipDataAfterCache = 0x40,
	AllowAsyncLoading = 0x80,
	ForVirtualTextureStreamingBuild = 0x100,
};

ENUM_CLASS_FLAGS(ETextureCacheFlags);

// Everything required to get the texture source data.
struct FTextureSourceLayerData
{
	ERawImageFormat::Type ImageFormat;
	EGammaSpace GammaSpace;
};

struct FTextureSourceBlockData
{
	TArray<TArray<FImage>> MipsPerLayer;
	int32 BlockX = 0;
	int32 BlockY = 0;
	int32 SizeInBlocksX = 1; // Normally each blocks covers a 1x1 block area
	int32 SizeInBlocksY = 1;
	int32 SizeX = 0;
	int32 SizeY = 0;
	int32 NumMips = 0;
	int32 NumSlices = 0;
	int32 MipBias = 0;
};

struct FTextureSourceData
{
	FTextureSourceData()
		: SizeInBlocksX(0)
		, SizeInBlocksY(0)
		, BlockSizeX(0)
		, BlockSizeY(0)
		, bValid(false)
	{}

	void Init(UTexture& InTexture, const FTextureBuildSettings* InBuildSettingsPerLayer, bool bAllowAsyncLoading);
	bool IsValid() const { return bValid; }

	void GetSourceMips(FTextureSource& Source, IImageWrapperModule* InImageWrapper);
	void GetAsyncSourceMips(IImageWrapperModule* InImageWrapper);

	void ReleaseMemory()
	{
		// Unload BulkData loaded with LoadBulkDataWithFileReader
		AsyncSource.RemoveBulkData();
		Blocks.Empty();
	}

	FName TextureName;
	FTextureSource AsyncSource;
	TArray<FTextureSourceLayerData> Layers;
	TArray<FTextureSourceBlockData> Blocks;
	int32 SizeInBlocksX;
	int32 SizeInBlocksY;
	int32 BlockSizeX;
	int32 BlockSizeY;
	bool bValid;
};

/**
 * Worker used to cache texture derived data.
 */
class FTextureCacheDerivedDataWorker : public FNonAbandonableTask
{
	/** Texture compressor module, must be loaded in the game thread. see FModuleManager::WarnIfItWasntSafeToLoadHere() */
	ITextureCompressorModule* Compressor;
	/** Image wrapper module, must be loaded in the game thread. see FModuleManager::WarnIfItWasntSafeToLoadHere() */
	IImageWrapperModule* ImageWrapper;
	/** Where to store derived data. */
	FTexturePlatformData* DerivedData;
	/** The texture for which derived data is being cached. */
	UTexture& Texture;
	/** Compression settings. */
	TArray<FTextureBuildSettings> BuildSettingsPerLayer;
	/** Derived data key suffix. */
	FString KeySuffix;
	/** Source mip images. */
	FTextureSourceData TextureData;
	/** Source mip images of the composite texture (e.g. normal map for compute roughness). Not necessarily in RGBA32F, usually only top mip as other mips need to be generated first */
	FTextureSourceData CompositeTextureData;
	/** DDC2 build function name to use to build this texture (if DDC2 is enabled and the target texture type has a DDC2 build function, empty otherwise) */
	FString BuildFunctionName;
	/** Texture cache flags. */
	ETextureCacheFlags CacheFlags;
	/** Have many bytes were loaded from DDC or built (for telemetry) */
	uint32 BytesCached = 0;
	/** Estimate of the peak amount of memory required to complete this task. */
	int64 RequiredMemoryEstimate = -1;

	/** true if caching has succeeded. */
	bool bSucceeded;
	/** true if the derived data was pulled from DDC */
	bool bLoadedFromDDC = false;

	/** Build the texture. This function is safe to call from any thread. */
	void BuildTexture(bool bReplaceExistingDDC = false);

	void ConsumeBuildFunctionOutput(const UE::DerivedData::FBuildOutput& BuildOutput, const FString& TexturePath, bool bReplaceExistingDDC);

public:

	/** Initialization constructor. */
	FTextureCacheDerivedDataWorker(
		ITextureCompressorModule* InCompressor,
		FTexturePlatformData* InDerivedData,
		UTexture* InTexture,
		const FTextureBuildSettings* InSettingsPerLayer,
		ETextureCacheFlags InCacheFlags);

	/** Does the work to cache derived data. Safe to call from any thread. */
	void DoWork();

	/** Finalize work. Must be called ONLY by the game thread! */
	void Finalize();

	/** Expose bytes cached for telemetry. */
	uint32 GetBytesCached() const
	{
		return BytesCached;
	}

	/** Estimate of the peak amount of memory required to complete this task. */
	int64 GetRequiredMemoryEstimate() const
	{
		return RequiredMemoryEstimate;
	}

	/** Expose how the resource was returned for telemetry. */
	bool WasLoadedFromDDC() const
	{
		return bLoadedFromDDC;
	}

	FORCEINLINE TStatId GetStatId() const
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(FTextureCacheDerivedDataWorker, STATGROUP_ThreadPoolAsyncTasks);
	}
};

struct FTextureAsyncCacheDerivedDataTask
{
	virtual ~FTextureAsyncCacheDerivedDataTask() = default;
	virtual void Finalize(bool& bOutFoundInCache, uint64& OutProcessedByteCount) = 0;
	virtual EQueuedWorkPriority GetPriority() const = 0;
	virtual bool SetPriority(EQueuedWorkPriority QueuedWorkPriority) = 0;
	virtual bool Cancel() = 0;
	virtual void Wait() = 0;
	virtual bool WaitWithTimeout(float TimeLimitSeconds) = 0;
	virtual bool Poll() const = 0;
};

class FTextureAsyncCacheDerivedDataWorkerTask final : public FTextureAsyncCacheDerivedDataTask, public FAsyncTask<FTextureCacheDerivedDataWorker>
{
public:
	FTextureAsyncCacheDerivedDataWorkerTask(
		FQueuedThreadPool* InQueuedPool,
		ITextureCompressorModule* InCompressor,
		FTexturePlatformData* InDerivedData,
		UTexture* InTexture,
		const FTextureBuildSettings* InSettingsPerLayer,
		ETextureCacheFlags InCacheFlags
		)
		: FAsyncTask<FTextureCacheDerivedDataWorker>(
			InCompressor,
			InDerivedData,
			InTexture,
			InSettingsPerLayer,
			InCacheFlags
			)
		, QueuedPool(InQueuedPool)
	{
	}

	void Finalize(bool& bOutFoundInCache, uint64& OutProcessedByteCount) final
	{
		GetTask().Finalize();
		bOutFoundInCache = GetTask().WasLoadedFromDDC();
		OutProcessedByteCount = GetTask().GetBytesCached();
	}

	EQueuedWorkPriority GetPriority() const final
	{
		return FAsyncTask<FTextureCacheDerivedDataWorker>::GetPriority();
	}

	bool SetPriority(EQueuedWorkPriority QueuedWorkPriority) final
	{
		return FAsyncTask<FTextureCacheDerivedDataWorker>::Reschedule(QueuedPool, QueuedWorkPriority);
	}

	bool Cancel() final
	{
		return FAsyncTask<FTextureCacheDerivedDataWorker>::IsDone() || FAsyncTask<FTextureCacheDerivedDataWorker>::Cancel();
	}

	void Wait() final
	{
		FAsyncTask<FTextureCacheDerivedDataWorker>::EnsureCompletion();
	}

	bool WaitWithTimeout(float TimeLimitSeconds) final
	{
		return FAsyncTask<FTextureCacheDerivedDataWorker>::WaitCompletionWithTimeout(TimeLimitSeconds);
	}

	bool Poll() const final
	{
		return FAsyncTask<FTextureCacheDerivedDataWorker>::IsWorkDone();
	}

private:
	FQueuedThreadPool* QueuedPool;
};

FTextureAsyncCacheDerivedDataTask* CreateTextureBuildTask(
	UTexture& Texture,
	FTexturePlatformData& DerivedData,
	const FTextureBuildSettings& Settings,
	EQueuedWorkPriority Priority,
	ETextureCacheFlags Flags);

#endif // WITH_EDITOR
