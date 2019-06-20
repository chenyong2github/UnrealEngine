// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "AmdAmfVideoEncoder.h"

#if PLATFORM_WINDOWS

#include "GameplayMediaEncoderSample.h"

#include "ScreenRendering.h"
#include "ShaderCore.h"
#include "RendererInterface.h"
#include "RHIStaticStates.h"
#include "PipelineStateCache.h"
#include "Modules/ModuleManager.h"
#include "CommonRenderResources.h"

GAMEPLAYMEDIAENCODER_START

DEFINE_LOG_CATEGORY(AmdAmf);

FAmdAmfVideoEncoder::FAmdAmfVideoEncoder(const FOutputSampleCallback& OutputCallback) :
	FBaseVideoEncoder(OutputCallback)
{}

FAmdAmfVideoEncoder::~FAmdAmfVideoEncoder()
{
	// BufferedFrames keep references to AMFData, we need to release them before destroying AMF
	for (FFrame& Frame : BufferedFrames)
	{
		Frame.EncodedData = nullptr;
	}

	// Cleanup in this order
	if (AmfEncoder)
	{
		AmfEncoder->Terminate();
		AmfEncoder = nullptr;
	}
	if (AmfContext)
	{
		AmfContext->Terminate();
		AmfContext = nullptr;
	}
	AmfFactory = nullptr;
	if (DllHandle)
	{
		FPlatformProcess::FreeDllHandle(DllHandle);
		DllHandle = nullptr;
	}
}

bool FAmdAmfVideoEncoder::Initialize(const FVideoEncoderConfig& InConfig)
{
	if (bInitialized)
	{
		UE_LOG(AmdAmf, Error, TEXT("Encoder already initialized. Re-initialization is not supported. Instead recreate the instance."));
		return false;
	}

	UE_LOG(AmdAmf, Log, TEXT("VideoEncoder config: %dx%d, %d FPS, %.2f Mbps"), InConfig.Width, InConfig.Height, InConfig.Framerate, InConfig.Bitrate / 1000000.0f);

	if (!FBaseVideoEncoder::Initialize(InConfig))
	{
		return false;
	}

	DllHandle = FPlatformProcess::GetDllHandle(AMF_DLL_NAME);
	if (DllHandle == nullptr)
	{
		return false;
	}

	AMFInit_Fn AmfInitFn = (AMFInit_Fn)FPlatformProcess::GetDllExport((HMODULE)DllHandle, TEXT(AMF_INIT_FUNCTION_NAME));
	if (AmfInitFn == nullptr)
	{
		return false;
	}
	CHECK_AMF_RET(AmfInitFn(AMF_FULL_VERSION, &AmfFactory));

	AMFQueryVersion_Fn AmfVersionFun = (AMFQueryVersion_Fn)FPlatformProcess::GetDllExport((HMODULE)DllHandle, TEXT(AMF_QUERY_VERSION_FUNCTION_NAME));
	if (AmfVersionFun == nullptr)
	{
		return false;
	}
	uint64 AmfVersion = 0;
	AmfVersionFun(&AmfVersion);

	CHECK_AMF_RET(AmfFactory->CreateContext(&AmfContext));
	CHECK_AMF_RET(AmfContext->InitDX11(GetUE4DxDevice()));

	CHECK_AMF_RET(AmfFactory->CreateComponent(AmfContext, AMFVideoEncoderVCE_AVC, &AmfEncoder));
	CHECK_AMF_RET(AmfEncoder->SetProperty(AMF_VIDEO_ENCODER_USAGE, /*AMF_VIDEO_ENCODER_USAGE_ULTRA_LOW_LATENCY*/AMF_VIDEO_ENCODER_USAGE_TRANSCONDING));
	CHECK_AMF_RET(AmfEncoder->SetProperty(AMF_VIDEO_ENCODER_PROFILE, AMF_VIDEO_ENCODER_PROFILE_MAIN));
	//CHECK_AMF_RET(AmfEncoder->SetProperty(AMF_VIDEO_ENCODER_B_PIC_PATTERN, 0));
	CHECK_AMF_RET(AmfEncoder->SetProperty(AMF_VIDEO_ENCODER_QUALITY_PRESET, AMF_VIDEO_ENCODER_QUALITY_PRESET_QUALITY));
	CHECK_AMF_RET(AmfEncoder->SetProperty(AMF_VIDEO_ENCODER_TARGET_BITRATE, InConfig.Bitrate));
	CHECK_AMF_RET(AmfEncoder->SetProperty(AMF_VIDEO_ENCODER_FRAMESIZE, ::AMFConstructSize(InConfig.Width, InConfig.Height)));
	CHECK_AMF_RET(AmfEncoder->SetProperty(AMF_VIDEO_ENCODER_FRAMERATE, ::AMFConstructRate(InConfig.Framerate, 1)));

	// generate key-frames every second, useful for seeking in resulting .mp4 and keeping recording ring buffer
	// of second-precise duration
	uint64 IdrPeriod = InConfig.Framerate;
	CHECK_AMF_RET(AmfEncoder->SetProperty(AMF_VIDEO_ENCODER_IDR_PERIOD, IdrPeriod));
	// insert SPS/PPS before every key-frame. .mp4 file video stream must start from SPS/PPS. Their size is
	// negligible so having them before every key-frame is not an issue but they presence simplifies
	// implementation significantly. Otherwise we would extract SPS/PPS from the first key-frame and store
	// them manually at the beginning of resulting .mp4 file
	CHECK_AMF_RET(AmfEncoder->SetProperty(AMF_VIDEO_ENCODER_HEADER_INSERTION_SPACING, IdrPeriod));

	CHECK_AMF_RET(AmfEncoder->Init(amf::AMF_SURFACE_RGBA, InConfig.Width, InConfig.Height));

	//LogAmfPropertyStorage(AmfEncoder);

	for (uint32 i = 0; i < NumBufferedFrames; ++i)
	{
		FFrame& Frame = BufferedFrames[i];
		ResetResolvedBackBuffer(Frame);
	}

	UE_LOG(AmdAmf, Log, TEXT("AMF H.264 encoder initialised, v.0x%X"), AmfVersion);

	bInitialized = true;

	return true;
}

