// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "VideoDecoder.h"
#include "VideoSink.h"
#include "Utils.h"
#include "HUDStats.h"

#include "ShaderCore.h"
#include "D3D11State.h"
#include "D3D11Resources.h"
#include "Async/Async.h"
#include "Misc/ScopeLock.h"
#include "Templates/AlignmentTemplates.h"
#include "HAL/Event.h"
#include "HAL/PlatformProcess.h"
#include "RHI.h"
#include "Logging/LogMacros.h"
#include "Modules/ModuleManager.h"

DECLARE_LOG_CATEGORY_EXTERN(LogVideoDecoder, Log, All);
DEFINE_LOG_CATEGORY(LogVideoDecoder);

const D3DFORMAT DX9_NV12_FORMAT = (D3DFORMAT)MAKEFOURCC('N', 'V', '1', '2');

inline ID3D11Device* GetUE4DxDevice()
{
	return static_cast<ID3D11Device*>(GDynamicRHI->RHIGetNativeDevice());
}

TUniquePtr<FDeviceInfo> FVideoDecoder::s_DeviceInfo;

int32 FVideoDecoder::InitDecode(const webrtc::VideoCodec* CodecSettings, int32 /*NumberOfCores*/)
{
	return Init(CodecSettings) ? WEBRTC_VIDEO_CODEC_OK : WEBRTC_VIDEO_CODEC_ERROR;
}

bool FVideoDecoder::Init(const webrtc::VideoCodec* CodecSettings)
{
	check(CodecSettings);
	checkf(CodecSettings->codecType == webrtc::kVideoCodecH264, TEXT("%d"), static_cast<int32>(CodecSettings->codecType));

	const TCHAR* Profiles[] = {
		TEXT("ProfileConstrainedBaseline"),
		TEXT("ProfileBaseline"),
		TEXT("ProfileMain"),
		TEXT("ProfileConstrainedHigh"),
		TEXT("ProfileHigh")
	};
	const webrtc::VideoCodecH264& H264 = CodecSettings->H264();
	UE_LOG(LogVideoDecoder, Verbose, TEXT("InitDecode: %dX%d, plType = %d, bitrate: start = %d, max = %d, min = %d, target = %d\nmax FPS = %d, max QP = %d\nH.264: frame dropping = %d, key frame interval = %d, profile = %s"), CodecSettings->width, CodecSettings->height, CodecSettings->plType, CodecSettings->startBitrate, CodecSettings->maxBitrate, CodecSettings->minBitrate, CodecSettings->targetBitrate, CodecSettings->maxFramerate, CodecSettings->qpMax, H264.frameDroppingOn, H264.keyFrameInterval, Profiles[H264.profile]);

	Config = { CodecSettings->width, CodecSettings->height };
	NewConfig = Config;

	InputQueuedEvent = FPlatformProcess::GetSynchEventFromPool();
	ExitingDecodingThreadEvent = FPlatformProcess::GetSynchEventFromPool();

	CHECK_HR(CoCreateInstance(CLSID_CMSH264DecoderMFT, nullptr, CLSCTX_INPROC_SERVER, IID_IMFTransform, reinterpret_cast<void**>(&H264Decoder)));

	if (!SetAttributes() ||
		!Reconfigure() ||
		!StartStreaming())
	{
		H264Decoder = nullptr;
		return false;
	}

	DecodingThread = MakeUnique<FThread>(TEXT("PixerStreamingPlayer Decoding"), [this]()
		{
			DecodeThreadFunc();
		});

	return true;
}

int32 FVideoDecoder::Release()
{
	StopDecoding();

	FPlatformProcess::ReturnSynchEventToPool(ExitingDecodingThreadEvent);
	FPlatformProcess::ReturnSynchEventToPool(InputQueuedEvent);

	UE_LOG(LogVideoDecoder, Verbose, TEXT("VideoDecoder destroyed"));

	return WEBRTC_VIDEO_CODEC_OK;
}

int32 FVideoDecoder::RegisterDecodeCompleteCallback(webrtc::DecodedImageCallback* Callback)
{
	check(Callback);
	DecodeCallback = Callback;

	return WEBRTC_VIDEO_CODEC_OK;
}

bool FVideoDecoder::Reconfigure()
{
	return SetInputMediaType() && SetOutputMediaType() && CheckDecoderStatus();
}

