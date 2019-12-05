// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "AmfVideoEncoder.h"
#include "Microsoft/AVEncoderMicrosoftCommon.h"
#include "CommonRenderResources.h"

THIRD_PARTY_INCLUDES_START
	#include "ThirdParty/AmdAmf/core/Result.h"
	#include "ThirdParty/AmdAmf/core/Factory.h"
	#include "ThirdParty/AmdAmf/components/VideoEncoderVCE.h"
	#include "ThirdParty/AmdAmf/core/Compute.h"
	#include "ThirdParty/AmdAmf/core/Plane.h"
THIRD_PARTY_INCLUDES_END

DECLARE_STATS_GROUP(TEXT("AmfVideoEncoder"), STATGROUP_AmfVideoEncoder, STATCAT_Advanced);
DECLARE_CYCLE_STAT(TEXT("AmfEncoder->QueryOutput"), STAT_Amf_QueryOutput, STATGROUP_AmfVideoEncoder);
DECLARE_CYCLE_STAT(TEXT("StreamEncodedFrame"), STAT_Amf_StreamEncodedFrame, STATGROUP_AmfVideoEncoder);
DECLARE_CYCLE_STAT(TEXT("OnEncodedVideoFrameCallback"), STAT_Amf_OnEncodedVideoFrameCallback, STATGROUP_AmfVideoEncoder);
DECLARE_CYCLE_STAT(TEXT("SubmitFrameToEncoder"), STAT_Amf_SubmitFrameToEncoder, STATGROUP_AmfVideoEncoder);
DECLARE_CYCLE_STAT(TEXT("AmfEncoder->SubmitInput"), STAT_Amf_SubmitInput, STATGROUP_AmfVideoEncoder);

// NOTE: This only exists in a more recent version of the AMF SDK.  Adding it here so I don't need to update the SDK  yet.
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
		UE_LOG(LogAVEncoder, Error, TEXT("`" #AMF_call "` failed with error code: %d"), Res);\
		/*check(false);*/\
		return false;\
	}\
}