bool FAmdAmfVideoEncoder::Start()
{
	return true;
}

void FAmdAmfVideoEncoder::Stop()
{
}

bool FAmdAmfVideoEncoder::SetBitrate(uint32 Bitrate)
{
	// update `Config` and `OutputType`
	if (!FBaseVideoEncoder::SetBitrate(Bitrate))
	{
		return false;
	}

	// reconfigure AMF
	CHECK_AMF_RET(AmfEncoder->SetProperty(AMF_VIDEO_ENCODER_TARGET_BITRATE, Bitrate));

	return true;
}

bool FAmdAmfVideoEncoder::SetFramerate(uint32 Framerate)
{
	// update `Config` and `OutputType`
	if (!FBaseVideoEncoder::SetFramerate(Framerate))
	{
		return false;
	}

	// reconfigure AMF
	CHECK_AMF_RET(AmfEncoder->SetProperty(AMF_VIDEO_ENCODER_FRAMERATE, ::AMFConstructRate(Framerate, 1)));

	return true;
}

DECLARE_CYCLE_STAT(TEXT("Process"), STAT_FAmdAmfVideoEncoder_Process, STATGROUP_AmdAmfVideoEncoder);

bool FAmdAmfVideoEncoder::Process(const FTexture2DRHIRef& Texture, FTimespan Timestamp, FTimespan Duration)
{
	SCOPE_CYCLE_COUNTER(STAT_FAmdAmfVideoEncoder_Process);

	check(IsInRenderingThread());

	// first process output to free reused instances of input frames
	return ProcessOutput() && ProcessInput(Texture, Timestamp, Duration);
}

