// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "WmfVideoEncoder.h"

#if PLATFORM_WINDOWS

#include "ScreenRendering.h"
#include "RendererInterface.h"
#include "RHIStaticStates.h"
#include "PipelineStateCache.h"
#include "Misc/CoreDelegates.h"
#include "Modules/ModuleManager.h"
#include "UtilityShaders.h"
#include "CommonRenderResources.h"

GAMEPLAYMEDIAENCODER_START

FWmfVideoEncoder::FWmfVideoEncoder(const FOutputSampleCallback& OutputCallback) :
	FBaseVideoEncoder(OutputCallback)
{
}

bool FWmfVideoEncoder::Initialize(const FVideoEncoderConfig& InConfig)
{
	// Fails to create H264 video processor in CoCreateInstance
	// Error: `CoCreateInstance(CLSID_VideoProcessorMFT, nullptr, CLSCTX_INPROC_SERVER, IID_IMFTransform, reinterpret_cast<void**>(&VideoProcessor))` failed: 0x80040154 - Class not registered
	if (!FWindowsPlatformMisc::VerifyWindowsVersion(6, 2) /*Is Win8 or higher?*/)
	{
		UE_LOG(GameplayMediaEncoder, Error, TEXT("WmfVideoEncoder for Windows 7 is not implemented"));
		return false;
	}

	if (!FBaseVideoEncoder::Initialize(InConfig))
	{
		return false;
	}

	UE_LOG(GameplayMediaEncoder, Log, TEXT("VideoEncoder config: %dx%d, %d FPS, %.2f Mbps"), InConfig.Width, InConfig.Height, InConfig.Framerate, InConfig.Bitrate / 1000000.0f);

	return InitializeVideoProcessor() && InitializeEncoder();
}

bool FWmfVideoEncoder::Process(const FTexture2DRHIRef& Texture, FTimespan Timestamp, FTimespan Duration)
{
	check(IsInRenderingThread());

	UE_LOG(GameplayMediaEncoder, Verbose, TEXT("Video input #%u: time %.3f, duration %.3f"), (uint32)InputCount, Timestamp.GetTotalSeconds(), Duration.GetTotalSeconds());
	InputCount++;

	if (!EnqueueInputFrame(Texture, Timestamp, Duration))
	{
		UE_LOG(GameplayMediaEncoder, Error, TEXT("Failed to enqueue media buffer"));
		return false;
	}

	return ProcessVideoProcessorInputFrame() &&
		ProcessVideoProcessorOutputFrame() &&
		ProcessEncoderInputFrame() &&
		ProcessEncoderOutputFrame();
}

bool FWmfVideoEncoder::Start()
{
	return true;
}

void FWmfVideoEncoder::Stop()
{
}

bool FWmfVideoEncoder::Flush()
{
	//while (!InputFrameQueue.IsEmpty())
	//{
	//	ProcessInputFrame();
	//	ProcessOutputFrame();
	//}

	//// Signal end of input stream, process remaining unfinished frames
	//CHECK_HR(H264Encoder->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, 0));
	//CHECK_HR(H264Encoder->ProcessMessage(MFT_MESSAGE_COMMAND_DRAIN, 0));

	//while (OutputFrameProcessedCount.GetValue() < InputFrameProcessedCount)
	//{Video sample encoded
	//	ProcessOutputFrame();
	//}

	return true;
}

bool FWmfVideoEncoder::InitializeVideoProcessor()
{
	bool bResult = true;

	// Create H264 encoder
	CHECK_HR(CoCreateInstance(CLSID_VideoProcessorMFT, nullptr, CLSCTX_INPROC_SERVER, IID_IMFTransform, reinterpret_cast<void**>(&VideoProcessor)));

	if (!SetVideoProcessorInputMediaType() || !SetVideoProcessorOutputMediaType())
	{
		VideoProcessor->Release();
		return false;
	}

	return true;
}

bool FWmfVideoEncoder::SetVideoProcessorInputMediaType()
{
	TRefCountPtr<IMFMediaType> InputMediaType;
	CHECK_HR(MFCreateMediaType(InputMediaType.GetInitReference()));
	CHECK_HR(InputMediaType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video));
	CHECK_HR(InputMediaType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_ARGB32));
	CHECK_HR(MFSetAttributeSize(InputMediaType, MF_MT_FRAME_SIZE, Config.Width, Config.Height));
	CHECK_HR(VideoProcessor->SetInputType(0, InputMediaType, 0));

	return true;
}