bool FVideoDecoder::SetAttributes()
{
	TComPtr<IMFAttributes> Attributes;
	CHECK_HR(H264Decoder->GetAttributes(&Attributes));

	// w/o "low latency" settings the first output happens with huge lag of > 1s (36-38 frames on NVIDIA GPU)
	CHECK_HR(Attributes->SetUINT32(MF_LOW_LATENCY, true));

	if (IsWindows8Plus())
	{
		uint32 bDx11Aware = 0;
		HRESULT HRes = Attributes->GetUINT32(MF_SA_D3D11_AWARE, &bDx11Aware);

		if (FAILED(HRes))
		{
			return FallbackToSwDecoding(TEXT("Failed to get MF_SA_D3D11_AWARE"));
		}
		else if (bDx11Aware == 0)
		{
			return FallbackToSwDecoding(TEXT("Not MF_SA_D3D11_AWARE"));
		}
		else if (FAILED(HRes = H264Decoder->ProcessMessage(MFT_MESSAGE_SET_D3D_MANAGER, reinterpret_cast<ULONG_PTR>(s_DeviceInfo->DxDeviceManager.GetReference()))))
		{
			return FallbackToSwDecoding(FString::Printf(TEXT("Failed to set MFT_MESSAGE_SET_D3D_MANAGER: 0x%X %s"), HRes, *GetComErrorDescription(HRes)));
		}
	}
	else // Windows 7
	{
		if (!s_DeviceInfo->Dx9Device || !s_DeviceInfo->Dx9DeviceManager)
		{
			return FallbackToSwDecoding(TEXT("Failed to create DirectX 9 device / device manager"));
		}

		uint32 bD3DAware = 0;
		HRESULT HRes = Attributes->GetUINT32(MF_SA_D3D_AWARE, &bD3DAware);

		if (FAILED(HRes))
		{
			return FallbackToSwDecoding(TEXT("Failed to get MF_SA_D3D_AWARE"));
		}
		else if (bD3DAware == 0)
		{
			return FallbackToSwDecoding(TEXT("Not MF_SA_D3D_AWARE"));
		}
		else if (FAILED(HRes = H264Decoder->ProcessMessage(MFT_MESSAGE_SET_D3D_MANAGER, reinterpret_cast<ULONG_PTR>(s_DeviceInfo->Dx9DeviceManager.GetReference()))))
		{
			return FallbackToSwDecoding(FString::Printf(TEXT("Failed to set MFT_MESSAGE_SET_D3D_MANAGER: 0x%X %s"), HRes, *GetComErrorDescription(HRes)));
		}
	}

	return true;
}

bool FVideoDecoder::SetInputMediaType()
{
	TRefCountPtr<IMFMediaType> InputMediaType;
	CHECK_HR(MFCreateMediaType(InputMediaType.GetInitReference()));
	CHECK_HR(InputMediaType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video));
	CHECK_HR(InputMediaType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264));
	CHECK_HR(MFSetAttributeSize(InputMediaType, MF_MT_FRAME_SIZE, Config.Width, Config.Height));
	// https://docs.microsoft.com/en-us/windows/desktop/medfound/h-264-video-decoder
	CHECK_HR(InputMediaType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_MixedInterlaceOrProgressive));

	HRESULT HRes = H264Decoder->SetInputType(0, InputMediaType, 0);
	if (bIsHardwareAccelerated && HRes == MF_E_UNSUPPORTED_D3D_TYPE)
		// h/w acceleration is not supported, e.g. unsupported resolution (4K), fall back to s/w decoding
	{
		return ReconfigureForSwDecoding(TEXT("MF_E_UNSUPPORTED_D3D_TYPE"));
	}
	else if (FAILED(HRes))
	{
		UE_LOG(LogVideoDecoder, Error, TEXT("H264Decoder->SetInputType failed: 0x%X %s"), HRes, *GetComErrorDescription(HRes));
		return false;
	}

	return true;
}

bool FVideoDecoder::SetOutputMediaType()
{
	TRefCountPtr<IMFMediaType> OutputMediaType;

	// Calling H264Decoder->GetOutputAvailableType returns following output media subtypes:
	// MFVideoFormat_NV12
	// MFVideoFormat_YV12
	// MFVideoFormat_IYUV
	// MFVideoFormat_I420
	// MFVideoFormat_YUY2
	for (int32_t TypeIndex = 0; ; ++TypeIndex)
	{
		CHECK_HR(H264Decoder->GetOutputAvailableType(0, TypeIndex, OutputMediaType.GetInitReference()));

		GUID OutputMediaMajorType, OutputMediaSubtype;
		CHECK_HR(OutputMediaType->GetGUID(MF_MT_MAJOR_TYPE, &OutputMediaMajorType));
		CHECK_HR(OutputMediaType->GetGUID(MF_MT_SUBTYPE, &OutputMediaSubtype));
		if (OutputMediaMajorType == MFMediaType_Video && OutputMediaSubtype == MFVideoFormat_NV12)
		{
			break;
		}
	}

	CHECK_HR(H264Decoder->SetOutputType(0, OutputMediaType, 0));

	return true;
}