DECLARE_CYCLE_STAT(TEXT("ProcessInput"), STAT_FAmdAmfVideoEncoder_ProcessInput, STATGROUP_AmdAmfVideoEncoder);

bool FAmdAmfVideoEncoder::ProcessInput(const FTexture2DRHIRef& Texture, FTimespan Timestamp, FTimespan Duration)
// prepare frame and submit to encoder
{
	SCOPE_CYCLE_COUNTER(STAT_FAmdAmfVideoEncoder_ProcessInput);

	UE_LOG(AmdAmf, VeryVerbose, TEXT("frame #%d input"), InputFrameCount);

	uint32 BufferIndex = InputFrameCount % NumBufferedFrames;
	FFrame& Frame = BufferedFrames[BufferIndex];

	if (Frame.bEncoding)
	{
		UE_LOG(AmdAmf, Verbose, TEXT("Dropped frame because encoder is lagging"));
		return true;
	}

	Frame.bEncoding = true;

	ResolveBackBuffer(Texture, Frame.ResolvedBackBuffer);

	Frame.FrameIdx = InputFrameCount;
	Frame.Timestamp = Timestamp;
	Frame.Duration = Duration;

	// ResolveBackBuffer can be asynchronous (executed by RHI Command List, so we need to do the same
	// for submitting frame to encoder
	ExecuteRHICommand([this, &Frame]() { SubmitFrameToEncoder(Frame); });

	++InputFrameCount;

	return true;
}

DECLARE_CYCLE_STAT(TEXT("SubmitFrameToEncoder"), STAT_FAmdAmfVideoEncoder_SubmitFrameToEncoder, STATGROUP_AmdAmfVideoEncoder);
DECLARE_CYCLE_STAT(TEXT("AmfEncoder->SubmitInput"), STAT_FAmdAmfVideoEncoder_AmfEncoder_SubmitInput, STATGROUP_AmdAmfVideoEncoder);

bool FAmdAmfVideoEncoder::SubmitFrameToEncoder(FFrame& Frame)
{
	SCOPE_CYCLE_COUNTER(STAT_FAmdAmfVideoEncoder_SubmitFrameToEncoder);

	amf::AMFSurfacePtr AmfSurfaceIn;
	ID3D11Texture2D* ResolvedBackBufferDX11 = static_cast<ID3D11Texture2D*>(GetD3D11TextureFromRHITexture(Frame.ResolvedBackBuffer)->GetResource());

	CHECK_AMF_RET(AmfContext->CreateSurfaceFromDX11Native(ResolvedBackBufferDX11, &AmfSurfaceIn, nullptr));

	{
		// if `-d3ddebug` is enabled `SubmitInput` crashes with DX11 error, see output window
		// we believe it's an internal AMF shader problem so we disable those errors explicitly, otherwise
		// DX Debug Layer can't be used at all
		FScopeDisabledDxDebugErrors Errors({
			D3D11_MESSAGE_ID_DEVICE_UNORDEREDACCESSVIEW_RETURN_TYPE_MISMATCH,
			D3D11_MESSAGE_ID_DEVICE_CSSETUNORDEREDACCESSVIEWS_TOOMANYVIEWS,
		});

		{
			SCOPE_CYCLE_COUNTER(STAT_FAmdAmfVideoEncoder_AmfEncoder_SubmitInput);
			CHECK_AMF_RET(AmfEncoder->SubmitInput(AmfSurfaceIn));
		}
	}

	return true;
}

DECLARE_CYCLE_STAT(TEXT("QueryEncoderOutput"), STAT_FAmdAmfVideoEncoder_QueryEncoderOutput, STATGROUP_AmdAmfVideoEncoder);

DECLARE_CYCLE_STAT(TEXT("AmfEncoder->QueryOutput"), STAT_FAmdAmfVideoEncoder_AmfEncoder_QueryOutput, STATGROUP_AmdAmfVideoEncoder);

