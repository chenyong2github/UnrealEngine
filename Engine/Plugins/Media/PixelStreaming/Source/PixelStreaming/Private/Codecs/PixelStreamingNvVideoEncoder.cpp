// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "PixelStreamingNvVideoEncoder.h"

#include "VideoEncoder.h"
#include "Utils.h"
#include "HUDStats.h"

#include "Stats/Stats.h"
#include "HAL/IConsoleManager.h"
#include "HAL/Thread.h"
#include "RenderingThread.h"
#include "RendererInterface.h"
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
#include "Modules/ModuleManager.h"

THIRD_PARTY_INCLUDES_START
	#include "Windows/AllowWindowsPlatformTypes.h"
	#include <d3d11_1.h>
	#include <d3d12.h>
	#include <dxgi1_4.h>
	#include "Windows/HideWindowsPlatformTypes.h"
THIRD_PARTY_INCLUDES_END

#if NVENC_VIDEO_ENCODER_DEBUG
	#include "ClearQuad.h"
#endif

TAutoConsoleVariable<float> CVarEncoderMaxBitrate(
	TEXT("Encoder.MaxBitrate"),
	50000000,
	TEXT("Max bitrate no matter what WebRTC says, in Bps"),
	ECVF_RenderThreadSafe);

TAutoConsoleVariable<FString> CVarEncoderTargetSize(
	TEXT("Encoder.TargetSize"),
	TEXT("1920x1080"),
	TEXT("Encoder target size in format widthxheight"),
	ECVF_Cheat);

TAutoConsoleVariable<int32> CVarEncoderUseBackBufferSize(
	TEXT("Encoder.UseBackBufferSize"),
	1,
	TEXT("Whether to use back buffer size or custom size"),
	ECVF_Cheat);

TAutoConsoleVariable<int32> CVarEncoderPrioritiseQuality(
	TEXT("Encoder.PrioritiseQuality"),
	0,
	TEXT("Reduces framerate automatically on bitrate reduction to trade FPS/latency for video quality"),
	ECVF_Cheat);

// #AMF(Andriy) : This is called Mbps, but the comment is Kbps
TAutoConsoleVariable<float> CVarEncoderLowMbps(
	TEXT("Encoder.LowBitrate"),
	1,
	TEXT("Lower bound of bitrate for quality adaptation, Kbps"),
	ECVF_Default);

// #AMF(Andriy) : This is called Mbps, but the comment is Kbps
TAutoConsoleVariable<float> CVarEncoderHighMbps(
	TEXT("Encoder.HighBitrate"),
	5,
	TEXT("Upper bound of bitrate for quality adaptation, Kbps"),
	ECVF_Default);

TAutoConsoleVariable<float> CVarEncoderMinFPS(
	TEXT("Encoder.MinFPS"),
	10.0,
	TEXT("Minimal FPS for quality adaptation"),
	ECVF_Default);

TAutoConsoleVariable<int> CVarEncoderMinQP(
	TEXT("Encoder.MinQP"),
	20,
	TEXT("0-54, lower values result in better quality but higher bitrate"),
	ECVF_Default);

TAutoConsoleVariable<FString> CVarEncoderRateControl(
	TEXT("Encoder.RateControl"),
	TEXT("CBR"),
	TEXT("PixelStreaming video encoder RateControl mode. Supported modes are `ConstQP`, `VBR`, `CBR`, `VBR_MinQP`"),
	ECVF_Default);


namespace
{
	constexpr const TCHAR* GetDllName()
	{
#if defined PLATFORM_WINDOWS
#if defined _WIN64
		return TEXT("nvEncodeAPI64.dll");
#else
		return TEXT("nvEncodeAPI.dll");
#endif
#elif defined PLATFORM_LINUX
		return TEXT("libnvidia-encode.so.1");
#else
		return TEXT("");
#endif
	}

	DECLARE_STATS_GROUP(TEXT("NvEnc"), STATGROUP_NvEnc, STATCAT_Advanced);

	DECLARE_CYCLE_STAT(TEXT("CopyBackBuffer"), STAT_NvEnc_CopyBackBuffer, STATGROUP_NvEnc);
	DECLARE_CYCLE_STAT(TEXT("SendBackBufferToEncoder"), STAT_NvEnc_SendBackBufferToEncoder, STATGROUP_NvEnc);
	DECLARE_CYCLE_STAT(TEXT("WaitForEncodeEvent"), STAT_NvEnc_WaitForEncodeEvent, STATGROUP_NvEnc);
	DECLARE_CYCLE_STAT(TEXT("RetrieveEncodedFrame"), STAT_NvEnc_RetrieveEncodedFrame, STATGROUP_NvEnc);
	DECLARE_CYCLE_STAT(TEXT("StreamEncodedFrame"), STAT_NvEnc_StreamEncodedFrame, STATGROUP_NvEnc);


	NV_ENC_PARAMS_RC_MODE ToRcMode(FString RcModeStr)
	{
		NV_ENC_PARAMS_RC_MODE Res;

		RcModeStr.ToLowerInline();
		if (RcModeStr == TEXT("constqp"))
		{
			Res = NV_ENC_PARAMS_RC_CONSTQP;
		}
		else if (RcModeStr == TEXT("vbr"))
		{
			Res = NV_ENC_PARAMS_RC_VBR;
		}
		else if (RcModeStr == TEXT("cbr"))
		{
			Res = NV_ENC_PARAMS_RC_CBR;
		}
		else if (RcModeStr == TEXT("cbr_lowdelay_hq"))
		{
			Res = NV_ENC_PARAMS_RC_CBR_LOWDELAY_HQ;
		}
		else if (RcModeStr == TEXT("cbr_hq"))
		{
			Res = NV_ENC_PARAMS_RC_CBR_HQ;
		}
		else if (RcModeStr == TEXT("vbr_hq"))
		{
			Res = NV_ENC_PARAMS_RC_VBR_HQ;
		}
		else
		{
			UE_LOG(LogVideoEncoder, Error, TEXT("Invalid Video Encoder Rate Control Mode \"%s\" ignored. Default \"CBR\" applied"), *CVarEncoderRateControl.GetValueOnAnyThread());
			Res = NV_ENC_PARAMS_RC_CBR;
		}

		return Res;
	}

	const TCHAR* ToString(NV_ENC_PARAMS_RC_MODE RcMode)
	{
		switch (RcMode)
		{
		case NV_ENC_PARAMS_RC_CONSTQP:
			return TEXT("ConstQP");
		case NV_ENC_PARAMS_RC_VBR:
			return TEXT("VBR");
		case NV_ENC_PARAMS_RC_CBR:
			return TEXT("CBR");
		case NV_ENC_PARAMS_RC_CBR_LOWDELAY_HQ:
			return TEXT("CBR_LOWDELAY_HQ");
		case NV_ENC_PARAMS_RC_CBR_HQ:
			return TEXT("CBR_HQ");
		case NV_ENC_PARAMS_RC_VBR_HQ:
			return TEXT("VBR_HQ");
		default:
			checkNoEntry();
			return TEXT("Unknown");
		}
	}