bool FVideoDecoder::CheckDecoderStatus()
{
	DWORD NumInputStreams, NumOutputStreams;
	CHECK_HR(H264Decoder->GetStreamCount(&NumInputStreams, &NumOutputStreams));
	if (NumInputStreams != 1 || NumOutputStreams != 1)
	{
		UE_LOG(LogVideoDecoder, Error, TEXT("Unexpected number of streams: input %d, output %d"), NumInputStreams, NumOutputStreams);
		return false;
	}

	DWORD DecoderStatus = 0;
	CHECK_HR(H264Decoder->GetInputStatus(0, &DecoderStatus));
	if (MFT_INPUT_STATUS_ACCEPT_DATA != DecoderStatus)
	{
		UE_LOG(LogVideoDecoder, Error, TEXT("Decoder doesn't accept data, status %d"), DecoderStatus);
		return false;
	}

	MFT_OUTPUT_STREAM_INFO OutputStreamInfo;
	CHECK_HR(H264Decoder->GetOutputStreamInfo(0, &OutputStreamInfo));
	if (!(OutputStreamInfo.dwFlags & MFT_OUTPUT_STREAM_FIXED_SAMPLE_SIZE))
	{
		return ReconfigureForSwDecoding(TEXT("Incompatible H.264 decoder: fixed sample size expected"));
	}
	if (!(OutputStreamInfo.dwFlags & MFT_OUTPUT_STREAM_WHOLE_SAMPLES))
	{
		return ReconfigureForSwDecoding(TEXT("Incompatible H.264 decoder: whole samples expected"));
	}
	if (bIsHardwareAccelerated && !(OutputStreamInfo.dwFlags & MFT_OUTPUT_STREAM_PROVIDES_SAMPLES))
	{
		// theoretically we can handle this situation with H/W decoder, but we can't reproduce it locally for testing so we aren't sure if H/W 
		// decoder would work in this case
		return ReconfigureForSwDecoding(TEXT("Incompatible H.264 decoder: h/w accelerated decoder is expected to provide output samples"));
	}
	if (!bIsHardwareAccelerated && (OutputStreamInfo.dwFlags & MFT_OUTPUT_STREAM_PROVIDES_SAMPLES))
	{
		UE_LOG(LogVideoDecoder, Warning, TEXT("Incompatible H.264 decoder: s/w decoder is expected to require preallocated output samples"));
		return false;
	}

	return true;
}

bool FVideoDecoder::FallbackToSwDecoding(FString Reason)
{
#if PLATFORM_XBOXONE
	return false; // s/w decoding is not supported on xbox yet
#else
	if (!bIsHardwareAccelerated)
	{
		// we've already tried to switch to s/w mode. whatever went wrong should have been already reported
		return false;
	}

	UE_LOG(LogVideoDecoder, Warning, TEXT("Falling back to s/w decoding: %s"), *Reason);

	bIsHardwareAccelerated = false;

	if (IsWindows8Plus())
	{
		// NOTE: the following doesn't apply to Windows 7 as it doesn't use DX11 device in decoding thread
		// as we don't use a dedicated DirextX device for s/w decoding, UE4's rendering device will be used from inside the decoder
		// to produce output samples, which means access from render and decoding threads. We need to enable multithread protection
		// for the device. Multithread protection can have performance impact, though its affect is expected to be negligible in most cases.
		// WARNING:
		// Once multithread protection is enabled we don't disable it, so UE4's rendering device stays protected for the rest of its lifetime.
		// Some other system could enable multithread protection after we did it, we have no means to know about this, and so disabling it
		// at the end of playback can cause GPU driver crash
		TComPtr<ID3D10Multithread> DxMultithread;
		CHECK_HR(DxMultithread.FromQueryInterface(__uuidof(ID3D10Multithread), GetUE4DxDevice()));
		DxMultithread->SetMultithreadProtected(1);
	}

	return true;
#endif
}

bool FVideoDecoder::ReconfigureForSwDecoding(FString Reason)
{
	if (!FallbackToSwDecoding(MoveTemp(Reason)))
	{
		return false;
	}

	// nullified previously set D3D Manager. This switches decoder to s/w mode.
	CHECK_HR(H264Decoder->ProcessMessage(MFT_MESSAGE_SET_D3D_MANAGER, 0));

	return Reconfigure();
}

bool FVideoDecoder::StartStreaming()
{
	// Signal decoder ready to decode
	CHECK_HR(H264Decoder->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0));
	CHECK_HR(H264Decoder->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0));

	return true;
}

int32 FVideoDecoder::Decode(const webrtc::EncodedImage& InputImage, bool MissingFrames, const webrtc::CodecSpecificInfo* CodecSpecificInfo, int64 RenderTimeMs)
{
	UE_LOG(LogVideoDecoder, VeryVerbose, TEXT("(%d) Decode: %dx%d, ts %u, ntp %lld, capture %lld, %s, length/size %zu/%zu, %s, %d"), RtcTimeMs(), InputImage._encodedWidth, InputImage._encodedHeight, InputImage.Timestamp(), InputImage.ntp_time_ms_, InputImage.capture_time_ms_, ToString(InputImage._frameType), InputImage._length, InputImage._size, InputImage._completeFrame ? TEXT("complete") : TEXT("incomplete"), InputImage.qp_);

	const auto& Timing = InputImage.timing_;
	UE_LOG(LogVideoDecoder, VeryVerbose, TEXT("timing: flags %u, encode start %lld, encode finish %lld, packetization finish %lld, pacer exit %lld, network timestamp %lld, network2 timestamp %lld, receive start %lld, receive finish %lld"), Timing.flags, Timing.encode_start_ms, Timing.encode_finish_ms, Timing.packetization_finish_ms, Timing.pacer_exit_ms, Timing.network_timestamp_ms, Timing.network2_timestamp_ms, Timing.receive_start_ms, Timing.receive_finish_ms);

	UE_LOG(LogVideoDecoder, VeryVerbose, TEXT("missing frames %d, render_ts_ms %lld"), MissingFrames, RenderTimeMs);

	return QueueBuffer(InputImage, MissingFrames, CodecSpecificInfo, RenderTimeMs) ? WEBRTC_VIDEO_CODEC_OK : WEBRTC_VIDEO_CODEC_ERROR;
}

