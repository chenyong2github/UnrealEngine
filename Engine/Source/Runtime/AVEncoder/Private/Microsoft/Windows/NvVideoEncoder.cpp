// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "NvVideoEncoder.h"
#include "Microsoft/AVEncoderMicrosoftCommon.h"

THIRD_PARTY_INCLUDES_START
	#include "NvEncoder/nvEncodeAPI.h"
THIRD_PARTY_INCLUDES_END

// This is mostly to use internally at Epic.
// Setting this to 1 will collect detailed timings in the `Timings` member array.
// It will also clear every frame with a solid colour before copying the backbuffer into it.
#define NVENC_VIDEO_ENCODER_DEBUG 0

#if NVENC_VIDEO_ENCODER_DEBUG
	#include "ClearQuad.h"
#endif

#define CHECK_NV_RES(NvCall)\
{\
	NVENCSTATUS Res = NvCall;\
	if (Res != NV_ENC_SUCCESS)\
	{\
		check(false);\
		UE_LOG(LogAVEncoder, Error, TEXT("`" #NvCall "` failed with error code: %d"), Res);\
		return false;\
	}\
}

namespace AVEncoder
{

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

	DECLARE_STATS_GROUP(TEXT("NvEnc"), STATGROUP_NvEncVideoEncoder, STATCAT_Advanced);

	DECLARE_CYCLE_STAT(TEXT("CopyTexture"), STAT_NvEnc_CopyTexture, STATGROUP_NvEncVideoEncoder);
	DECLARE_CYCLE_STAT(TEXT("SubmitFrameToEncoder"), STAT_NvEnc_SubmitFrameToEncoder, STATGROUP_NvEncVideoEncoder);
	DECLARE_CYCLE_STAT(TEXT("WaitForEncodeEvent"), STAT_NvEnc_WaitForEncodeEvent, STATGROUP_NvEncVideoEncoder);
	DECLARE_CYCLE_STAT(TEXT("RetrieveEncodedFrame"), STAT_NvEnc_RetrieveEncodedFrame, STATGROUP_NvEncVideoEncoder);
	DECLARE_CYCLE_STAT(TEXT("OnEncodedVideoFrameCallback"), STAT_NvEnc_OnEncodedVideoFrameCallback, STATGROUP_NvEncVideoEncoder);

	NV_ENC_PARAMS_RC_MODE ToNvEncRcMode(FH264Settings::ERateControlMode RcMode)
	{
		switch(RcMode)
		{
		case FH264Settings::ERateControlMode::ConstQP:
			return NV_ENC_PARAMS_RC_CONSTQP;
		case FH264Settings::ERateControlMode::VBR:
			return NV_ENC_PARAMS_RC_VBR;
		case FH264Settings::ERateControlMode::CBR:
			return NV_ENC_PARAMS_RC_CBR;
		default:
			UE_LOG(LogAVEncoder, Error, TEXT("Invalid rate control mode (%d) for nvenc"), (int)RcMode);
			return NV_ENC_PARAMS_RC_CBR;
		}
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

	const TCHAR* ToString(NV_ENC_PIC_TYPE PicType)
	{
		switch (PicType)
		{
		case NV_ENC_PIC_TYPE_P:
			return TEXT("NV_ENC_PIC_TYPE_P");
		case NV_ENC_PIC_TYPE_B:
			return TEXT("NV_ENC_PIC_TYPE_B");
		case NV_ENC_PIC_TYPE_I:
			return TEXT("NV_ENC_PIC_TYPE_I");
		case NV_ENC_PIC_TYPE_IDR:
			return TEXT("NV_ENC_PIC_TYPE_IDR");
		case NV_ENC_PIC_TYPE_BI:
			return TEXT("NV_ENC_PIC_TYPE_BI");
		case NV_ENC_PIC_TYPE_SKIPPED:
			return TEXT("NV_ENC_PIC_TYPE_SKIPPED");
		case NV_ENC_PIC_TYPE_INTRA_REFRESH:
			return TEXT("NV_ENC_PIC_TYPE_INTRA_REFRESH");
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

} // anonymous namespace

// Video encoder implementation based on NVIDIA Video Codecs SDK: https://developer.nvidia.com/nvidia-video-codec-sdk
// Uses only encoder part
class FNvVideoEncoder : public FVideoEncoder
{
public:

	FNvVideoEncoder();
	~FNvVideoEncoder();

	//
	// FVideoEncoder interface
	//
	const TCHAR* GetName() const override;
	const TCHAR* GetType() const override;
	bool Initialize(const FVideoEncoderConfig& InConfig) override;
	void Shutdown() override;
	bool CopyTexture(FTexture2DRHIRef Texture, FTimespan CaptureTs, FTimespan Duration, FBufferId& OutBufferId, FIntPoint Resolution) override;
	void Drop(FBufferId BufferId) override;
	void Encode(FBufferId BufferId, bool bForceKeyFrame, uint32 Bitrate, TUniquePtr<FEncoderVideoFrameCookie> Cookie) override;
	FVideoEncoderConfig GetConfig() const override;
	bool SetBitrate(uint32 Bitrate) override;
	bool SetFramerate(uint32 Framerate) override;
	bool SetParameter(const FString& Parameter, const FString& Value) override;

private:

	struct FInputFrame
	{
		FInputFrame() {}
		UE_NONCOPYABLE(FInputFrame);
		void* RegisteredResource = nullptr;
		NV_ENC_INPUT_PTR MappedResource = nullptr;
		NV_ENC_BUFFER_FORMAT BufferFormat;
		FTexture2DRHIRef Texture;
		ID3D11Texture2D* SharedTexture = nullptr;
		bool bForceKeyFrame = false;
		FTimespan CaptureTs;
		FTimespan Duration;
		FGPUFenceRHIRef CopyFence;
	};

	struct FOutputFrame
	{
		FOutputFrame() {}
		UE_NONCOPYABLE(FOutputFrame);
		NV_ENC_OUTPUT_PTR BitstreamBuffer = nullptr;
		HANDLE EventHandle = nullptr;
		TUniquePtr<FEncoderVideoFrameCookie> Cookie;
	};

	enum class EFrameState
	{
		Free,
		Capturing,
		Captured,
		Encoding
	};

	struct FFrame
	{
		FFrame() {}
		UE_NONCOPYABLE(FFrame);

		// Array index of this FFrame. This is set at startup, and should never be changed
		FBufferId Id = 0;

		TAtomic<EFrameState> State = { EFrameState::Free };
		// Bitrate requested at the time the video encoder asked us to encode this frame
		// We save this, because we can't use it at the moment we receive it.
		uint32 BitrateRequested = 0;
		FInputFrame InputFrame;
		FOutputFrame OutputFrame;
		uint64 FrameIdx = 0;

		// Some timestamps to track how long a frame spends in each step
		FTimespan CopyBufferStartTs;
		FTimespan CopyBufferFinishTs;
		FTimespan EncodingStartTs;
		FTimespan EncodingFinishTs;
	};

	bool InitFrameInputBuffer(FFrame& Frame, uint32 Width, uint32 Heigh);
	bool InitializeResources();
	void ReleaseFrameInputBuffer(FFrame& Frame);
	void ReleaseResources();
	void RegisterAsyncEvent(void** OutEvent);
	void UnregisterAsyncEvent(void* Event);
	FFrame* CheckForFinishedCopy();

	bool UpdateFramerate();

	/**
	*  Update some encoder settings
	*
	* @param ResolutionIf both X and Y are NOT 0, then set the encoder resolution
	* @param Bitrate If not 0, then set the encoder average bitrate
	*/
	void UpdateNvEncConfig(FIntPoint Resolution, uint32 Bitrate);

	void UpdateRes(FFrame& Frame, FIntPoint Resolution);
	void CopyTexture(const FTexture2DRHIRef& Texture, FFrame& Frame, FIntPoint Resolution);

	void EncoderCheckLoop();

	void ProcessFrame(FFrame& Frame);

	void UpdateSettings(FInputFrame& InputFrame, uint32 Bitrate);
	void SubmitFrameToEncoder(FFrame& Frame);

	bool bInitialized = false;
	void* DllHandle = nullptr;
	TUniquePtr<NV_ENCODE_API_FUNCTION_LIST> NvEncodeAPI;
	void* EncoderInterface = nullptr;
	NV_ENC_INITIALIZE_PARAMS NvEncInitializeParams;

	// Used to atomically change NvEnc settings, so if the outside calls GetConfig, it
	// gets a valid result, instead of something that was in the middle of being updated.
	mutable FCriticalSection ConfigCS;

	NV_ENC_CONFIG NvEncConfig;
	uint32 CapturedFrameCount = 0; // of captured, not encoded frames
	static constexpr uint32 NumBufferedFrames = 3;
	FFrame BufferedFrames[NumBufferedFrames];
	TUniquePtr<FThread> EncoderThread;
	FThreadSafeBool bExitEncoderThread = false;

	// Desired config.
	// This is not applied immediately. It's applied when the next frame is sent to
	// the encoder
	FVideoEncoderConfig Config;
	FH264Settings ConfigH264;

	class FEncoderDevice
	{
	public:
		FEncoderDevice();

		TRefCountPtr<ID3D11Device> Device;
		TRefCountPtr<ID3D11DeviceContext> DeviceContext;
	};

	TUniquePtr<FEncoderDevice> EncoderDevice;

#if NVENC_VIDEO_ENCODER_DEBUG
	// This is just for debugging
	void ClearFrame(FFrame& Frame);
	// Timings in milliseconds. Just for debugging
	struct FFrameTiming
	{
		// 0 : CopyBufferStart -> CopyBufferFinish
		// 1 : CopyBufferStart -> EncodingStart
		// 2 : CopyBufferStart -> EncodingFinish
		double Total[3];
		// 0 : CopyBufferStart -> CopyBufferFinish
		// 1 : CopyBufferFinish -> EncodingStart
		// 2 : EncodingStart -> EncodingFinish
		double Steps[3];
	};
	TArray<FFrameTiming> Timings;
#endif

	// When we receive an "Encode" call, we can't send to the encoder right away because maybe the
	// texture copy hasn't completed yet.
	// So, we put them in this queue and keep checking if the texture copy finished
	TQueue<FFrame*> CopyingQueue;
};

//////////////////////////////////////////////////////////////////////////
// FNvVideoEncoder implementation
//////////////////////////////////////////////////////////////////////////

FNvVideoEncoder::FEncoderDevice::FEncoderDevice()
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
			CHECK_HR_VOID(UE4D3DDevice->QueryInterface(__uuidof(IDXGIDevice), (void**)DXGIDevice.GetInitReference()));
			CHECK_HR_VOID(DXGIDevice->GetAdapter(Adapter.GetInitReference()));
			FeatureLevel = D3D_FEATURE_LEVEL_11_0;
		}
		else if (RHIName == TEXT("D3D12"))
		{
			auto UE4D3DDevice = static_cast<ID3D12Device*>(GDynamicRHI->RHIGetNativeDevice());
			checkf(UE4D3DDevice != nullptr, TEXT("Cannot initialize NvEnc with invalid device"));
			LUID AdapterLuid = UE4D3DDevice->GetAdapterLuid();
			TRefCountPtr<IDXGIFactory4> DXGIFactory;
			CHECK_HR_VOID(CreateDXGIFactory(IID_PPV_ARGS(DXGIFactory.GetInitReference())));
			// To use a shared texture from D3D12, we need to use a D3D 11.1 device, because we need the
			// D3D11Device1::OpenSharedResource1 method
			FeatureLevel = D3D_FEATURE_LEVEL_11_1;
			CHECK_HR_VOID(DXGIFactory->EnumAdapterByLuid(AdapterLuid, IID_PPV_ARGS(Adapter.GetInitReference())));
		}
		else
		{
			UE_LOG(LogAVEncoder, Fatal, TEXT("NvEnc requires D3D11/D3D12"));
			return;
		}

		D3D_FEATURE_LEVEL ActualFeatureLevel;

		CHECK_HR_VOID(D3D11CreateDevice(
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
			UE_LOG(LogAVEncoder, Fatal, TEXT("Failed to create a D3D 11.1 device. This is needed when using the D3D12 renderer."));
		}
	}
	else
	{
		UE_LOG(LogAVEncoder, Error, TEXT("Attempting to create Encoder Device without existing RHI"));
	}
}

const uint32 BitstreamSize = 1024 * 1024 * 2;

#define NV_RESULT(NvFunction) NvFunction == NV_ENC_SUCCESS

FNvVideoEncoder::FNvVideoEncoder()
{
	DllHandle = FPlatformProcess::GetDllHandle(GetDllName());
	checkf(DllHandle != nullptr, TEXT("Failed to load NvEncode dll"));
	if (!DllHandle)
	{
		return;
	}

}

FNvVideoEncoder::~FNvVideoEncoder()
{
	if (DllHandle)
	{
		UE_LOG(LogAVEncoder, Fatal, TEXT("FNvVideoEncoder Shutdown not called before destruction."));
	}
}

const TCHAR* FNvVideoEncoder::GetName() const
{
	return TEXT("h264.nvenc");
}
const TCHAR* FNvVideoEncoder::GetType() const
{
	return TEXT("h264");
}

bool FNvVideoEncoder::Initialize(const FVideoEncoderConfig& InConfig)
{
	check(!bInitialized);

	Config = InConfig;
	ConfigH264 = {};
	ReadH264Settings(Config.Options, ConfigH264);

	UE_LOG(LogAVEncoder, Log, TEXT("FNvVideoEncoder initialization with %u*%d, %u FPS"),
		Config.Width, Config.Height, Config.Framerate);

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
		NvEncInitializeParams.encodeWidth = NvEncInitializeParams.darWidth = Config.Width;
		NvEncInitializeParams.encodeHeight = NvEncInitializeParams.darHeight = Config.Height;
		NvEncInitializeParams.encodeGUID = NV_ENC_CODEC_H264_GUID;

		if (Config.Preset == FVideoEncoderConfig::EPreset::LowLatency)
			NvEncInitializeParams.presetGUID = NV_ENC_PRESET_LOW_LATENCY_HQ_GUID;
		else if (Config.Preset == FVideoEncoderConfig::EPreset::HighQuality)
			NvEncInitializeParams.presetGUID = NV_ENC_PRESET_HQ_GUID;
		else
		{
			check(false);
		}

		NvEncInitializeParams.frameRateNum = Config.Framerate;
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

		NvEncConfig.profileGUID = Config.Preset == FVideoEncoderConfig::EPreset::LowLatency ? NV_ENC_H264_PROFILE_BASELINE_GUID : NV_ENC_H264_PROFILE_MAIN_GUID;

		NvEncConfig.gopLength = NvEncInitializeParams.frameRateNum; // once a sec

		NV_ENC_RC_PARAMS& RcParams = NvEncConfig.rcParams;
		RcParams.rateControlMode = ToNvEncRcMode(ConfigH264.RcMode);

		RcParams.enableMinQP = true;
		RcParams.minQP = { 20, 20, 20 };

		RcParams.maxBitRate = Config.MaxBitrate;
		RcParams.averageBitRate = FMath::Min(Config.Bitrate, RcParams.maxBitRate);

		NvEncConfig.encodeCodecConfig.h264Config.idrPeriod = NvEncConfig.gopLength;

		// configure "entire frame as a single slice"
		// seems WebRTC implementation doesn't work well with slicing, default mode 
		// (Mode=3/ModeData=4 - 4 slices per frame) produces (rarely) grey full screen or just top half of it. 
		// it also can be related with our handling of slices in proxy's FakeVideoEncoder
		if (Config.Preset == FVideoEncoderConfig::EPreset::LowLatency)
		{
			NvEncConfig.encodeCodecConfig.h264Config.sliceMode = 0;
			NvEncConfig.encodeCodecConfig.h264Config.sliceModeData = 0;
		}
		else
		{
			NvEncConfig.encodeCodecConfig.h264Config.sliceMode = 3;
			NvEncConfig.encodeCodecConfig.h264Config.sliceModeData = 1;
		}

		// let encoder slice encoded frame so they can fit into RTP packets
		// commented out because at some point it started to produce immediately visible visual artifacts on players
		//NvEncConfig.encodeCodecConfig.h264Config.sliceMode = 1;
		//NvEncConfig.encodeCodecConfig.h264Config.sliceModeData = 1100; // max bytes per slice

		// repeat SPS/PPS with each key-frame for a case when the first frame (with mandatory SPS/PPS) 
		// was dropped by WebRTC
		NvEncConfig.encodeCodecConfig.h264Config.repeatSPSPPS = 1;

		// maybe doesn't have an effect, high level is chosen because we aim at high bitrate
		NvEncConfig.encodeCodecConfig.h264Config.level = Config.Preset==FVideoEncoderConfig::EPreset::LowLatency ? NV_ENC_LEVEL_H264_52 : NV_ENC_LEVEL_H264_51;

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
			UE_LOG(LogAVEncoder, Fatal, TEXT("NvEnc doesn't support async mode"));
			return false;
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

	if (!InitializeResources())
	{
		return false;
	}

	EncoderThread = MakeUnique<FThread>(TEXT("NvVideoEncoder"), [this]() { EncoderCheckLoop(); });

	UE_LOG(LogAVEncoder, Log, TEXT("NvEnc initialised"));
	
	bInitialized = true;
	return true;
}

void FNvVideoEncoder::Shutdown()
{
	if (!DllHandle)
	{
		return;
	}

	if (EncoderThread)
	{
		bExitEncoderThread = true;

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

FVideoEncoderConfig FNvVideoEncoder::GetConfig() const
{
	FVideoEncoderConfig Cfg;
	FScopeLock ScopedLock(&ConfigCS);
	Cfg.Bitrate = NvEncConfig.rcParams.averageBitRate;
	Cfg.Framerate = NvEncInitializeParams.frameRateNum;
	Cfg.Width = NvEncInitializeParams.encodeWidth;
	Cfg.Height = NvEncInitializeParams.encodeHeight;
	return Cfg;
}

bool FNvVideoEncoder::SetBitrate(uint32 Bitrate)
{
	Config.Bitrate;
	return true;
}

bool FNvVideoEncoder::SetFramerate(uint32 Framerate)
{
	Config.Framerate = Framerate;
	return true;
}

bool FNvVideoEncoder::SetParameter(const FString& Parameter, const FString& Value)
{
	return ReadH264Setting(Parameter, Value, ConfigH264);
}

bool FNvVideoEncoder::CopyTexture(const FTexture2DRHIRef Texture, FTimespan CaptureTs, FTimespan Duration, FBufferId& OutBufferId, FIntPoint Resolution)
{
	check(IsInRenderingThread());

	// Find a free slot we can use
	FFrame* Frame = nullptr;
	for (FFrame& Slot : BufferedFrames)
	{
		if (Slot.State.Load() == EFrameState::Free)
		{
			Frame = &Slot;
			OutBufferId = Slot.Id;
			break;
		}
	}

	if (!Frame)
	{
		UE_LOG(LogAVEncoder, Verbose, TEXT("Frame dropped because NvEnc queue is full"));
		return false;
	}

	Frame->FrameIdx = CapturedFrameCount++;
	Frame->InputFrame.CaptureTs = CaptureTs;
	Frame->InputFrame.Duration = Duration;
	Frame->CopyBufferStartTs = FTimespan::FromSeconds(FPlatformTime::Seconds());

#if NVENC_VIDEO_ENCODER_DEBUG
	// By clearing the frame at this point, we can catch the occasional glimpse of a solid color
	// frame in PixelStreaming if there are any bugs detecting when the copy finished
	ClearFrame(*Frame);
#endif

	CopyTexture(Texture, *Frame, Resolution);

	UE_LOG(LogAVEncoder, Verbose, TEXT("Buffer #%d (%d) captured"), Frame->FrameIdx, OutBufferId);
	Frame->State = EFrameState::Capturing;

	return true;
}

void FNvVideoEncoder::CopyTexture(const FTexture2DRHIRef& Texture, FFrame& Frame, FIntPoint Resolution)
{
	SCOPE_CYCLE_COUNTER(STAT_NvEnc_CopyTexture);
	UpdateRes(Frame, Resolution.Size() ? Resolution : Texture->GetSizeXY());
	CopyTextureImpl(Texture, Frame.InputFrame.Texture, Frame.InputFrame.CopyFence);
}

FNvVideoEncoder::FFrame* FNvVideoEncoder::CheckForFinishedCopy()
{
	FFrame* Frame;
	if (!CopyingQueue.Peek(Frame))
	{
		return nullptr;
	}

	{
		EFrameState State = Frame->State.Load();
		checkf(State == EFrameState::Capturing, TEXT("Buffer %d : Expected state %d, but found %d"), Frame->Id, (int)EFrameState::Captured, (int)State);
	}

	if (Frame->InputFrame.CopyFence->Poll())
	{
		CopyingQueue.Pop();
		Frame->State = EFrameState::Captured;
		Frame->CopyBufferFinishTs = FTimespan::FromSeconds(FPlatformTime::Seconds());
		return Frame;
	}
	else
	{
		return nullptr;
	}

}

void FNvVideoEncoder::Encode(FBufferId BufferId, bool bForceKeyFrame, uint32 Bitrate, TUniquePtr<FEncoderVideoFrameCookie> Cookie)
{
	FFrame& Frame = BufferedFrames[BufferId];

	{
		EFrameState State = Frame.State.Load();
		checkf(State == EFrameState::Capturing, TEXT("Buffer %d : Expected state %d, but found %d"), BufferId, (int)EFrameState::Captured, (int)State);
	}

	Frame.InputFrame.bForceKeyFrame = bForceKeyFrame;
	Frame.BitrateRequested = Bitrate;
	Frame.OutputFrame.Cookie = MoveTemp(Cookie);
	CopyingQueue.Enqueue(&Frame);
}

void FNvVideoEncoder::Drop(FBufferId BufferId)
{
	FFrame& Frame = BufferedFrames[BufferId];

	{
		EFrameState State = Frame.State.Load();
		checkf(State == EFrameState::Capturing, TEXT("Buffer %d: Expected state %d, found %d")
			, BufferId, (int)EFrameState::Capturing, (int)State);
	}

	Frame.State = EFrameState::Free;

	UE_LOG(LogAVEncoder, Log, TEXT("Buffer #%d (%d) dropped"), BufferedFrames[BufferId].FrameIdx, BufferId);
}

void FNvVideoEncoder::UpdateNvEncConfig(FIntPoint Resolution, uint32 Bitrate)
{
	bool bSettingsChanged = false;
	bool bResolutionChanged = false;

	// Putting all this in a single scope, so we can put changes to the config struct
	// inside a big lock
	{
		FScopeLock ScopedLock(&ConfigCS);

		//
		// If an explicit Bitrate was specified, use that one, if not, use the one
		// from the Config struct
		//
		if (Bitrate)
		{
			if (NvEncConfig.rcParams.averageBitRate != Bitrate)
			{
				NvEncConfig.rcParams.averageBitRate = Bitrate;
				Config.Bitrate = Bitrate;
				bSettingsChanged = true;
			}
		}
		else 
		{ 
			if (NvEncConfig.rcParams.averageBitRate != Config.Bitrate)
			{
				NvEncConfig.rcParams.averageBitRate = Config.Bitrate;
				bSettingsChanged = true;
			}
		}

		if (NvEncConfig.rcParams.minQP.qpIntra != ConfigH264.QP)
		{
			NvEncConfig.rcParams.minQP.qpIntra = NvEncConfig.rcParams.minQP.qpInterP = NvEncConfig.rcParams.minQP.qpInterB = ConfigH264.QP;
			UE_LOG(LogAVEncoder, Log, TEXT("MinQP %u"), ConfigH264.QP);
			bSettingsChanged = true;
		}

		NV_ENC_PARAMS_RC_MODE RcMode = ToNvEncRcMode(ConfigH264.RcMode);
		if (RcMode != NvEncConfig.rcParams.rateControlMode)
		{
			NvEncConfig.rcParams.rateControlMode = RcMode;
			UE_LOG(LogAVEncoder, Log, TEXT("Rate Control mode %s"), ToString(RcMode));
			bSettingsChanged = true;
		}

		if (UpdateFramerate())
		{
			bSettingsChanged = true;
		}

		// Only try and change resolution if required
		if (Resolution.X && Resolution.Y)
		{
			if (Resolution.X != NvEncInitializeParams.encodeWidth ||
				Resolution.Y != NvEncInitializeParams.encodeHeight)
			{
				NvEncInitializeParams.encodeWidth = NvEncInitializeParams.darWidth = Resolution.X;
				NvEncInitializeParams.encodeHeight = NvEncInitializeParams.darHeight = Resolution.Y;

				bSettingsChanged = true;
				bResolutionChanged = true;
			}
		}

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

bool FNvVideoEncoder::UpdateFramerate()
{

	if (NvEncInitializeParams.frameRateNum != Config.Framerate)
	{
		NvEncInitializeParams.frameRateNum = Config.Framerate;
		UE_LOG(LogAVEncoder, Log, TEXT("NvEnc reconfigured to %d FPS"), NvEncInitializeParams.frameRateNum);
		return true;
	}

	return false;
}

// checks if resolution changed, either the game res changed or new streaming resolution was specified by the console var
void FNvVideoEncoder::UpdateRes(FFrame& Frame, FIntPoint Resolution)
{
	check(IsInRenderingThread());

	FInputFrame& InputFrame = Frame.InputFrame;

	// check if target resolution matches our currently allocated `InputFrame.Texture` resolution
	if (InputFrame.Texture->GetSizeX() == Resolution.X && InputFrame.Texture->GetSizeY() == Resolution.Y)
	{
		return;
	}

	// reallocate and re-register InputFrame with NvEnc
	ReleaseFrameInputBuffer(Frame);
	verify(InitFrameInputBuffer(Frame, Resolution.X, Resolution.Y));
}

void FNvVideoEncoder::SubmitFrameToEncoder(FFrame& Frame)
{
	check(Frame.State.Load() == EFrameState::Captured);

	SCOPE_CYCLE_COUNTER(STAT_NvEnc_SubmitFrameToEncoder);

	Frame.State = EFrameState::Encoding;
	Frame.EncodingStartTs = FTimespan::FromSeconds(FPlatformTime::Seconds());

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

	if (Frame.InputFrame.bForceKeyFrame)
		PicParams.encodePicFlags |= NV_ENC_PIC_FLAG_FORCEIDR;

	_NVENCSTATUS Result = NvEncodeAPI->nvEncEncodePicture(EncoderInterface, &PicParams);
	checkf(NV_RESULT(Result), TEXT("Failed to encode frame (status: %d)"), Result);
}

void FNvVideoEncoder::EncoderCheckLoop()
{
	// This thread will both encode frames and will also wait for the next frame
	// to finish encoding.
	TQueue<FFrame*> CurrentlyEncodingQueue;

	while (!bExitEncoderThread)
	{
		//
		// Check if any frames finished copying so we can submit then to the encoder
		//
		while(true)
		{
			FFrame* Frame = CheckForFinishedCopy();
			if (!Frame)
			{
				break;
			}
			UpdateNvEncConfig(Frame->InputFrame.Texture->GetSizeXY(), Frame->BitrateRequested);
			SubmitFrameToEncoder(*Frame);
			CurrentlyEncodingQueue.Enqueue(Frame);
		}

		//
		// Check for finished encoding work
		//
		if (!CurrentlyEncodingQueue.IsEmpty())
		{
			HANDLE Handle = (**CurrentlyEncodingQueue.Peek()).OutputFrame.EventHandle;
			DWORD Result = WaitForSingleObject(Handle, 2);
			if (Result == WAIT_OBJECT_0)
			{
				FFrame* Frame = nullptr;
				verify(CurrentlyEncodingQueue.Dequeue(Frame));
				ResetEvent(Frame->OutputFrame.EventHandle);
				UE_LOG(LogAVEncoder, Verbose, TEXT("Buffer #%d (%d) encoded"), Frame->FrameIdx, Frame->Id);
				ProcessFrame(*Frame);
			}
			else if (Result == WAIT_TIMEOUT)
			{
				// Nothing to do. This is expected
			}
			else
			{
				check(0 && "Unexpected code path");
			}
		}
	}
}

void FNvVideoEncoder::ProcessFrame(FFrame& Frame)
{
	check(Frame.State.Load() == EFrameState::Encoding);

	FOutputFrame& OutputFrame = Frame.OutputFrame;

	FAVPacket Packet(EPacketType::Video);
	NV_ENC_PIC_TYPE PicType;
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

		PicType = LockBitstream.pictureType;
		checkf(PicType == NV_ENC_PIC_TYPE_IDR || Frame.InputFrame.bForceKeyFrame==false, TEXT("key frame requested by but not provided by NvEnc. NvEnc provided %d"), (int)PicType);
		Packet.Video.bKeyFrame = (PicType == NV_ENC_PIC_TYPE_IDR) ? true : false;
		Packet.Video.FrameAvgQP = LockBitstream.frameAvgQP; 
		Packet.Data = TArray<uint8>(reinterpret_cast<const uint8*>(LockBitstream.bitstreamBufferPtr), LockBitstream.bitstreamSizeInBytes);
		Result = NvEncodeAPI->nvEncUnlockBitstream(EncoderInterface, Frame.OutputFrame.BitstreamBuffer);
		checkf(NV_RESULT(Result), TEXT("Failed to unlock bitstream (status: %d)"), Result);
	}

	Frame.EncodingFinishTs = FTimespan::FromSeconds(FPlatformTime::Seconds());

	Packet.Timestamp = Frame.InputFrame.CaptureTs;
	Packet.Duration = Frame.InputFrame.Duration;
	Packet.Video.Width = Frame.InputFrame.Texture->GetSizeX();
	Packet.Video.Height = Frame.InputFrame.Texture->GetSizeY();
	Packet.Video.Framerate = NvEncInitializeParams.frameRateNum;
	Packet.Timings.EncodeStartTs = Frame.EncodingStartTs;
	Packet.Timings.EncodeFinishTs = Frame.EncodingFinishTs;

#if NVENC_VIDEO_ENCODER_DEBUG
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

	UE_LOG(LogAVEncoder, VeryVerbose, TEXT("encoded %s ts %lld, %d bytes")
		, ToString(PicType)
		, Packet.Timestamp.GetTicks()
		, (int)Packet.Data.Num());

	{
		SCOPE_CYCLE_COUNTER(STAT_NvEnc_OnEncodedVideoFrameCallback);
		OnEncodedVideoFrame(Packet, MoveTemp(Frame.OutputFrame.Cookie));
	}

	Frame.State = EFrameState::Free;
}

bool FNvVideoEncoder::InitFrameInputBuffer(FFrame& Frame, uint32 Width, uint32 Height)
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
		InputFrame.Texture = RHICreateTexture2D(Width, Height, EPixelFormat::PF_B8G8R8A8, 1, 1, TexCreate_RenderTargetable | TexCreate_Shared, CreateInfo);
	}

	// Share this texture with the encoder device.
	FString RHIName = GDynamicRHI->GetName();

	if (RHIName == TEXT("D3D11"))
	{
		ID3D11Texture2D* ResolvedTexture = (ID3D11Texture2D*)InputFrame.Texture->GetTexture2D()->GetNativeResource();

		TRefCountPtr<IDXGIResource> DXGIResource;
		CHECK_HR_DEFAULT(ResolvedTexture->QueryInterface(IID_PPV_ARGS(DXGIResource.GetInitReference())));

		//
		// NOTE : The HANDLE IDXGIResource::GetSharedHandle gives us is NOT an NT Handle, and therefre we should not call CloseHandle on it
		//
		HANDLE SharedHandle;
		CHECK_HR_DEFAULT(DXGIResource->GetSharedHandle(&SharedHandle));
		CHECK_HR_DEFAULT(EncoderDevice->Device->OpenSharedResource(SharedHandle, __uuidof(ID3D11Texture2D), (LPVOID*)&InputFrame.SharedTexture));
	}
	else if (RHIName == TEXT("D3D12"))
	{
		auto UE4D3DDevice = static_cast<ID3D12Device*>(GDynamicRHI->RHIGetNativeDevice());
		static uint32 NamingIdx = 0;
		ID3D12Resource* ResolvedTexture = (ID3D12Resource*)InputFrame.Texture->GetTexture2D()->GetNativeResource();

		//
		// NOTE: ID3D12Device::CreateSharedHandle gives as an NT Handle, and so we need to call CloseHandle on it
		//
		HANDLE SharedHandle;
		HRESULT Res1 = UE4D3DDevice->CreateSharedHandle(ResolvedTexture, NULL, GENERIC_ALL, *FString::Printf(TEXT("PixelStreaming_NvEnc_%u"), NamingIdx++), &SharedHandle);
		CHECK_HR_DEFAULT(Res1);

		TRefCountPtr <ID3D11Device1> Device1;
		CHECK_HR_DEFAULT(EncoderDevice->Device->QueryInterface(__uuidof(ID3D11Device1), (void**)Device1.GetInitReference()));
		CHECK_HR_DEFAULT(Device1->OpenSharedResource1(SharedHandle, __uuidof(ID3D11Texture2D), (LPVOID*)&InputFrame.SharedTexture));
		verify(CloseHandle(SharedHandle));
	}

	// Register input back buffer
	{
		NV_ENC_REGISTER_RESOURCE RegisterResource;
		FMemory::Memzero(RegisterResource);
		EPixelFormat PixelFormat = InputFrame.Texture->GetFormat();

		RegisterResource.version = NV_ENC_REGISTER_RESOURCE_VER;
		RegisterResource.resourceType = NV_ENC_INPUT_RESOURCE_TYPE_DIRECTX;
		RegisterResource.resourceToRegister = (void*)InputFrame.SharedTexture;
		RegisterResource.width = Width;
		RegisterResource.height = Height;
		RegisterResource.bufferFormat = NV_ENC_BUFFER_FORMAT_ABGR;	// Make sure ResolvedTexture is created with a compatible format
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

	return true;
}

bool FNvVideoEncoder::InitializeResources()
{
	for (uint32 i = 0; i < NumBufferedFrames; ++i)
	{
		FFrame& Frame = BufferedFrames[i];

		if (!InitFrameInputBuffer(Frame, NvEncInitializeParams.encodeWidth, NvEncInitializeParams.encodeHeight))
		{
			return false;
		}

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

	return true;
}

void FNvVideoEncoder::ReleaseFrameInputBuffer(FFrame& Frame)
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

	InputFrame.Texture.SafeRelease();
	if (InputFrame.SharedTexture)
	{
		InputFrame.SharedTexture->Release();
		InputFrame.SharedTexture = nullptr;
	}

	if (InputFrame.CopyFence)
	{
		InputFrame.CopyFence.SafeRelease();
	}
}

void FNvVideoEncoder::ReleaseResources()
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

void FNvVideoEncoder::RegisterAsyncEvent(void** OutEvent)
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

void FNvVideoEncoder::UnregisterAsyncEvent(void* Event)
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

#if NVENC_VIDEO_ENCODER_DEBUG
// Fills with a solid colour
void FNvVideoEncoder::ClearFrame(FFrame& Frame)
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

	FRHIRenderPassInfo RPInfo(Frame.InputFrame.Texture, ERenderTargetActions::Load_Store);
	TransitionRenderPassTargets(RHICmdList, RPInfo);
	RHICmdList.BeginRenderPass(RPInfo, TEXT("ClearCanvas"));
	RHICmdList.SetViewport(0, 0, 0.0f
		, Frame.InputFrame.Texture->GetSizeXY().X
		, Frame.InputFrame.Texture->GetSizeXY().Y, 1.0f);

	DrawClearQuad(RHICmdList, Colors[Frame.Id]);
	RHICmdList.EndRenderPass();
}
#endif


//////////////////////////////////////////////////////////////////////////
// FNvVideoEncoderFactory implementation
//////////////////////////////////////////////////////////////////////////

FNvVideoEncoderFactory::FNvVideoEncoderFactory()
{
}

FNvVideoEncoderFactory::~FNvVideoEncoderFactory()
{
}

const TCHAR* FNvVideoEncoderFactory::GetName() const
{
	return TEXT("nvenc");
}

TArray<FString> FNvVideoEncoderFactory::GetSupportedCodecs() const
{
	TArray<FString> Codecs;

	if (!IsRHIDeviceNVIDIA())
	{
		UE_LOG(LogAVEncoder, Log, TEXT("No NvEnc because no NVidia card found"));
		return Codecs;
	}

	void* Handle = FPlatformProcess::GetDllHandle(GetDllName());
	if (Handle == nullptr)
	{
		UE_LOG(LogAVEncoder, Error, TEXT("NVidia card found, but no NvEnc DLL installed."));
		return Codecs;
	}
	else
	{
		FPlatformProcess::FreeDllHandle(Handle);
	}

	Codecs.Add(TEXT("h264"));
	return Codecs;
}

TUniquePtr<FVideoEncoder> FNvVideoEncoderFactory::CreateEncoder(const FString& Codec)
{
	if (Codec=="h264")
	{
		return TUniquePtr<FVideoEncoder>(new FNvVideoEncoder());
	}
	else
	{
		UE_LOG(LogAVEncoder, Error, TEXT("FNvVideoEncoderFactory doesn't support the %s codec"), *Codec);
		return nullptr;
	}
}


} // namespace AVEncoder