	static bool D3D_ShouldCreateWithD3DDebug()
	{
		// Use a debug device if specified on the command line.
		static bool bCreateWithD3DDebug =
			FParse::Param(FCommandLine::Get(), TEXT("d3ddebug")) ||
			FParse::Param(FCommandLine::Get(), TEXT("d3debug")) ||
			FParse::Param(FCommandLine::Get(), TEXT("dxdebug"));
		return bCreateWithD3DDebug;
	}

	static bool D3D_ShouldAllowAsyncResourceCreation()
	{
		static bool bAllowAsyncResourceCreation = !FParse::Param(FCommandLine::Get(), TEXT("nod3dasync"));
		return bAllowAsyncResourceCreation;
	}


}

FPixelStreamingNvVideoEncoder::FEncoderDevice::FEncoderDevice()
{
	if (GDynamicRHI)
	{
		FString RHIName = GDynamicRHI->GetName();

		TRefCountPtr<IDXGIDevice> DXGIDevice;
		uint32 DeviceFlags = D3D_ShouldAllowAsyncResourceCreation() ? 0 : D3D11_CREATE_DEVICE_SINGLETHREADED;
		if (D3D_ShouldCreateWithD3DDebug())
			DeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
		D3D_FEATURE_LEVEL FeatureLevel = D3D_FEATURE_LEVEL_11_1;
		TRefCountPtr<IDXGIAdapter> Adapter;

		if (RHIName == TEXT("D3D11"))
		{
			auto UE4D3DDevice = static_cast<ID3D11Device*>(GDynamicRHI->RHIGetNativeDevice());
			checkf(UE4D3DDevice != nullptr, TEXT("Cannot initialize NvEnc with invalid device"));
			CHECK_HR_DX9_VOID(UE4D3DDevice->QueryInterface(__uuidof(IDXGIDevice), (void**)DXGIDevice.GetInitReference()));
			CHECK_HR_DX9_VOID(DXGIDevice->GetAdapter(Adapter.GetInitReference()));
			FeatureLevel = D3D_FEATURE_LEVEL_11_0;
		}
		else if (RHIName == TEXT("D3D12"))
		{
			auto UE4D3DDevice = static_cast<ID3D12Device*>(GDynamicRHI->RHIGetNativeDevice());
			checkf(UE4D3DDevice != nullptr, TEXT("Cannot initialize NvEnc with invalid device"));
			LUID AdapterLuid = UE4D3DDevice->GetAdapterLuid();
			TRefCountPtr<IDXGIFactory4> DXGIFactory;
			CHECK_HR_DX9_VOID(CreateDXGIFactory(IID_PPV_ARGS(DXGIFactory.GetInitReference())));
			// To use a shared texture from D3D12, we need to use a D3D 11.1 device, because we need the
			// D3D11Device1::OpenSharedResource1 method
			FeatureLevel = D3D_FEATURE_LEVEL_11_1;
			CHECK_HR_DX9_VOID(DXGIFactory->EnumAdapterByLuid(AdapterLuid, IID_PPV_ARGS(Adapter.GetInitReference())));
		}
		else
		{
			UE_LOG(PixelStreamer, Fatal, TEXT("NvEnc requires D3D11/D3D12"));
			return;
		}

		D3D_FEATURE_LEVEL ActualFeatureLevel;

		CHECK_HR_DX9_VOID(D3D11CreateDevice(
			Adapter,
			D3D_DRIVER_TYPE_UNKNOWN,
			NULL,
			DeviceFlags,
			&FeatureLevel,
			1,
			D3D11_SDK_VERSION,
			Device.GetInitReference(),
			&ActualFeatureLevel,
			DeviceContext.GetInitReference()
		));

		// If we are using D3D12, make sure we got a 11.1 device
		if (FeatureLevel == D3D_FEATURE_LEVEL_11_1 && ActualFeatureLevel!=D3D_FEATURE_LEVEL_11_1)
		{
			UE_LOG(PixelStreamer, Fatal, TEXT("Failed to create a D3D 11.1 device. This is needed when using the D3D12 renderer."));
		}
	}
	else
	{
		UE_LOG(PixelStreamer, Error, TEXT("Attempting to create Encoder Device without existing RHI"));
	}
}

const uint32 BitstreamSize = 1024 * 1024 * 2;

#define NV_RESULT(NvFunction) NvFunction == NV_ENC_SUCCESS

bool FPixelStreamingNvVideoEncoder::CheckPlatformCompatibility()
{
	if (!IsRHIDeviceNVIDIA())
	{
		UE_LOG(PixelStreamer, Log, TEXT("Can't initialize Pixel Streaming with NvEnc because no NVidia card found"));
		return false;
	}

	void* Handle = FPlatformProcess::GetDllHandle(GetDllName());
	if (Handle == nullptr)
	{
		UE_LOG(PixelStreamer, Error, TEXT("NVidia card found, but no NvEnc DLL installed."));
		return false;
	}
	else
	{
		FPlatformProcess::FreeDllHandle(Handle);
		return true;
	}
}

FPixelStreamingNvVideoEncoder::FPixelStreamingNvVideoEncoder()
	: InitialMaxFPS(GEngine->GetMaxFPS())
{
	DllHandle = FPlatformProcess::GetDllHandle(GetDllName());
	checkf(DllHandle != nullptr, TEXT("Failed to load NvEncode dll"));
	if (!DllHandle)
	{
		return;
	}

	Init();
}