bool FVideoDecoder::QueueBuffer(const webrtc::EncodedImage& InputImage, bool MissingFrames, const webrtc::CodecSpecificInfo* CodecSpecificInfo, int64 RenderTimeMs)
{
	// do this synchronously to make a copy of input data

	TRefCountPtr<IMFMediaBuffer> MediaBuffer;

	int64 CaptureTs = 0;
	SIZE_T BufferSize = InputImage._length;
	if (FHUDStats::Get().bEnabled)
	{
		BufferSize -= sizeof(CaptureTs); // capture timestamp is appended to encoded frame
		CaptureTs = *reinterpret_cast<const int64*>(InputImage._buffer + BufferSize);
	}

	CHECK_HR(MFCreateMemoryBuffer(BufferSize, MediaBuffer.GetInitReference()));

	// Copy frame data into MF media buffer
	BYTE* MediaBufferPtr = nullptr;
	CHECK_HR(MediaBuffer->Lock(&MediaBufferPtr, nullptr, nullptr));
	FMemory::Memcpy(MediaBufferPtr, InputImage._buffer, BufferSize);
	CHECK_HR(MediaBuffer->Unlock());

	// Update MF media buffer length
	CHECK_HR(MediaBuffer->SetCurrentLength(BufferSize));

	TRefCountPtr<IMFSample> Sample;
	CHECK_HR(MFCreateSample(Sample.GetInitReference()));
	CHECK_HR(Sample->AddBuffer(MediaBuffer));
	// don't bother converting 90KHz -> 10MHz, decoder doesn't care and we can lose precision on convertion back and forth
	CHECK_HR(Sample->SetSampleTime(InputImage.Timestamp()));
	// to pass capture timestamp through decoder we set it as sample duration as we don't use duration
	CHECK_HR(Sample->SetSampleDuration(CaptureTs));

	UE_LOG(LogVideoDecoder, VeryVerbose, TEXT("(%d) enqueueing sample ts %u, capture ts %lld, queue size %d"), RtcTimeMs(), InputImage.Timestamp(), CaptureTs, InputQueueSize.GetValue() + 1);
	verify(InputQueue.Enqueue(Sample));
	InputQueueSize.Increment();
	InputQueuedEvent->Trigger();

	return true;
}

void FVideoDecoder::DecodeThreadFunc()
{
	LLM_SCOPE(ELLMTag::VideoStreaming);
	// first checks if decoder has output and only if it asks for more input the input is provided
	// this way we work around decoder hanging if all samples from its internal pool are in use (h/w decoder)

	HRESULT HResult;

	while (!bExitDecodingThread)
	{
		DWORD Status;
		MFT_OUTPUT_DATA_BUFFER OutputDataBuffer = {};

		FSwTextureSamplePtr SwTextureSample;

		if (!bIsHardwareAccelerated)
		{
			// s/w decoder requires preallocated samples
			SwTextureSample = SwTextureSamplePool.AcquireShared();
			if (!SwTextureSample->Init({ static_cast<int32>(Config.Width), static_cast<int32>(Config.Height) }))
			{
				break;
			}
			OutputDataBuffer.pSample = SwTextureSample->GetMFSample();
		}

		HResult = H264Decoder->ProcessOutput(0, 1, &OutputDataBuffer, &Status);

		if (OutputDataBuffer.pEvents)
		{
			// https://docs.microsoft.com/en-us/windows/desktop/api/mftransform/nf-mftransform-imftransform-processoutput
			// The caller is responsible for releasing any events that the MFT allocates.
			OutputDataBuffer.pEvents->Release();
			OutputDataBuffer.pEvents = nullptr;
		}

		if (HResult == MF_E_TRANSFORM_NEED_MORE_INPUT)
		{
			//UE_LOG(LogVideoDecoder, VeryVerbose, TEXT("ProcessOutput needs more input"));

			if (Config.Width != NewConfig.Width || Config.Height != NewConfig.Height)
			{
				UE_LOG(LogVideoDecoder, Verbose, TEXT("ProcessOutput reconfiguration"));
				Config = NewConfig;
				// draining completed, decoder's buffer is empty, now we can reconfigure decoder input type
				SetInputMediaType();
				SetOutputMediaType(); // required after setting input media type
			}

			if (InputQueue.IsEmpty())
			{
				InputQueuedEvent->Wait();
				// just go full cycle after this (doesn't take long) which either results in `ProcessInput()` call below
				// or exits decoding thread if requested
			}
			else
			{
				if (!ProcessInput())
				{
					return;
				}
			}
		}
		else if (HResult == MF_E_TRANSFORM_STREAM_CHANGE)
		{
			if (OutputDataBuffer.dwStatus & MFT_OUTPUT_DATA_BUFFER_FORMAT_CHANGE)
			{
				if (!SetOutputMediaType())
				{
					break;
				}
			}
			else
			{
				UE_LOG(LogVideoDecoder, Error, TEXT("MF_E_TRANSFORM_STREAM_CHANGE"));
				return;
			}
		}
		else if (SUCCEEDED(HResult))
		{
			//UE_LOG(LogWmfVideoDecoder, VeryVerbose, TEXT("ProcessOutput: %d %s, status %d"), HResult, *GetComErrorDescription(HResult), OutputDataBuffer.dwStatus);
			check(OutputDataBuffer.dwStatus != MFT_OUTPUT_DATA_BUFFER_INCOMPLETE);

			if (!OutputDataBuffer.pSample)
			{
				UE_LOG(LogVideoDecoder, Log, TEXT("ProcessOutput returned empty sample: %d %s"), HResult, *GetComErrorDescription(HResult));
				continue; // no data, probably draining at the end of stream
			}

			if (bIsHardwareAccelerated)
			{
				if (!ProcessOutputHW(OutputDataBuffer.pSample))
				{
					break;
				}
			}
#if PLATFORM_WINDOWS
			else
			{
				if (!ProcessOutputSW(SwTextureSample.ToSharedRef()))
				{
					break;
				}
			}
#endif
		}
		else
		{
			UE_LOG(LogVideoDecoder, Error, TEXT("ProcessOutput failed: 0x%X %s"), HResult, *GetComErrorDescription(HResult));
			return;
		}
	}

	// drop buffered frames if any and stop internal decoder processing
	HResult = H264Decoder->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, 0);
	if (FAILED(HResult))
	{
		UE_LOG(LogVideoDecoder, Error, TEXT("Flushing on decoding thread exit failed: %d %s"), HResult, *GetComErrorDescription(HResult));
	}

	UE_LOG(LogVideoDecoder, Verbose, TEXT("Decoding thread exit"));

	// notify that the thread is not stuck and is going to exit immediately
	ExitingDecodingThreadEvent->Trigger();
}

