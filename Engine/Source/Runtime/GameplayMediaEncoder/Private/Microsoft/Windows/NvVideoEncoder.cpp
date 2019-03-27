// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "NvVideoEncoder.h"

#if PLATFORM_WINDOWS

#include "CoreMinimal.h"
#include "Misc/CoreDelegates.h"
#include "ScreenRendering.h"
#include "RendererInterface.h"
#include "PipelineStateCache.h"
#include "CommonRenderResources.h"
#include "Modules/ModuleManager.h"

#include "SceneUtils.h"
#include "EncoderDevice.h"

#include "VideoRecordingSystem.h"

DEFINE_LOG_CATEGORY(NvVideoEncoder);

const uint32 BitstreamSize = 1024 * 1024 * 2;

#define CHECK_NV_RES(NvCall)\
{\
	NVENCSTATUS Res = NvCall;\
	if (Res != NV_ENC_SUCCESS)\
	{\
		UE_LOG(NvVideoEncoder, Error, TEXT("`" #NvCall "` failed with error code: %d"), Res);\
		/*check(false);*/\
		return false;\
	}\
}

CSV_DECLARE_CATEGORY_EXTERN(GameplayMediaEncoder);

DECLARE_CYCLE_STAT(TEXT("NvEnc_WaitForEncodeEvent"), STAT_NvEnc_WaitForEncodeEvent, STATGROUP_VideoRecordingSystem);
DECLARE_FLOAT_COUNTER_STAT(TEXT("NvEnc_CaptureToEncodeStart"), STAT_NvEnc_CaptureToEncodeStart, STATGROUP_VideoRecordingSystem);
DECLARE_FLOAT_COUNTER_STAT(TEXT("NvEnc_EncodeTime"), STAT_NvEnc_EncodeTime, STATGROUP_VideoRecordingSystem);
DECLARE_FLOAT_COUNTER_STAT(TEXT("NvEnc_EncodeToWriterTime"), STAT_NvEnc_EncodeToWriterTime, STATGROUP_VideoRecordingSystem);

#define CLOSE_EVENT_HANDLE(EventHandle) CloseHandle(EventHandle);

GAMEPLAYMEDIAENCODER_START

FNvVideoEncoder::FNvVideoEncoder(const FOutputSampleCallback& OutputCallback, TSharedPtr<FEncoderDevice> InEncoderDevice)
	: FBaseVideoEncoder(OutputCallback)
	, EncoderDevice(InEncoderDevice)
{
}

FNvVideoEncoder::~FNvVideoEncoder()
{
	bExitEncoderThread = true;

	if (EncoderThread.IsValid())
	{
		// Trigger an event to ensure we can get out of the encoder thread.
		SetEvent(EncodeQueue.EncodeEvent);

		// Exit encoder runnable thread before shutting down NvEnc interface
		EncoderThread->Join();
	}
	ReleaseResources();

	if (EncoderInterface)
	{
		NVENCSTATUS Result = NvEncodeAPI->nvEncDestroyEncoder(EncoderInterface);
		if (Result != NV_ENC_SUCCESS)
		{
			UE_LOG(NvVideoEncoder, Error, TEXT("Failed to destroy NvEnc interface"));
		}
		EncoderInterface = nullptr;
	}

	if (DllHandle)
	{
		FPlatformProcess::FreeDllHandle(DllHandle);
	}
}

bool FNvVideoEncoder::Initialize(const FVideoEncoderConfig& InConfig)
{
	if (bInitialized)
	{
		UE_LOG(NvVideoEncoder, Error, TEXT("Encoder already initialized. Re-initialization is not implemented."));
		return false;
	}

	// Fails to register a DX11 resource in nvEncRegisterResource, need to use CUDA on Win7
	// Error: `NvEncodeAPI->nvEncRegisterResource(EncoderInterface, &RegisterResource)` failed with error code: 22
	if (!FWindowsPlatformMisc::VerifyWindowsVersion(6, 2) /*Is Win8 or higher?*/)
	{
		UE_LOG(NvVideoEncoder, Error, TEXT("NvEncoder for Windows 7 is not implemented"));
		return false;
	}

	if (!FBaseVideoEncoder::Initialize(InConfig))
	{
		return false;
	}

	DllHandle = FPlatformProcess::GetDllHandle(TEXT("nvEncodeAPI64.dll"));
	if (DllHandle == nullptr)
	{
		UE_LOG(NvVideoEncoder, Error, TEXT("Failed to load NvEncode dll"));
		return false;
	}

	// create the encoder instance
	{		
		// define a function pointer for creating an instance of nvEncodeAPI
		typedef NVENCSTATUS(NVENCAPI *NVENCAPIPROC)(NV_ENCODE_API_FUNCTION_LIST*);
		NVENCAPIPROC NvEncodeAPICreateInstanceFunc;

#	pragma warning(push)
#		pragma warning(disable: 4191) // https://stackoverflow.com/a/4215425/453271
		NvEncodeAPICreateInstanceFunc = (NVENCAPIPROC)FPlatformProcess::GetDllExport((HMODULE)DllHandle, TEXT("NvEncodeAPICreateInstance"));
#	pragma warning(pop)

		if (NvEncodeAPICreateInstanceFunc == nullptr)
		{
			UE_LOG(NvVideoEncoder, Error, TEXT("NvEncodeAPICreateInstance failed"));
			return false;
		}

		NvEncodeAPI.Reset(new NV_ENCODE_API_FUNCTION_LIST);
		FMemory::Memzero(NvEncodeAPI.Get(), sizeof(NV_ENCODE_API_FUNCTION_LIST));
		NvEncodeAPI->version = NV_ENCODE_API_FUNCTION_LIST_VER;
		CHECK_NV_RES(NvEncodeAPICreateInstanceFunc(NvEncodeAPI.Get()));
	}

	// Open an encoding session
	{
		NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS OpenEncodeSessionExParams;
		FMemory::Memzero(OpenEncodeSessionExParams);
		OpenEncodeSessionExParams.version = NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS_VER;
		OpenEncodeSessionExParams.device = EncoderDevice->Device;
		OpenEncodeSessionExParams.deviceType = NV_ENC_DEVICE_TYPE_DIRECTX;	// Currently only DX11 is supported
		OpenEncodeSessionExParams.apiVersion = NVENCAPI_VERSION;
		CHECK_NV_RES(NvEncodeAPI->nvEncOpenEncodeSessionEx(&OpenEncodeSessionExParams, &EncoderInterface));
	}	

	FMemory::Memzero(NvEncInitializeParams);

	// Set initialization parameters
	{
		NvEncInitializeParams.version = NV_ENC_INITIALIZE_PARAMS_VER;
		NvEncInitializeParams.encodeWidth = Config.Width;
		NvEncInitializeParams.encodeHeight = Config.Height;
		NvEncInitializeParams.darWidth = Config.Width;
		NvEncInitializeParams.darHeight = Config.Height;
		NvEncInitializeParams.encodeGUID = NV_ENC_CODEC_H264_GUID;
		NvEncInitializeParams.presetGUID = NV_ENC_PRESET_HQ_GUID;

		NvEncInitializeParams.frameRateNum = Config.Framerate;
		NvEncInitializeParams.frameRateDen = 1;

		NvEncInitializeParams.enablePTD = 1;
		NvEncInitializeParams.reportSliceOffsets = 0;
		NvEncInitializeParams.enableSubFrameWrite = 0;
		NvEncInitializeParams.encodeConfig = &NvEncConfig;
		NvEncInitializeParams.maxEncodeWidth = Config.Width;
		NvEncInitializeParams.maxEncodeHeight = Config.Height;
	}

	// Get preset config and tweak it accordingly
	{
		NV_ENC_PRESET_CONFIG PresetConfig = {};
		PresetConfig.version = NV_ENC_PRESET_CONFIG_VER;
		PresetConfig.presetCfg.version = NV_ENC_CONFIG_VER;
		CHECK_NV_RES(NvEncodeAPI->nvEncGetEncodePresetConfig(EncoderInterface, NvEncInitializeParams.encodeGUID, NvEncInitializeParams.presetGUID, &PresetConfig));

		FMemory::Memcpy(&NvEncConfig, &PresetConfig.presetCfg, sizeof(NV_ENC_CONFIG));
		
		//NvEncConfig.profileGUID = NV_ENC_H264_PROFILE_BASELINE_GUID;
		NvEncConfig.profileGUID = NV_ENC_H264_PROFILE_MAIN_GUID;
		NvEncConfig.gopLength = Config.Framerate; // once a sec
		NvEncConfig.encodeCodecConfig.h264Config.idrPeriod = Config.Framerate;
		//NvEncConfig.rcParams.rateControlMode = NV_ENC_PARAMS_RC_CBR;
		//NvEncConfig.rcParams.rateControlMode = NV_ENC_PARAMS_RC_2_PASS_VBR;
		NvEncConfig.rcParams.averageBitRate = Config.Bitrate;

		// configure "entire frame as a single slice"
		NvEncConfig.encodeCodecConfig.h264Config.sliceMode = 3;
		NvEncConfig.encodeCodecConfig.h264Config.sliceModeData = 1;

		// repeat SPS/PPS with each key-frame for simplicity of saving recording ring-buffer to .mp4
		// (video stream in .mp4 must start with SPS/PPS)
		NvEncConfig.encodeCodecConfig.h264Config.repeatSPSPPS = 1;

		// high level is chosen because we aim at high bitrate
		NvEncConfig.encodeCodecConfig.h264Config.level = NV_ENC_LEVEL_H264_51;			
	}		

	// Get encoder capability
	{
		NV_ENC_CAPS_PARAM CapsParam;
		FMemory::Memzero(CapsParam);
		CapsParam.version = NV_ENC_CAPS_PARAM_VER;
		CapsParam.capsToQuery = NV_ENC_CAPS_ASYNC_ENCODE_SUPPORT;
		int32 AsyncMode = 0;
		CHECK_NV_RES(NvEncodeAPI->nvEncGetEncodeCaps(EncoderInterface, NvEncInitializeParams.encodeGUID, &CapsParam, &AsyncMode));
		if (AsyncMode == 0)
		{
			UE_LOG(NvVideoEncoder, Error, TEXT("NvEnc doesn't support async mode"));
			return false;
		}

		NvEncInitializeParams.enableEncodeAsync = true;
	}
	
	CHECK_NV_RES(NvEncodeAPI->nvEncInitializeEncoder(EncoderInterface, &NvEncInitializeParams));

	if (!InitializeResources())
	{
		return false;
	}

	EncoderThread.Reset(new FThread(TEXT("NvVideoEncoder"), [this]() { EncoderCheckLoop(); }));

	bInitialized = true;

	return true;
}


bool FNvVideoEncoder::Start()
{
	return true;
}

void FNvVideoEncoder::Stop()
{
}

bool FNvVideoEncoder::SetBitrate(uint32 Bitrate)
{
	// update config and `OutputType`
	if (!FBaseVideoEncoder::SetBitrate(Bitrate))
	{
		return false;
	}

	NvEncInitializeParams.encodeConfig->rcParams.averageBitRate = Bitrate;

	return Reconfigure();
}

bool FNvVideoEncoder::SetFramerate(uint32 Framerate)
{
	// update config and `OutputType`
	if (!FBaseVideoEncoder::SetFramerate(Framerate))
	{
		return false;
	}

	NvEncInitializeParams.frameRateNum = Framerate;

	return Reconfigure();
}

bool FNvVideoEncoder::Reconfigure()
{
	// reconfigure NvEnc
	NV_ENC_RECONFIGURE_PARAMS NvEncReconfigureParams = {};
	NvEncReconfigureParams.version = NV_ENC_RECONFIGURE_PARAMS_VER;
	FMemory::Memcpy(&NvEncReconfigureParams.reInitEncodeParams, &NvEncInitializeParams, sizeof(NvEncInitializeParams));

	CHECK_NV_RES(NvEncodeAPI->nvEncReconfigureEncoder(EncoderInterface, &NvEncReconfigureParams));

	return true;
}

DECLARE_CYCLE_STAT(TEXT("FNvVideoEncoder_Process"), STAT_FNvVideoEncoder_Process, STATGROUP_VideoRecordingSystem);
DECLARE_CYCLE_STAT(TEXT("FNvVideoEncoder_Process_CheckEncoded"), STAT_FNvVideoEncoder_Process_CheckEncoded, STATGROUP_VideoRecordingSystem);

bool FNvVideoEncoder::Process(const FTexture2DRHIRef& Texture, FTimespan Timestamp, FTimespan Duration)
{
	SCOPE_CYCLE_COUNTER(STAT_FNvVideoEncoder_Process);

	check(IsInRenderingThread());

	return ProcessInput(Texture, Timestamp, Duration);
}

bool FNvVideoEncoder::ProcessInput(const FTexture2DRHIRef& Texture, FTimespan Timestamp, FTimespan Duration)
{
	UE_LOG(NvVideoEncoder, Verbose, TEXT("Video input #%u: time %.3f, duration %.3f"), (uint32)InputCount, Timestamp.GetTotalSeconds(), Duration.GetTotalSeconds());

	uint32 BufferIndexToWrite = InputCount % NumBufferedFrames;
	FFrame& Frame = BufferedFrames[BufferIndexToWrite];
	// If we don't have any free buffers, then we skip this rendered frame
	if (Frame.bEncoding)
	{
		return false;
	}

	Frame.bEncoding = true;

	CSV_SCOPED_TIMING_STAT(GameplayMediaEncoder, CopyBackBuffer);
	CopyBackBuffer(Texture, Frame);

	Frame.FrameIdx = InputCount;
	Frame.TimeStamp = Timestamp;
	Frame.Duration = Duration;
	Frame.CaptureTimeStamp = FTimespan::FromSeconds(FPlatformTime::Seconds());

	FFrame* FramePtr = &Frame;
	ExecuteRHICommand([this, FramePtr]() {
		EncodeQueue.Push(FramePtr);
	});

	InputCount++;

	return true;
}

DECLARE_CYCLE_STAT(TEXT("FNvVideoEncoder_CopyBackBuffer"), STAT_FNvVideoEncoder_CopyBackBuffer, STATGROUP_VideoRecordingSystem);

void FNvVideoEncoder::CopyBackBuffer(const FTexture2DRHIRef& SrcBackBuffer, const FFrame& DstFrame)
{
	SCOPE_CYCLE_COUNTER(STAT_FNvVideoEncoder_CopyBackBuffer);

	IRendererModule* RendererModule = &FModuleManager::GetModuleChecked<IRendererModule>("Renderer");
	FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();

	SCOPED_DRAW_EVENTF(RHICmdList, NvVideoEncoder_CopyBackBuffer, TEXT("NvVideoEncoder_CopyBackBuffer %u"), static_cast<uint32>(DstFrame.FrameIdx));

	if (SrcBackBuffer->GetFormat() == DstFrame.ResolvedBackBuffer->GetFormat() &&
		SrcBackBuffer->GetSizeXY() == DstFrame.ResolvedBackBuffer->GetSizeXY())
	{
		RHICmdList.CopyToResolveTarget(SrcBackBuffer, DstFrame.ResolvedBackBuffer, FResolveParams());
	}
	else // Texture format mismatch, use a shader to do the copy.
	{
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		SetRenderTarget(RHICmdList, DstFrame.ResolvedBackBuffer, FTextureRHIRef());
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
		RHICmdList.SetViewport(0, 0, 0.0f, DstFrame.ResolvedBackBuffer->GetSizeX(), DstFrame.ResolvedBackBuffer->GetSizeY(), 1.0f);
	
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

		if (DstFrame.ResolvedBackBuffer->GetSizeX() != SrcBackBuffer->GetSizeX() || DstFrame.ResolvedBackBuffer->GetSizeY() != SrcBackBuffer->GetSizeY())
			PixelShader->SetParameters(RHICmdList, TStaticSamplerState<SF_Bilinear>::GetRHI(), SrcBackBuffer);
		else
			PixelShader->SetParameters(RHICmdList, TStaticSamplerState<SF_Point>::GetRHI(), SrcBackBuffer);
	
		RendererModule->DrawRectangle(
			RHICmdList,
			0, 0,									// Dest X, Y
			DstFrame.ResolvedBackBuffer->GetSizeX(),			// Dest Width
			DstFrame.ResolvedBackBuffer->GetSizeY(),			// Dest Height
			0, 0,									// Source U, V
			1, 1,									// Source USize, VSize
			DstFrame.ResolvedBackBuffer->GetSizeXY(),		// Target buffer size
			FIntPoint(1, 1),						// Source texture size
			*VertexShader,
			EDRF_Default);
	}
}

bool FNvVideoEncoder::InitFrameInputBuffer(FFrame& Frame)
{
	// Create resolved back buffer texture
	{
		// Make sure format used here is compatible with NV_ENC_BUFFER_FORMAT specified later in NV_ENC_REGISTER_RESOURCE bufferFormat
		FRHIResourceCreateInfo CreateInfo;
		// TexCreate_Shared textures are forced to be EPixelFormat::PF_B8G8R8A8.
		Frame.ResolvedBackBuffer = RHICreateTexture2D(Config.Width, Config.Height, EPixelFormat::PF_B8G8R8A8, 1, 1, TexCreate_RenderTargetable | TexCreate_Shared, CreateInfo);
	}

	// Share this texture with the encoder device.
	{
		ID3D11Texture2D* ResolvedBackBuffer = (ID3D11Texture2D*)Frame.ResolvedBackBuffer->GetTexture2D()->GetNativeResource();

		IDXGIResource* DXGIResource = NULL;
		CHECK_HR(ResolvedBackBuffer->QueryInterface(__uuidof(IDXGIResource), (LPVOID*)&DXGIResource));

		HANDLE SharedHandle;
		CHECK_HR(DXGIResource->GetSharedHandle(&SharedHandle));
		CHECK_HR(DXGIResource->Release());

		CHECK_HR(EncoderDevice->Device->OpenSharedResource(SharedHandle, __uuidof(ID3D11Texture2D), (LPVOID*)&Frame.SharedBackBuffer));
	}

	FMemory::Memzero(Frame.InputFrame);
	// Register input back buffer
	{
		NV_ENC_REGISTER_RESOURCE RegisterResource;
		FMemory::Memzero(RegisterResource);
		RegisterResource.version = NV_ENC_REGISTER_RESOURCE_VER;
		RegisterResource.resourceType = NV_ENC_INPUT_RESOURCE_TYPE_DIRECTX;
		RegisterResource.resourceToRegister = (void*)Frame.SharedBackBuffer;
		RegisterResource.width = Config.Width;
		RegisterResource.height = Config.Height;
		// TexCreate_Shared textures are forced to be EPixelFormat::PF_B8G8R8A8.
		RegisterResource.bufferFormat = NV_ENC_BUFFER_FORMAT_ABGR;	// Make sure ResolvedBackBuffer is created with a compatible format
		CHECK_NV_RES(NvEncodeAPI->nvEncRegisterResource(EncoderInterface, &RegisterResource));

		Frame.InputFrame.RegisteredResource = RegisterResource.registeredResource;
		Frame.InputFrame.BufferFormat = RegisterResource.bufferFormat;
	}

	// Map input buffer resource
	{
		NV_ENC_MAP_INPUT_RESOURCE MapInputResource;
		FMemory::Memzero(MapInputResource);
		MapInputResource.version = NV_ENC_MAP_INPUT_RESOURCE_VER;
		MapInputResource.registeredResource = Frame.InputFrame.RegisteredResource;
		CHECK_NV_RES(NvEncodeAPI->nvEncMapInputResource(EncoderInterface, &MapInputResource));
		Frame.InputFrame.MappedResource = MapInputResource.mappedResource;
	}

	return true;
}

bool FNvVideoEncoder::InitializeResources()
{
	for (uint32 i = 0; i < NumBufferedFrames; ++i)
	{
		FFrame& Frame = BufferedFrames[i];
		
		if (!InitFrameInputBuffer(Frame))
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
			CHECK_NV_RES(NvEncodeAPI->nvEncCreateBitstreamBuffer(EncoderInterface, &CreateBitstreamBuffer));
			Frame.OutputFrame.BitstreamBuffer = CreateBitstreamBuffer.bitstreamBuffer;
		}

		RegisterAsyncEvent(&Frame.OutputFrame.EventHandle);
	}

	return true;
}

bool FNvVideoEncoder::ReleaseFrameInputBuffer(FNvVideoEncoder::FFrame& Frame)
{
	if (Frame.InputFrame.MappedResource)
	{
		CHECK_NV_RES(NvEncodeAPI->nvEncUnmapInputResource(EncoderInterface, Frame.InputFrame.MappedResource));
		Frame.InputFrame.MappedResource = nullptr;
	}

	if (Frame.InputFrame.RegisteredResource)
	{
		CHECK_NV_RES(NvEncodeAPI->nvEncUnregisterResource(EncoderInterface, Frame.InputFrame.RegisteredResource));
		Frame.InputFrame.RegisteredResource = nullptr;
	}

	Frame.ResolvedBackBuffer.SafeRelease();
	if (Frame.SharedBackBuffer)
	{
		Frame.SharedBackBuffer->Release();
	}

	return true;
}

bool FNvVideoEncoder::RegisterAsyncEvent(void** OutEvent)
{
	NV_ENC_EVENT_PARAMS EventParams;
	FMemory::Memzero(EventParams);
	EventParams.version = NV_ENC_EVENT_PARAMS_VER;
#if defined PLATFORM_WINDOWS
	EventParams.completionEvent = CreateEvent(nullptr, true, false, nullptr);
#endif
	CHECK_NV_RES(NvEncodeAPI->nvEncRegisterAsyncEvent(EncoderInterface, &EventParams));
	*OutEvent = EventParams.completionEvent;

	return true;
}

bool FNvVideoEncoder::UnregisterAsyncEvent(void* Event)
{
	if (Event)
	{
		NV_ENC_EVENT_PARAMS EventParams;
		FMemory::Memzero(EventParams);
		EventParams.version = NV_ENC_EVENT_PARAMS_VER;
		EventParams.completionEvent = Event;
		CHECK_NV_RES(NvEncodeAPI->nvEncUnregisterAsyncEvent(EncoderInterface, &EventParams));
	}

	return true;
}

bool FNvVideoEncoder::ReleaseResources()
{
	for (uint32 i = 0; i < NumBufferedFrames; ++i)
	{
		FFrame& Frame = BufferedFrames[i];

		if (!ReleaseFrameInputBuffer(Frame))
		{
			return false;
		}

		if (Frame.OutputFrame.BitstreamBuffer)
		{
			CHECK_NV_RES(NvEncodeAPI->nvEncDestroyBitstreamBuffer(EncoderInterface, Frame.OutputFrame.BitstreamBuffer));
			Frame.OutputFrame.BitstreamBuffer = nullptr;
		}

		if (Frame.OutputFrame.EventHandle)
		{
			UnregisterAsyncEvent(Frame.OutputFrame.EventHandle);
			CLOSE_EVENT_HANDLE(Frame.OutputFrame.EventHandle);
			Frame.OutputFrame.EventHandle = nullptr;
		}
	}

	return true;
}

void FNvVideoEncoder::EncoderCheckLoop()
{
	// This thread will both encode frames and will also wait for the next frame
	// to finish encoding.
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
		HANDLE Handles[NUM_ENCODING_EVENTS];
		Handles[START_ENCODING_EVENT] = EncodeQueue.EncodeEvent;
		FFrame& Frame = BufferedFrames[OutputCount % NumBufferedFrames];
		Handles[FINISHED_ENCODING_EVENT] = Frame.OutputFrame.EventHandle;
		DWORD Result = WaitForMultipleObjects(NUM_ENCODING_EVENTS, Handles, false, INFINITE);

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
					SubmitFrameToEncoder(*Frames[Idx]);
				}
			}
			else if (Result == WAIT_OBJECT_0 + FINISHED_ENCODING_EVENT)
			{
				// A frame has finished encoding so we can now handle the
				// encoded data.
				Frame.EncodeEndTimeStamp = FTimespan::FromSeconds(FPlatformTime::Seconds());
				ResetEvent(Frame.OutputFrame.EventHandle);
				HandleEncodedFrame(Frame);
				++OutputCount;
			}
		}
		else
		{
			break;
		}
	}
}