void FPixelStreamingNvVideoEncoder::Init()
{
	if (InitialMaxFPS == 0)
	{
		const float DefaultFPS = 60.0;
		InitialMaxFPS = DefaultFPS;

		//check(IsInRenderingThread());
		AsyncTask(ENamedThreads::GameThread, [this]()
		{
			GEngine->SetMaxFPS(InitialMaxFPS);
		});
	}

	EncoderDevice = MakeUnique<FEncoderDevice>();

	_NVENCSTATUS Result;

	// Load NvEnc dll and create an NvEncode API instance
	{
		// define a function pointer for creating an instance of nvEncodeAPI
		typedef NVENCSTATUS(NVENCAPI *NVENCAPIPROC)(NV_ENCODE_API_FUNCTION_LIST*);
		NVENCAPIPROC NvEncodeAPICreateInstanceFunc;

#if defined PLATFORM_WINDOWS
#	pragma warning(push)
#		pragma warning(disable: 4191) // https://stackoverflow.com/a/4215425/453271
		NvEncodeAPICreateInstanceFunc = (NVENCAPIPROC)GetProcAddress((HMODULE)DllHandle, "NvEncodeAPICreateInstance");
#	pragma warning(pop)
#else
		NvEncodeAPICreateInstanceFunc = (NVENCAPIPROC)dlsym(DllHandle, "NvEncodeAPICreateInstance");
#endif
		checkf(NvEncodeAPICreateInstanceFunc != nullptr, TEXT("NvEncodeAPICreateInstance failed"));
		NvEncodeAPI.Reset(new NV_ENCODE_API_FUNCTION_LIST);
		FMemory::Memzero(NvEncodeAPI.Get(), sizeof(NV_ENCODE_API_FUNCTION_LIST));
		NvEncodeAPI->version = NV_ENCODE_API_FUNCTION_LIST_VER;
		Result = NvEncodeAPICreateInstanceFunc(NvEncodeAPI.Get());
		checkf(NV_RESULT(Result), TEXT("Unable to create NvEnc API function list: error %d"), Result);
	}
	// Open an encoding session
	{
		NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS OpenEncodeSessionExParams;
		FMemory::Memzero(OpenEncodeSessionExParams);
		OpenEncodeSessionExParams.version = NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS_VER;
		OpenEncodeSessionExParams.device = EncoderDevice->Device;
		OpenEncodeSessionExParams.deviceType = NV_ENC_DEVICE_TYPE_DIRECTX;	// Currently only DX11 is supported
		OpenEncodeSessionExParams.apiVersion = NVENCAPI_VERSION;
		Result = NvEncodeAPI->nvEncOpenEncodeSessionEx(&OpenEncodeSessionExParams, &EncoderInterface);
		checkf(NV_RESULT(Result), TEXT("Unable to open NvEnc encoding session (status: %d)"), Result);
	}
	// Set initialization parameters
	{
		FMemory::Memzero(NvEncInitializeParams);
		NvEncInitializeParams.version = NV_ENC_INITIALIZE_PARAMS_VER;
		// hardcoded to FullHD for now, if actual resolution is different it will be changed dynamically
		NvEncInitializeParams.encodeWidth = NvEncInitializeParams.darWidth = 1920;
		NvEncInitializeParams.encodeHeight = NvEncInitializeParams.darHeight = 1080;
		NvEncInitializeParams.encodeGUID = NV_ENC_CODEC_H264_GUID;
		NvEncInitializeParams.presetGUID = NV_ENC_PRESET_LOW_LATENCY_HQ_GUID;
		NvEncInitializeParams.frameRateNum = 60;
		NvEncInitializeParams.frameRateDen = 1;
		NvEncInitializeParams.enablePTD = 1;
		NvEncInitializeParams.reportSliceOffsets = 0;
		NvEncInitializeParams.enableSubFrameWrite = 0;
		NvEncInitializeParams.encodeConfig = &NvEncConfig;
		NvEncInitializeParams.maxEncodeWidth = 3840;
		NvEncInitializeParams.maxEncodeHeight = 2160;
		FParse::Value(FCommandLine::Get(), TEXT("NvEncMaxEncodeWidth="), NvEncInitializeParams.maxEncodeWidth);
		FParse::Value(FCommandLine::Get(), TEXT("NvEncMaxEncodeHeight="), NvEncInitializeParams.maxEncodeHeight);
	}
	// Get preset config and tweak it accordingly
	{
		NV_ENC_PRESET_CONFIG PresetConfig;
		FMemory::Memzero(PresetConfig);
		PresetConfig.version = NV_ENC_PRESET_CONFIG_VER;
		PresetConfig.presetCfg.version = NV_ENC_CONFIG_VER;
		Result = NvEncodeAPI->nvEncGetEncodePresetConfig(EncoderInterface, NvEncInitializeParams.encodeGUID, NvEncInitializeParams.presetGUID, &PresetConfig);
		checkf(NV_RESULT(Result), TEXT("Failed to select NVEncoder preset config (status: %d)"), Result);
		FMemory::Memcpy(&NvEncConfig, &PresetConfig.presetCfg, sizeof(NV_ENC_CONFIG));

		NvEncConfig.profileGUID = NV_ENC_H264_PROFILE_BASELINE_GUID;
		//NvEncConfig.profileGUID = NV_ENC_H264_PROFILE_HIGH_GUID;
		//NvEncConfig.gopLength = NVENC_INFINITE_GOPLENGTH;
		NvEncConfig.gopLength = NvEncInitializeParams.frameRateNum; // once a sec
		//NvEncConfig.frameIntervalP = 1;
		//NvEncConfig.frameFieldMode = NV_ENC_PARAMS_FRAME_FIELD_MODE_FRAME;
		//NvEncConfig.mvPrecision = NV_ENC_MV_PRECISION_QUARTER_PEL;

		NV_ENC_RC_PARAMS& RcParams = NvEncConfig.rcParams;
		RcParams.rateControlMode = ToRcMode(CVarEncoderRateControl.GetValueOnAnyThread());

		RcParams.enableMinQP = true;
		RcParams.minQP = { 20, 20, 20 };

		RcParams.maxBitRate = static_cast<uint32>(CVarEncoderMaxBitrate.GetValueOnAnyThread());

		//NvEncConfig.encodeCodecConfig.h264Config.chromaFormatIDC = 1;
		NvEncConfig.encodeCodecConfig.h264Config.idrPeriod = NvEncConfig.gopLength;

		// configure "entire frame as a single slice"
		// seems WebRTC implementation doesn't work well with slicing, default mode 
		// (Mode=3/ModeData=4 - 4 slices per frame) produces (rarely) grey full screen or just top half of it. 
		// it also can be related with our handling of slices in proxy's FakeVideoEncoder
		NvEncConfig.encodeCodecConfig.h264Config.sliceMode = 0;
		NvEncConfig.encodeCodecConfig.h264Config.sliceModeData = 0;

		// let encoder slice encoded frame so they can fit into RTP packets
		// commented out because at some point it started to produce immediately visible visual artifacts on players
		//NvEncConfig.encodeCodecConfig.h264Config.sliceMode = 1;
		//NvEncConfig.encodeCodecConfig.h264Config.sliceModeData = 1100; // max bytes per slice

		// repeat SPS/PPS with each key-frame for a case when the first frame (with mandatory SPS/PPS) 
		// was dropped by WebRTC
		NvEncConfig.encodeCodecConfig.h264Config.repeatSPSPPS = 1;

		// maybe doesn't have an effect, high level is chosen because we aim at high bitrate
		NvEncConfig.encodeCodecConfig.h264Config.level = NV_ENC_LEVEL_H264_52;
	}

	// Get encoder capability
	{
		NV_ENC_CAPS_PARAM CapsParam;
		FMemory::Memzero(CapsParam);
		CapsParam.version = NV_ENC_CAPS_PARAM_VER;
		CapsParam.capsToQuery = NV_ENC_CAPS_ASYNC_ENCODE_SUPPORT;
		int32 AsyncMode = 0;
		Result = NvEncodeAPI->nvEncGetEncodeCaps(EncoderInterface, NvEncInitializeParams.encodeGUID, &CapsParam, &AsyncMode);
		checkf(NV_RESULT(Result), TEXT("Failed to get NVEncoder capability params (status: %d)"), Result);
		if (AsyncMode == 0)
		{
			UE_LOG(LogVideoEncoder, Fatal, TEXT("NvEnc doesn't support async mode"));
			return;
		}

		NvEncInitializeParams.enableEncodeAsync = true;
	}

	Result = NvEncodeAPI->nvEncInitializeEncoder(EncoderInterface, &NvEncInitializeParams);
	checkf(NV_RESULT(Result), TEXT("Failed to initialize NVEncoder (status: %d)"), Result);

	FBufferId Id = 0;
	for(FFrame& Frame : BufferedFrames)
	{
		Frame.Id = Id++;
	}

	InitializeResources();

	EncoderThread = MakeUnique<FThread>(TEXT("NvVideoEncoder"), [this]() { EncoderCheckLoop(); });

	UE_LOG(LogVideoEncoder, Log, TEXT("NvEnc initialised: %dFPS%s"), static_cast<int32>(InitialMaxFPS), CVarEncoderPrioritiseQuality.GetValueOnAnyThread() != 0 ? TEXT(", prioritise quality") : TEXT(""));
}