void FVideoDecoder::StopDecoding()
{
	if (DecodingThread.IsValid())
	{
		// stop decoding immediately w/o flushing
		bExitDecodingThread = true;
		InputQueuedEvent->Trigger(); // decoding thread can wait for input, release it

		// Sometimes, for unknown reason, decoding thread can get stuck in `H264Decoder->ProcessOutput` call.
		// This was never reproduced locally (https://jira.it.epicgames.net/browse/FORT-194183)
		// signal the decoder to halt whatever it's doing, trying to unblock it, though we don't know if this helps
		HRESULT HResult = H264Decoder->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, 0);
		if (FAILED(HResult))
		{
			UE_LOG(LogVideoDecoder, Warning, TEXT("Flushing on stopping decoding failed: %d %s"), HResult, *GetComErrorDescription(HResult));
		}

		// if ^^ doesn't help, we still need to make sure we are not stuck on joining the decoding thread, so wait for its signal that it's not
		// blocked. 
		if (!ExitingDecodingThreadEvent->Wait(FTimespan::FromSeconds(1)))
		{
			// the following (commented out) trick doesn't work because it's not possible to terminate an UE4 thread

			//// Timeout. Kill the thread. This means possibly leaving internal decoding threads running, crashing in WMF decoder destructor,
			//// potential inability to start the next streaming session and other horrible things. Still better than doing nothing as getting stuck on 
			//// joining decoding thread means that hang detector will kill the game anyway
			//UE_LOG(LogVideoDecoder, Error, TEXT("Video decoder hang detected. Killing the decoder thread."));
			//DecodingThread->Kill(false); // don't wait, it's stuck

			// instead of ^^, the only solution we have is to leave it running, which causes memory leak at least, but also can crash or other nasty
			// stuff. we do this only because an alternative is to get stuck in `DecodingThread` destructor waiting for Hang Detector to kick in and kill 
			// the game.
			DecodingThread.Release(); // just leave it alone with the hope that destroying decoder will unblock it
		}
		else
		{
			// join only when we are sure it's not stuck
			DecodingThread->Join();
		}

		DecodingThread.Reset();
	}
}