bool FAmdAmfVideoEncoder::ProcessOutput()
// check if output is ready and handle it
{
	SCOPE_CYCLE_COUNTER(STAT_FAmdAmfVideoEncoder_QueryEncoderOutput);

	check(IsInRenderingThread());

	// more than one output frame can be ready
	while (BufferedFrames[OutputFrameCount % NumBufferedFrames].bEncoding)
	{
		amf::AMFDataPtr EncodedData;
		AMF_RESULT Ret;
		{
			SCOPE_CYCLE_COUNTER(STAT_FAmdAmfVideoEncoder_AmfEncoder_QueryOutput);
			Ret = AmfEncoder->QueryOutput(&EncodedData);
		}
		if (Ret == AMF_OK && EncodedData != nullptr)
		{
			UE_LOG(AmdAmf, VeryVerbose, TEXT("frame #%d encoded"), OutputFrameCount);

			FFrame& Frame = BufferedFrames[OutputFrameCount % NumBufferedFrames];
			check(Frame.bEncoding);
			checkf(OutputFrameCount == Frame.FrameIdx, TEXT("%d - %d"), OutputFrameCount, Frame.FrameIdx);
			Frame.EncodedData = EncodedData;

			if (!HandleEncodedFrame(Frame))
			{
				return false;
			}

			++OutputFrameCount;
		}
		else if (Ret == AMF_REPEAT)
		{
			break; // not ready yet
		}
		else
		{
			UE_LOG(AmdAmf, Error, TEXT("Failed to query AMF H.264 Encoder output: %d, %p"), Ret, EncodedData.GetPtr());
			return false;
		}
	}

	return true;
}

DECLARE_CYCLE_STAT(TEXT("ProcessEncodedFrame"), STAT_FAmdAmfVideoEncoder_ProcessEncodedFrame, STATGROUP_AmdAmfVideoEncoder);

bool FAmdAmfVideoEncoder::HandleEncodedFrame(FFrame& Frame)
{
	SCOPE_CYCLE_COUNTER(STAT_FAmdAmfVideoEncoder_ProcessEncodedFrame);

	checkf(Frame.bEncoding && Frame.EncodedData != nullptr, TEXT("Internal error: %d, %p"), static_cast<bool>(Frame.bEncoding), Frame.EncodedData.GetPtr());
	if (!Frame.bEncoding || Frame.EncodedData == nullptr)
	{
		return false;
	}

	// Query for buffer interface
	amf::AMFBufferPtr EncodedBuffer(Frame.EncodedData);
	void* EncodedBufferPtr = EncodedBuffer->GetNative();
	size_t EncodedBufferSize = EncodedBuffer->GetSize();

	// Retrieve encoded frame from AMFBuffer and copy to IMFMediaBuffer
	TRefCountPtr<IMFMediaBuffer> MediaBuffer;
	CHECK_HR(MFCreateMemoryBuffer(EncodedBufferSize, MediaBuffer.GetInitReference()));
	CHECK_HR(MediaBuffer->SetCurrentLength(EncodedBufferSize));
	BYTE* MediaBufferData = nullptr;
	DWORD MediaBufferLength = 0;
	MediaBuffer->Lock(&MediaBufferData, nullptr, &MediaBufferLength);
	FMemory::Memcpy(MediaBufferData, EncodedBufferPtr, EncodedBufferSize);
	MediaBuffer->Unlock();

	FGameplayMediaEncoderSample OutputSample{ EMediaType::Video };
	if (!OutputSample.CreateSample())
	{
		return false;
	}
	CHECK_HR(OutputSample.GetSample()->AddBuffer(MediaBuffer));
	OutputSample.SetTime(Frame.Timestamp);
	OutputSample.SetDuration(Frame.Duration);

	// mark as a key-frame (if it is)
	uint64 OutputFrameType;
	CHECK_AMF_RET(EncodedBuffer->GetProperty(AMF_VIDEO_ENCODER_OUTPUT_DATA_TYPE, &OutputFrameType));
	CHECK_HR(OutputSample.GetSample()->SetUINT32(MFSampleExtension_CleanPoint, OutputFrameType == AMF_VIDEO_ENCODER_OUTPUT_DATA_TYPE_IDR ? 1 : 0));

	UE_LOG(AmdAmf, Verbose, TEXT("encoded frame #%d: time %.3f, duration %.3f, size %d, type %d"), Frame.FrameIdx, OutputSample.GetTime().GetTotalSeconds(), OutputSample.GetDuration().GetTotalSeconds(), EncodedBufferSize, OutputFrameType);

	// only now when we're done dealing with encoded data we can "release" this frame
	// to be reused for encoding input
	Frame.EncodedData = nullptr;
	Frame.bEncoding = false;

	OutputCallback(OutputSample);
	return true;
}