bool FWmfVideoEncoder::SetVideoProcessorOutputMediaType()
{
	// `OutputType` is initialized in `FBaseVideoEncoder::Initialize()`
	CHECK_HR(VideoProcessor->SetOutputType(0, OutputType, 0));

	return true;
}

bool FWmfVideoEncoder::ProcessVideoProcessorInputFrame()
{
	bool bResult = true;

	if (!InputQueue.IsEmpty())
	{
		FGameplayMediaEncoderSample Sample;
		bResult = InputQueue.Peek(Sample);
		check(bResult);
		HRESULT HResult = VideoProcessor->ProcessInput(0, Sample.GetSample(), 0);
		if (SUCCEEDED(HResult))
		{
			bResult = InputQueue.Pop();
			check(bResult);
			InputQueueSize.Decrement();
			InputFrameProcessedCount++;
			UE_LOG(GameplayMediaEncoder, VeryVerbose, TEXT("Video processor processed %d input frames, queue size %d"), InputFrameProcessedCount, InputQueueSize.GetValue());
		}
		else
		{
			UE_LOG(GameplayMediaEncoder, Error, TEXT("FWmfVideoEncoder->ProcessVideoProcessorInputFrame failed: %d"), HResult);
			return false;
		}
	}

	return true;
}

bool FWmfVideoEncoder::ProcessVideoProcessorOutputFrame()
{
	bool bOutputIncomplete = true;
	while (bOutputIncomplete)
	{
		FGameplayMediaEncoderSample Sample{ EMediaType::Video };
		CreateInputSample(Sample);

		MFT_OUTPUT_DATA_BUFFER OutputDataBuffer;
		OutputDataBuffer.dwStreamID = 0;
		OutputDataBuffer.pSample = Sample.GetSample();
		OutputDataBuffer.dwStatus = 0;
		OutputDataBuffer.pEvents = nullptr;

		// have to reset sample before use
		TRefCountPtr<IMFMediaBuffer> MediaBuffer;
		CHECK_HR(Sample.GetSample()->GetBufferByIndex(0, MediaBuffer.GetInitReference()));
		CHECK_HR(MediaBuffer->SetCurrentLength(0));

		DWORD Status;
		HRESULT HResult = VideoProcessor->ProcessOutput(0, 1, &OutputDataBuffer, &Status);
		if (OutputDataBuffer.pEvents)
		{
			// https://docs.microsoft.com/en-us/windows/desktop/api/mftransform/nf-mftransform-imftransform-processoutput
			// The caller is responsible for releasing any events that the MFT allocates.
			OutputDataBuffer.pEvents->Release();
			OutputDataBuffer.pEvents = nullptr;
		}
		if (SUCCEEDED(HResult))
		{
			if (OutputDataBuffer.pSample)
			{
				EncoderInputQueue.Enqueue(Sample);
				EncoderInputQueueSize.Increment();
			}

			bOutputIncomplete = (OutputDataBuffer.dwStatus == MFT_OUTPUT_DATA_BUFFER_INCOMPLETE);
		}
		else
		{
			UE_LOG(GameplayMediaEncoder, Error, TEXT("FWmfVideoEncoder::ProcessVideoProcessorOutputFrame failed: %d"), HResult);
			return false;
		}
	}

	return true;
}

bool FWmfVideoEncoder::CreateInputSample(FGameplayMediaEncoderSample& Sample)
{
	if (!Sample.CreateSample())
	{
		return false;
	}

	// Use a single 8-bit texture to store NV12 texture as UE4 doesn't support NV12 texture directly.
	FRHIResourceCreateInfo CreateInfo;
	FTexture2DRHIRef Texture = RHICreateTexture2D(Config.Width, Config.Height * 3 / 2, PF_G8, 1, 1, 0, CreateInfo);
	ID3D11Texture2D* DX11Texture = (ID3D11Texture2D*)(GetD3D11TextureFromRHITexture(Texture)->GetResource());

	TRefCountPtr<IMFMediaBuffer> MediaBuffer;

	if (!FWindowsPlatformMisc::VerifyWindowsVersion(6, 1) /*Win7*/)
	{
		UE_LOG(GameplayMediaEncoder, Error, TEXT("Win8+ is required"));
		return false;
	}

	CHECK_HR(MFCreateDXGISurfaceBuffer(__uuidof(ID3D11Texture2D), DX11Texture, 0, false, MediaBuffer.GetInitReference()));

	CHECK_HR(Sample.GetSample()->AddBuffer(MediaBuffer));

	return true;
}