FPixelStreamingNvVideoEncoder::~FPixelStreamingNvVideoEncoder()
{
	if (!DllHandle)
	{
		return;
	}

	FCoreDelegates::PostRenderingThreadCreated.RemoveAll(this);
	FCoreDelegates::PreRenderingThreadDestroyed.RemoveAll(this);

	if (EncoderThread)
	{
		bExitEncoderThread = true;

		// Trigger an event to ensure we can get out of the encoder thread.
		SetEvent(EncodeQueue.EncodeEvent);

		// Exit encoder runnable thread before shutting down NvEnc interface
		EncoderThread->Join();
	}

	ReleaseResources();

	if (EncoderInterface)
	{
		_NVENCSTATUS Result = NvEncodeAPI->nvEncDestroyEncoder(EncoderInterface);
		checkf(NV_RESULT(Result), TEXT("Failed to destroy NvEnc interface (status: %d)"), Result);
		EncoderInterface = nullptr;
	}

#if defined PLATFORM_WINDOWS
		FPlatformProcess::FreeDllHandle(DllHandle);
#else
		dlclose(DllHandle);
#endif
		DllHandle = nullptr;
}

bool FPixelStreamingNvVideoEncoder::CopyBackBuffer(const FTexture2DRHIRef& BackBuffer, FTimespan Timestamp, FBufferId& BufferId)
{
	check(IsInRenderingThread());

	// Find a free slot we can use
	FFrame* Frame = nullptr;
	for (FFrame& Slot : BufferedFrames)
	{
		if (Slot.State.Load() == EFrameState::Free)
		{
			Frame = &Slot;
			BufferId = Slot.Id;
			break;
		}
	}

	if (!Frame)
	{
		UE_LOG(LogVideoEncoder, Verbose, TEXT("Frame dropped because NvEnc queue is full"));
		return false;
	}


	Frame->FrameIdx = CapturedFrameCount++;
	Frame->InputFrame.CaptureTs = Timestamp;

#if NVENC_VIDEO_ENCODER_DEBUG
	Frame->CopyBufferStartTs = FTimespan::FromSeconds(FPlatformTime::Seconds());
	// By clearing the frame at this point, we can catch the occasional glimpse of a solid color
	// frame in PixelStreaming if there are any bugs detecting when the copy finished
	ClearFrame(*Frame);
#endif

	CopyBackBuffer(BackBuffer, *Frame);

	UE_LOG(LogVideoEncoder, Verbose, TEXT("Buffer #%d (%d) captured"), Frame->FrameIdx, BufferId);
	Frame->State = EFrameState::Capturing;

	return true;
}

void FPixelStreamingNvVideoEncoder::CopyBackBuffer(const FTexture2DRHIRef& BackBuffer, FFrame& Frame)
{
	SCOPE_CYCLE_COUNTER(STAT_NvEnc_CopyBackBuffer);
	FInputFrame& InputFrame = Frame.InputFrame;

	UpdateRes(BackBuffer, Frame);

	IRendererModule* RendererModule = &FModuleManager::GetModuleChecked<IRendererModule>("Renderer");
	FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();

	InputFrame.CopyFence->Clear();
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

	RHICmdList.WriteGPUFence(Frame.InputFrame.CopyFence);
}


void FPixelStreamingNvVideoEncoder::EncodeFrame(FBufferId BufferId, const webrtc::EncodedImage& EncodedFrame, uint32 Bitrate)
{
	FFrame& Frame = BufferedFrames[BufferId];

	{
		EFrameState State = Frame.State.Load();
		checkf(State == EFrameState::Capturing, TEXT("Buffer %d : Expected state %d, but found %d"), BufferId, (int)EFrameState::Captured, (int)State);
	}

	// Loop and sleep until the fence is signaled.
	// Also, at the moment of writing there is not proper GPU fence for D3D11 in UE4. It uses FGenericRHIGPUFence.
	// Due to this, if this thread doesn't make progress because of the fence not being signaled, it can end up stalling the RenderThread
	// when restarting a PixelStreaming client session (e.g: Refreshing the browser page).
	// This causes a deadlock, because the RenderThread is the one signaling the fence.
	// So, if we are waiting for too long, just ignore the fence.
	{
		double StartTime = FPlatformTime::Seconds();
		while (!Frame.InputFrame.CopyFence->Poll())
		{
			FPlatformProcess::Sleep(2.0f/1000);
			if ((FPlatformTime::Seconds() - StartTime) > 0.250f)
			{
				UE_LOG(LogVideoEncoder, Warning, TEXT("Buffer #%d taking too long to reach the GPU fence. Ignoring fence."), (int)BufferId);
				break;
			}
		}
	}

#if NVENC_VIDEO_ENCODER_DEBUG
	Frame.CopyBufferFinishTs = FTimespan::FromSeconds(FPlatformTime::Seconds());
#endif

	Frame.State = EFrameState::Captured;
	Frame.OutputFrame.EncodedFrame = EncodedFrame;
	Frame.OutputFrame.EncodedFrame._encodedWidth = NvEncInitializeParams.encodeWidth;
	Frame.OutputFrame.EncodedFrame._encodedHeight = NvEncInitializeParams.encodeHeight;

	// Save the requested bitrate, so we can reconfigure the encoder later
	Frame.BitrateRequested = Bitrate;
	EncodeQueue.Push(&Frame);

	UE_LOG(LogVideoEncoder, VeryVerbose, TEXT("Buffer #%d (%d), ts %u sent to the encoder thread"), Frame.FrameIdx, BufferId, Frame.OutputFrame.EncodedFrame.Timestamp());

}

void FPixelStreamingNvVideoEncoder::OnFrameDropped(FBufferId BufferId)
{
	FFrame& Frame = BufferedFrames[BufferId];

	{
		EFrameState State = Frame.State.Load();
		checkf(State == EFrameState::Capturing, TEXT("Buffer %d: Expected state %d, found %d")
			, BufferId, (int)EFrameState::Capturing, (int)State);
	}

	Frame.State = EFrameState::Free;

	UE_LOG(LogVideoEncoder, Log, TEXT("Buffer #%d (%d) dropped"), BufferedFrames[BufferId].FrameIdx, BufferId);
}

