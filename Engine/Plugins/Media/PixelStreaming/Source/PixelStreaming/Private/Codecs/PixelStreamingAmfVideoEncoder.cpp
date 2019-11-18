// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "PixelStreamingAmfVideoEncoder.h"
#include "VideoEncoder.h"
#include "Utils.h"
#include "HUDStats.h"

#include "RendererInterface.h"
#include "Modules/ModuleManager.h"
#include "RHIStaticStates.h"
#include "Shader.h"
#include "GlobalShader.h"
#include "ScreenRendering.h"
#include "CommonRenderResources.h"
#include "Engine/Engine.h"
#include "Misc/CoreDelegates.h"
#include "Math/UnrealMathUtility.h"
#include "Templates/Atomic.h"
#include "Async/Async.h"

DECLARE_STATS_GROUP(TEXT("AmfVideoEncoder"), STATGROUP_Amf, STATCAT_Advanced);
DECLARE_CYCLE_STAT(TEXT("CopyBackBuffer"), STAT_Amf_CopyBackBuffer, STATGROUP_Amf);
DECLARE_CYCLE_STAT(TEXT("QueryEncoderOutput"), STAT_Amf_QueryEncoderOutput, STATGROUP_Amf);
DECLARE_CYCLE_STAT(TEXT("AmfEncoder->QueryOutput"), STAT_Amf_QueryOutput, STATGROUP_Amf);
DECLARE_CYCLE_STAT(TEXT("StreamEncodedFrame"), STAT_Amf_StreamEncodedFrame, STATGROUP_Amf);
DECLARE_CYCLE_STAT(TEXT("ProcessInput"), STAT_Amf_ProcessInput, STATGROUP_Amf);
DECLARE_CYCLE_STAT(TEXT("SubmitFrameToEncoder"), STAT_Amf_SubmitFrameToEncoder, STATGROUP_Amf);
DECLARE_CYCLE_STAT(TEXT("AmfEncoder->SubmitInput"), STAT_Amf_SubmitInput, STATGROUP_Amf);

// #AMF : This only exists in a more recent version of the AMF SDK.  Adding it here so I don't need to update the SDK  yet.
#ifdef AMF_VIDEO_ENCODER_LOWLATENCY_MODE
	// If you get this error, it means the AMF SDK was updated and already contains the property, so you can remove this check and the property that was added explicitly.
	#error "AMF_VIDEO_ENCODER_LOWLATENCY_MODE already exists. Please remove duplicate"
#endif
#define AMF_VIDEO_ENCODER_LOWLATENCY_MODE                       L"LowLatencyInternal"       // bool; default = false, enables low latency mode and POC mode 2 in the encoder