bool FWmfVideoEncoder::InitializeEncoder()
{
	bool bResult = true;

	// Create H264 encoder
	CHECK_HR(CoCreateInstance(CLSID_CMSH264EncoderMFT, nullptr, CLSCTX_INPROC_SERVER, IID_IMFTransform, reinterpret_cast<void**>(&H264Encoder)));

	if (!SetEncoderOutputMediaType() ||
		!SetEncoderInputMediaType() ||
		!CheckEncoderStatus() ||
		!RetrieveStreamInfo() ||
		!StartStreaming())
	{
		H264Encoder->Release();
		return false;
	}

	return true;
}

bool FWmfVideoEncoder::SetEncoderInputMediaType()
{
	TRefCountPtr<IMFMediaType> InputMediaType;
	CHECK_HR(MFCreateMediaType(InputMediaType.GetInitReference()));
	CHECK_HR(InputMediaType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video));
	// List of input subtypes: https://docs.microsoft.com/en-us/windows/desktop/medfound/h-264-video-encoder
	CHECK_HR(InputMediaType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12));
	CHECK_HR(InputMediaType->SetUINT32(MF_MT_AVG_BITRATE, Config.Bitrate));
	CHECK_HR(MFSetAttributeRatio(InputMediaType, MF_MT_FRAME_RATE, Config.Framerate, 1));
	CHECK_HR(MFSetAttributeSize(InputMediaType, MF_MT_FRAME_SIZE, Config.Width, Config.Height));
	CHECK_HR(MFSetAttributeRatio(InputMediaType, MF_MT_PIXEL_ASPECT_RATIO, 1, 1));
	CHECK_HR(InputMediaType->SetUINT32(MF_MT_INTERLACE_MODE, /*MFVideoInterlace_MixedInterlaceOrProgressive*/MFVideoInterlace_Progressive));

	CHECK_HR(H264Encoder->SetInputType(0, InputMediaType, 0));

	return true;
}

bool FWmfVideoEncoder::SetEncoderOutputMediaType()
{
	// `OutputType` is filled in `FBaseVideoEncoder::Initialize`
	CHECK_HR(H264Encoder->SetOutputType(0, OutputType, 0));

	return true;
}

bool FWmfVideoEncoder::SetBitrate(uint32 Bitrate)
{
	// update `OutputType` and apply it
	return FBaseVideoEncoder::SetBitrate(Bitrate) && SetEncoderOutputMediaType();
}

bool FWmfVideoEncoder::SetFramerate(uint32 Framerate)
{
	// update `OutputType` and apply it
	return FBaseVideoEncoder::SetFramerate(Framerate) && SetEncoderOutputMediaType();
}

bool FWmfVideoEncoder::RetrieveStreamInfo()
{
	CHECK_HR(H264Encoder->GetOutputStreamInfo(0, &OutputStreamInfo));

	return true;
}

bool FWmfVideoEncoder::CheckEncoderStatus()
{
	DWORD EncoderStatus = 0;
	CHECK_HR(H264Encoder->GetInputStatus(0, &EncoderStatus));
	if (MFT_INPUT_STATUS_ACCEPT_DATA != EncoderStatus)
	{
		return false;
	}

	return true;
}

bool FWmfVideoEncoder::StartStreaming()
{
	// Signal encoder ready to encode
	CHECK_HR(H264Encoder->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, 0));
	CHECK_HR(H264Encoder->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0));
	CHECK_HR(H264Encoder->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0));

	return true;
}

