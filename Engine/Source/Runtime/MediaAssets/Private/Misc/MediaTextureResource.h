// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Containers/Queue.h"
#include "Math/Color.h"
#include "MediaSampleSource.h"
#include "Misc/Guid.h"
#include "Misc/Timespan.h"
#include "Templates/RefCounting.h"
#include "Templates/SharedPointer.h"
#include "TextureResource.h"
#include "UnrealClient.h"
#include "IMediaTimeSource.h"
#include "RHIResources.h"
#include "Async/Async.h"
#include "RenderingThread.h"
#include "RendererInterface.h"

class FMediaPlayerFacade;
class IMediaPlayer;
class IMediaTextureSample;
class UMediaTexture;
struct FGenerateMipsStruct;

enum class EMediaTextureSinkFormat;
enum class EMediaTextureSinkMode;

#if PLATFORM_WINDOWS || (defined(PLATFORM_PS4) && PLATFORM_PS4) || (defined(PLATFORM_PS5) && PLATFORM_PS5)
#define USE_LIMITED_FENCEWAIT	1
#else
#define USE_LIMITED_FENCEWAIT	0
#endif

#if USE_LIMITED_FENCEWAIT
static const double MaxWaitForFence = 2.0;	// HACK: wait a max of 2s for a GPU fence, then assume we will never see it signal & pretent it did signal
#endif

/**
 * Texture resource type for media textures.
 */