void FPixelStreamingNvVideoEncoder::SubscribeToFrameEncodedEvent(FVideoEncoder& Subscriber)
{
	FScopeLock lock{ &SubscribersMutex };
	bool AlreadyInSet = false;
	Subscribers.Add(&Subscriber, &AlreadyInSet);
	check(!AlreadyInSet);
}

void FPixelStreamingNvVideoEncoder::UnsubscribeFromFrameEncodedEvent(FVideoEncoder& Subscriber)
{
	FScopeLock lock{ &SubscribersMutex };
	int32 res = Subscribers.Remove(&Subscriber);
	check(res == 1);
}

void FPixelStreamingNvVideoEncoder::UpdateNvEncConfig(const FInputFrame& InputFrame, uint32 Bitrate)
{
	bool bSettingsChanged = false;
	bool bResolutionChanged = false;

	float MaxBitrate = CVarEncoderMaxBitrate.GetValueOnAnyThread();
	uint32 ClampedBitrate = FMath::Min(Bitrate, static_cast<uint32_t>(MaxBitrate));
	if (NvEncConfig.rcParams.averageBitRate != ClampedBitrate)
	{
		NvEncConfig.rcParams.averageBitRate = ClampedBitrate;
		RequestedBitrateMbps = ClampedBitrate / 1000000.0;
		bSettingsChanged = true;
	}

	uint32 MinQP = static_cast<uint32>(CVarEncoderMinQP.GetValueOnAnyThread());
	MinQP = FMath::Clamp(MinQP, 0u, 54u);
	if (NvEncConfig.rcParams.minQP.qpIntra != MinQP)
	{
		NvEncConfig.rcParams.minQP.qpIntra = NvEncConfig.rcParams.minQP.qpInterP = NvEncConfig.rcParams.minQP.qpInterB = MinQP;
		UE_LOG(LogVideoEncoder, Log, TEXT("MinQP %u"), MinQP);
		bSettingsChanged = true;
	}

	NV_ENC_PARAMS_RC_MODE RcMode = ToRcMode(CVarEncoderRateControl.GetValueOnAnyThread());
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

	if (InputFrame.BackBuffer->GetSizeX() != NvEncInitializeParams.encodeWidth ||
		InputFrame.BackBuffer->GetSizeY() != NvEncInitializeParams.encodeHeight)
	{
		NvEncInitializeParams.encodeWidth = NvEncInitializeParams.darWidth = InputFrame.BackBuffer->GetSizeX();
		NvEncInitializeParams.encodeHeight = NvEncInitializeParams.darHeight = InputFrame.BackBuffer->GetSizeY();

		bSettingsChanged = true;
		bResolutionChanged = true;
	}

	if (bSettingsChanged)
	{
		NV_ENC_RECONFIGURE_PARAMS NvEncReconfigureParams;
		FMemory::Memcpy(&NvEncReconfigureParams.reInitEncodeParams, &NvEncInitializeParams, sizeof(NvEncInitializeParams));
		NvEncReconfigureParams.version = NV_ENC_RECONFIGURE_PARAMS_VER;
		NvEncReconfigureParams.forceIDR = bResolutionChanged;

		_NVENCSTATUS Result = NvEncodeAPI->nvEncReconfigureEncoder(EncoderInterface, &NvEncReconfigureParams);
		checkf(NV_RESULT(Result), TEXT("Failed to reconfigure encoder (status: %d)"), Result);
	}
}

bool FPixelStreamingNvVideoEncoder::UpdateFramerate()
{
	float Fps;

	if (!CVarEncoderPrioritiseQuality.GetValueOnAnyThread())
	{
		Fps = InitialMaxFPS;
	}
	else
	{

		// #AMF(Andriy) : This seems wrong.  averageBitRate is bits/sec.

		// Quality of video suffers if B/W is limited and drops below some threshold. We can sacrifice
		// responsiveness (latency) to improve video quality. We reduce framerate and so encoder
		// can spread limited bitrate over fewer frames.
		float Mbps = NvEncConfig.rcParams.averageBitRate;

		// bitrate lower than lower bound results always in min FPS
		// bitrate between lower and upper bounds results in FPS proportionally between min and max FPS
		// bitrate higher than upper bound results always in max FPS
		const float UpperBoundMbps = CVarEncoderHighMbps.GetValueOnAnyThread();
		const float LowerBoundMbps = FMath::Min(CVarEncoderLowMbps.GetValueOnAnyThread(), UpperBoundMbps);
		const float MaxFps = InitialMaxFPS;
		const float MinFps = FMath::Min(CVarEncoderMinFPS.GetValueOnAnyThread(), MaxFps);

		if (Mbps < LowerBoundMbps)
			Fps = MinFps;
		else if (Mbps < UpperBoundMbps)
		{
			Fps = MinFps + (MaxFps - MinFps) / (UpperBoundMbps - LowerBoundMbps) * (Mbps - LowerBoundMbps);
		}
		else
			Fps = MaxFps;
	}

	if (NvEncInitializeParams.frameRateNum != Fps)
	{
		// SetMaxFPS must be called from the game thread because it changes a console var
		AsyncTask(ENamedThreads::GameThread, [Fps]() { GEngine->SetMaxFPS(Fps); });

		NvEncInitializeParams.frameRateNum = static_cast<uint32>(Fps);
		UE_LOG(LogVideoEncoder, Log, TEXT("NvEnc reconfigured to %d FPS"), NvEncInitializeParams.frameRateNum);
		return true;
	}

	return false;
}

// checks if resolution changed, either the game res changed or new streaming resolution was specified by the console var
void FPixelStreamingNvVideoEncoder::UpdateRes(const FTexture2DRHIRef& BackBuffer, FFrame& Frame)
{
	check(IsInRenderingThread());

	FInputFrame& InputFrame = Frame.InputFrame;

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

	// check if target resolution matches our currently allocated `InputFrame.BackBuffer` resolution
	if (InputFrame.BackBuffer->GetSizeX() == Width && InputFrame.BackBuffer->GetSizeY() == Height)
	{
		return;
	}

	// reallocate and re-register InputFrame with NvEnc
	ReleaseFrameInputBuffer(Frame);
	InitFrameInputBuffer(Frame, Width, Height);
}

