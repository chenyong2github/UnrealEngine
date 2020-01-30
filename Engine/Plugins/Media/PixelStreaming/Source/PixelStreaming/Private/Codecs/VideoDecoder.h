// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "PixelStreamingPrivate.h"
#include "SwTextureSample.h"
#include "WmfMediaHardwareVideoDecodingTextureSample.h"

#include "MediaObjectPool.h"

#include "Containers/Queue.h"
#include "HAL/ThreadSafeBool.h"
#include "HAL/CriticalSection.h"
#include "HAL/Thread.h"
#include "Containers/Array.h"
#include "Templates/UniquePtr.h"
#include "Templates/SharedPointer.h"
#include "Templates/Function.h"

class FEvent;
class IMediaTextureSample;

struct ID3D11Device;
struct ID3D11DeviceContext;
struct IMFDXGIDeviceManager;
struct IDirect3D9;
struct IDirect3DDevice9;
struct IDirect3DDeviceManager9;
struct IMFSample;
struct IMFTransform;

struct FDeviceInfo
{
	TRefCountPtr<ID3D11Device> DxDevice;
	TRefCountPtr<ID3D11DeviceContext> DxDeviceContext;
	TRefCountPtr<IMFDXGIDeviceManager> DxDeviceManager;
	TRefCountPtr<IDirect3D9> Dx9;
	TRefCountPtr<IDirect3DDevice9> Dx9Device;
	TRefCountPtr<IDirect3DDeviceManager9> Dx9DeviceManager;
};

struct FVideoDecoderConfig
{
	uint32 Width = 0;
	uint32 Height = 0;
};

struct FMediaBuffer : public IMediaPoolable
{
	TArray<uint8> Data;
	FTimespan Timestamp;
	FTimespan Duration;
};

class FVideoDecoder : public webrtc::VideoDecoder
{
public:
	static bool CreateDXManagerAndDevice();
	static bool DestroyDXManagerAndDevice();

	// ~BEGIN webrtc::VideoDecoder impl
	int32 InitDecode(const webrtc::VideoCodec* CodecSettings, int32 /*NumberOfCores*/) override;
	int32 Release() override;

	int32 RegisterDecodeCompleteCallback(webrtc::DecodedImageCallback* Callback) override;

	int32 Decode(const webrtc::EncodedImage& InputImage, bool MissingFrames, const webrtc::CodecSpecificInfo* CodecSpecificInfo, int64 RenderTimeMs) override;

	bool PrefersLateDecoding() const override
	{
		return true; // means that the decoder can provide a limited number of output samples
	}

	const char* ImplementationName() const override
	{
		return "PixelStreamingPlayer VideoDecoder";
	}
	// ~END webrtc::VideoDecoder impl

protected:
	struct FOutputFrame
	{
		TRefCountPtr<IMFSample> Sample;
		FTexture2DRHIRef Texture;

		void Reset()
		{
			Sample.SafeRelease();
			Texture.SafeRelease();
		}
	};

	static bool CreateDXGIManagerAndDevice();
	static bool CreateDX9ManagerAndDevice();

	bool Init(const webrtc::VideoCodec* CodecSettings);
	bool Reconfigure();
	bool SetAttributes();
	bool SetInputMediaType();
	bool SetOutputMediaType();
	bool CheckDecoderStatus();
	bool StartStreaming();
	bool QueueBuffer(const webrtc::EncodedImage& InputImage, bool MissingFrames, const webrtc::CodecSpecificInfo* CodecSpecificInfo, int64 RenderTimeMs);
	void DecodeThreadFunc();
	void StopDecoding();
	bool ProcessInput();
	virtual bool ProcessOutputHW(IMFSample* MFSample);
	bool ProcessOutputSW(const FSwTextureSampleRef& TextureSample);
	bool CopyTexture(IMFSample* Sample, const TSharedPtr<FWmfMediaHardwareVideoDecodingTextureSample, ESPMode::ThreadSafe>& OutTexture);
	// used to switch to s/w decoding before decoder is configured (input/output media types)
	bool FallbackToSwDecoding(FString Reason);
	// used to switch to s/w decoding when decoder was already configured
	bool ReconfigureForSwDecoding(FString Reason);

protected:
	FVideoDecoderConfig Config;
	FVideoDecoderConfig NewConfig;

	webrtc::DecodedImageCallback* DecodeCallback = nullptr;

	TQueue<TRefCountPtr<IMFSample>> InputQueue;
	FEvent* InputQueuedEvent = nullptr;
	FThreadSafeCounter InputQueueSize = 0;
	TUniquePtr<FThread> DecodingThread;
	// tells decoding thread to drop all buffered frames and to terminate
	FThreadSafeBool bExitDecodingThread = false;
	// notifies that decoding thread is not blocked and is going to exit
	// a supplement to an ability to "join" decoding thread as joining doesn't allow to specify timeout
	FEvent* ExitingDecodingThreadEvent = nullptr;

	TRefCountPtr<IMFTransform> H264Decoder;
	int32 InputFrameProcessedCount = 0;
	int32 OutputFrameProcessedCount = 0;
	FCriticalSection DecodeCS;

	// is used to fall back to s/w decoding if h/w acceleration is not available or input type is not compatible (usually anything bigger than 4K res)
	bool bIsHardwareAccelerated = true;

	FWmfMediaHardwareVideoDecodingTextureSamplePool HwTextureSamplePool;
	FSwTextureSamplePool SwTextureSamplePool;

	// D3D11 device dedicated for h/w decoding to avoid mutithreading collisions with UE4 rendering device
	static TUniquePtr<FDeviceInfo> s_DeviceInfo;
};

class FVideoDecoderFactory : public webrtc::VideoDecoderFactory
{
public:
	std::vector<webrtc::SdpVideoFormat> GetSupportedFormats() const override;
	std::unique_ptr<webrtc::VideoDecoder> CreateVideoDecoder(const webrtc::SdpVideoFormat& format) override;
};