bool FWmfVideoEncoder::EnqueueInputFrame(const FTexture2DRHIRef& Texture, FTimespan Timestamp, FTimespan Duration)
{
	bool bResult = true;

	FRHIResourceCreateInfo CreateInfo;
	FTexture2DRHIRef EncoderInputTexture = RHICreateTexture2D(Config.Width, Config.Height, PF_R8G8B8A8, 1, 1, TexCreate_ShaderResource | TexCreate_RenderTargetable, CreateInfo);

	ResolveBackBuffer(Texture, EncoderInputTexture);

	TRefCountPtr<IMFMediaBuffer> MediaBuffer;
	ID3D11Texture2D* DX11Texture = (ID3D11Texture2D*)(GetD3D11TextureFromRHITexture(EncoderInputTexture)->GetResource());
	CHECK_HR(MFCreateDXGISurfaceBuffer(__uuidof(ID3D11Texture2D), DX11Texture, 0, false, MediaBuffer.GetInitReference()));

	FGameplayMediaEncoderSample Sample{ EMediaType::Video };
	if (!Sample.CreateSample())
	{
		return false;
	}
	CHECK_HR(Sample.GetSample()->AddBuffer(MediaBuffer));
	Sample.SetTime(Timestamp);
	Sample.SetDuration(Duration);

	bResult = InputQueue.Enqueue(Sample);
	check(bResult);
	InputQueueSize.Increment();

	return true;
}

bool FWmfVideoEncoder::CreateOutputSample(FGameplayMediaEncoderSample& OutputSample)
{
	TRefCountPtr<IMFMediaBuffer> MediaBuffer;
	CHECK_HR(MFCreateMemoryBuffer(OutputStreamInfo.cbSize, MediaBuffer.GetInitReference()));
	// Update MF media buffer length
	CHECK_HR(MediaBuffer->SetCurrentLength(OutputStreamInfo.cbSize));

	if (!OutputSample.CreateSample())
	{
		return false;
	}
	CHECK_HR(OutputSample.GetSample()->AddBuffer(MediaBuffer));

	return true;
}

bool FWmfVideoEncoder::ProcessEncoderInputFrame()
{
	bool bResult = true;

	if (!EncoderInputQueue.IsEmpty())
	{
		FGameplayMediaEncoderSample Sample{ EMediaType::Video };
		bResult = EncoderInputQueue.Peek(Sample);
		check(bResult);
		HRESULT HResult = H264Encoder->ProcessInput(0, Sample.GetSample(), 0);
		if (SUCCEEDED(HResult))
		{
			bResult = EncoderInputQueue.Pop();
			check(bResult);
			EncoderInputQueueSize.Decrement();
			EncoderInputProcessedCount++;
			UE_LOG(GameplayMediaEncoder, VeryVerbose, TEXT("Video encoder processed %d input frames, queue size %d"), EncoderInputProcessedCount, InputQueueSize.GetValue());
		}
		else
		{
			UE_LOG(GameplayMediaEncoder, Error, TEXT("FWmfVideoEncoder::ProcessEncoderInputFrame failed: %d"), HResult);
			return false;
		}
	}

	return true;
}