void FPixelStreamingNvVideoEncoder::SubmitFrameToEncoder(FFrame& Frame)
{
	check(Frame.State.Load() == EFrameState::Captured);

#if NVENC_VIDEO_ENCODER_DEBUG
	Frame.EncodingStartTs = FTimespan::FromSeconds(FPlatformTime::Seconds());
#endif

	SCOPE_CYCLE_COUNTER(STAT_NvEnc_SendBackBufferToEncoder);

	Frame.State = EFrameState::Encoding;
	Frame.OutputFrame.EncodedFrame.timing_.encode_start_ms = rtc::TimeMicros() / 1000;

	NV_ENC_PIC_PARAMS PicParams;
	FMemory::Memzero(PicParams);
	PicParams.version = NV_ENC_PIC_PARAMS_VER;
	PicParams.inputBuffer = Frame.InputFrame.MappedResource;
	PicParams.bufferFmt = Frame.InputFrame.BufferFormat;
	PicParams.inputWidth = NvEncInitializeParams.encodeWidth;
	PicParams.inputHeight = NvEncInitializeParams.encodeHeight;
	PicParams.outputBitstream = Frame.OutputFrame.BitstreamBuffer;
	PicParams.completionEvent = Frame.OutputFrame.EventHandle;
	PicParams.inputTimeStamp = Frame.FrameIdx;
	PicParams.pictureStruct = NV_ENC_PIC_STRUCT_FRAME;

	if (Frame.OutputFrame.EncodedFrame._frameType == webrtc::kVideoFrameKey)
		PicParams.encodePicFlags |= NV_ENC_PIC_FLAG_FORCEIDR;

	_NVENCSTATUS Result = NvEncodeAPI->nvEncEncodePicture(EncoderInterface, &PicParams);
	checkf(NV_RESULT(Result), TEXT("Failed to encode frame (status: %d)"), Result);
}

void FPixelStreamingNvVideoEncoder::EncoderCheckLoop()
{
	// This thread will both encode frames and will also wait for the next frame
	// to finish encoding.
	TQueue<FFrame*> CurrentlyEncodingQueue;

	while (true)
	{
		// Wait for either the command to encode frames or the information
		// that the next frame has finished encoding.
		// The signalling events are a pair of handles for windows events so we
		// can wait for one or the other.
		enum EncodeEvents
		{
			START_ENCODING_EVENT = 0,
			FINISHED_ENCODING_EVENT = 1,
			NUM_ENCODING_EVENTS = 2
		};

		int NumEvents = 1;
		HANDLE Handles[NUM_ENCODING_EVENTS];
		Handles[START_ENCODING_EVENT] = EncodeQueue.EncodeEvent;

		if (!CurrentlyEncodingQueue.IsEmpty())
		{
			NumEvents++;
			Handles[FINISHED_ENCODING_EVENT] = (**CurrentlyEncodingQueue.Peek()).OutputFrame.EventHandle;
		}

		DWORD Result = WaitForMultipleObjects(NumEvents, Handles, false, INFINITE);

		if (!bExitEncoderThread)
		{
			if (Result == WAIT_OBJECT_0 + START_ENCODING_EVENT)
			{
				// Get the list of all frames we want to encode.
				FFrame* Frames[NumBufferedFrames];
				int NumFrames;
				EncodeQueue.PopAll(Frames, NumFrames);
				for (int Idx = 0; Idx < NumFrames; Idx++)
				{
					FFrame& Frame = *Frames[Idx];
					UpdateNvEncConfig(Frame.InputFrame, Frame.BitrateRequested);
					SubmitFrameToEncoder(Frame);
					CurrentlyEncodingQueue.Enqueue(&Frame);
				}
			}
			else if (Result == WAIT_OBJECT_0 + FINISHED_ENCODING_EVENT)
			{
				FFrame* Frame = nullptr;
				verify(CurrentlyEncodingQueue.Dequeue(Frame));
				ResetEvent(Frame->OutputFrame.EventHandle);
				UE_LOG(LogVideoEncoder, Verbose, TEXT("Buffer #%d (%d) encoded"), Frame->FrameIdx, Frame->Id);
				ProcessFrame(*Frame);
			}
		}
		else
		{
			break;
		}

	}
}

void FPixelStreamingNvVideoEncoder::ProcessFrame(FFrame& Frame)
{
	check(Frame.State.Load() == EFrameState::Encoding);

	FOutputFrame& OutputFrame = Frame.OutputFrame;

	FHUDStats& Stats = FHUDStats::Get();

	// Retrieve encoded frame from output buffer
	{
		SCOPE_CYCLE_COUNTER(STAT_NvEnc_RetrieveEncodedFrame);

		NV_ENC_LOCK_BITSTREAM LockBitstream;
		FMemory::Memzero(LockBitstream);
		LockBitstream.version = NV_ENC_LOCK_BITSTREAM_VER;
		LockBitstream.outputBitstream = OutputFrame.BitstreamBuffer;
		LockBitstream.doNotWait = NvEncInitializeParams.enableEncodeAsync;

		_NVENCSTATUS Result = NvEncodeAPI->nvEncLockBitstream(EncoderInterface, &LockBitstream);
		checkf(NV_RESULT(Result), TEXT("Failed to lock bitstream (status: %d)"), Result);

		checkf(LockBitstream.pictureType == NV_ENC_PIC_TYPE_IDR || OutputFrame.EncodedFrame._frameType == webrtc::kVideoFrameDelta, TEXT("key frame requested by webrtc but not provided by NvEnc: %d - %d"), OutputFrame.EncodedFrame._frameType, LockBitstream.pictureType);
		OutputFrame.EncodedFrame._frameType = (LockBitstream.pictureType == NV_ENC_PIC_TYPE_IDR) ?
			webrtc::kVideoFrameKey : webrtc::kVideoFrameDelta;

		OutputFrame.EncodedFrame.qp_ = LockBitstream.frameAvgQP;

		EncodedFrameBuffer = TArray<uint8>(reinterpret_cast<const uint8*>(LockBitstream.bitstreamBufferPtr), LockBitstream.bitstreamSizeInBytes);

		int64 CaptureTs = 0;
		if (Stats.bEnabled)
		{
			CaptureTs = Frame.InputFrame.CaptureTs.GetTicks();
			EncodedFrameBuffer.Append(reinterpret_cast<const uint8*>(&CaptureTs), sizeof(CaptureTs));
		}

		OutputFrame.EncodedFrame._buffer = EncodedFrameBuffer.GetData();
		OutputFrame.EncodedFrame._length = OutputFrame.EncodedFrame._size = EncodedFrameBuffer.Num();

		Result = NvEncodeAPI->nvEncUnlockBitstream(EncoderInterface, Frame.OutputFrame.BitstreamBuffer);
		checkf(NV_RESULT(Result), TEXT("Failed to unlock bitstream (status: %d)"), Result);
	}

	OutputFrame.EncodedFrame.timing_.encode_finish_ms = rtc::TimeMicros() / 1000;
	OutputFrame.EncodedFrame.timing_.flags = webrtc::VideoSendTiming::kTriggeredByTimer;

	double LatencyMs = static_cast<double>(OutputFrame.EncodedFrame.timing_.encode_finish_ms - OutputFrame.EncodedFrame.timing_.encode_start_ms);
	double BitrateMbps = EncodedFrameBuffer.Num() * 8 * NvEncInitializeParams.frameRateNum / 1000000.0;

	if (Stats.bEnabled)
	{
		Stats.EncoderLatencyMs.Update(LatencyMs);
		Stats.EncoderBitrateMbps.Update(BitrateMbps);
		Stats.EncoderQP.Update(OutputFrame.EncodedFrame.qp_);
	}

#if NVENC_VIDEO_ENCODER_DEBUG
	Frame.EncodingFinishTs = FTimespan::FromSeconds(FPlatformTime::Seconds());
	{
		FFrameTiming Timing;
		Timing.Total[0] = (Frame.CopyBufferFinishTs - Frame.CopyBufferStartTs).GetTotalMilliseconds();
		Timing.Total[1] = (Frame.EncodingStartTs - Frame.CopyBufferStartTs).GetTotalMilliseconds();
		Timing.Total[2] = (Frame.EncodingFinishTs - Frame.CopyBufferStartTs).GetTotalMilliseconds();

		Timing.Steps[0] = (Frame.CopyBufferFinishTs - Frame.CopyBufferStartTs).GetTotalMilliseconds();
		Timing.Steps[1] = (Frame.EncodingStartTs - Frame.CopyBufferFinishTs).GetTotalMilliseconds();
		Timing.Steps[2] = (Frame.EncodingFinishTs - Frame.EncodingStartTs).GetTotalMilliseconds();
		Timings.Add(Timing);
		// Limit the array size
		if (Timings.Num()>1000)
		{
			Timings.RemoveAt(0);
		}
	}
#endif

	UE_LOG(LogVideoEncoder, VeryVerbose, TEXT("encoded %s ts %u, capture ts %lld, QP %d/%.0f,  latency %.0f/%.0f ms, bitrate %.3f/%.3f/%.3f Mbps, %zu bytes"), ToString(OutputFrame.EncodedFrame._frameType), OutputFrame.EncodedFrame.Timestamp(), Frame.InputFrame.CaptureTs.GetTicks(), OutputFrame.EncodedFrame.qp_, Stats.EncoderQP.Get(), LatencyMs, Stats.EncoderLatencyMs.Get(), RequestedBitrateMbps, BitrateMbps, Stats.EncoderBitrateMbps.Get(), OutputFrame.EncodedFrame._length);

	// Stream the encoded frame
	{
		SCOPE_CYCLE_COUNTER(STAT_NvEnc_StreamEncodedFrame);
		OnEncodedFrame(OutputFrame.EncodedFrame);
	}

	Frame.State = EFrameState::Free;
}