class FMediaTextureResource
	: public FRenderTarget
	, public FTextureResource
{
public:

	/** 
	 * Creates and initializes a new instance.
	 *
	 * @param InOwner The Movie texture object to create a resource for (must not be nullptr).
	 * @param InOwnerDim Reference to the width and height of the texture that owns this resource (will be updated by resource).
	 * @param InOWnerSize Reference to the size in bytes of the texture that owns this resource (will be updated by resource).
	 * @param InClearColor The initial clear color.
	 * @param InTextureGuid The initial external texture GUID.
	 * @param bEnableGenMips If true mips generation will be enabled (possibly optimizing for NumMips == 1 case)
	 * @param InNumMips The initial number of mips to be generated for the output texture
	 */
	FMediaTextureResource(UMediaTexture& InOwner, FIntPoint& InOwnerDim, SIZE_T& InOwnerSize, FLinearColor InClearColor, FGuid InTextureGuid, bool bEnableGenMips, uint8 InNumMips);

	/** Virtual destructor. */
	virtual ~FMediaTextureResource() 
	{
	}

public:

	/** Parameters for the Render method. */
	struct FRenderParams
	{
		/** Whether the texture can be cleared. */
		bool CanClear;

		/** The clear color to use when clearing the texture. */
		FLinearColor ClearColor;

		/** The texture's current external texture GUID. */
		FGuid CurrentGuid;

		/** The texture's previously used external texture GUID. */
		FGuid PreviousGuid;

		/** The player's play rate. */
		float Rate;

		/** The player facade that provides the video samples to render. */
		TWeakPtr<FMediaTextureSampleSource, ESPMode::ThreadSafe> SampleSource;

		/** Whether output should be in sRGB color space. */
		bool SrgbOutput;

		/** Number of mips wanted */
		uint8 NumMips;

		/** The time of the video frame to render (in player's clock). */
		FMediaTimeStamp Time;

		/** Explicit texture sample to render - if set time will be ignored */
		TSharedPtr<IMediaTextureSample, ESPMode::ThreadSafe> TextureSample;
	};

	/**
	 * Render the texture resource.
	 *
	 * This method is called on the render thread by the MediaTexture that owns this
	 * texture resource to clear or redraw the resource using the given parameters.
	 *
	 * @param Params Render parameters.
	 */
	void Render(const FRenderParams& Params);

	/**
	 * Flush out any pending data like texture samples waiting for retirement etc.
	 * @note this call can stall for noticable amounts of time under certain circumstances
	 */
	void FlushPendingData();

public:

	//~ FRenderTarget interface

	virtual FIntPoint GetSizeXY() const override;

public:

	//~ FTextureResource interface

	virtual FString GetFriendlyName() const override;
	virtual uint32 GetSizeX() const override;
	virtual uint32 GetSizeY() const override;
	virtual void InitDynamicRHI() override;
	virtual void ReleaseDynamicRHI() override;

protected:

	/**
	 * Clear the texture using the given clear color.
	 *
	 * @param ClearColor The clear color to use.
	 * @param SrgbOutput Whether the output texture is in sRGB color space.
	 */
	void ClearTexture(const FLinearColor& ClearColor, bool SrgbOutput);

	/**
	 * Render the given texture sample by converting it on the GPU.
	 *
	 * @param Sample The texture sample to convert.
	 * @param ClearColor The clear color to use for the output texture.
	 * @param SrgbOutput Whether the output texture is in sRGB color space.
	 * @param Number of mips
	 * @see CopySample
	 */
	void ConvertSample(const TSharedPtr<IMediaTextureSample, ESPMode::ThreadSafe>& Sample, const FLinearColor& ClearColor, bool SrgbOutput, uint8 InNumMips);

	void ConvertTextureToOutput(FRHITexture2D* InputTexture, const TSharedPtr<IMediaTextureSample, ESPMode::ThreadSafe>& Sample, bool SrgbOutput);

	/**
	 * Render the given texture sample by using it as or copying it to the render target.
	 *
	 * @param Sample The texture sample to copy.
	 * @param ClearColor The clear color to use for the output texture.
	 * @param SrgbOutput Whether the output texture is in sRGB color space.
	 * @param Number of mips
	 * @see ConvertSample
	 */
	void CopySample(const TSharedPtr<IMediaTextureSample, ESPMode::ThreadSafe>& Sample, const FLinearColor& ClearColor, bool SrgbOutput, uint8 InNumMips, const FGuid & TextureGUID);

	/** Calculates the current resource size and notifies the owner texture. */
	void UpdateResourceSize();

	/**
	 * Set the owner's texture reference to the given texture.
	 *
	 * @param NewTexture The texture to set.
	 */
	void UpdateTextureReference(FRHITexture2D* NewTexture);

	/**
	 * Create/update output render target as needed
	 */
	void CreateOutputRenderTarget(const FIntPoint & InDim, EPixelFormat InPixelFormat, bool bInSRGB, const FLinearColor & InClearColor, uint8 InNumMips);

	/**
	 * Caches next available sample from queue in MediaTexture owner to keep single consumer access
	 *
	 * @param InSampleQueue SampleQueue to query sample information from
	 */
	void CacheNextAvailableSampleTime(const TSharedPtr<FMediaTextureSampleSource, ESPMode::ThreadSafe>& InSampleQueue) const;

	/** Setup sampler state from owner's settings as needed */
	void SetupSampler();

	/** Copy to local buffer from external texture */
	void CopyFromExternalTexture(const TSharedPtr <IMediaTextureSample, ESPMode::ThreadSafe>& Sample, const FGuid & TextureGUID);

	bool RequiresConversion(const TSharedPtr<IMediaTextureSample, ESPMode::ThreadSafe>& Sample, bool SrgbOutput, uint8 numMips) const;
	bool RequiresConversion(const FTexture2DRHIRef& SampleTexture, const FIntPoint & OutputDim, bool SrgbOutput, uint8 numMips) const;

private:

	/** Platform uses GL/ES ImageExternal */
	bool bUsesImageExternal;

	/** Whether the texture has been cleared. */
	bool Cleared;

	/** Tracks the current clear color. */
	FLinearColor CurrentClearColor;

	/** The external texture GUID to use when initializing this resource. */
	FGuid InitialTextureGuid;

	/** Input render target if the texture samples don't provide one (for conversions). */
	TRefCountPtr<FRHITexture2D> InputTarget;

	/** Output render target if the texture samples don't provide one. */
	TRefCountPtr<FRHITexture2D> OutputTarget;

	/** The media texture that owns this resource. */
	UMediaTexture& Owner;

	/** Reference to the owner's texture dimensions field. */
	FIntPoint& OwnerDim;

	/** Reference to the owner's texture size field. */
	SIZE_T& OwnerSize;

	/** Enable mips generation */
	bool bEnableGenMips;

	/** Current number of mips to be generated as output */
	uint8 CurrentNumMips;

	/** Current texture sampler filter value */
	ESamplerFilter CurrentSamplerFilter;

	/** The current media player facade to get video samples from. */
	TWeakPtr<FMediaPlayerFacade, ESPMode::ThreadSafe> PlayerFacadePtr;

	/** cached media sample to postpone releasing it until the next sample rendering as it can get overwritten due to asynchronous rendering */
	TSharedPtr<IMediaTextureSample, ESPMode::ThreadSafe> CurrentSample;

	/** prior samples not yet ready for retirement as GPU may still actively use them */
	template<typename ObjectRefType> struct TGPUsyncedDataDeleter
	{
		~TGPUsyncedDataDeleter()
		{
			Flush();
		}

		void Retire(const ObjectRefType& Object)
		{
			FRHICommandListImmediate& CommandList = FRHICommandListExecutor::GetImmediateCommandList();

			// Prep "retirement package"
			FRetiringObjectInfo Info;
			Info.Object = Object;
			Info.GPUFence = CommandList.CreateGPUFence(TEXT("MediaTextureResourceReuseFence"));
			Info.RetireTime = FPlatformTime::Seconds();

			// Insert fence. We assume that GPU-workload-wise this marks the spot usage of the sample is done
			CommandList.WriteGPUFence(Info.GPUFence);

			// Recall for later checking...
			FScopeLock Lock(&CS);
			Objects.Push(Info);
		}

		bool Update()
		{
			FScopeLock Lock(&CS);

			// Check for any retired samples that are not done being touched by the GPU...
			int32 Idx = 0;
			for (; Idx < Objects.Num(); ++Idx)
			{
#if USE_LIMITED_FENCEWAIT
				double Now = FPlatformTime::Seconds();
#endif

				// Either no fence present or the fence has been signaled?
				if (Objects[Idx].GPUFence.IsValid() && !Objects[Idx].GPUFence->Poll())
				{
					// No. This one is still busy, we can stop...

#if USE_LIMITED_FENCEWAIT
					// HACK: But how long has this been going on? Might we have a fence that never will signal?
					if ((Now - Objects[Idx].RetireTime) < MaxWaitForFence)
#else
					if (1)
#endif
					{
						break;
					}
				}
			}
			// Remove (hence return to the pool / free up fence) all the finished ones...
			if (Idx != 0)
			{
				Objects.RemoveAt(0, Idx);
			}
			return Objects.Num() != 0;
		}

		void Flush()
		{
			// See if all samples are ready to be retired now...
			if (!Update())
			{
				// They are. No need for any async task...
				return;
			}

			// Some samples still need the GPU to get done. Use async task to get this done...
			TFunction<void()> FlushTask = [LastObjects{ MoveTemp(Objects) }]()
			{
				while (1)
				{
#if USE_LIMITED_FENCEWAIT
					double Now = FPlatformTime::Seconds();
#endif
					int32 Idx = 0;
					for (; Idx < LastObjects.Num(); ++Idx)
					{
						// Still not signaled?
						if (LastObjects[Idx].GPUFence.IsValid() && !LastObjects[Idx].GPUFence->Poll())
						{
#if USE_LIMITED_FENCEWAIT
							// HACK: But how long has this been going on? Might we have a fence that never will signal?
							if ((Now - LastObjects[Idx].RetireTime) < MaxWaitForFence)
#else
							if (1)
#endif
							{
								break;
							}
						}
					}
					if (Idx == LastObjects.Num())
					{
						break;
					}

					FPlatformProcess::Sleep(5.0f / 1000.0f);
				}
			};
			Async(EAsyncExecution::ThreadPool, MoveTemp(FlushTask));
		}

		struct FRetiringObjectInfo
		{
			ObjectRefType Object;
			FGPUFenceRHIRef GPUFence;
			double RetireTime;
		};

		TArray<FRetiringObjectInfo> Objects;
		FCriticalSection CS;
	};

	typedef TGPUsyncedDataDeleter<TSharedPtr<IMediaTextureSample, ESPMode::ThreadSafe>> FPriorSamples;

	TSharedRef<FPriorSamples, ESPMode::ThreadSafe> PriorSamples;

	/** cached params etc. for use with mip generator */
	TRefCountPtr<IPooledRenderTarget> MipGenerationCache;
};