bool FVideoDecoder::ProcessInput()
{
	TRefCountPtr<IMFSample> Sample;
	verify(InputQueue.Dequeue(Sample));
	InputQueueSize.Decrement();

	if (Sample.IsValid())
	{
		// sample is provided, process it normally
		HRESULT HResult = H264Decoder->ProcessInput(0, Sample, 0);

		if (FAILED(HResult))
		{
			UE_LOG(LogVideoDecoder, Error, TEXT("WMF Video Decoder ProcessInput() failed: %d %s"), HResult, *GetComErrorDescription(HResult));
			return false;
		}

		LONGLONG SampleTime = 0;
		CHECK_HR(Sample->GetSampleTime(&SampleTime));
		LONGLONG CaptureTs = 0;
		CHECK_HR(Sample->GetSampleDuration(&CaptureTs));

		UE_LOG(LogVideoDecoder, VeryVerbose, TEXT("ProcessInput: #%d, ts %lld, capture ts %lld, queue %d"), InputFrameProcessedCount, SampleTime, CaptureTs, InputQueueSize.GetValue());
		++InputFrameProcessedCount;
	}
	else
	{
		// empty sample that indicates a request to drain decoder's buffer and (potentially) reconfigure decoder 
		// (can happen on switching tracks)
		UE_LOG(LogVideoDecoder, Verbose, TEXT("ProcessInput: draining buffer, queue %d"), InputQueueSize.GetValue());
		// Microsoft uses a different terminology, "FLUSH" means immediate release of buffered frames and
		// stopping internal decoding while "DRAIN" means finishing decoding buffered frames w/o requesting
		// more input
		CHECK_HR(H264Decoder->ProcessMessage(MFT_MESSAGE_COMMAND_DRAIN, 0));
		// https://docs.microsoft.com/en-us/windows/desktop/medfound/mft-message-command-drain
		// we use sync MFT (despite it's h/w accelerated, ok, most of the time).
		// after this MFT should not ask for more input until it's drained its buffer, so next `MF_E_TRANSFORM_NEED_MORE_INPUT`
		// indicates buffer is empty and we can reconfigure decoder
	}

	return true;
}

bool FVideoDecoder::ProcessOutputHW(IMFSample* MFSample)
{
	TSharedRef<FWmfMediaHardwareVideoDecodingTextureSample, ESPMode::ThreadSafe> TextureSample = HwTextureSamplePool.AcquireShared();

	if (!CopyTexture(MFSample, TextureSample))
	{
		return false;
	}

	// Wrap in WebRTC types and pass down the pipeline
	check(DecodeCallback);
	webrtc::VideoFrame VideoFrame = webrtc::VideoFrame::Builder{}.
		set_video_frame_buffer(new rtc::RefCountedObject<FVideoFrameBuffer>(TextureSample)).
		set_timestamp_rtp(TextureSample->GetTime().GetTicks()).
		build();

	DecodeCallback->Decoded(VideoFrame);

	UE_LOG(LogVideoDecoder, VeryVerbose, TEXT("ProcessOutputHW: #%d (%d), ts %lld, capture ts %lld"), OutputFrameProcessedCount, InputFrameProcessedCount - OutputFrameProcessedCount, TextureSample->GetTime().GetTicks(), TextureSample->GetDuration().GetTicks());
	++OutputFrameProcessedCount;

	// decoder `AddRef`ed it for us. Not releasing it leads to decoder hanging
	MFSample->Release();

	return true;
}

bool FVideoDecoder::ProcessOutputSW(const FSwTextureSampleRef& TextureSample)
{
	bool bSuccess = TextureSample->ProcessOutputSample();
	if (!bSuccess)
	{
		UE_LOG(LogVideoDecoder, Error, TEXT("ProcessOutputSW: Failed to process output sample"));
		return false;
	}

	// Wrap in WebRTC types and pass down the pipeline
	check(DecodeCallback);
	webrtc::VideoFrame VideoFrame = webrtc::VideoFrame::Builder{}.
		set_video_frame_buffer(new rtc::RefCountedObject<FVideoFrameBuffer>(TextureSample)).
		set_timestamp_rtp(TextureSample->GetTime().GetTicks()).
		build();

	DecodeCallback->Decoded(VideoFrame);

	UE_LOG(LogVideoDecoder, VeryVerbose, TEXT("ProcessOutputSW: #%d, ts %.3f, d %.3f"), OutputFrameProcessedCount, TextureSample->GetTime().GetTotalSeconds(), TextureSample->GetDuration().GetTotalSeconds());
	++OutputFrameProcessedCount;
	return true;
}