#define CHECK_AMF_RET(AMF_call)\
{\
	AMF_RESULT Res = AMF_call;\
	if (!(Res== AMF_OK || Res==AMF_ALREADY_INITIALIZED))\
	{\
		UE_LOG(LogVideoEncoder, Error, TEXT("`" #AMF_call "` failed with error code: %d"), Res);\
		/*check(false);*/\
		return false;\
	}\
}

#define CHECK_AMF_NORET(AMF_call)\
{\
	AMF_RESULT Res = AMF_call;\
	if (Res != AMF_OK)\
	{\
		UE_LOG(LogVideoEncoder, Error, TEXT("`" #AMF_call "` failed with error code: %d"), Res);\
	}\
}

FThreadSafeCounter FPixelStreamingAmfVideoEncoder::ImplCounter(0);

namespace
{

// scope-disable particular DX11 Debug Layer errors
class FScopeDisabledDxDebugErrors final
{
private:

public:
	FScopeDisabledDxDebugErrors(TArray<D3D11_MESSAGE_ID>&& ErrorsToDisable)
	{
		TRefCountPtr<ID3D11Debug> Debug;
		ID3D11Device* DxDevice = static_cast<ID3D11Device*>(GDynamicRHI->RHIGetNativeDevice());
		HRESULT HRes = DxDevice->QueryInterface(__uuidof(ID3D11Debug), reinterpret_cast<void**>(Debug.GetInitReference()));

		if (HRes == E_NOINTERFACE)
		{
			// Debug Layer is not enabled, so no need to disable its errors
			return;
		}

		if (!SUCCEEDED(HRes) ||
			!SUCCEEDED(HRes = Debug->QueryInterface(__uuidof(ID3D11InfoQueue), reinterpret_cast<void**>(InfoQueue.GetInitReference()))))
		{
			UE_LOG(LogVideoEncoder, VeryVerbose, TEXT("Failed to get ID3D11InfoQueue: 0x%X - %s"), HRes, *GetComErrorDescription(HRes));
			return;
		}

		D3D11_INFO_QUEUE_FILTER filter = {};
		filter.DenyList.NumIDs = ErrorsToDisable.Num();
		filter.DenyList.pIDList = ErrorsToDisable.GetData();
		bSucceeded = SUCCEEDED(InfoQueue->PushStorageFilter(&filter));
	}

	~FScopeDisabledDxDebugErrors()
	{
		if (bSucceeded)
		{
			InfoQueue->PopStorageFilter();
		}
	}

private:
	TRefCountPtr<ID3D11InfoQueue> InfoQueue;
	bool bSucceeded = false;
};
}

bool FPixelStreamingAmfVideoEncoder::CheckPlatformCompatibility()
{
	if (!IsRHIDeviceAMD())
	{
		UE_LOG(PixelStreamer, Log, TEXT("Can't initialize Pixel Streaming with AMF because no AMD card found"));
		return false;
	}

	void* Handle = FPlatformProcess::GetDllHandle(AMF_DLL_NAME);
	if (Handle == nullptr)
	{
		UE_LOG(PixelStreamer, Error, TEXT("AMD card found, but no AMF DLL installed."));
		return false;
	}
	else
	{
		FPlatformProcess::FreeDllHandle(Handle);
		return true;
	}
}

FPixelStreamingAmfVideoEncoder::FPixelStreamingAmfVideoEncoder()
{
	if (!Initialize())
	{
		UE_LOG(LogVideoEncoder, Fatal, TEXT("Failed to initialize AMF"));
	}
}

FPixelStreamingAmfVideoEncoder::~FPixelStreamingAmfVideoEncoder()
{
	// Increment the counter, so that if any pending render commands sent from EncoderCheckLoop 
	// to the Render Thread still reference "this", they will be ignored because the counter is different
	ImplCounter.Increment();

	Shutdown();
}

bool FPixelStreamingAmfVideoEncoder::Initialize()
{
	EncoderConfig.AverageBitRate = 10000000;
	EncoderConfig.FrameRate = 60; 
	EncoderConfig.Width = 1920;
	EncoderConfig.Height = 1080;
	EncoderConfig.MinQP = 20;

	UE_LOG(LogVideoEncoder, Log, TEXT("FPixelStreamingAmfVideoEncoder initialization with : %dx%d, %d FPS, %.2f Mbps")
		, EncoderConfig.Width
		, EncoderConfig.Height
		, EncoderConfig.FrameRate
		, EncoderConfig.AverageBitRate / 1000000.0f);

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

	FString RHIName = GDynamicRHI->GetName();
	check(RHIName == TEXT("D3D11"));
	ID3D11Device* DxDevice = static_cast<ID3D11Device*>(GDynamicRHI->RHIGetNativeDevice());

	CHECK_AMF_RET(AmfFactory->CreateContext(&AmfContext));

	checkf(DxDevice != nullptr, TEXT("Cannot initialize NvEnc with invalid device"));
	CHECK_AMF_RET(AmfContext->InitDX11(DxDevice));

	CHECK_AMF_RET(AmfFactory->CreateComponent(AmfContext, AMFVideoEncoderVCE_AVC, &AmfEncoder));
	CHECK_AMF_RET(AmfEncoder->SetProperty(AMF_VIDEO_ENCODER_USAGE, AMF_VIDEO_ENCODER_USAGE_ULTRA_LOW_LATENCY));
	CHECK_AMF_RET(AmfEncoder->SetProperty(AMF_VIDEO_ENCODER_LOWLATENCY_MODE, true));
	CHECK_AMF_RET(AmfEncoder->SetProperty(AMF_VIDEO_ENCODER_PROFILE, AMF_VIDEO_ENCODER_PROFILE_BASELINE));
	//CHECK_AMF_RET(AmfEncoder->SetProperty(AMF_VIDEO_ENCODER_B_PIC_PATTERN, 0));
	CHECK_AMF_RET(AmfEncoder->SetProperty(AMF_VIDEO_ENCODER_QUALITY_PRESET, AMF_VIDEO_ENCODER_QUALITY_PRESET_BALANCED));
	CHECK_AMF_RET(AmfEncoder->SetProperty(AMF_VIDEO_ENCODER_TARGET_BITRATE, EncoderConfig.AverageBitRate));
	CHECK_AMF_RET(AmfEncoder->SetProperty(AMF_VIDEO_ENCODER_FRAMESIZE, ::AMFConstructSize(EncoderConfig.Width, EncoderConfig.Height)));
	// #AMF : Is this correct ?
	CHECK_AMF_RET(AmfEncoder->SetProperty(AMF_VIDEO_ENCODER_ASPECT_RATIO, ::AMFConstructRatio(EncoderConfig.Width, EncoderConfig.Height)));
	CHECK_AMF_RET(AmfEncoder->SetProperty(AMF_VIDEO_ENCODER_FRAMERATE, ::AMFConstructRate(EncoderConfig.FrameRate, 1)));

	// generate key-frames every second, useful for seeking in resulting .mp4 and keeping recording ring buffer
	// of second-precise duration
	uint64 IdrPeriod = EncoderConfig.FrameRate;
	CHECK_AMF_RET(AmfEncoder->SetProperty(AMF_VIDEO_ENCODER_IDR_PERIOD, IdrPeriod));
	// insert SPS/PPS before every key-frame. .mp4 file video stream must start from SPS/PPS. Their size is
	// negligible so having them before every key-frame is not an issue but they presence simplifies
	// implementation significantly. Otherwise we would extract SPS/PPS from the first key-frame and store
	// them manually at the beginning of resulting .mp4 file
	CHECK_AMF_RET(AmfEncoder->SetProperty(AMF_VIDEO_ENCODER_HEADER_INSERTION_SPACING, IdrPeriod));

	CHECK_AMF_RET(AmfEncoder->Init(amf::AMF_SURFACE_RGBA, EncoderConfig.Width, EncoderConfig.Height));

	CHECK_AMF_RET(AmfEncoder->SetProperty(AMF_VIDEO_ENCODER_MIN_QP, EncoderConfig.MinQP));
	CHECK_AMF_RET(AmfEncoder->SetProperty(AMF_VIDEO_ENCODER_QP_I, EncoderConfig.MinQP));
	CHECK_AMF_RET(AmfEncoder->SetProperty(AMF_VIDEO_ENCODER_QP_P, EncoderConfig.MinQP));
	CHECK_AMF_RET(AmfEncoder->SetProperty(AMF_VIDEO_ENCODER_QP_B, EncoderConfig.MinQP));


	//LogAmfPropertyStorage(AmfEncoder);

	FBufferId Id = 0;
	for (FFrame& Frame : BufferedFrames)
	{
		// We keep `Id` as const, because it not supposed to change at all once initialized
		*(const_cast<FBufferId*>(&Frame.Id)) = Id++;
		ResetResolvedBackBuffer(Frame.InputFrame, EncoderConfig.Width, EncoderConfig.Height);
	}

	UE_LOG(LogVideoEncoder, Log, TEXT("AMF H.264 encoder initialised, v.0x%X"), AmfVersion);

	return true;
}

void FPixelStreamingAmfVideoEncoder::Shutdown()
{
	// BufferedFrames keep references to AMFData, we need to release them before destroying AMF
	for (FFrame& Frame : BufferedFrames)
	{
		Frame.OutputFrame.EncodedData = nullptr;
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

bool FPixelStreamingAmfVideoEncoder::CopyBackBuffer(const FTexture2DRHIRef& BackBuffer, FTimespan Timestamp, FBufferId& BufferId)
{
	check(IsInRenderingThread());

	// First process output, to free slots we can use for the new request
	if (!ProcessOutput())
	{
		return false;
	}

	// Find a free slot we can use
	FFrame* Frame = nullptr;
	for (FFrame& Slot : BufferedFrames)
	{
		if (Slot.State == EFrameState::Free)
		{
			Frame = &Slot;
			BufferId = Slot.Id;
			break;
		}
	}

	if (!Frame)
	{
		UE_LOG(LogVideoEncoder, Verbose, TEXT("Frame dropped because Amf queue is full"));
		return false;
	}

	Frame->FrameIdx = CapturedFrameCount++;
	Frame->InputFrame.CaptureTs = Timestamp;
	CopyBackBuffer(BackBuffer, Frame->InputFrame);

	UE_LOG(LogVideoEncoder, Verbose, TEXT("Buffer #%d (%d) captured"), Frame->FrameIdx, BufferId);
	Frame->State = EFrameState::Captured;

	return true;
}

void FPixelStreamingAmfVideoEncoder::CopyBackBuffer(const FTexture2DRHIRef& BackBuffer, FInputFrame& InputFrame)
{
	SCOPE_CYCLE_COUNTER(STAT_Amf_CopyBackBuffer);

	UpdateRes(BackBuffer, InputFrame);

	IRendererModule* RendererModule = &FModuleManager::GetModuleChecked<IRendererModule>("Renderer");
	FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();

	if (BackBuffer->GetFormat() == InputFrame.BackBuffer->GetFormat() &&
		BackBuffer->GetSizeXY() == InputFrame.BackBuffer->GetSizeXY())
	{
		RHICmdList.CopyToResolveTarget(BackBuffer, InputFrame.BackBuffer, FResolveParams{});
	}
	else // Texture format mismatch, use a shader to do the copy.
	{
		// #todo-renderpasses there's no explicit resolve here? Do we need one?
		FRHIRenderPassInfo RPInfo(InputFrame.BackBuffer, ERenderTargetActions::Load_Store);
		RHICmdList.BeginRenderPass(RPInfo, TEXT("CopyBackbuffer"));
		{
			RHICmdList.SetViewport(0, 0, 0.0f, InputFrame.BackBuffer->GetSizeX(), InputFrame.BackBuffer->GetSizeY(), 1.0f);

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

			if (InputFrame.BackBuffer->GetSizeX() != BackBuffer->GetSizeX() || InputFrame.BackBuffer->GetSizeY() != BackBuffer->GetSizeY())
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
				InputFrame.BackBuffer->GetSizeX(),			// Dest Width
				InputFrame.BackBuffer->GetSizeY(),			// Dest Height
				0, 0,									// Source U, V
				1, 1,									// Source USize, VSize
				InputFrame.BackBuffer->GetSizeXY(),		// Target buffer size
				FIntPoint(1, 1),						// Source texture size
				*VertexShader,
				EDRF_Default);
		}
		RHICmdList.EndRenderPass();
	}
}

bool FPixelStreamingAmfVideoEncoder::UpdateFramerate()
{
	// #AMF : Implement this
	return true;
}

// #AMF : Move most of this code shared common code
void FPixelStreamingAmfVideoEncoder::UpdateRes(const FTexture2DRHIRef& BackBuffer, FInputFrame& InputFrame)
{
	check(IsInRenderingThread());

	// find out what resolution we'd like to stream, it's either "native" (BackBuffer) resolution or something configured specially
	bool bUseBackBufferSize = CVarEncoderUseBackBufferSize.GetValueOnRenderThread() > 0;
	uint32 Width;
	uint32 Height;
	if (bUseBackBufferSize)
	{
		Width = BackBuffer->GetSizeX();
		Height = BackBuffer->GetSizeY();
	}
	else
	{
		FString EncoderTargetSize = CVarEncoderTargetSize.GetValueOnRenderThread();
		FString TargetWidth, TargetHeight;
		bool bValidSize = EncoderTargetSize.Split(TEXT("x"), &TargetWidth, &TargetHeight);
		if (bValidSize)
		{
			Width = FCString::Atoi(*TargetWidth);
			Height = FCString::Atoi(*TargetHeight);
		}
		else
		{
			UE_LOG(LogVideoEncoder, Error, TEXT("CVarEncoderTargetSize is not in a valid format: %s. It should be e.g: \"1920x1080\""), *EncoderTargetSize);
			CVarEncoderTargetSize->Set(*FString::Printf(TEXT("%dx%d"), InputFrame.BackBuffer->GetSizeX(), InputFrame.BackBuffer->GetSizeY()));
			return;
		}
	}

	// check if target resolution matches of currently allocated `InputFrame.BackBuffer` resolution
	if (InputFrame.BackBuffer->GetSizeX() == Width && InputFrame.BackBuffer->GetSizeY() == Height)
	{
		return;
	}

	// reallocate and re-register InputFrame with NvEnc
	ResetResolvedBackBuffer(InputFrame, Width, Height);
}

void FPixelStreamingAmfVideoEncoder::EncodeFrame(FBufferId BufferId, const webrtc::EncodedImage& EncodedFrame, uint32 Bitrate)
{
	FFrame& Frame = BufferedFrames[BufferId];
	checkf(Frame.State==EFrameState::Captured, TEXT("Buffer %d : Expected state %d, but found %d"), BufferId, (int)EFrameState::Captured, (int)Frame.State);

	Frame.OutputFrame.EncodedFrame = EncodedFrame;
	Frame.OutputFrame.EncodedFrame._encodedWidth = EncoderConfig.Width;
	Frame.OutputFrame.EncodedFrame._encodedHeight = EncoderConfig.Height;

	int32 CurrImplCounter = ImplCounter.GetValue();
	ENQUEUE_RENDER_COMMAND(AmfEncEncodeFrame)(
	[this, &Frame, BufferId, Bitrate, CurrImplCounter](FRHICommandListImmediate& RHICmdList)
	{
		if (CurrImplCounter != ImplCounter.GetValue()) // Check if the "this" we captured is still valid
			return;

		EncodeFrameInRenderingThread(Frame, Bitrate);

		UE_LOG(LogVideoEncoder, VeryVerbose, TEXT("Buffer #%d (%d), ts %u started encoding"), Frame.FrameIdx, BufferId, Frame.OutputFrame.EncodedFrame.Timestamp());
	});
}

void FPixelStreamingAmfVideoEncoder::UpdateEncoderConfig(const FInputFrame& InputFrame, uint32 Bitrate)
{
	check(IsInRenderingThread());

	// #AMF : Log what's is being changed

	// #AMF : Remove these if not needed
	bool bSettingsChanged = false;
	bool bResolutionChanged = false;

	float MaxBitrate = CVarEncoderMaxBitrate.GetValueOnRenderThread();
	uint32 ClampedBitrate = FMath::Min(Bitrate, static_cast<uint32_t>(MaxBitrate));
	if (EncoderConfig.AverageBitRate != ClampedBitrate)
	{
		EncoderConfig.AverageBitRate = ClampedBitrate;
		RequestedBitrateMbps = ClampedBitrate / 1000000.0;
		bSettingsChanged = true;
		CHECK_AMF_NORET(AmfEncoder->SetProperty(AMF_VIDEO_ENCODER_TARGET_BITRATE, EncoderConfig.AverageBitRate));
	}

	uint32 MinQP = static_cast<uint32>(CVarEncoderMinQP.GetValueOnRenderThread());
	MinQP = FMath::Clamp(MinQP, 0u, 54u);
	if (EncoderConfig.MinQP != MinQP)
	{
		EncoderConfig.MinQP = MinQP;
		UE_LOG(LogVideoEncoder, Log, TEXT("MinQP %u"), MinQP);
		bSettingsChanged = true;

		CHECK_AMF_NORET(AmfEncoder->SetProperty(AMF_VIDEO_ENCODER_MIN_QP, ::AMFConstructRate(EncoderConfig.MinQP, 1)));
		CHECK_AMF_NORET(AmfEncoder->SetProperty(AMF_VIDEO_ENCODER_QP_I, ::AMFConstructRate(EncoderConfig.MinQP, 1)));
		CHECK_AMF_NORET(AmfEncoder->SetProperty(AMF_VIDEO_ENCODER_QP_P, ::AMFConstructRate(EncoderConfig.MinQP, 1)));
		CHECK_AMF_NORET(AmfEncoder->SetProperty(AMF_VIDEO_ENCODER_QP_B, ::AMFConstructRate(EncoderConfig.MinQP, 1)));
	}

	// #AMF : Implement this
#if 0
	NV_ENC_PARAMS_RC_MODE RcMode = ToRcMode(CVarEncoderRateControl.GetValueOnRenderThread());
	if (RcMode != NvEncConfig.rcParams.rateControlMode)
	{
		NvEncConfig.rcParams.rateControlMode = RcMode;
		UE_LOG(LogVideoEncoder, Log, TEXT("Rate Control mode %s"), ToString(RcMode));
		bSettingsChanged = true;
	}

	if (UpdateFramerate())
	{
		bSettingsChanged = true;
	}

#endif

	if (InputFrame.BackBuffer->GetSizeX() != EncoderConfig.Width ||
		InputFrame.BackBuffer->GetSizeY() != EncoderConfig.Height)
	{
		EncoderConfig.Width = InputFrame.BackBuffer->GetSizeX();
		EncoderConfig.Height = InputFrame.BackBuffer->GetSizeY();
		EncoderConfig.ForceIDR = true;

		bSettingsChanged = true;
		bResolutionChanged = true;
		CHECK_AMF_NORET(AmfEncoder->SetProperty(AMF_VIDEO_ENCODER_FRAMESIZE, ::AMFConstructSize(EncoderConfig.Width, EncoderConfig.Height)));
		// #AMF : Is this correct ?
		CHECK_AMF_NORET(AmfEncoder->SetProperty(AMF_VIDEO_ENCODER_ASPECT_RATIO, ::AMFConstructRatio(EncoderConfig.Width, EncoderConfig.Height)));

	}
}

void FPixelStreamingAmfVideoEncoder::EncodeFrameInRenderingThread(FFrame& Frame, uint32 Bitrate)
{
	check(IsInRenderingThread());
	check(Frame.State==EFrameState::Captured);

	UpdateEncoderConfig(Frame.InputFrame, Bitrate);

	//
	// Process the new input
	{
		SCOPE_CYCLE_COUNTER(STAT_Amf_ProcessInput);
		Frame.State = EFrameState::Encoding;
		EncodingQueue.Enqueue(&Frame);
		FRHICommandList& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();
		if (RHICmdList.Bypass())
		{
			FRHISubmitFrameToEncoder Command(this, &Frame);
			Command.Execute(RHICmdList);
		}
		else
		{
			ALLOC_COMMAND_CL(RHICmdList, FRHISubmitFrameToEncoder)(this, &Frame);
		}
	}
}

bool FPixelStreamingAmfVideoEncoder::SubmitFrameToEncoder(FFrame& Frame)
{
	SCOPE_CYCLE_COUNTER(STAT_Amf_SubmitFrameToEncoder);

	check(Frame.State==EFrameState::Encoding);

	// #AMF : Check how fast/slow this CreateSurfaceFromDXN11Native is. Maybe this can be done once per FFrame at startup?
	//		  E.g: FFrame::AmfSurfaceIn
	amf::AMFSurfacePtr AmfSurfaceIn;
	ID3D11Texture2D* ResolvedBackBufferDX11 = static_cast<ID3D11Texture2D*>(GetD3D11TextureFromRHITexture(Frame.InputFrame.BackBuffer)->GetResource());
	CHECK_AMF_RET(AmfContext->CreateSurfaceFromDX11Native(ResolvedBackBufferDX11, &AmfSurfaceIn, nullptr));

	if (Frame.OutputFrame.EncodedFrame._frameType == webrtc::kVideoFrameKey)
	{
		CHECK_AMF_RET(AmfSurfaceIn->SetProperty(AMF_VIDEO_ENCODER_FORCE_PICTURE_TYPE, AMF_VIDEO_ENCODER_PICTURE_TYPE_IDR));
	}

	{
		// if `-d3ddebug` is enabled `SubmitInput` crashes with DX11 error, see output window
		// we believe it's an internal AMF shader problem so we disable those errors explicitly, otherwise
		// DX Debug Layer can't be used at all
		FScopeDisabledDxDebugErrors Errors({
			D3D11_MESSAGE_ID_DEVICE_UNORDEREDACCESSVIEW_RETURN_TYPE_MISMATCH,
			D3D11_MESSAGE_ID_DEVICE_CSSETUNORDEREDACCESSVIEWS_TOOMANYVIEWS
		});

		{
			SCOPE_CYCLE_COUNTER(STAT_Amf_SubmitInput);
			CHECK_AMF_RET(AmfEncoder->SubmitInput(AmfSurfaceIn));
		}
	}

	return true;
}

// check if output is ready and handle it
bool FPixelStreamingAmfVideoEncoder::ProcessOutput()
{
	SCOPE_CYCLE_COUNTER(STAT_Amf_QueryEncoderOutput);

	check(IsInRenderingThread());

	while(!EncodingQueue.IsEmpty())
	{
		FFrame* Frame = *EncodingQueue.Peek();
		check(Frame->State==EFrameState::Encoding);

		amf::AMFDataPtr EncodedData;
		AMF_RESULT Ret;
		{
			SCOPE_CYCLE_COUNTER(STAT_Amf_QueryOutput);
			Ret = AmfEncoder->QueryOutput(&EncodedData);
		}

		if (Ret == AMF_OK && EncodedData != nullptr)
		{
			UE_LOG(LogVideoEncoder, VeryVerbose, TEXT("frame #%d encoded"), Frame->Id);
			verify(EncodingQueue.Pop()==true);
			Frame->OutputFrame.EncodedData = EncodedData;
			if (!HandleEncodedFrame(*Frame))
			{
				return false;
			}
		}
		else if (Ret == AMF_REPEAT)
		{
			break; // not ready yet
		}
		else
		{
			UE_LOG(LogVideoEncoder, Error, TEXT("Failed to query AMF H.264 Encoder output: %d, %p"), Ret, EncodedData.GetPtr());
			return false;
		}
	}

	return true;
}

bool FPixelStreamingAmfVideoEncoder::HandleEncodedFrame(FFrame& Frame)
{
	check(Frame.State==EFrameState::Encoding);

	FOutputFrame& OutputFrame = Frame.OutputFrame;

	FHUDStats& Stats = FHUDStats::Get();

	{
		// Query for buffer interface
		amf::AMFBufferPtr EncodedBuffer(Frame.OutputFrame.EncodedData);
		void* EncodedBufferPtr = EncodedBuffer->GetNative();
		size_t EncodedBufferSize = EncodedBuffer->GetSize();
		// Check if it's a keyframe
		uint64 OutputFrameType;
		CHECK_AMF_RET(EncodedBuffer->GetProperty(AMF_VIDEO_ENCODER_OUTPUT_DATA_TYPE, &OutputFrameType));
		bool bKeyFrame = OutputFrameType==AMF_VIDEO_ENCODER_OUTPUT_DATA_TYPE_IDR ? true : false;

		checkf(bKeyFrame == true || OutputFrame.EncodedFrame._frameType==webrtc::kVideoFrameDelta, TEXT("key frame requested by webrtc but not provided by Amf"));
		OutputFrame.EncodedFrame._frameType = bKeyFrame ? webrtc::kVideoFrameKey : webrtc::kVideoFrameDelta;

		// #AMF : Implement this. No idea how to get the frame average QP
		//OutputFrame.EncodedFrame.qp_ = 20;

		EncodedFrameBuffer = TArray<uint8>(reinterpret_cast<const uint8*>(EncodedBufferPtr), EncodedBufferSize);

		int64 CaptureTs = 0;
		if (Stats.bEnabled)
		{
			CaptureTs = Frame.InputFrame.CaptureTs.GetTicks();
			EncodedFrameBuffer.Append(reinterpret_cast<const uint8*>(&CaptureTs), sizeof(CaptureTs));
		}

		OutputFrame.EncodedFrame._buffer = EncodedFrameBuffer.GetData();
		OutputFrame.EncodedFrame._length = OutputFrame.EncodedFrame._size = EncodedFrameBuffer.Num();
	}

	OutputFrame.EncodedFrame.timing_.encode_finish_ms = rtc::TimeMicros() / 1000;
	OutputFrame.EncodedFrame.timing_.flags = webrtc::VideoSendTiming::kTriggeredByTimer;

	double LatencyMs = static_cast<double>(OutputFrame.EncodedFrame.timing_.encode_finish_ms - OutputFrame.EncodedFrame.timing_.encode_start_ms);
	double BitrateMbps = EncodedFrameBuffer.Num() * 8 * EncoderConfig.FrameRate / 1000000.0;

	if (Stats.bEnabled)
	{
		Stats.EncoderLatencyMs.Update(LatencyMs);
		Stats.EncoderBitrateMbps.Update(BitrateMbps);
		Stats.EncoderQP.Update(OutputFrame.EncodedFrame.qp_);
	}

	UE_LOG(LogVideoEncoder, VeryVerbose, TEXT("encoded %s ts %u, capture ts %lld, QP %d/%.0f,  latency %.0f/%.0f ms, bitrate %.3f/%.3f/%.3f Mbps, %zu bytes")
		, ToString(OutputFrame.EncodedFrame._frameType)
		, OutputFrame.EncodedFrame.Timestamp()
		, Frame.InputFrame.CaptureTs.GetTicks()
		, OutputFrame.EncodedFrame.qp_
		, Stats.EncoderQP.Get()
		, LatencyMs
		, Stats.EncoderLatencyMs.Get()
		, EncoderConfig.AverageBitRate / 1000000.0f
		, BitrateMbps
		, Stats.EncoderBitrateMbps.Get()
		, OutputFrame.EncodedFrame._length);

	// Stream the encoded frame
	{
		SCOPE_CYCLE_COUNTER(STAT_Amf_StreamEncodedFrame);
		OnEncodedFrame(OutputFrame.EncodedFrame);
	}

	Frame.State = EFrameState::Free;

	return true;
}

void FPixelStreamingAmfVideoEncoder::OnFrameDropped(FBufferId BufferId)
{
	checkf(BufferedFrames[BufferId].State==EFrameState::Captured, TEXT("Buffer %d: Expected state %d, found %d"), BufferId, (int)EFrameState::Captured, (int)BufferedFrames[BufferId].State);
	BufferedFrames[BufferId].State = EFrameState::Free;
	UE_LOG(LogVideoEncoder, Log, TEXT("Buffer #%d (%d) dropped"), BufferedFrames[BufferId].FrameIdx, BufferId);
}

void FPixelStreamingAmfVideoEncoder::SubscribeToFrameEncodedEvent(FVideoEncoder& Subscriber)
{
	FScopeLock lock{ &SubscribersMutex };
	bool AlreadyInSet = false;
	Subscribers.Add(&Subscriber, &AlreadyInSet);
	check(!AlreadyInSet);
}

void FPixelStreamingAmfVideoEncoder::UnsubscribeFromFrameEncodedEvent(FVideoEncoder& Subscriber)
{
	FScopeLock lock{ &SubscribersMutex };
	int32 res = Subscribers.Remove(&Subscriber);
	check(res == 1);
}

void FPixelStreamingAmfVideoEncoder::ResetResolvedBackBuffer(FInputFrame& InputFrame, uint32 Width, uint32 Height)
{
	InputFrame.BackBuffer.SafeRelease();

	// Make sure format used here is compatible with AMF_SURFACE_FORMAT specified in encoder Init() function.
	FRHIResourceCreateInfo CreateInfo;
	InputFrame.BackBuffer = RHICreateTexture2D(Width, Height, EPixelFormat::PF_R8G8B8A8, 1, 1, TexCreate_RenderTargetable, CreateInfo);
}

//#AMF Move this to the base class
void FPixelStreamingAmfVideoEncoder::OnEncodedFrame(const webrtc::EncodedImage& EncodedImage)
{
	FScopeLock lock{ &SubscribersMutex };
	for (auto s : Subscribers)
	{
		s->OnEncodedFrame(EncodedImage);
	}
}