void FAmdAmfVideoEncoder::ResetResolvedBackBuffer(FFrame& Frame)
{
	Frame.ResolvedBackBuffer.SafeRelease();

	// Make sure format used here is compatible with AMF_SURFACE_FORMAT specified in encoder Init() function.
	FRHIResourceCreateInfo CreateInfo;
	Frame.ResolvedBackBuffer = RHICreateTexture2D(Config.Width, Config.Height, EPixelFormat::PF_R8G8B8A8, 1, 1, TexCreate_RenderTargetable, CreateInfo);
}

DECLARE_CYCLE_STAT(TEXT("ResolveBackBuffer"), STAT_FAmdAmfVideoEncoder_ResolveBackBuffer, STATGROUP_AmdAmfVideoEncoder);

void FAmdAmfVideoEncoder::ResolveBackBuffer(const FTexture2DRHIRef& BackBuffer, const FTexture2DRHIRef& ResolvedBackBuffer)
{
	SCOPE_CYCLE_COUNTER(STAT_FAmdAmfVideoEncoder_ResolveBackBuffer);

	IRendererModule* RendererModule = &FModuleManager::GetModuleChecked<IRendererModule>("Renderer");
	FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();

	if (BackBuffer->GetFormat() == ResolvedBackBuffer->GetFormat() &&
		BackBuffer->GetSizeXY() == ResolvedBackBuffer->GetSizeXY())
	{
		RHICmdList.CopyToResolveTarget(BackBuffer, ResolvedBackBuffer, FResolveParams());
	}
	else // Texture format mismatch, use a shader to do the copy.
	{
		FRHIRenderPassInfo RPInfo(ResolvedBackBuffer, ERenderTargetActions::Load_Store);
		RHICmdList.BeginRenderPass(RPInfo, TEXT("AmdAmfVideoEncoder"));
		RHICmdList.SetViewport(0, 0, 0.0f, ResolvedBackBuffer->GetSizeX(), ResolvedBackBuffer->GetSizeY(), 1.0f);

		FGraphicsPipelineStateInitializer GraphicsPSOInit;
		RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
		GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
		GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
		GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();

		TShaderMap<FGlobalShaderType>* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
		TShaderMapRef<FScreenVS> VertexShader(ShaderMap);
		TShaderMapRef<FScreenPS> PixelShader(ShaderMap);

		GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
		GraphicsPSOInit.BoundShaderState.VertexShaderRHI = GETSAFERHISHADER_VERTEX(*VertexShader);
		GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(*PixelShader);
		GraphicsPSOInit.PrimitiveType = PT_TriangleList;

		SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

		if (ResolvedBackBuffer->GetSizeX() != BackBuffer->GetSizeX() || ResolvedBackBuffer->GetSizeY() != BackBuffer->GetSizeY())
		{
			PixelShader->SetParameters(RHICmdList, TStaticSamplerState<SF_Bilinear>::GetRHI(), BackBuffer);
		}
		else
		{
			PixelShader->SetParameters(RHICmdList, TStaticSamplerState<SF_Point>::GetRHI(), BackBuffer);
		}

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

		RHICmdList.EndRenderPass();
	}
}

GAMEPLAYMEDIAENCODER_END

#endif