bool FVideoDecoder::CopyTexture(IMFSample* Sample, const TSharedPtr<FWmfMediaHardwareVideoDecodingTextureSample, ESPMode::ThreadSafe>& OutTexture)
{
	DWORD BuffersNum = 0;
	CHECK_HR(Sample->GetBufferCount(&BuffersNum));

	if (BuffersNum != 1)
	{
		UE_LOG(LogVideoDecoder, Error, TEXT("Unexpected number of buffers in decoded IMFSample: %d"), BuffersNum);
		return false;
	}

	LONGLONG SampleTime = 0;
	CHECK_HR(Sample->GetSampleTime(&SampleTime));
	LONGLONG SampleDuration = 0;
	CHECK_HR(Sample->GetSampleDuration(&SampleDuration));

	ID3D11Device* UE4DxDevice = static_cast<ID3D11Device*>(GDynamicRHI->RHIGetNativeDevice());

	TComPtr<IMFMediaBuffer> Buffer;
	CHECK_HR(Sample->GetBufferByIndex(0, &Buffer));

	if (IsWindows8Plus())
	{
		TComPtr<IMFDXGIBuffer> DXGIBuffer;
		CHECK_HR(DXGIBuffer.FromQueryInterface(__uuidof(IMFDXGIBuffer), Buffer));
		TComPtr<ID3D11Texture2D> Texture2D;
		CHECK_HR(DXGIBuffer->GetResource(IID_PPV_ARGS(&Texture2D)));
		uint32 ViewIndex = 0;
		CHECK_HR(DXGIBuffer->GetSubresourceIndex(&ViewIndex));
		D3D11_TEXTURE2D_DESC TextureDesc;
		Texture2D->GetDesc(&TextureDesc);

		check(OutTexture->GetMediaTextureSampleConverter() != nullptr);

		// initializes only once per pooled texture
		ID3D11Texture2D* SharedTexture = OutTexture->InitializeSourceTexture(
			s_DeviceInfo->DxDevice,
			SampleTime,
			SampleDuration,
			FIntPoint(Config.Width, Config.Height),
			PF_NV12,
			EMediaTextureSampleFormat::CharNV12);

		D3D11_BOX SrcBox;
		SrcBox.left = 0;
		SrcBox.top = 0;
		SrcBox.front = 0;
		SrcBox.right = Config.Width;
		SrcBox.bottom = Config.Height;
		SrcBox.back = 1;

		TComPtr<IDXGIKeyedMutex> KeyedMutex;
		SharedTexture->QueryInterface(_uuidof(IDXGIKeyedMutex), (void**)&KeyedMutex);

		if (KeyedMutex)
		{
			// No wait on acquire since sample is new and key is 0.
			if (KeyedMutex->AcquireSync(0, 0) == S_OK)
			{
				s_DeviceInfo->DxDeviceContext->CopySubresourceRegion(SharedTexture, 0, 0, 0, 0, Texture2D, ViewIndex, &SrcBox);

				// Mark texture as updated with key of 1
				// Sample will be read in FWmfMediaHardwareVideoDecodingParameters::ConvertTextureFormat_RenderThread
				KeyedMutex->ReleaseSync(1);
			}
		}
		//UE_LOG(LogWmfVideoDecoder, VeryVerbose, TEXT("CopyTexture %dX%d -> %dX%d"), TextureDesc.Width, TextureDesc.Height, Config.Width, Config.Height);

		// Make sure texture is updated before giving access to the sample in the rendering thread.
		s_DeviceInfo->DxDeviceContext->Flush();
	}
	else
	{
		TComPtr<IDirect3DSurface9> Dx9DecoderSurface;
		TComPtr<IMFGetService> BufferService;
		CHECK_HR(Buffer->QueryInterface(IID_PPV_ARGS(&BufferService)));
		CHECK_HR(BufferService->GetService(MR_BUFFER_SERVICE, IID_PPV_ARGS(&Dx9DecoderSurface)));

		D3DSURFACE_DESC Dx9SurfaceDesc;
		CHECK_HR_DX9(Dx9DecoderSurface->GetDesc(&Dx9SurfaceDesc));
		check(Dx9SurfaceDesc.Format == DX9_NV12_FORMAT);
		if (Dx9SurfaceDesc.Format != DX9_NV12_FORMAT)
		{
			UE_LOG(LogVideoDecoder, Error, TEXT("Decoded DX9 surface is not in NV12 format"));
		}

		// Read back DX9 surface data and pass it onto the texture sample
		D3DLOCKED_RECT Dx9DecoderTexLockedRect;
		CHECK_HR_DX9(Dx9DecoderSurface->LockRect(&Dx9DecoderTexLockedRect, nullptr, D3DLOCK_READONLY));
		check(Dx9DecoderTexLockedRect.pBits && Dx9DecoderTexLockedRect.Pitch > 0);

		OutTexture->Initialize(
			Dx9DecoderTexLockedRect.pBits,									// Buffer ptr
			Dx9DecoderTexLockedRect.Pitch * Dx9SurfaceDesc.Height * 3 / 2,	// Buffer size
			FIntPoint(Dx9SurfaceDesc.Width, Dx9SurfaceDesc.Height * 3 / 2),	// InDim
			FIntPoint(Config.Width, Config.Height),							// OutDim
			EMediaTextureSampleFormat::CharNV12,							// SampleFormat
			Dx9DecoderTexLockedRect.Pitch,									// Buffer stride
			SampleTime,
			SampleDuration);

		CHECK_HR_DX9(Dx9DecoderSurface->UnlockRect());
	}

	return true;
}

bool FVideoDecoder::CreateDXManagerAndDevice()
{
	if (IsWindows8Plus())
	{
		return CreateDXGIManagerAndDevice();
	}
	else
	{
		return CreateDX9ManagerAndDevice();
	}

	return false;
}