#define CHECK_AMF_NORET(AMF_call)\
{\
	AMF_RESULT Res = AMF_call;\
	if (Res != AMF_OK)\
	{\
		UE_LOG(LogAVEncoder, Error, TEXT("`" #AMF_call "` failed with error code: %d"), Res);\
	}\
}

namespace AVEncoder
{

namespace {
	// enumerates all available properties of AMFPropertyStorage interface and logs their name,
	// current and default values and other info
	inline bool LogAmfPropertyStorage(amf::AMFPropertyStorageEx* PropertyStorage)
	{
		SIZE_T NumProps = PropertyStorage->GetPropertiesInfoCount();
		for (int i = 0; i != NumProps; ++i)
		{
			const amf::AMFPropertyInfo* Info;
			CHECK_AMF_RET(PropertyStorage->GetPropertyInfo(i, &Info));

			if (Info->accessType != amf::AMF_PROPERTY_ACCESS_PRIVATE)
			{
				amf::AMFVariant Value;
				CHECK_AMF_RET(PropertyStorage->GetProperty(Info->name, &Value));

				FString EnumDesc;
				if (Info->pEnumDescription)
				{
					int j = 0;
					for (; /*j != Value.ToInt32()*/; ++j)
					{
						if (Info->pEnumDescription[j].value == Value.ToInt32())
						{
							break;
						}
					}
					EnumDesc = TEXT(" ") + FString(Info->pEnumDescription[j].name);
				}

				UE_LOG(LogAVEncoder, Log, TEXT("Prop %s (%s): value: %s%s, default value: %s (%s - %s), access: %d"),
					Info->name,
					Info->desc,
					Value.ToWString().c_str(),
					*EnumDesc,
					amf::AMFVariant{ Info->defaultValue }.ToWString().c_str(),
					amf::AMFVariant{ Info->minValue }.ToWString().c_str(),
					amf::AMFVariant{ Info->maxValue }.ToWString().c_str(),
					static_cast<int>(Info->accessType));
			}
			else
			{
				UE_LOG(LogAVEncoder, VeryVerbose, TEXT("Prop: %s (%s) - PRIVATE"), Info->name, Info->desc);
			}
		}

		return true;
	}


	const TCHAR* ToString(AMF_VIDEO_ENCODER_OUTPUT_DATA_TYPE_ENUM PicType)
	{
		switch (PicType)
		{
		case AMF_VIDEO_ENCODER_OUTPUT_DATA_TYPE_IDR:
			return TEXT("AMF_VIDEO_ENCODER_OUTPUT_DATA_TYPE_IDR");
		case AMF_VIDEO_ENCODER_OUTPUT_DATA_TYPE_I:
			return TEXT("AMF_VIDEO_ENCODER_OUTPUT_DATA_TYPE_I");
		case AMF_VIDEO_ENCODER_OUTPUT_DATA_TYPE_P:
			return TEXT("AMF_VIDEO_ENCODER_OUTPUT_DATA_TYPE_P");
		case AMF_VIDEO_ENCODER_OUTPUT_DATA_TYPE_B:
			return TEXT("AMF_VIDEO_ENCODER_OUTPUT_DATA_TYPE_B");
		default:
			checkNoEntry();
			return TEXT("Unknown");
		}
	}

	AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_ENUM ToAmfRcMode(FH264Settings::ERateControlMode RcMode)
	{
		switch(RcMode)
		{
		case FH264Settings::ERateControlMode::ConstQP:
			return AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_CONSTANT_QP;
		case FH264Settings::ERateControlMode::VBR:
			return AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_LATENCY_CONSTRAINED_VBR;
		case FH264Settings::ERateControlMode::CBR:
			return AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_CBR;
		default:
			UE_LOG(LogAVEncoder, Error, TEXT("Invalid rate control mode (%d) for nvenc"), (int)RcMode);
			return AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_CBR;
		}
	};

} // anonymous namespace

class FAmfVideoEncoder : public FVideoEncoder
{
public:
	FAmfVideoEncoder();
	~FAmfVideoEncoder();

private:

	//
	// FVideoEncoder interface
	//
	const TCHAR* GetName() const override;
	const TCHAR* GetType() const override;
	bool Initialize(const FVideoEncoderConfig& InConfig) override;
	void Shutdown() override;
	bool CopyTexture(FTexture2DRHIRef Texture, FTimespan CaptureTs, FTimespan Duration, FBufferId& OutBufferId, FIntPoint Resolution = {0, 0}) override;
	void Drop(FBufferId BufferId) override;
	void Encode(FBufferId BufferId, bool bForceKeyFrame, uint32 Bitrate = 0, TUniquePtr<AVEncoder::FEncoderVideoFrameCookie> Cookie = nullptr) override;
	FVideoEncoderConfig GetConfig() const override;
	bool SetBitrate(uint32 Bitrate) override;
	bool SetFramerate(uint32 Framerate) override;
	bool SetParameter(const FString& Parameter, const FString& Value) override;

	struct FInputFrame
	{
		FInputFrame() {}
		UE_NONCOPYABLE(FInputFrame);
		FTexture2DRHIRef Texture;
		FTimespan CaptureTs;
		FTimespan Duration;
		bool bForceKeyFrame = false;
	};

	struct FOutputFrame
	{
		FOutputFrame() {}
		UE_NONCOPYABLE(FOutputFrame);
		amf::AMFDataPtr EncodedData;
		TUniquePtr<FEncoderVideoFrameCookie> Cookie;
	};

	enum class EFrameState
	{
		Free,
		Capturing,
		Captured,
		EncoderFailed,
		Encoding
	};

	struct FFrame
	{
		const FBufferId Id = 0;
		TAtomic<EFrameState> State = { EFrameState::Free };
		FInputFrame InputFrame;
		FOutputFrame OutputFrame;
		uint64 FrameIdx = 0;

		// Some timestamps to track how long a frame spends in each step
		FTimespan CopyBufferStartTs;
		FTimespan CopyBufferFinishTs;
		FTimespan EncodingStartTs;
		FTimespan EncodingFinishTs;
	};

	bool bInitialized = false;
	amf_handle DllHandle = nullptr;
	amf::AMFFactory* AmfFactory = nullptr;
	amf::AMFContextPtr AmfContext;
	amf::AMFComponentPtr AmfEncoder;
	FVideoEncoderConfig Config;
	FH264Settings ConfigH264;

	uint32 CapturedFrameCount = 0; // of captured, not encoded frames
	static const uint32 NumBufferedFrames = 3;
	FFrame BufferedFrames[NumBufferedFrames];

	TQueue<FFrame*> EncodingQueue;

	// Used to make sure we don't have a race condition trying to access a deleted "this" captured
	// in the render command lambda sent to the render thread from EncoderCheckLoop
	static FThreadSafeCounter ImplCounter;

	FRHICOMMAND_MACRO(FRHISubmitFrameToEncoder)
	{
		FAmfVideoEncoder* Encoder;
		FFrame* Frame;
		FRHISubmitFrameToEncoder(FAmfVideoEncoder* InEncoder, FFrame* InFrame)
			: Encoder(InEncoder), Frame(InFrame)
		{
		}

		void Execute(FRHICommandListBase& CmdList)
		{
			Encoder->SubmitFrameToEncoder(*Frame);
		}
	};

	void ResetFrameInputBuffer(FFrame& Frame, FIntPoint Resolution);
	bool ProcessOutput();
	void UpdateRes(FFrame& Frame, FIntPoint Resolution);
	void EncodeFrameInRenderingThread(FFrame& Frame, uint32 Bitrate);
	bool SubmitFrameToEncoder(FFrame& Frame);
	void UpdateEncoderConfig(FIntPoint Resolution, uint32 Bitrate);
	void HandleDroppedFrame(FFrame& Frame);
	void HandleEncodedFrame(FFrame& Frame);
};

//////////////////////////////////////////////////////////////////////////
// FAmfVideoEncoder
//////////////////////////////////////////////////////////////////////////

FThreadSafeCounter FAmfVideoEncoder::ImplCounter(0);

FAmfVideoEncoder::FAmfVideoEncoder()
{
}

FAmfVideoEncoder::~FAmfVideoEncoder()
{
	if (DllHandle)
	{
		UE_LOG(LogAVEncoder, Fatal, TEXT("FAmfVideoEncoder Shutdown not called before destruction."));
	}
}

const TCHAR* FAmfVideoEncoder::GetName() const
{
	return TEXT("h264.amf");
}

const TCHAR* FAmfVideoEncoder::GetType() const
{
	return TEXT("h264");
}

bool FAmfVideoEncoder::Initialize(const FVideoEncoderConfig& InConfig)
{
	check(!bInitialized);

	Config = InConfig;
	ConfigH264 = {};
	ReadH264Settings(Config.Options, ConfigH264);

	UE_LOG(LogAVEncoder, Log, TEXT("FPixelStreamingAmfVideoEncoder initialization with %dx%d, %d FPS")
		, Config.Width, Config.Height, Config.Framerate);

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
	if (RHIName != TEXT("D3D11"))
	{
		UE_LOG(LogAVEncoder, Fatal, TEXT("AMF not supported with a %s renderer"), *RHIName);
	}

	ID3D11Device* DxDevice = static_cast<ID3D11Device*>(GDynamicRHI->RHIGetNativeDevice());

	CHECK_AMF_RET(AmfFactory->CreateContext(&AmfContext));

	checkf(DxDevice != nullptr, TEXT("Cannot initialize NvEnc with invalid device"));
	CHECK_AMF_RET(AmfContext->InitDX11(DxDevice));

	CHECK_AMF_RET(AmfFactory->CreateComponent(AmfContext, AMFVideoEncoderVCE_AVC, &AmfEncoder));

	if (Config.Preset == FVideoEncoderConfig::EPreset::LowLatency)
	{
		CHECK_AMF_RET(AmfEncoder->SetProperty(AMF_VIDEO_ENCODER_USAGE, AMF_VIDEO_ENCODER_USAGE_ULTRA_LOW_LATENCY));
		CHECK_AMF_RET(AmfEncoder->SetProperty(AMF_VIDEO_ENCODER_LOWLATENCY_MODE, true));
		CHECK_AMF_RET(AmfEncoder->SetProperty(AMF_VIDEO_ENCODER_PROFILE, AMF_VIDEO_ENCODER_PROFILE_BASELINE));
		CHECK_AMF_RET(AmfEncoder->SetProperty(AMF_VIDEO_ENCODER_QUALITY_PRESET, AMF_VIDEO_ENCODER_QUALITY_PRESET_BALANCED));
	}
	else
	{
		CHECK_AMF_RET(AmfEncoder->SetProperty(AMF_VIDEO_ENCODER_USAGE, AMF_VIDEO_ENCODER_USAGE_TRANSCONDING));
		CHECK_AMF_RET(AmfEncoder->SetProperty(AMF_VIDEO_ENCODER_PROFILE, AMF_VIDEO_ENCODER_PROFILE_MAIN));
		CHECK_AMF_RET(AmfEncoder->SetProperty(AMF_VIDEO_ENCODER_QUALITY_PRESET, AMF_VIDEO_ENCODER_QUALITY_PRESET_QUALITY));
	}

	//CHECK_AMF_RET(AmfEncoder->SetProperty(AMF_VIDEO_ENCODER_B_PIC_PATTERN, 0));
	CHECK_AMF_RET(AmfEncoder->SetProperty(AMF_VIDEO_ENCODER_TARGET_BITRATE, Config.Bitrate));
	CHECK_AMF_RET(AmfEncoder->SetProperty(AMF_VIDEO_ENCODER_FRAMESIZE, ::AMFConstructSize(Config.Width, Config.Height)));
	CHECK_AMF_RET(AmfEncoder->SetProperty(AMF_VIDEO_ENCODER_ASPECT_RATIO, ::AMFConstructRatio(Config.Width, Config.Height)));
	CHECK_AMF_RET(AmfEncoder->SetProperty(AMF_VIDEO_ENCODER_FRAMERATE, ::AMFConstructRate(Config.Framerate, 1)));

	// generate key-frames every second, useful for seeking in resulting .mp4 and keeping recording ring buffer
	// of second-precise duration
	uint64 IdrPeriod = Config.Framerate;
	CHECK_AMF_RET(AmfEncoder->SetProperty(AMF_VIDEO_ENCODER_IDR_PERIOD, IdrPeriod));
	// insert SPS/PPS before every key-frame. .mp4 file video stream must start from SPS/PPS. Their size is
	// negligible so having them before every key-frame is not an issue but they presence simplifies
	// implementation significantly. Otherwise we would extract SPS/PPS from the first key-frame and store
	// them manually at the beginning of resulting .mp4 file
	CHECK_AMF_RET(AmfEncoder->SetProperty(AMF_VIDEO_ENCODER_HEADER_INSERTION_SPACING, IdrPeriod));

	CHECK_AMF_RET(AmfEncoder->Init(amf::AMF_SURFACE_RGBA, Config.Width, Config.Height));

	// #AVENCODER : This doesn't seem to be working. It fails with code 3 (AMF_ACCESS_DENIED) or gives a warning that has bee initialized already,
	// depending if it's called before the Init (gives error 3), or after the Init (giveswarning).
	CHECK_AMF_RET(AmfEncoder->SetProperty(AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD, ToAmfRcMode(ConfigH264.RcMode)));

	CHECK_AMF_RET(AmfEncoder->SetProperty(AMF_VIDEO_ENCODER_MIN_QP, ConfigH264.QP));
	CHECK_AMF_RET(AmfEncoder->SetProperty(AMF_VIDEO_ENCODER_QP_I, ConfigH264.QP));
	CHECK_AMF_RET(AmfEncoder->SetProperty(AMF_VIDEO_ENCODER_QP_P, ConfigH264.QP));
	// #AVENCODER : This doesn't seem to be working. It fails with code 3 (AMF_ACCESS_DENIED) or gives a warning that has bee initialized already,
	// depending if it's called before the Init (gives error 3), or after the Init (giveswarning).
	CHECK_AMF_RET(AmfEncoder->SetProperty(AMF_VIDEO_ENCODER_QP_B, ConfigH264.QP));

	//LogAmfPropertyStorage(AmfEncoder);

	FBufferId Id = 0;
	for (FFrame& Frame : BufferedFrames)
	{
		// We keep `Id` as const, because it not supposed to change at all once initialized
		*(const_cast<FBufferId*>(&Frame.Id)) = Id++;
		ResetFrameInputBuffer(Frame, { (int)Config.Width, (int)Config.Height });
	}
	UE_LOG(LogAVEncoder, Log, TEXT("AMF H.264 encoder initialised, v.0x%X"), AmfVersion);
	bInitialized = true;
	return true;
}

void FAmfVideoEncoder::Shutdown()
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

bool FAmfVideoEncoder::CopyTexture(FTexture2DRHIRef Texture, FTimespan CaptureTs, FTimespan Duration, FBufferId& OutBufferId, FIntPoint Resolution)
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
		if (Slot.State.Load() == EFrameState::Free)
		{
			Frame = &Slot;
			OutBufferId = Slot.Id;
			break;
		}
	}

	if (!Frame)
	{
		UE_LOG(LogAVEncoder, Verbose, TEXT("Frame dropped because Amf queue is full"));
		return false;
	}

	Frame->FrameIdx = CapturedFrameCount++;
	Frame->InputFrame.CaptureTs = CaptureTs;
	Frame->InputFrame.Duration = Duration;
	Frame->CopyBufferStartTs = FTimespan::FromSeconds(FPlatformTime::Seconds());

	{
		UpdateRes(*Frame, Resolution.Size() ? Resolution : Texture->GetSizeXY());
		CopyTextureImpl(Texture, Frame->InputFrame.Texture, nullptr);
	}

	UE_LOG(LogAVEncoder, Verbose, TEXT("Buffer #%d (%d) captured"), Frame->FrameIdx, OutBufferId);
	Frame->State = EFrameState::Capturing;

	return true;
}

void FAmfVideoEncoder::Drop(FBufferId BufferId)
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


void FAmfVideoEncoder::Encode(FBufferId BufferId, bool bForceKeyFrame, uint32 Bitrate, TUniquePtr<AVEncoder::FEncoderVideoFrameCookie> Cookie)
{
	FFrame& Frame = BufferedFrames[BufferId];

	{
		EFrameState State = Frame.State.Load();
		checkf(State == EFrameState::Capturing, TEXT("Buffer %d : Expected state %d, but found %d"), BufferId, (int)EFrameState::Captured, (int)State);
	}

	Frame.State = EFrameState::Captured;
	Frame.CopyBufferFinishTs = FTimespan::FromSeconds(FPlatformTime::Seconds());
	Frame.InputFrame.bForceKeyFrame = bForceKeyFrame;
	Frame.OutputFrame.Cookie = MoveTemp(Cookie);

	int32 CurrImplCounter = ImplCounter.GetValue();
	ENQUEUE_RENDER_COMMAND(AmfEncEncodeFrame)(
	[this, &Frame, BufferId, Bitrate, CurrImplCounter](FRHICommandListImmediate& RHICmdList)
	{
		if (CurrImplCounter != ImplCounter.GetValue()) // Check if the "this" we captured is still valid
			return;

		EncodeFrameInRenderingThread(Frame, Bitrate);

		UE_LOG(LogAVEncoder, VeryVerbose, TEXT("Buffer #%d (%d), ts %lld started encoding"), Frame.FrameIdx, BufferId, Frame.InputFrame.CaptureTs.GetTicks());
	});
}

FVideoEncoderConfig FAmfVideoEncoder::GetConfig() const
{
	return Config;
}
bool FAmfVideoEncoder::SetBitrate(uint32 Bitrate)
{
	Config.Bitrate = Bitrate;
	return true;
}

bool FAmfVideoEncoder::SetFramerate(uint32 Framerate)
{
	Config.Framerate = Framerate;
	return true;
}

bool FAmfVideoEncoder::SetParameter(const FString& Parameter, const FString& Value)
{
	return ReadH264Setting(Parameter, Value, ConfigH264);
}

void FAmfVideoEncoder::ResetFrameInputBuffer(FFrame& Frame, FIntPoint Resolution)
{
	Frame.InputFrame.Texture.SafeRelease();

	// Make sure format used here is compatible with AMF_SURFACE_FORMAT specified in encoder Init() function.
	FRHIResourceCreateInfo CreateInfo;
	Frame.InputFrame.Texture = RHICreateTexture2D(Resolution.X, Resolution.Y, EPixelFormat::PF_R8G8B8A8, 1, 1, TexCreate_RenderTargetable, CreateInfo);
}

bool FAmfVideoEncoder::ProcessOutput()
{
	check(IsInRenderingThread());

	while(true)
	{

		// Drop any failed submits
		// If for some reason this frame failed to be submitted to AMF (e.g: call to AmfEncoder->SubmitInput failed due to AMF_INPUT_FULL), then we need to tell webrtc to drop it
		while(!EncodingQueue.IsEmpty() && (*EncodingQueue.Peek())->State == EFrameState::EncoderFailed)
		{
			FFrame* Frame;
			EncodingQueue.Dequeue(Frame);
			HandleDroppedFrame(*Frame);
		}

		amf::AMFDataPtr EncodedData;
		AMF_RESULT Ret;
		{
			SCOPE_CYCLE_COUNTER(STAT_Amf_QueryOutput);
			Ret = AmfEncoder->QueryOutput(&EncodedData);
		}

		// If AMF says a frame finished encoding, then we must have one in EncodingQueue by design
		if (Ret == AMF_OK && EncodedData != nullptr)
		{
			FFrame* Frame;
			verify(EncodingQueue.Dequeue(Frame));
			Frame->OutputFrame.EncodedData = EncodedData;
			HandleEncodedFrame(*Frame);
		}
		else if (Ret == AMF_REPEAT)
		{
			break; // not ready yet
		}
		else
		{
			UE_LOG(LogAVEncoder, Error, TEXT("Failed to query AMF H.264 Encoder output: %d, %p"), Ret, EncodedData.GetPtr());
			return false;
		}
	}

	return true;
}


void FAmfVideoEncoder::HandleDroppedFrame(FFrame& Frame)
{
	check(Frame.State==EFrameState::EncoderFailed);

	FOutputFrame& OutputFrame = Frame.OutputFrame;

	FAVPacket Packet(EPacketType::Video);
	Frame.EncodingFinishTs = FTimespan::FromSeconds(FPlatformTime::Seconds());
	Packet.Timestamp = Frame.InputFrame.CaptureTs;
	Packet.Duration = Frame.InputFrame.Duration;
	Packet.Timings.EncodeStartTs = Frame.EncodingStartTs;
	Packet.Timings.EncodeFinishTs = Frame.EncodingFinishTs;

	UE_LOG(LogAVEncoder, VeryVerbose, TEXT("dropping frame with ts %lld due to encoder failure") , Packet.Timestamp.GetTicks());

	{
		SCOPE_CYCLE_COUNTER(STAT_Amf_OnEncodedVideoFrameCallback);
		OnEncodedVideoFrame(Packet, MoveTemp(Frame.OutputFrame.Cookie));
	}

	Frame.State = EFrameState::Free;
}

void FAmfVideoEncoder::HandleEncodedFrame(FFrame& Frame)
{
	check(Frame.State==EFrameState::Encoding);

	FOutputFrame& OutputFrame = Frame.OutputFrame;

	FAVPacket Packet(EPacketType::Video);

	//
	// Query for buffer interface
	//
	amf::AMFBufferPtr EncodedBuffer(Frame.OutputFrame.EncodedData);
	void* EncodedBufferPtr = EncodedBuffer->GetNative();
	size_t EncodedBufferSize = EncodedBuffer->GetSize();

	//
	// Check if it's a keyframe
	//
	amf_int64 PicType;
	CHECK_AMF_NORET(EncodedBuffer->GetProperty(AMF_VIDEO_ENCODER_OUTPUT_DATA_TYPE, &PicType));
	bool bKeyFrame = PicType == AMF_VIDEO_ENCODER_OUTPUT_DATA_TYPE_IDR ? true : false;
	checkf(bKeyFrame == true || Frame.InputFrame.bForceKeyFrame == false, TEXT("key frame requested by webrtc but not provided by Amf"));

	Frame.EncodingFinishTs = FTimespan::FromSeconds(FPlatformTime::Seconds());
	Packet.Timestamp = Frame.InputFrame.CaptureTs;
	Packet.Duration = Frame.InputFrame.Duration;
	Packet.Video.bKeyFrame = bKeyFrame;
	Packet.Video.Width = Frame.InputFrame.Texture->GetSizeX();
	Packet.Video.Height = Frame.InputFrame.Texture->GetSizeY();
	Packet.Video.Framerate = Config.Framerate;
	// #AVENCODER : How to get frame average QP from Amf ?
	Packet.Video.FrameAvgQP = 20;
	Packet.Data = TArray<uint8>(reinterpret_cast<const uint8*>(EncodedBufferPtr), EncodedBufferSize);
	Packet.Timings.EncodeStartTs = Frame.EncodingStartTs;
	Packet.Timings.EncodeFinishTs = Frame.EncodingFinishTs;

	// Release AMF's buffer
	Frame.OutputFrame.EncodedData = nullptr;

	UE_LOG(LogAVEncoder, VeryVerbose, TEXT("encoded %s ts %lld, %d bytes")
		, ToString((AMF_VIDEO_ENCODER_OUTPUT_DATA_TYPE_ENUM)PicType)
		, Packet.Timestamp.GetTicks()
		, (int)Packet.Data.Num());

	{
		SCOPE_CYCLE_COUNTER(STAT_Amf_OnEncodedVideoFrameCallback);
		OnEncodedVideoFrame(Packet, MoveTemp(Frame.OutputFrame.Cookie));
	}

	Frame.State = EFrameState::Free;
}


void FAmfVideoEncoder::UpdateRes(FFrame& Frame, FIntPoint Resolution)
{
	check(IsInRenderingThread());

	FInputFrame& InputFrame = Frame.InputFrame;

	// check if target resolution matches our currently allocated `InputFrame.Texture` resolution
	if (InputFrame.Texture->GetSizeX() == Resolution.X && InputFrame.Texture->GetSizeY() == Resolution.Y)
	{
		return;
	}

	ResetFrameInputBuffer(Frame, Resolution);
}

void FAmfVideoEncoder::EncodeFrameInRenderingThread(FFrame& Frame, uint32 Bitrate)
{
	check(IsInRenderingThread());
	check(Frame.State==EFrameState::Captured);

	UpdateEncoderConfig(Frame.InputFrame.Texture->GetSizeXY(), Bitrate);

	//
	// Process the new input
	{
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

bool FAmfVideoEncoder::SubmitFrameToEncoder(FFrame& Frame)
{
	SCOPE_CYCLE_COUNTER(STAT_Amf_SubmitFrameToEncoder);
	check(Frame.State==EFrameState::Captured);

	Frame.EncodingStartTs = FTimespan::FromSeconds(FPlatformTime::Seconds());

	amf::AMFSurfacePtr AmfSurfaceIn;
	ID3D11Texture2D* ResolvedBackBufferDX11 = static_cast<ID3D11Texture2D*>(GetD3D11TextureFromRHITexture(Frame.InputFrame.Texture)->GetResource());
	CHECK_AMF_RET(AmfContext->CreateSurfaceFromDX11Native(ResolvedBackBufferDX11, &AmfSurfaceIn, nullptr));

	if (Frame.InputFrame.bForceKeyFrame)
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
			//CHECK_AMF_RET(AmfEncoder->SubmitInput(AmfSurfaceIn));
			AMF_RESULT Res =  AmfEncoder->SubmitInput(AmfSurfaceIn);
			if (Res == AMF_OK)
			{
				Frame.State = EFrameState::Encoding;
			}
			else
			{
				UE_LOG(LogAVEncoder, Error, TEXT("AmfEncoder->SubmitInput failed failed with error code: %d"), Res);
				Frame.State = EFrameState::EncoderFailed;
			}
			EncodingQueue.Enqueue(&Frame);
		}
	}

	return true;
}


void FAmfVideoEncoder::UpdateEncoderConfig(FIntPoint Resolution, uint32 Bitrate)
{
	check(IsInRenderingThread());

	//
	// If an explicit Bitrate was specified, use that one, if not, use the one
	// from the Config struct
	//
	uint32 AmfBitrate = 0;
	CHECK_AMF_NORET(AmfEncoder->GetProperty(AMF_VIDEO_ENCODER_TARGET_BITRATE, &AmfBitrate));
	if (Bitrate)
	{
		if (AmfBitrate != Bitrate)
		{
			UE_LOG(LogAVEncoder, Verbose, TEXT("Setting AMF's bitrate to %u"), Bitrate);
			CHECK_AMF_NORET(AmfEncoder->SetProperty(AMF_VIDEO_ENCODER_TARGET_BITRATE, Bitrate));
			Config.Bitrate = Bitrate;
		}
	}
	else if (AmfBitrate != Config.Bitrate)
	{
		UE_LOG(LogAVEncoder, Verbose, TEXT("Setting AMF's bitrate to %u"), Config.Bitrate);
		CHECK_AMF_NORET(AmfEncoder->SetProperty(AMF_VIDEO_ENCODER_TARGET_BITRATE, Config.Bitrate));
	}

	//
	// Update QP if required
	//
	uint32 AmfQP = 0;
	CHECK_AMF_NORET(AmfEncoder->GetProperty(AMF_VIDEO_ENCODER_MIN_QP, &AmfQP));
	if (AmfQP != ConfigH264.QP)
	{
		UE_LOG(LogAVEncoder, Verbose, TEXT("Setting AMF's MIN_QP/QP_I/QP_P/QP_B to %u"), ConfigH264.QP);
		CHECK_AMF_NORET(AmfEncoder->SetProperty(AMF_VIDEO_ENCODER_MIN_QP, ConfigH264.QP));
		CHECK_AMF_NORET(AmfEncoder->SetProperty(AMF_VIDEO_ENCODER_QP_I  , ConfigH264.QP));
		CHECK_AMF_NORET(AmfEncoder->SetProperty(AMF_VIDEO_ENCODER_QP_P  , ConfigH264.QP));
		CHECK_AMF_NORET(AmfEncoder->SetProperty(AMF_VIDEO_ENCODER_QP_B  , ConfigH264.QP));
	}

	//
	// Update resolution if required
	//
	if (Resolution.Size())
	{
		AMFSize AmfResolution;
		CHECK_AMF_NORET(AmfEncoder->GetProperty(AMF_VIDEO_ENCODER_FRAMESIZE, &AmfResolution));
		if (Resolution != FIntPoint(AmfResolution.width, AmfResolution.height))
		{
			UE_LOG(LogAVEncoder, Verbose, TEXT("Setting AMF's Resolution to %dx%d"), Resolution.X, Resolution.Y);
			CHECK_AMF_NORET(AmfEncoder->SetProperty(AMF_VIDEO_ENCODER_FRAMESIZE, ::AMFConstructSize(Resolution.X, Resolution.Y)));
			CHECK_AMF_NORET(AmfEncoder->SetProperty(AMF_VIDEO_ENCODER_ASPECT_RATIO, ::AMFConstructRatio(Resolution.X, Resolution.Y)));
		}
	}

	//
	// Update framerate if necessary
	// 
	AMFRate AmfFramerate;
	CHECK_AMF_NORET(AmfEncoder->GetProperty(AMF_VIDEO_ENCODER_FRAMERATE, &AmfFramerate));
	if (AmfFramerate.num != Config.Framerate)
	{
		UE_LOG(LogAVEncoder, Verbose, TEXT("Setting AMF's framerate to %d"), Config.Framerate);
		CHECK_AMF_NORET(AmfEncoder->SetProperty(AMF_VIDEO_ENCODER_FRAMERATE, ::AMFConstructRate(Config.Framerate, 1)));
		CHECK_AMF_NORET(AmfEncoder->SetProperty(AMF_VIDEO_ENCODER_IDR_PERIOD, Config.Framerate));
		CHECK_AMF_NORET(AmfEncoder->SetProperty(AMF_VIDEO_ENCODER_HEADER_INSERTION_SPACING, Config.Framerate));
	}

	//
	// Implement Rate Control Mode
	//
	// #AVENCODER : This is not working. GetProperty AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD fails with error 3 (AMF_ACCESS_DENIED)
#if 0
	amf_int64 AmfRcMode = AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_CBR;
	amf_int64 CfgRcMode = ToAmfRcMode(ConfigH264.RcMode);
	CHECK_AMF_NORET(AmfEncoder->GetProperty(AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD, &AmfRcMode));
	if (AmfRcMode != CfgRcMode)
	{
		UE_LOG(LogAVEncoder, Verbose, TEXT("Setting AMF's control rate method to %d"), int(CfgRcMode));
		CHECK_AMF_NORET(AmfEncoder->SetProperty(AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD, CfgRcMode));
	}
#endif
}

//////////////////////////////////////////////////////////////////////////
// FAmfVideoEncoderFactory
//////////////////////////////////////////////////////////////////////////

FAmfVideoEncoderFactory::FAmfVideoEncoderFactory()
{
}

FAmfVideoEncoderFactory::~FAmfVideoEncoderFactory()
{
}

const TCHAR* FAmfVideoEncoderFactory::GetName() const
{
	return TEXT("amf");
}

TArray<FString> FAmfVideoEncoderFactory::GetSupportedCodecs() const
{
	TArray<FString> Codecs;

	if (!IsRHIDeviceAMD())
	{
		UE_LOG(LogAVEncoder, Log, TEXT("No AMF available because no AMD card found"));
		return Codecs;
	}

	void* Handle = FPlatformProcess::GetDllHandle(AMF_DLL_NAME);
	if (Handle == nullptr)
	{
		UE_LOG(LogAVEncoder, Error, TEXT("AMD card found, but no AMF DLL installed."));
		return Codecs;
	}
	else
	{
		FPlatformProcess::FreeDllHandle(Handle);
	}

	Codecs.Add(TEXT("h264"));
	return Codecs;
}

TUniquePtr<FVideoEncoder> FAmfVideoEncoderFactory::CreateEncoder(const FString& Codec)
{
	if (Codec=="h264")
	{
		return TUniquePtr<FVideoEncoder>(new FAmfVideoEncoder());
	}
	else
	{
		UE_LOG(LogAVEncoder, Error, TEXT("FAmfVideoEncoderFactory doesn't support the %s codec"), *Codec);
		return nullptr;
	}
}

} // namespace AVEncoder