void FPixelStreamingNvVideoEncoder::InitFrameInputBuffer(FFrame& Frame, uint32 Width, uint32 Height)
{
	FInputFrame& InputFrame = Frame.InputFrame;

	// Create (if necessary) and clear the GPU Fence so we can detect when the copy finished
	if (!InputFrame.CopyFence)
	{
		FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();
		InputFrame.CopyFence = RHICmdList.CreateGPUFence(*FString::Printf(TEXT("PixelStreamingCopy_%d"), Frame.Id));
	}

	// Create resolved back buffer texture
	{
		// Make sure format used here is compatible with NV_ENC_BUFFER_FORMAT specified later in NV_ENC_REGISTER_RESOURCE bufferFormat
		FRHIResourceCreateInfo CreateInfo;
		InputFrame.BackBuffer = RHICreateTexture2D(Width, Height, EPixelFormat::PF_B8G8R8A8, 1, 1, TexCreate_RenderTargetable | TexCreate_Shared, CreateInfo);
	}

	// Share this texture with the encoder device.
	FString RHIName = GDynamicRHI->GetName();

	if (RHIName == TEXT("D3D11"))
	{
		ID3D11Texture2D* ResolvedBackBuffer = (ID3D11Texture2D*)InputFrame.BackBuffer->GetTexture2D()->GetNativeResource();

		TRefCountPtr<IDXGIResource> DXGIResource;
		CHECK_HR_DX9_VOID(ResolvedBackBuffer->QueryInterface(IID_PPV_ARGS(DXGIResource.GetInitReference())));

		//
		// NOTE : The HANDLE IDXGIResource::GetSharedHandle gives us is NOT an NT Handle, and therefre we should not call CloseHandle on it
		//
		HANDLE SharedHandle;
		CHECK_HR_DX9_VOID(DXGIResource->GetSharedHandle(&SharedHandle));
		CHECK_HR_DX9_VOID(EncoderDevice->Device->OpenSharedResource(SharedHandle, __uuidof(ID3D11Texture2D), (LPVOID*)&InputFrame.SharedBackBuffer));
	}
	else if (RHIName == TEXT("D3D12"))
	{
		auto UE4D3DDevice = static_cast<ID3D12Device*>(GDynamicRHI->RHIGetNativeDevice());
		static uint32 NamingIdx = 0;
		ID3D12Resource* ResolvedBackBuffer = (ID3D12Resource*)InputFrame.BackBuffer->GetTexture2D()->GetNativeResource();

		//
		// NOTE: ID3D12Device::CreateSharedHandle gives as an NT Handle, and so we need to call CloseHandle on it
		//
		HANDLE SharedHandle;
		HRESULT Res1 = UE4D3DDevice->CreateSharedHandle(ResolvedBackBuffer, NULL, GENERIC_ALL, *FString::Printf(TEXT("PixelStreaming_NvEnc_%u"), NamingIdx++), &SharedHandle);
		CHECK_HR_DX9_VOID(Res1);

		TRefCountPtr <ID3D11Device1> Device1;
		CHECK_HR_DX9_VOID(EncoderDevice->Device->QueryInterface(__uuidof(ID3D11Device1), (void**)Device1.GetInitReference()));
		CHECK_HR_DX9_VOID(Device1->OpenSharedResource1(SharedHandle, __uuidof(ID3D11Texture2D), (LPVOID*)&InputFrame.SharedBackBuffer));
		verify(CloseHandle(SharedHandle));
	}

	// Register input back buffer
	{
		NV_ENC_REGISTER_RESOURCE RegisterResource;
		FMemory::Memzero(RegisterResource);
		EPixelFormat PixelFormat = InputFrame.BackBuffer->GetFormat();

		RegisterResource.version = NV_ENC_REGISTER_RESOURCE_VER;
		RegisterResource.resourceType = NV_ENC_INPUT_RESOURCE_TYPE_DIRECTX;
		RegisterResource.resourceToRegister = (void*)InputFrame.SharedBackBuffer;
		RegisterResource.width = Width;
		RegisterResource.height = Height;
		RegisterResource.bufferFormat = NV_ENC_BUFFER_FORMAT_ABGR;	// Make sure ResolvedBackBuffer is created with a compatible format
		_NVENCSTATUS Result = NvEncodeAPI->nvEncRegisterResource(EncoderInterface, &RegisterResource);
		checkf(NV_RESULT(Result), TEXT("Failed to register input back buffer (status: %d)"), Result);

		InputFrame.RegisteredResource = RegisterResource.registeredResource;
		InputFrame.BufferFormat = RegisterResource.bufferFormat;
	}
	// Map input buffer resource
	{
		NV_ENC_MAP_INPUT_RESOURCE MapInputResource;
		FMemory::Memzero(MapInputResource);
		MapInputResource.version = NV_ENC_MAP_INPUT_RESOURCE_VER;
		MapInputResource.registeredResource = InputFrame.RegisteredResource;
		_NVENCSTATUS Result = NvEncodeAPI->nvEncMapInputResource(EncoderInterface, &MapInputResource);
		checkf(NV_RESULT(Result), TEXT("Failed to map NvEnc input resource (status: %d)"), Result);
		InputFrame.MappedResource = MapInputResource.mappedResource;
	}
}