bool FWmfVideoEncoder::ProcessEncoderOutputFrame()
{
	DWORD mftOutFlags;
	CHECK_HR(H264Encoder->GetOutputStatus(&mftOutFlags));

	if (mftOutFlags == MFT_OUTPUT_STATUS_SAMPLE_READY)
	{
		FGameplayMediaEncoderSample OutputSample{ EMediaType::Video };
		if (!CreateOutputSample(OutputSample))
		{
			return false;
		}

		bool bOutputIncomplete = true;
		while (bOutputIncomplete)
		{
			MFT_OUTPUT_DATA_BUFFER OutputDataBuffer = {};
			OutputDataBuffer.pSample = OutputSample.GetSample();

			// have to reset sample before use
			TRefCountPtr<IMFMediaBuffer> MediaBufferToReset;
			CHECK_HR(OutputSample.GetSample()->GetBufferByIndex(0, MediaBufferToReset.GetInitReference()));
			CHECK_HR(MediaBufferToReset->SetCurrentLength(0));

			DWORD Status;
			HRESULT HResult = H264Encoder->ProcessOutput(0, 1, &OutputDataBuffer, &Status);
			if (OutputDataBuffer.pEvents)
			{
				// https://docs.microsoft.com/en-us/windows/desktop/api/mftransform/nf-mftransform-imftransform-processoutput
				// The caller is responsible for releasing any events that the MFT allocates.
				OutputDataBuffer.pEvents->Release();
				OutputDataBuffer.pEvents = nullptr;
			}

			if (HResult == MF_E_TRANSFORM_NEED_MORE_INPUT)
			{
				return true;
			}
			else if (HResult == MF_E_TRANSFORM_STREAM_CHANGE)
			{
				if (OutputDataBuffer.dwStatus & MFT_OUTPUT_DATA_BUFFER_FORMAT_CHANGE)
				{
					check(false);
				}
				else
				{
					check(false);
					UE_LOG(GameplayMediaEncoder, Error, TEXT("MF_E_TRANSFORM_STREAM_CHANGE"));
					return false;
				}
			}
			else if (SUCCEEDED(HResult))
			{
				if (OutputDataBuffer.pSample)
				{
					DWORD OutputSize;
					CHECK_HR(OutputSample.GetSample()->GetTotalLength(&OutputSize));

					UE_LOG(GameplayMediaEncoder, Verbose, TEXT("Video encoded: #%d, time %.3f, duration %.3f, size %d"), EncodedFrameCount.GetValue(), OutputSample.GetTime().GetTotalSeconds(), OutputSample.GetDuration().GetTotalSeconds(), OutputSize);

					EncodedFrameCount.Increment();

					if (!OutputCallback(OutputSample))
					{
						return false;
					}
				}

				bOutputIncomplete = (OutputDataBuffer.dwStatus == MFT_OUTPUT_DATA_BUFFER_INCOMPLETE);
			}
			else
			{
				UE_LOG(GameplayMediaEncoder, Error, TEXT("FWmfVideoEncoder::ProcessEncoderOutputFrame failed: %d"), HResult);
				return false;
			}
		}
	}

	return true;
}

void FWmfVideoEncoder::ResolveBackBuffer(const FTexture2DRHIRef& BackBuffer, const FTexture2DRHIRef& ResolvedBackBuffer)
{
	IRendererModule* RendererModule = &FModuleManager::GetModuleChecked<IRendererModule>("Renderer");
	FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();

	if (BackBuffer->GetFormat() == ResolvedBackBuffer->GetFormat() &&
		BackBuffer->GetSizeXY() == ResolvedBackBuffer->GetSizeXY())
	{
		RHICmdList.CopyToResolveTarget(BackBuffer, ResolvedBackBuffer, FResolveParams());
	}
	else // Texture format mismatch, use a shader to do the copy.
	{
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		SetRenderTarget(RHICmdList, ResolvedBackBuffer, FTextureRHIRef());
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
		RHICmdList.SetViewport(0, 0, 0.0f, ResolvedBackBuffer->GetSizeX(), ResolvedBackBuffer->GetSizeY(), 1.0f);

		FGraphicsPipelineStateInitializer GraphicsPSOInit;
		RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
		GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
		GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
		GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();

		TShaderMap<FGlobalShaderType>* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
		TShaderMapRef<FScreenVS> VertexShader(ShaderMap);
		TShaderMapRef<FScreenSwizzlePS> PixelShader(ShaderMap);

		GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
		GraphicsPSOInit.BoundShaderState.VertexShaderRHI = GETSAFERHISHADER_VERTEX(*VertexShader);
		GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(*PixelShader);
		GraphicsPSOInit.PrimitiveType = PT_TriangleList;

		SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

		if (ResolvedBackBuffer->GetSizeX() != BackBuffer->GetSizeX() || ResolvedBackBuffer->GetSizeY() != BackBuffer->GetSizeY())
			PixelShader->SetParameters(RHICmdList, TStaticSamplerState<SF_Bilinear>::GetRHI(), BackBuffer);
		else
			PixelShader->SetParameters(RHICmdList, TStaticSamplerState<SF_Point>::GetRHI(), BackBuffer);

		RendererModule->DrawRectangle(
			RHICmdList,
			0, 0,									// Dest X, Y
			ResolvedBackBuffer->GetSizeX(),			// Dest Width
			ResolvedBackBuffer->GetSizeY(),			// Dest Height
			0, 0,									// Source U, V
			1, 1,									// Source USize, VSize
			ResolvedBackBuffer->GetSizeXY(),		// Target buffer size
			FIntPoint(1, 1),						// Source texture size
			*VertexShader,
			EDRF_Default);
	}
}

GAMEPLAYMEDIAENCODER_END


#endif // PLATFORM_WINDOWS