DECLARE_CYCLE_STAT(TEXT("FNvVideoEncoder_TransferRenderTargetToHWEncoder"), STAT_FNvVideoEncoder_TransferRenderTargetToHWEncoder, STATGROUP_VideoRecordingSystem);
DECLARE_CYCLE_STAT(TEXT("FNvVideoEncoder_TransferRenderTargetToHWEncoder_nvEncEncodePicture"), STAT_FNvVideoEncoder_TransferRenderTargetToHWEncoder_nvEncEncodePicture, STATGROUP_VideoRecordingSystem);

bool FNvVideoEncoder::SubmitFrameToEncoder(FFrame& Frame)
{
	SCOPE_CYCLE_COUNTER(STAT_FNvVideoEncoder_TransferRenderTargetToHWEncoder);
	CSV_SCOPED_TIMING_STAT(GameplayMediaEncoder, TransferRenderTargetToHwEncoder);

	NV_ENC_PIC_PARAMS PicParams;
	FMemory::Memzero(PicParams);
	PicParams.version = NV_ENC_PIC_PARAMS_VER;
	PicParams.inputBuffer = Frame.InputFrame.MappedResource;
	PicParams.bufferFmt = Frame.InputFrame.BufferFormat;
	PicParams.inputWidth = Config.Width;
	PicParams.inputHeight = Config.Height;
	PicParams.outputBitstream = Frame.OutputFrame.BitstreamBuffer;
	PicParams.completionEvent = Frame.OutputFrame.EventHandle;
	PicParams.inputTimeStamp = Frame.FrameIdx;
	PicParams.pictureStruct = NV_ENC_PIC_STRUCT_FRAME;

	Frame.EncodeStartTimeStamp = FTimespan::FromSeconds(FPlatformTime::Seconds());

	{
		SCOPE_CYCLE_COUNTER(STAT_FNvVideoEncoder_TransferRenderTargetToHWEncoder_nvEncEncodePicture);
		CSV_SCOPED_TIMING_STAT(GameplayMediaEncoder, TransferRenderTargetToHWEncoder_nvEncEncodePicture);
		FRHICommandList& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();
		SCOPED_DRAW_EVENTF(RHICmdList, NvVideoEncoder_TransferRenderTargetToHWEncoder, TEXT("NvVideoEncoder_TransferRenderTargetToHWEncoder %u"), static_cast<uint32>(Frame.FrameIdx));

		CHECK_NV_RES(NvEncodeAPI->nvEncEncodePicture(EncoderInterface, &PicParams));
	}

	return true;
}