void FPixelStreamingNvVideoEncoder::InitializeResources()
{
	for (uint32 i = 0; i < NumBufferedFrames; ++i)
	{
		FFrame& Frame = BufferedFrames[i];

		InitFrameInputBuffer(Frame, NvEncInitializeParams.encodeWidth, NvEncInitializeParams.encodeHeight);

		FMemory::Memzero(Frame.OutputFrame);
		// Create output bitstream buffer
		{
			NV_ENC_CREATE_BITSTREAM_BUFFER CreateBitstreamBuffer;
			FMemory::Memzero(CreateBitstreamBuffer);
			CreateBitstreamBuffer.version = NV_ENC_CREATE_BITSTREAM_BUFFER_VER;
			CreateBitstreamBuffer.size = BitstreamSize;
			CreateBitstreamBuffer.memoryHeap = NV_ENC_MEMORY_HEAP_SYSMEM_CACHED;
			_NVENCSTATUS Result = NvEncodeAPI->nvEncCreateBitstreamBuffer(EncoderInterface, &CreateBitstreamBuffer);
			checkf(NV_RESULT(Result), TEXT("Failed to create NvEnc bitstream buffer (status: %d)"), Result);
			Frame.OutputFrame.BitstreamBuffer = CreateBitstreamBuffer.bitstreamBuffer;
		}

		RegisterAsyncEvent(&Frame.OutputFrame.EventHandle);
	}
}

void FPixelStreamingNvVideoEncoder::ReleaseFrameInputBuffer(FFrame& Frame)
{
	FInputFrame& InputFrame = Frame.InputFrame;

	if (InputFrame.MappedResource)
	{
		_NVENCSTATUS Result = NvEncodeAPI->nvEncUnmapInputResource(EncoderInterface, InputFrame.MappedResource);
		checkf(NV_RESULT(Result), TEXT("Failed to unmap input resource (status: %d)"), Result);
		InputFrame.MappedResource = nullptr;
	}

	if (InputFrame.RegisteredResource)
	{
		_NVENCSTATUS Result = NvEncodeAPI->nvEncUnregisterResource(EncoderInterface, InputFrame.RegisteredResource);
		checkf(NV_RESULT(Result), TEXT("Failed to unregister input buffer resource (status: %d)"), Result);
		InputFrame.RegisteredResource = nullptr;
	}

	InputFrame.BackBuffer.SafeRelease();
	if (InputFrame.SharedBackBuffer)
	{
		InputFrame.SharedBackBuffer->Release();
		InputFrame.SharedBackBuffer = nullptr;
	}

	if (InputFrame.CopyFence)
	{
		InputFrame.CopyFence.SafeRelease();
	}
}

void FPixelStreamingNvVideoEncoder::ReleaseResources()
{
	for (uint32 i = 0; i < NumBufferedFrames; ++i)
	{
		FFrame& Frame = BufferedFrames[i];

		ReleaseFrameInputBuffer(Frame);

		if (Frame.OutputFrame.BitstreamBuffer)
		{
			_NVENCSTATUS Result = NvEncodeAPI->nvEncDestroyBitstreamBuffer(EncoderInterface, Frame.OutputFrame.BitstreamBuffer);
			checkf(NV_RESULT(Result), TEXT("Failed to destroy output buffer bitstream (status: %d)"), Result);
			Frame.OutputFrame.BitstreamBuffer = nullptr;
		}

		if (Frame.OutputFrame.EventHandle)
		{
			UnregisterAsyncEvent(Frame.OutputFrame.EventHandle);
			::CloseHandle(Frame.OutputFrame.EventHandle);
			Frame.OutputFrame.EventHandle = nullptr;
		}
	}
}

void FPixelStreamingNvVideoEncoder::RegisterAsyncEvent(void** OutEvent)
{
	NV_ENC_EVENT_PARAMS EventParams;
	FMemory::Memzero(EventParams);
	EventParams.version = NV_ENC_EVENT_PARAMS_VER;
#if defined PLATFORM_WINDOWS
	EventParams.completionEvent = CreateEvent(nullptr, false, false, nullptr);
#endif
	_NVENCSTATUS Result = NvEncodeAPI->nvEncRegisterAsyncEvent(EncoderInterface, &EventParams);
	checkf(NV_RESULT(Result), TEXT("Failed to register async event (status: %d)"), Result);
	*OutEvent = EventParams.completionEvent;
}

void FPixelStreamingNvVideoEncoder::UnregisterAsyncEvent(void* Event)
{
	if (Event)
	{
		NV_ENC_EVENT_PARAMS EventParams;
		FMemory::Memzero(EventParams);
		EventParams.version = NV_ENC_EVENT_PARAMS_VER;
		EventParams.completionEvent = Event;
		bool Result = NV_RESULT(NvEncodeAPI->nvEncUnregisterAsyncEvent(EncoderInterface, &EventParams));
		checkf(Result, TEXT("Failed to unregister async event"));
	}
}

void FPixelStreamingNvVideoEncoder::OnEncodedFrame(const webrtc::EncodedImage& EncodedImage)
{
	FScopeLock lock{ &SubscribersMutex };
	for (auto s : Subscribers)
	{
		s->OnEncodedFrame(EncodedImage);
	}
}

#if NVENC_VIDEO_ENCODER_DEBUG
// Fills with a solid colour
void FPixelStreamingNvVideoEncoder::ClearFrame(FFrame& Frame)
{
	check(IsInRenderingThread());

	FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();

	static_assert(NumBufferedFrames == 3, "Unexpected number of slots. Please update the array to match.");
	FLinearColor Colors[NumBufferedFrames] =
	{
		FLinearColor(1,0,0),
		FLinearColor(0,1,0),
		FLinearColor(0,0,1)
	};

	FRHIRenderPassInfo RPInfo(Frame.InputFrame.BackBuffer, ERenderTargetActions::Load_Store);
	TransitionRenderPassTargets(RHICmdList, RPInfo);
	RHICmdList.BeginRenderPass(RPInfo, TEXT("ClearCanvas"));
	RHICmdList.SetViewport(0, 0, 0.0f
		, Frame.InputFrame.BackBuffer->GetSizeXY().X
		, Frame.InputFrame.BackBuffer->GetSizeXY().Y, 1.0f);

	DrawClearQuad(RHICmdList, Colors[Frame.Id]);
	RHICmdList.EndRenderPass();
}
#endif