bool FVideoDecoder::CreateDXGIManagerAndDevice()
{
	s_DeviceInfo = MakeUnique<FDeviceInfo>();

	UINT ResetToken = 0;
	CHECK_HR(MFCreateDXGIDeviceManager(&ResetToken, s_DeviceInfo->DxDeviceManager.GetInitReference()));

	if (GDynamicRHI == nullptr)
	{
		UE_LOG(LogVideoDecoder, Error, TEXT("Dynamic RHI is nullptr"));
		return false;
	}

	if (TCString<TCHAR>::Stricmp(GDynamicRHI->GetName(), TEXT("D3D11")) != 0)
	{
		UE_LOG(LogVideoDecoder, Error, TEXT("Dynamic RHI is not D3D11"));
		return false;
	}

	// Create device from same adapter as already existing device
	ID3D11Device* UE4DxDevice = static_cast<ID3D11Device*>(GDynamicRHI->RHIGetNativeDevice());

	TComPtr<IDXGIDevice> DXGIDevice;
	UE4DxDevice->QueryInterface(__uuidof(IDXGIDevice), (void**)&DXGIDevice);

	TComPtr<IDXGIAdapter> DXGIAdapter;
	DXGIDevice->GetAdapter((IDXGIAdapter**)&DXGIAdapter);

	D3D_FEATURE_LEVEL FeatureLevel;

	uint32 DeviceCreationFlags = 0;

	uint32 UE4DxDeviceCreationFlags = UE4DxDevice->GetCreationFlags();
	if ((UE4DxDeviceCreationFlags & D3D11_CREATE_DEVICE_DEBUG) != 0)
	{
		DeviceCreationFlags |= D3D11_CREATE_DEVICE_DEBUG;
	}

	CHECK_HR(D3D11CreateDevice(
		DXGIAdapter,
		D3D_DRIVER_TYPE_UNKNOWN,
		nullptr,
		DeviceCreationFlags,
		nullptr,
		0,
		D3D11_SDK_VERSION,
		s_DeviceInfo->DxDevice.GetInitReference(),
		&FeatureLevel,
		s_DeviceInfo->DxDeviceContext.GetInitReference()));

	if (FeatureLevel < D3D_FEATURE_LEVEL_9_3)
	{
		UE_LOG(LogVideoDecoder, Error, TEXT("Unable to Create D3D11 Device with feature level 9.3 or above"));
		return false;
	}

	CHECK_HR(s_DeviceInfo->DxDeviceManager->ResetDevice(s_DeviceInfo->DxDevice, ResetToken));

	// multithread protect the newly created device as we're going to use it from decoding thread and from render thread for texture
	// sharing between decoding and rendering DX devices
	TComPtr<ID3D10Multithread> DxMultithread;
	CHECK_HR(DxMultithread.FromQueryInterface(__uuidof(ID3D10Multithread), s_DeviceInfo->DxDevice));
	DxMultithread->SetMultithreadProtected(1);

	UE_LOG(LogVideoDecoder, Log, TEXT("D3D11 Device for h/w accelerated decoding created: %p"), s_DeviceInfo->DxDevice.GetReference());

	return true;
}

bool FVideoDecoder::CreateDX9ManagerAndDevice()
{
	s_DeviceInfo = MakeUnique<FDeviceInfo>();

	UINT ResetToken = 0;
	CHECK_HR_DX9(DXVA2CreateDirect3DDeviceManager9(&ResetToken, s_DeviceInfo->Dx9DeviceManager.GetInitReference()));

	s_DeviceInfo->Dx9 = Direct3DCreate9(D3D_SDK_VERSION);
	check(s_DeviceInfo->Dx9);
	if (!s_DeviceInfo->Dx9)
	{
		UE_LOG(LogVideoDecoder, Error, TEXT("Direct3DCreate9(D3D_SDK_VERSION) failed"));
		return false;
	}

	s_DeviceInfo->Dx9Device = nullptr;
	D3DPRESENT_PARAMETERS PresentParam;
	FMemory::Memzero(PresentParam);
	PresentParam.BackBufferWidth = 1;
	PresentParam.BackBufferHeight = 1;
	PresentParam.BackBufferFormat = D3DFMT_UNKNOWN;
	PresentParam.BackBufferCount = 1;
	PresentParam.SwapEffect = D3DSWAPEFFECT_DISCARD;
	PresentParam.hDeviceWindow = nullptr;
	PresentParam.Windowed = true;
	PresentParam.Flags = D3DPRESENTFLAG_VIDEO;
	CHECK_HR_DX9(s_DeviceInfo->Dx9->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, nullptr, D3DCREATE_MULTITHREADED | D3DCREATE_MIXED_VERTEXPROCESSING, &PresentParam, s_DeviceInfo->Dx9Device.GetInitReference()));

	CHECK_HR_DX9(s_DeviceInfo->Dx9DeviceManager->ResetDevice(s_DeviceInfo->Dx9Device, ResetToken));

	return true;
}

bool FVideoDecoder::DestroyDXManagerAndDevice()
{
	s_DeviceInfo.Reset();

	return true;
}

std::vector<webrtc::SdpVideoFormat> FVideoDecoderFactory::GetSupportedFormats() const
{
	return { CreateH264Format(webrtc::H264::kProfileConstrainedBaseline, webrtc::H264::kLevel5_2) };
}

std::unique_ptr<webrtc::VideoDecoder> FVideoDecoderFactory::CreateVideoDecoder(const webrtc::SdpVideoFormat& format)
{
	return std::make_unique<FVideoDecoder>();
}