DECLARE_CYCLE_STAT(TEXT("FNvVideoEncoder_ProcessEncodedFrame"), STAT_FNvVideoEncoder_ProcessEncodedFrame, STATGROUP_VideoRecordingSystem);
DECLARE_CYCLE_STAT(TEXT("FNvVideoEncoder_ProcessEncodedFrame_Lock"), STAT_FNvVideoEncoder_ProcessEncodedFrame_Lock, STATGROUP_VideoRecordingSystem);
DECLARE_CYCLE_STAT(TEXT("FNvVideoEncoder_ProcessEncodedFrame_Copy"), STAT_FNvVideoEncoder_ProcessEncodedFrame_Copy, STATGROUP_VideoRecordingSystem);
DECLARE_CYCLE_STAT(TEXT("FNvVideoEncoder_ProcessEncodedFrame_Unlock"), STAT_FNvVideoEncoder_ProcessEncodedFrame_Unlock, STATGROUP_VideoRecordingSystem);
DECLARE_CYCLE_STAT(TEXT("FNvVideoEncoder_ProcessEncodedFrame_Callback"), STAT_FNvVideoEncoder_ProcessEncodedFrame_Callback, STATGROUP_VideoRecordingSystem);

bool FNvVideoEncoder::HandleEncodedFrame(FNvVideoEncoder::FFrame& Frame)
{
	SCOPE_CYCLE_COUNTER(STAT_FNvVideoEncoder_ProcessEncodedFrame);

	// If the expected frame hasn't been doing encoding, then nothing to do
	checkf(Frame.bEncoding, TEXT("This should not happen"));
	if (!Frame.bEncoding)
	{
		return false;
	}

	FTimespan Now = FTimespan::FromSeconds(FPlatformTime::Seconds());
	double CaptureToEncodeStartTime = (Frame.EncodeStartTimeStamp - Frame.CaptureTimeStamp).GetTotalMilliseconds();
	double EncodeTime = (Frame.EncodeEndTimeStamp - Frame.EncodeStartTimeStamp).GetTotalMilliseconds();
	double EncodeToWriterTime = (Now - Frame.EncodeEndTimeStamp).GetTotalMilliseconds();

	SET_FLOAT_STAT(STAT_NvEnc_CaptureToEncodeStart, CaptureToEncodeStartTime);
	SET_FLOAT_STAT(STAT_NvEnc_EncodeTime, EncodeTime);
	SET_FLOAT_STAT(STAT_NvEnc_EncodeToWriterTime, EncodeToWriterTime);

	// log encoding latency for every 1000th frame
	if (Frame.FrameIdx % 1000 == 0)
	{
		UE_LOG(NvVideoEncoder, Verbose, TEXT("#%d %.2f %.2f %.2f"),
			Frame.FrameIdx,
			CaptureToEncodeStartTime,
			EncodeTime,
			EncodeToWriterTime);
	}

	bool bIdrFrame = false;

	// Retrieve encoded frame from output buffer
	{
		NV_ENC_LOCK_BITSTREAM LockBitstream = {};
		LockBitstream.version = NV_ENC_LOCK_BITSTREAM_VER;
		LockBitstream.outputBitstream = Frame.OutputFrame.BitstreamBuffer;
		LockBitstream.doNotWait = true;
		FRHICommandList& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();

		{
			SCOPE_CYCLE_COUNTER(STAT_FNvVideoEncoder_ProcessEncodedFrame_Lock);
			CSV_SCOPED_TIMING_STAT(GameplayMediaEncoder, ProcessEncodedFrame_Lock);
			SCOPED_DRAW_EVENTF(RHICmdList, NvVideoEncoder_ProcessEncodedFrame_Lock, TEXT("NvVideoEncoder_ProcessEncodedFrame_Lock %u"), static_cast<uint32>(Frame.FrameIdx));
			CHECK_NV_RES(NvEncodeAPI->nvEncLockBitstream(EncoderInterface, &LockBitstream));
		}

		{
			SCOPE_CYCLE_COUNTER(STAT_FNvVideoEncoder_ProcessEncodedFrame_Copy);
			CSV_SCOPED_TIMING_STAT(GameplayMediaEncoder, ProcessEncodedFrame_Copy);
			Frame.EncodedFrame.SetNum(LockBitstream.bitstreamSizeInBytes);
			FMemory::Memcpy(Frame.EncodedFrame.GetData(), LockBitstream.bitstreamBufferPtr, LockBitstream.bitstreamSizeInBytes);
		}

		bIdrFrame = LockBitstream.pictureType == NV_ENC_PIC_TYPE_IDR;

		{
			SCOPE_CYCLE_COUNTER(STAT_FNvVideoEncoder_ProcessEncodedFrame_Unlock);
			CSV_SCOPED_TIMING_STAT(GameplayMediaEncoder, ProcessEncodedFrame_Unlock);
			SCOPED_DRAW_EVENTF(RHICmdList, NvVideoEncoder_ProcessEncodedFrame_Unlock, TEXT("NvVideoEncoder_ProcessEncodedFrame_Unlock %u"), static_cast<uint32>(Frame.FrameIdx));
			CHECK_NV_RES(NvEncodeAPI->nvEncUnlockBitstream(EncoderInterface, Frame.OutputFrame.BitstreamBuffer));
		}
	}

	{
		SCOPE_CYCLE_COUNTER(STAT_FNvVideoEncoder_ProcessEncodedFrame_Callback);

		FGameplayMediaEncoderSample OutputSample{ EMediaType::Video };
		if (!OutputSample.CreateSample())
		{
			return false;
		}
		int32 BufferSize = Frame.EncodedFrame.Num();
		uint32 Alignment = 0;
		TRefCountPtr<IMFMediaBuffer> WmfBuffer;
		CHECK_HR(MFCreateAlignedMemoryBuffer(BufferSize, Alignment, WmfBuffer.GetInitReference()));

		CHECK_HR(OutputSample.GetSample()->SetUINT32(MFSampleExtension_CleanPoint, bIdrFrame ? 1 : 0));

		// Copy data to the WmfBuffer
		uint8* Dst = nullptr;
		CHECK_HR(WmfBuffer->Lock(&Dst, nullptr, nullptr));
		FMemory::Memcpy(Dst, Frame.EncodedFrame.GetData(), Frame.EncodedFrame.Num());
		CHECK_HR(WmfBuffer->Unlock());
		CHECK_HR(WmfBuffer->SetCurrentLength(Frame.EncodedFrame.Num()));

		CHECK_HR(OutputSample.GetSample()->AddBuffer(WmfBuffer));
		OutputSample.SetTime(Frame.TimeStamp);
		OutputSample.SetDuration(Frame.Duration);

		UE_LOG(NvVideoEncoder, Verbose, TEXT("encoded frame #%d: time %.3f, duration %.3f, size %d%s"), Frame.FrameIdx, OutputSample.GetTime().GetTotalSeconds(), OutputSample.GetDuration().GetTotalSeconds(), BufferSize, OutputSample.IsVideoKeyFrame() ? TEXT(", key frame") : TEXT(""));

		if (!OutputCallback(OutputSample))
		{
			return false;
		}
	}

	Frame.bEncoding = false;

	return true;
}

#undef CHECK_NV_RES

GAMEPLAYMEDIAENCODER_END

#endif // PLATFORM_WINDOWS

