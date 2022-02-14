// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Templates/SharedPointer.h"
#include "Templates/RefCounting.h"
#include "Async/Async.h"
#include "RHI.h"
#include "HAL/ThreadSafeBool.h"
#include "Rendering/SlateRenderer.h"
#include "Framework/Application/SlateApplication.h"
#include "IPixelStreamingTextureSource.h"
#include "IPixelStreamingModule.h"

/*
 * Interface to wrap a Texture for use with the TextureSource
 * By extending this class you can add additional operations to a class derived from BackBufferTextureSource
 */

class IPixelStreamingBackBufferTextureWrapper : public FRefCountBase
{
public:
	explicit IPixelStreamingBackBufferTextureWrapper(FTexture2DRHIRef InTexture)
	{
		Texture = InTexture;
	}

	FTexture2DRHIRef& GetTexture()
	{
		return Texture;
	}

private:
	FTexture2DRHIRef Texture;
};

/*
 * Base class for TextureSources that get their textures from the UE backbuffer.
 * Textures are copied from the backbuffer using a triplebuffering mechanism so that texture read access is always thread safe while writes are occurring.
 * If no texture has been written since the last read then the same texture will be read again.
 * This class also has the additional functionality of scaling textures from the backbuffer.
 * Note: This is a template class that uses CRTP - see: https://en.wikipedia.org/wiki/Curiously_recurring_template_pattern
 * For derived types of this class they must contain the following static methods:
 *
 * static void CopyTexture(const FTexture2DRHIRef& SourceTexture, TRefCountPtr<IPixelStreamingBackBufferTextureWrapper> DestTexture, FGPUFenceRHIRef& CopyFence)
 * static TRefCountPtr<IPixelStreamingBackBufferTextureWrapper> CreateTexture(int Width, int Height)
 * static FTexture2DRHIRef ToTextureRef(TRefCountPtr<IPixelStreamingBackBufferTextureWrapper> Texture)
 * static FString& GetName()
 */

class PIXELSTREAMING_API IBackBufferTextureSource : public IPixelStreamingTextureSource, public TSharedFromThis<IBackBufferTextureSource, ESPMode::ThreadSafe>
{
public:
	IBackBufferTextureSource(float InFrameScale);
	IBackBufferTextureSource()
		: IBackBufferTextureSource(1.0) {}

	virtual ~IBackBufferTextureSource();

	TRefCountPtr<IPixelStreamingBackBufferTextureWrapper> GetCurrent();

	// abstract methods for overriding
	virtual void CopyTexture(const FTexture2DRHIRef& SourceTexture, TRefCountPtr<IPixelStreamingBackBufferTextureWrapper> DestTexture, FGPUFenceRHIRef& CopyFence) = 0;
	virtual TRefCountPtr<IPixelStreamingBackBufferTextureWrapper> CreateTexture(int Width, int Height) = 0;
	virtual FString GetName() const = 0;

	/* Begin IPixelStreamingTextureSource interface */
	virtual void SetEnabled(bool bInEnabled) override;
	virtual FTexture2DRHIRef GetTexture() override { return GetCurrent()->GetTexture(); }

	virtual bool IsEnabled() const override { return *bEnabled; }
	virtual bool IsAvailable() const override { return bInitialized; }
	virtual int GetSourceWidth() const override { return SourceWidth; }
	virtual int GetSourceHeight() const override { return SourceHeight; }
	/* End IPixelStreamingTextureSource interface */

	float GetFrameScaling() const { return FrameScale; }

private:
	void OnBackBufferReady_RenderThread(SWindow& SlateWindow, const FTexture2DRHIRef& FrameBuffer);
	void Initialize(int Width, int Height);

	struct FCaptureFrame
	{
		TRefCountPtr<IPixelStreamingBackBufferTextureWrapper> TextureWrapper;
		FGPUFenceRHIRef Fence;
		bool bAvailable = true;
		uint64 PreWaitingOnCopy;
	};

private:
	const float FrameScale;
	int SourceWidth = 0;
	int SourceHeight = 0;
	FThreadSafeBool bInitialized = false;
	TSharedRef<bool, ESPMode::ThreadSafe> bEnabled = MakeShared<bool, ESPMode::ThreadSafe>(true);
	FSlateRenderer::FOnBackBufferReadyToPresent* OnBackbuffer = nullptr;
	FDelegateHandle BackbufferDelegateHandle;
	FCriticalSection DestructorCriticalSection;

	/*
	 * Triple buffer setup with queued write buffers (since we have to wait for RHI copy).
	 * 1 Read buffer (read the captured texture)
	 * 1 Temp buffer (for swapping what is read and written)
	 * 2 Write buffers (2 write buffers because UE can render two frames before presenting sometimes)
	 */
	FCriticalSection CriticalSection;
	bool bWriteParity = true;
	FCaptureFrame WriteBuffers[2];
	TRefCountPtr<IPixelStreamingBackBufferTextureWrapper> TempBuffer;
	TRefCountPtr<IPixelStreamingBackBufferTextureWrapper> ReadBuffer;
	FThreadSafeBool bIsTempDirty;
};

/*
 * Basic IPixelStreamingBackBufferTextureWrapper for use with FBackBufferTextureSource
 */

class PIXELSTREAMING_API FPixelStreamingRHIBackBufferTexture : public IPixelStreamingBackBufferTextureWrapper
{
public:
	FPixelStreamingRHIBackBufferTexture(FTexture2DRHIRef InTexture)
		: IPixelStreamingBackBufferTextureWrapper(InTexture) {}
};

class PIXELSTREAMING_API FBackBufferTextureSource : public IBackBufferTextureSource
{
public:
	FBackBufferTextureSource()
		: IBackBufferTextureSource() {}
	FBackBufferTextureSource(float InScale)
		: IBackBufferTextureSource(InScale) {}

	virtual void CopyTexture(const FTexture2DRHIRef& SourceTexture, TRefCountPtr<IPixelStreamingBackBufferTextureWrapper> DestTexture, FGPUFenceRHIRef& CopyFence) override;
	virtual TRefCountPtr<IPixelStreamingBackBufferTextureWrapper> CreateTexture(int Width, int Height) override;

	virtual FString GetName() const override { return TEXT("FBackBufferTextureSource"); }
};

/*
 * Extention for IPixelStreamingBackBufferTextureWrapper which also stores a CPU accessible memory address of the texture
 */
class PIXELSTREAMING_API FPixelStreamingCPUReadableBackbufferTexture : public IPixelStreamingBackBufferTextureWrapper
{
public:
	FPixelStreamingCPUReadableBackbufferTexture(FTexture2DRHIRef InTexture)
		: IPixelStreamingBackBufferTextureWrapper(InTexture) {}

	TArray<FColor>& GetRawPixels()
	{
		return RawPixels;
	}

private:
	TArray<FColor> RawPixels;
};

class PIXELSTREAMING_API FBackBufferToCPUTextureSource : public IBackBufferTextureSource
{
public:
	FBackBufferToCPUTextureSource()
		: IBackBufferTextureSource() {}
	FBackBufferToCPUTextureSource(float InScale)
		: IBackBufferTextureSource(InScale) {}

	virtual void CopyTexture(const FTexture2DRHIRef& SourceTexture, TRefCountPtr<IPixelStreamingBackBufferTextureWrapper> DestTexture, FGPUFenceRHIRef& CopyFence) override;
	virtual TRefCountPtr<IPixelStreamingBackBufferTextureWrapper> CreateTexture(int Width, int Height) override;

	virtual FString GetName() const override { return TEXT("FBackBufferToCPUTextureSource"); }
};