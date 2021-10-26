// Copyright Epic Games, Inc. All Rights Reserved.

#include "NVENC_EncoderH264.h"
#include "HAL/Platform.h"
#include "VideoEncoderCommon.h"
#include "CodecPacket.h"
#include "AVEncoderDebug.h"
#include "RHI.h"
#include "HAL/Event.h"
#include "HAL/PlatformTime.h"
#include "Misc/Timespan.h"
#include "HAL/PlatformProcess.h"
#include "CudaModule.h"
#include "Misc/ScopedEvent.h"
#include "Async/Async.h"
#include <stdio.h>

#define MAX_GPU_INDEXES 50
#define DEFAULT_BITRATE 1000000u
#define MAX_FRAMERATE_DIFF 0
#define MIN_UPDATE_FRAMERATE_SECS 5

namespace
{
	NV_ENC_PARAMS_RC_MODE ConvertRateControlModeNVENC(AVEncoder::FVideoEncoder::RateControlMode mode)
	{
		switch(mode)
		{
		case AVEncoder::FVideoEncoder::RateControlMode::CONSTQP:
			return NV_ENC_PARAMS_RC_CONSTQP;
		case AVEncoder::FVideoEncoder::RateControlMode::VBR:
			return NV_ENC_PARAMS_RC_VBR;
		default:
		case AVEncoder::FVideoEncoder::RateControlMode::CBR:
			return NV_ENC_PARAMS_RC_CBR;
		}
	}

	NV_ENC_MULTI_PASS ConvertMultipassModeNVENC(AVEncoder::FVideoEncoder::MultipassMode mode)
	{
		switch(mode)
		{
		case AVEncoder::FVideoEncoder::MultipassMode::DISABLED:
			return NV_ENC_MULTI_PASS_DISABLED;
		case AVEncoder::FVideoEncoder::MultipassMode::QUARTER:
			return NV_ENC_TWO_PASS_QUARTER_RESOLUTION;
		default:
		case AVEncoder::FVideoEncoder::MultipassMode::FULL:
			return NV_ENC_TWO_PASS_FULL_RESOLUTION;
		}
	}

	GUID ConvertH264Profile(AVEncoder::FVideoEncoder::H264Profile profile)
	{
		switch(profile)
		{
		default:
		case AVEncoder::FVideoEncoder::H264Profile::AUTO:
			return NV_ENC_CODEC_PROFILE_AUTOSELECT_GUID;
		case AVEncoder::FVideoEncoder::H264Profile::BASELINE:
			return NV_ENC_H264_PROFILE_BASELINE_GUID;
		case AVEncoder::FVideoEncoder::H264Profile::MAIN:
			return NV_ENC_H264_PROFILE_MAIN_GUID;
		case AVEncoder::FVideoEncoder::H264Profile::HIGH:
			return NV_ENC_H264_PROFILE_HIGH_GUID;
		case AVEncoder::FVideoEncoder::H264Profile::HIGH444:
			return NV_ENC_H264_PROFILE_HIGH_444_GUID;
		case AVEncoder::FVideoEncoder::H264Profile::STEREO:
			return NV_ENC_H264_PROFILE_STEREO_GUID;
		case AVEncoder::FVideoEncoder::H264Profile::SVC_TEMPORAL_SCALABILITY:
			return NV_ENC_H264_PROFILE_SVC_TEMPORAL_SCALABILTY;
		case AVEncoder::FVideoEncoder::H264Profile::PROGRESSIVE_HIGH:
			return NV_ENC_H264_PROFILE_PROGRESSIVE_HIGH_GUID;
		case AVEncoder::FVideoEncoder::H264Profile::CONSTRAINED_HIGH:
			return NV_ENC_H264_PROFILE_CONSTRAINED_HIGH_GUID;
		}
	}
} // namespace

namespace AVEncoder
{
	// Console variables for NVENC

	TAutoConsoleVariable<int32>  CVarIntraRefreshPeriodFrames(
	TEXT("NVENC.IntraRefreshPeriodFrames"),
	0,
	TEXT("The total number of frames between each intra refresh. Smallers values will cause intra refresh more often. Default: 0. Values <= 0 will disable intra refresh."),
	ECVF_Default);

	TAutoConsoleVariable<int32>  CVarIntraRefreshCountFrames(
	TEXT("NVENC.IntraRefreshCountFrames"),
	0,
	TEXT("The total number of frames within the intra refresh period that should be used as 'intra refresh' frames. Smaller values make stream recovery quicker at the cost of more bandwidth usage. Default: 0."),
	ECVF_Default);

	TAutoConsoleVariable<bool>  CVarKeyframeQPUseLastQP(
	TEXT("NVENC.KeyframeQPUseLastQP"),
	true,
	TEXT("If true QP of keyframes is no worse than the last frame transmitted (may cost latency), if false, it may be keyframe QP may be worse if network conditions require it (lower latency/worse quality). Default: true."),
	ECVF_Default);

	TAutoConsoleVariable<int32>  CVarKeyframeInterval(
	TEXT("NVENC.KeyframeInterval"),
	300,
	TEXT("Every N frames an IDR frame is sent. Default: 300. Note: A value <= 0 will disable sending of IDR frames on an interval."),
	ECVF_Default);

	template<typename T>
	void NVENCCommandLineParseValue(const TCHAR* Match, TAutoConsoleVariable<T>& CVar)
	{
		T Value;
		if (FParse::Value(FCommandLine::Get(), Match, Value))
		{
			CVar->Set(Value, ECVF_SetByCommandline);
		}
	};

	void NVENCCommandLineParseOption(const TCHAR* Match, TAutoConsoleVariable<bool>& CVar)
	{
		FString ValueMatch(Match);
		ValueMatch.Append(TEXT("="));
		FString Value;
		if (FParse::Value(FCommandLine::Get(), *ValueMatch, Value)) {
			if (Value.Equals(FString(TEXT("true")), ESearchCase::IgnoreCase)) {
				CVar->Set(true, ECVF_SetByCommandline);
			}
			else if (Value.Equals(FString(TEXT("false")), ESearchCase::IgnoreCase)) {
				CVar->Set(false, ECVF_SetByCommandline);
			}
		}
		else if (FParse::Param(FCommandLine::Get(), Match))
		{
			CVar->Set(true, ECVF_SetByCommandline);
		}
	}

	void NVENCParseCommandLineFlags()
	{
		// CVar changes can only be triggered from the game thread
		{
			// We block on our current thread until these CVars are set on the game thread, the settings of these CVars needs to happen before we can proceed.
            FScopedEvent ThreadBlocker;

			AsyncTask(ENamedThreads::GameThread, [&ThreadBlocker]()
			{ 
				NVENCCommandLineParseValue(TEXT("-NVENCIntraRefreshPeriodFrames="), CVarIntraRefreshPeriodFrames);
				NVENCCommandLineParseValue(TEXT("-NVENCIntraRefreshCountFrames="), CVarIntraRefreshCountFrames);
				NVENCCommandLineParseOption(TEXT("-NVENCKeyFrameQPUseLastQP="), CVarKeyframeQPUseLastQP);
				NVENCCommandLineParseValue(TEXT("-NVENCKeyframeInterval="), CVarKeyframeInterval);

				// Unblocks the thread
                ThreadBlocker.Trigger();
			});
            // Blocks current thread until game thread calls trigger (indicating it is done)
        }
	}

	static bool GetEncoderInfo(FNVENCCommon& NVENC, FVideoEncoderInfo& EncoderInfo);
	static int GetEncoderCapability(FNVENCCommon& NVENC, void* InEncoder, NV_ENC_CAPS InCapsToQuery);

	bool FVideoEncoderNVENC_H264::GetIsAvailable(const FVideoEncoderInput& InVideoFrameFactory, FVideoEncoderInfo& OutEncoderInfo)
	{
		FNVENCCommon& NVENC = FNVENCCommon::Setup();
		bool bIsAvailable = NVENC.GetIsAvailable();
		if(bIsAvailable)
		{
			OutEncoderInfo.CodecType = ECodecType::H264;
		}
		return bIsAvailable;
	}

	void FVideoEncoderNVENC_H264::Register(FVideoEncoderFactory& InFactory)
	{
		FNVENCCommon& NVENC = FNVENCCommon::Setup();
		if(NVENC.GetIsAvailable() && IsRHIDeviceNVIDIA())
		{
			FVideoEncoderInfo EncoderInfo;
			if(GetEncoderInfo(NVENC, EncoderInfo))
			{
				InFactory.Register(EncoderInfo, []() { return TUniquePtr<FVideoEncoder>(new FVideoEncoderNVENC_H264()); });
			}
		}
	}

	FVideoEncoderNVENC_H264::FVideoEncoderNVENC_H264() : NVENC(FNVENCCommon::Setup()) {}

	FVideoEncoderNVENC_H264::~FVideoEncoderNVENC_H264() { Shutdown(); }

	bool FVideoEncoderNVENC_H264::Setup(TSharedRef<FVideoEncoderInput> InputFrameFactory, const FLayerConfig& InitConfig)
	{
		if(!NVENC.GetIsAvailable())
		{
			UE_LOG(LogEncoderNVENC, Error, TEXT("NVENC not avaliable"));
			return false;
		}

		FrameFormat = InputFrameFactory->GetFrameFormat();
		switch(FrameFormat)
		{
#if PLATFORM_WINDOWS
		case AVEncoder::EVideoFrameFormat::D3D11_R8G8B8A8_UNORM:
		case AVEncoder::EVideoFrameFormat::D3D12_R8G8B8A8_UNORM:
			EncoderDevice = InputFrameFactory->GetD3D11EncoderDevice();
			break;
#endif
		case AVEncoder::EVideoFrameFormat::CUDA_R8G8B8A8_UNORM:
			EncoderDevice = InputFrameFactory->GetCUDAEncoderContext();
			break;
		case AVEncoder::EVideoFrameFormat::Undefined:
		default:
			UE_LOG(LogEncoderNVENC, Error, TEXT("Frame format %s is not supported by NVENC_Encoder on this platform."), *ToString(FrameFormat));
			return false;
		}

		if(!EncoderDevice)
		{
			UE_LOG(LogEncoderNVENC, Error, TEXT("NVENC needs encoder device."));
			return false;
		}

		FLayerConfig mutableConfig = InitConfig;
		if(mutableConfig.MaxFramerate == 0)
		{
			mutableConfig.MaxFramerate = 60;
		}
		
		// Parse NVENC settings from command line (if any relevant ones are passed)
		NVENCParseCommandLineFlags();

		return AddLayer(mutableConfig);
	}

	FVideoEncoder::FLayer* FVideoEncoderNVENC_H264::CreateLayer(uint32 layerIdx, FLayerConfig const& config)
	{
		auto const layer = new FNVENCLayer(layerIdx, config, *this);
		if(!layer->Setup())
		{
			delete layer;
			return nullptr;
		}
		return layer;
	}

	void FVideoEncoderNVENC_H264::DestroyLayer(FLayer* layer) { delete layer; }

	void FVideoEncoderNVENC_H264::Encode(FVideoEncoderInputFrame const* InFrame, const FEncodeOptions& EncodeOptions)
	{
		for(auto&& layer : Layers)
		{
			auto const nvencLayer = static_cast<FNVENCLayer*>(layer);
			nvencLayer->Encode(InFrame, EncodeOptions);
		}
	}

	void FVideoEncoderNVENC_H264::Flush()
	{
		for(auto&& layer : Layers)
		{
			auto const nvencLayer = static_cast<FNVENCLayer*>(layer);
			nvencLayer->Flush();
		}
	}

	void FVideoEncoderNVENC_H264::Shutdown()
	{
		for(auto&& layer : Layers)
		{
			auto const nvencLayer = static_cast<FNVENCLayer*>(layer);
			nvencLayer->Shutdown();
			DestroyLayer(nvencLayer);
		}
		Layers.Reset();
	}

	// --- FVideoEncoderNVENC_H264::FLayer ------------------------------------------------------------
	FVideoEncoderNVENC_H264::FNVENCLayer::FNVENCLayer(uint32 layerIdx, FLayerConfig const& config, FVideoEncoderNVENC_H264& encoder)
		: FLayer(config), Encoder(encoder), NVENC(FNVENCCommon::Setup()), CodecGUID(NV_ENC_CODEC_H264_GUID), LayerIndex(layerIdx)
	{
	}

	FVideoEncoderNVENC_H264::FNVENCLayer::~FNVENCLayer() {}

	bool FVideoEncoderNVENC_H264::FNVENCLayer::Setup()
	{
		if(CreateSession() && CreateInitialConfig())
		{
			// create encoder
			auto const result = NVENC.nvEncInitializeEncoder(NVEncoder, &EncoderInitParams);
			if(result != NV_ENC_SUCCESS)
			{
				UE_LOG(LogEncoderNVENC, Error, TEXT("Unable to initialize NvEnc encoder (%s)."), *NVENC.GetErrorString(NVEncoder, result));
			}
			else
			{
				EncoderThread = MakeUnique<FThread>(TEXT("NvencEncoderLayerThread"), [this]() { ProcessFramesFunc(); });
				return true;
			}
		}

		return false;
	}

	bool FVideoEncoderNVENC_H264::FNVENCLayer::CreateSession()
	{
		if(!NVEncoder)
		{
			// create the encoder session
			NVENCStruct(NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS, OpenEncodeSessionExParams);
			OpenEncodeSessionExParams.apiVersion = NVENCAPI_VERSION;
			OpenEncodeSessionExParams.device = Encoder.EncoderDevice;

			switch(Encoder.FrameFormat)
			{
			case EVideoFrameFormat::D3D11_R8G8B8A8_UNORM:
			case EVideoFrameFormat::D3D12_R8G8B8A8_UNORM:
				OpenEncodeSessionExParams.deviceType = NV_ENC_DEVICE_TYPE_DIRECTX;
				break;
			case EVideoFrameFormat::CUDA_R8G8B8A8_UNORM:
				OpenEncodeSessionExParams.deviceType = NV_ENC_DEVICE_TYPE_CUDA;
				break;
			default:
				UE_LOG(LogEncoderNVENC, Error, TEXT("FrameFormat %s unavailable."), *ToString(Encoder.FrameFormat));
				return false;
				break;
			}

			auto const result = NVENC.nvEncOpenEncodeSessionEx(&OpenEncodeSessionExParams, &NVEncoder);
			if(result != NV_ENC_SUCCESS)
			{
				UE_LOG(LogEncoderNVENC, Error, TEXT("Unable to open NvEnc encoding session (%s)."), *NVENC.GetErrorString(NVEncoder, result));
				NVEncoder = nullptr;
				return false;
			}
		}

		return true;
	}

	bool FVideoEncoderNVENC_H264::FNVENCLayer::CreateInitialConfig()
	{
		// set the initialization parameters
		FMemory::Memzero(EncoderInitParams);

		CurrentConfig.MaxFramerate = 60;

		EncoderInitParams.version = NV_ENC_INITIALIZE_PARAMS_VER;
		EncoderInitParams.encodeGUID = NV_ENC_CODEC_H264_GUID;
		EncoderInitParams.presetGUID = NV_ENC_PRESET_P4_GUID;
		EncoderInitParams.frameRateNum = CurrentConfig.MaxFramerate;
		EncoderInitParams.frameRateDen = 1;
		EncoderInitParams.enablePTD = 1;
		EncoderInitParams.reportSliceOffsets = 0;
		EncoderInitParams.enableSubFrameWrite = 0;
		EncoderInitParams.maxEncodeWidth = 4096;
		EncoderInitParams.maxEncodeHeight = 4096;
		EncoderInitParams.tuningInfo = NV_ENC_TUNING_INFO_ULTRA_LOW_LATENCY;

		// load a preset configuration
		NVENCStruct(NV_ENC_PRESET_CONFIG, PresetConfig);
		PresetConfig.presetCfg.version = NV_ENC_CONFIG_VER;
		auto const result = NVENC.nvEncGetEncodePresetConfigEx(NVEncoder, EncoderInitParams.encodeGUID, EncoderInitParams.presetGUID, EncoderInitParams.tuningInfo, &PresetConfig);
		if(result != NV_ENC_SUCCESS)
		{
			UE_LOG(LogEncoderNVENC, Error, TEXT("Unable to get NvEnc preset config (%s)."), *NVENC.GetErrorString(NVEncoder, result));
			return false;
		}

		// copy the preset config to our config
		FMemory::Memcpy(&EncoderConfig, &PresetConfig.presetCfg, sizeof(NV_ENC_CONFIG));
		EncoderConfig.profileGUID = ConvertH264Profile(CurrentConfig.H264Profile);
		EncoderConfig.rcParams.version = NV_ENC_RC_PARAMS_VER;

		EncoderInitParams.encodeConfig = &EncoderConfig;

		////////////////////////////////
		// H.264 specific settings
		////////////////////////////////

		/*
		* Intra refresh - used to stabilise stream on the decoded side when frames are dropped/lost.
		*/
		int32 IntraRefreshPeriodFrames = CVarIntraRefreshPeriodFrames.GetValueOnAnyThread();
		int32 IntraRefreshCountFrames = CVarIntraRefreshCountFrames.GetValueOnAnyThread();
		bool bIntraRefreshSupported = GetEncoderCapability(NVENC, NVEncoder, NV_ENC_CAPS_SUPPORT_INTRA_REFRESH) > 0; 
		bool bIntraRefreshEnabled = IntraRefreshPeriodFrames > 0;

		if (bIntraRefreshEnabled && bIntraRefreshSupported) 
		{
			EncoderConfig.encodeCodecConfig.h264Config.enableIntraRefresh = 1;
			EncoderConfig.encodeCodecConfig.h264Config.intraRefreshPeriod = IntraRefreshPeriodFrames;
			EncoderConfig.encodeCodecConfig.h264Config.intraRefreshCnt = IntraRefreshCountFrames;
			
			UE_LOG(LogEncoderNVENC, Log, TEXT("NVENC intra refresh enabled."));
			UE_LOG(LogEncoderNVENC, Log, TEXT("NVENC intra refresh period set to = %d"), IntraRefreshPeriodFrames);
			UE_LOG(LogEncoderNVENC, Log, TEXT("NVENC intra refresh count = %d"), IntraRefreshCountFrames);
		}
		else if(bIntraRefreshEnabled && !bIntraRefreshSupported)
		{
			UE_LOG(LogEncoderNVENC, Error, TEXT("NVENC intra refresh capability is not supported on this device, cannot use this feature"));
		}

		/*
		* IDR period - how often to send IDR (instantaneous decode refresh) frames, a.k.a keyframes. This can stabilise a stream that dropped/lost some frames (but at the cost of more bandwidth).
		*/
		int32 IdrPeriod = CVarKeyframeInterval.GetValueOnAnyThread();
		if(IdrPeriod > 0)
		{
			EncoderConfig.encodeCodecConfig.h264Config.idrPeriod = IdrPeriod;
		}

		/*
		* Repeat SPS/PPS - sends sequence and picture parameter info with every IDR frame - maximum stabilisation of the stream when IDR is sent.
		*/
		EncoderConfig.encodeCodecConfig.h264Config.repeatSPSPPS = 1;

		/*
		* Slice mode - set the slice mode to "entire frame as a single slice" because WebRTC implementation doesn't work well with slicing. The default slicing mode
		* produces (rarely, but especially under packet loss) grey full screen or just top half of it.
		*/
		EncoderConfig.encodeCodecConfig.h264Config.sliceMode = 0;
		EncoderConfig.encodeCodecConfig.h264Config.sliceModeData = 0;

		UpdateConfig();
		return true;
	}

	void FVideoEncoderNVENC_H264::FNVENCLayer::MaybeReconfigure()
	{
		FScopeLock lock(&ConfigMutex);
		if(NeedsReconfigure)
		{
			UpdateConfig();
			
			if(EncoderInitParams.frameRateNum != CurrentConfig.MaxFramerate)
			{
				EncoderInitParams.frameRateNum = CurrentConfig.MaxFramerate;
			}

			NVENCStruct(NV_ENC_RECONFIGURE_PARAMS, ReconfigureParams);
			FMemory::Memcpy(&ReconfigureParams.reInitEncodeParams, &EncoderInitParams, sizeof(EncoderInitParams));

			auto const result = NVENC.nvEncReconfigureEncoder(NVEncoder, &ReconfigureParams);
			if(result != NV_ENC_SUCCESS)
				UE_LOG(LogEncoderNVENC, Error, TEXT("Failed to update NVENC encoder configuration (%s)"), *NVENC.GetErrorString(NVEncoder, result));

			NeedsReconfigure = false;
		}
	}

	void FVideoEncoderNVENC_H264::FNVENCLayer::UpdateLastEncodedQP(uint32 InLastEncodedQP)
	{
		if(InLastEncodedQP == LastEncodedQP)
		{
			// QP is the same, do nothing.
			return;
		}

		LastEncodedQP = InLastEncodedQP;
		NeedsReconfigure = true;
	}

	void FVideoEncoderNVENC_H264::FNVENCLayer::UpdateConfig()
	{
		EncoderInitParams.encodeWidth = EncoderInitParams.darWidth = CurrentConfig.Width;
		EncoderInitParams.encodeHeight = EncoderInitParams.darHeight = CurrentConfig.Height;

		uint32_t const MinQP = static_cast<uint32_t>(CurrentConfig.QPMin);
		uint32_t const MaxQP = static_cast<uint32_t>(CurrentConfig.QPMax);

		NV_ENC_RC_PARAMS& RateControlParams = EncoderInitParams.encodeConfig->rcParams;
		RateControlParams.rateControlMode = ConvertRateControlModeNVENC(CurrentConfig.RateControlMode);
		RateControlParams.averageBitRate = CurrentConfig.TargetBitrate > -1 ? CurrentConfig.TargetBitrate : DEFAULT_BITRATE;
		RateControlParams.maxBitRate = CurrentConfig.MaxBitrate > -1 ? CurrentConfig.MaxBitrate : DEFAULT_BITRATE; // Not used for CBR
		RateControlParams.multiPass = ConvertMultipassModeNVENC(CurrentConfig.MultipassMode);
		RateControlParams.minQP = {MinQP, MinQP, MinQP};
		RateControlParams.maxQP = {MaxQP, MaxQP, MaxQP}; 
		RateControlParams.enableMinQP = CurrentConfig.QPMin > -1;
		RateControlParams.enableMaxQP = CurrentConfig.QPMax > -1;

		// If we have QP ranges turned on use the last encoded QP to guide the max QP for an i-frame, so the i-frame doesn't look too blocky
		// Note: this does nothing if we have i-frames turned off.
		if(RateControlParams.enableMaxQP && LastEncodedQP > 0 && CVarKeyframeQPUseLastQP.GetValueOnAnyThread())
		{
			RateControlParams.maxQP.qpIntra = LastEncodedQP;
		}

		EncoderInitParams.encodeConfig->profileGUID = ConvertH264Profile(CurrentConfig.H264Profile);

		NV_ENC_CONFIG_H264& H264Config = EncoderInitParams.encodeConfig->encodeCodecConfig.h264Config;
		H264Config.enableFillerDataInsertion = CurrentConfig.FillData ? 1 : 0;

		if(CurrentConfig.RateControlMode == AVEncoder::FVideoEncoder::RateControlMode::CBR && CurrentConfig.FillData)
		{
			// `outputPictureTimingSEI` is used in CBR mode to fill video frame with data to match the requested bitrate.
			H264Config.outputPictureTimingSEI = 1;
		}
	}

	void FVideoEncoderNVENC_H264::FNVENCLayer::Encode(FVideoEncoderInputFrame const* InFrame, const FEncodeOptions& EncodeOptions)
	{
		uint64 EncodeStartMs = FPlatformTime::ToMilliseconds64(FPlatformTime::Cycles64());
		FInputOutput* Buffer = GetOrCreateBuffer(static_cast<const FVideoEncoderInputFrameImpl*>(InFrame->Obtain()));

		if(Buffer)
		{
			Buffer->EncodeStartMs = EncodeStartMs;

			if(MapInputTexture(*Buffer))
			{
				Buffer->PicParams = {};
				Buffer->PicParams.version = NV_ENC_PIC_PARAMS_VER;
				Buffer->PicParams.inputWidth = Buffer->Width;
				Buffer->PicParams.inputHeight = Buffer->Height;
				Buffer->PicParams.inputPitch = Buffer->Pitch ? Buffer->Pitch : Buffer->Width;
				Buffer->PicParams.inputBuffer = Buffer->MappedInput;
				Buffer->PicParams.bufferFmt = Buffer->BufferFormat;
				Buffer->PicParams.encodePicFlags = 0;

				if(EncodeOptions.bForceKeyFrame)
				{
					LastKeyFrameTime = FDateTime::UtcNow();
					Buffer->PicParams.encodePicFlags |= NV_ENC_PIC_FLAG_FORCEIDR;
				}

				Buffer->PicParams.inputTimeStamp = Buffer->TimeStamp = InFrame->GetTimestampUs();
				Buffer->PicParams.outputBitstream = Buffer->OutputBitstream;
				Buffer->PicParams.pictureStruct = NV_ENC_PIC_STRUCT_FRAME;
				Buffer->PicParams.frameIdx = InFrame->GetFrameID();

				PendingEncodes.Enqueue(Buffer);
				FramesPending->Trigger();
			}
		}
	}

	void FVideoEncoderNVENC_H264::FNVENCLayer::Flush()
	{
		FInputOutput* EmptyBuffer = CreateBuffer();

		if(!EmptyBuffer)
		{
			return;
		}

		EmptyBuffer->PicParams = {};
		EmptyBuffer->PicParams.version = NV_ENC_PIC_PARAMS_VER;
		EmptyBuffer->PicParams.encodePicFlags = NV_ENC_PIC_FLAG_EOS;

		PendingEncodes.Enqueue(EmptyBuffer);
	}

	void FVideoEncoderNVENC_H264::FNVENCLayer::Shutdown()
	{
		Flush();

		
		FramesPending->Trigger();
		EncoderThread->Join();

		if(NVEncoder)
		{
			auto const result = NVENC.nvEncDestroyEncoder(NVEncoder);
			if(result != NV_ENC_SUCCESS)
			{
				UE_LOG(LogEncoderNVENC, Error, TEXT("Failed to destroy NVENC encoder (%s)."), *NVENC.GetErrorString(NVEncoder, result));
			}
			NVEncoder = nullptr;
		}
	}

// This function throws error in static analysis as the Buffer is never explicitly set
// however dequeue will guarantee that it is never null and hence a false positive
#pragma warning( push )
#pragma warning( disable : 6011 )
	void FVideoEncoderNVENC_H264::FNVENCLayer::ProcessFramesFunc()
	{
		TQueue<FInputOutput*> LiveBuffers;
		FInputOutput* Buffer;
		NVENCSTATUS result;

		while(bShouldEncoderThreadRun)
		{
			FramesPending->Wait();

			while (bShouldEncoderThreadRun && PendingEncodes.Dequeue(Buffer))
			{
				// we have kicked off a flush of the encoder clear all the buffers
				if (Buffer->PicParams.encodePicFlags == NV_ENC_PIC_FLAG_EOS)
				{
					FInputOutput* EOSBuffer = Buffer;
					DestroyBuffer(EOSBuffer);

					while (IdleBuffers.Dequeue(Buffer))
					{
						FInputOutput* OldBuffer = Buffer;
						DestroyBuffer(OldBuffer);
					}

					// We are exiting so clear all live buffers
					while (LiveBuffers.Dequeue(Buffer))
					{
						FInputOutput* LiveBuffer = Buffer;
						if (LiveBuffer->SourceFrame)
						{
							LiveBuffer->SourceFrame->Release();
							LiveBuffer->SourceFrame = nullptr;
						}
						DestroyBuffer(LiveBuffer);
					}

					bShouldEncoderThreadRun = false;
					return;
				}


				MaybeReconfigure();

				// check if we have not reconfigued the buffer size
				if(Buffer->Width != EncoderInitParams.encodeWidth || Buffer->Height != EncoderInitParams.encodeHeight)
				{
					// clear all live buffers as they are now invalid
					LiveBuffers.Enqueue(Buffer);

					while(LiveBuffers.Dequeue(Buffer))
					{
						Buffer->SourceFrame->Release();
						Buffer->SourceFrame = nullptr;
						DestroyBuffer(Buffer);
					}

					continue;
				}

				// Do synchronous encode
				result = NVENC.nvEncEncodePicture(NVEncoder, &(Buffer->PicParams));

				// Enqueue buffers until we get a success then Dequeue all the LiveBuffers
				if(result == NV_ENC_ERR_NEED_MORE_INPUT)
				{
					LiveBuffers.Enqueue(Buffer);
				}
				else if(result == NV_ENC_SUCCESS)
				{
					LiveBuffers.Enqueue(Buffer);

					while(bShouldEncoderThreadRun && LiveBuffers.Dequeue(Buffer))
					{
						// lock output buffers for CPU access
						if(LockOutputBuffer(*Buffer))
						{
							if(Encoder.OnEncodedPacket)
							{
								// create packet with buffer contents
								FCodecPacketImpl Packet;

								Packet.Data = static_cast<const uint8*>(Buffer->BitstreamData);
								Packet.DataSize = Buffer->BitstreamDataSize;
								
								if(Buffer->PictureType & NV_ENC_PIC_TYPE_IDR)
								{
									UE_LOG(LogEncoderNVENC, Verbose, TEXT("Generated IDR Frame"));
									Packet.IsKeyFrame = true;
								}
								else
								{
									// If it is not a keyframe store the QP.
									UpdateLastEncodedQP(Buffer->FrameAvgQP);
								}

								Packet.VideoQP = Buffer->FrameAvgQP;
								Packet.Timings.StartTs = FTimespan::FromMilliseconds(Buffer->EncodeStartMs);
								Packet.Timings.FinishTs = FTimespan::FromMilliseconds(FPlatformTime::ToMilliseconds64(FPlatformTime::Cycles64()));
								Packet.Framerate = EncoderInitParams.frameRateNum;

								// yeet it out
								if(Encoder.OnEncodedPacket)
								{
									Encoder.OnEncodedPacket(LayerIndex, Buffer->SourceFrame, Packet);
								}
							}

							UnlockOutputBuffer(*Buffer);
						}



						// We have consumed the encoded buffer we can now return it to the pool
						if(Buffer->SourceFrame)
						{
							Buffer->SourceFrame->Release();
							Buffer->SourceFrame = nullptr;
						}

						IdleBuffers.Enqueue(Buffer);
					}
				}
				else
				{
					// Something went wrong
					UE_LOG(LogEncoderNVENC, Error, TEXT("nvEncEncodePicture returned %s"), *NVENC.GetErrorString(NVEncoder, result));

					if(Buffer->SourceFrame)
					{
						Buffer->SourceFrame->Release();
						Buffer->SourceFrame = nullptr;
						DestroyBuffer(Buffer);
					}
				}
			}
		}

		// We are exiting so clear all live buffers
		while(LiveBuffers.Dequeue(Buffer))
		{
			if (Buffer->SourceFrame)
			{
				Buffer->SourceFrame->Release();
				Buffer->SourceFrame = nullptr;
			}
		}
	}
#pragma warning( pop )

	int FVideoEncoderNVENC_H264::FNVENCLayer::GetCapability(NV_ENC_CAPS CapsToQuery) const
	{
		int CapsValue = 0;
		NVENCStruct(NV_ENC_CAPS_PARAM, CapsParam);
		CapsParam.capsToQuery = CapsToQuery;
		auto const result = NVENC.nvEncGetEncodeCaps(NVEncoder, CodecGUID, &CapsParam, &CapsValue);
		if(result != NV_ENC_SUCCESS)
		{
			UE_LOG(LogEncoderNVENC, Warning, TEXT("Failed to query for NVENC capability %d (%s)."), CapsToQuery, *NVENC.GetErrorString(NVEncoder, result));
			return 0;
		}
		return CapsValue;
	}

	FVideoEncoderNVENC_H264::FNVENCLayer::FInputOutput* FVideoEncoderNVENC_H264::FNVENCLayer::GetOrCreateBuffer(const FVideoEncoderInputFrameImpl* InFrame)
	{
		void* TextureToCompress = nullptr;

		switch(InFrame->GetFormat())
		{
#if PLATFORM_WINDOWS
		case EVideoFrameFormat::D3D11_R8G8B8A8_UNORM:
			TextureToCompress = InFrame->GetD3D11().EncoderTexture;
			break;
		case EVideoFrameFormat::D3D12_R8G8B8A8_UNORM:
			UE_LOG(LogEncoderNVENC, Error, TEXT("NVENC got passed a DX12 texture which it does not support, make sure it has been shared with a DX11 context and pass that texture instead."));
			return nullptr; // NVENC cant encode a D3D12 texture directly this should be converted to D3D11
#endif
		case EVideoFrameFormat::CUDA_R8G8B8A8_UNORM:
			TextureToCompress = InFrame->GetCUDA().EncoderTexture;
			break;
		case AVEncoder::EVideoFrameFormat::Undefined:
		default:
			break;
		}

		if(!TextureToCompress)
		{
			UE_LOG(LogEncoderNVENC, Fatal, TEXT("Got passed a null texture to encode."));
			return nullptr;
		}

		FInputOutput* Buffer = nullptr;
		FInputOutput* TempBuffer = nullptr;
		TQueue<FInputOutput*> TempQueue;
		while(!IdleBuffers.IsEmpty())
		{
			IdleBuffers.Dequeue(TempBuffer);
			if(TempBuffer->InputTexture == TextureToCompress)
			{
				Buffer = TempBuffer;
				break;
			}
			else
			{
				TempQueue.Enqueue(TempBuffer);
			}
		}

		while(!TempQueue.IsEmpty())
		{
			TempQueue.Dequeue(TempBuffer);
			IdleBuffers.Enqueue(TempBuffer);
		}

		if(Buffer)
		{
			// Check for buffer and InputFrameFactory texture resolution mismatch
			if(InFrame->GetWidth() != Buffer->Width || InFrame->GetHeight() != Buffer->Height)
			{
				DestroyBuffer(Buffer);
				Buffer = nullptr;
			}
		}

		if(!Buffer)
		{
			Buffer = CreateBuffer();
			Buffer->SourceFrame = InFrame;

			if(Buffer && !RegisterInputTexture(*Buffer, TextureToCompress, FIntPoint(InFrame->GetWidth(), InFrame->GetHeight())))
			{
				Buffer->SourceFrame->Release();
				DestroyBuffer(Buffer);
				Buffer = nullptr;
			}
		}
		else
		{
			Buffer->SourceFrame = InFrame;
		}

		return Buffer;
	}

	FVideoEncoderNVENC_H264::FNVENCLayer::FInputOutput* FVideoEncoderNVENC_H264::FNVENCLayer::CreateBuffer()
	{
		FInputOutput* Buffer = new FInputOutput();

		// output bit stream buffer
		NVENCStruct(NV_ENC_CREATE_BITSTREAM_BUFFER, CreateParam);
		{
			auto const result = NVENC.nvEncCreateBitstreamBuffer(NVEncoder, &CreateParam);
			if(result != NV_ENC_SUCCESS)
			{
				UE_LOG(LogEncoderNVENC, Error, TEXT("Failed to create NVENC output buffer (%s)."), *NVENC.GetErrorString(NVEncoder, result));
				DestroyBuffer(Buffer);
				return nullptr;
			}
		}

		Buffer->OutputBitstream = CreateParam.bitstreamBuffer;

		return Buffer;
	}

	void FVideoEncoderNVENC_H264::FNVENCLayer::DestroyBuffer(FInputOutput* InBuffer)
	{
		// unregister input texture - if any
		UnregisterInputTexture(*InBuffer);

		// destroy output buffer - if any
		UnlockOutputBuffer(*InBuffer);
		if(InBuffer->OutputBitstream)
		{
			auto const result = NVENC.nvEncDestroyBitstreamBuffer(NVEncoder, InBuffer->OutputBitstream);
			if(result != NV_ENC_SUCCESS)
			{
				UE_LOG(LogEncoderNVENC, Warning, TEXT("Failed to destroy NVENC output buffer (%s)."), *NVENC.GetErrorString(NVEncoder, result));
			}
			InBuffer->OutputBitstream = nullptr;
		}

		// release source texture
		InBuffer->InputTexture = nullptr;

		delete InBuffer;
	}

	void FVideoEncoderNVENC_H264::FNVENCLayer::CreateResourceDIRECTX(FInputOutput& InBuffer, NV_ENC_REGISTER_RESOURCE& RegisterParam, FIntPoint TextureSize)
	{
#if PLATFORM_WINDOWS
		D3D11_TEXTURE2D_DESC Desc;
		static_cast<ID3D11Texture2D*>(InBuffer.InputTexture)->GetDesc(&Desc);

		switch(Desc.Format)
		{
		case DXGI_FORMAT_NV12:
			InBuffer.BufferFormat = NV_ENC_BUFFER_FORMAT_NV12;
			break;
		case DXGI_FORMAT_R8G8B8A8_UNORM:
			InBuffer.BufferFormat = NV_ENC_BUFFER_FORMAT_ABGR;
			break;
		case DXGI_FORMAT_B8G8R8A8_UNORM:
			InBuffer.BufferFormat = NV_ENC_BUFFER_FORMAT_ARGB;
			break;
		default:
			UE_LOG(LogEncoderNVENC, Error, TEXT("Invalid input texture format for NVENC (%d)"), Desc.Format);
			return;
		}

		InBuffer.Width = TextureSize.X;
		InBuffer.Height = TextureSize.Y;
		InBuffer.Pitch = 0;

		RegisterParam.resourceType = NV_ENC_INPUT_RESOURCE_TYPE_DIRECTX;
		RegisterParam.width = Desc.Width;
		RegisterParam.height = Desc.Height;
		RegisterParam.pitch = InBuffer.Pitch;
		RegisterParam.bufferFormat = InBuffer.BufferFormat;
		RegisterParam.bufferUsage = NV_ENC_INPUT_IMAGE;
#endif
	}

	void FVideoEncoderNVENC_H264::FNVENCLayer::CreateResourceCUDAARRAY(FInputOutput& InBuffer, NV_ENC_REGISTER_RESOURCE& RegisterParam, FIntPoint TextureSize)
	{
		InBuffer.Width = TextureSize.X;
		InBuffer.Height = TextureSize.Y;
		InBuffer.Pitch = TextureSize.X * 4;
		InBuffer.BufferFormat = NV_ENC_BUFFER_FORMAT_ARGB;

		RegisterParam.resourceType = NV_ENC_INPUT_RESOURCE_TYPE_CUDAARRAY;
		RegisterParam.width = InBuffer.Width;
		RegisterParam.height = InBuffer.Height;
		RegisterParam.pitch = InBuffer.Pitch;
		RegisterParam.bufferFormat = InBuffer.BufferFormat;
		RegisterParam.bufferUsage = NV_ENC_INPUT_IMAGE;
	}

	bool FVideoEncoderNVENC_H264::FNVENCLayer::RegisterInputTexture(FInputOutput& InBuffer, void* InTexture, FIntPoint TextureSize)
	{
		if(!InBuffer.InputTexture)
		{
			InBuffer.InputTexture = InTexture;
			NVENCStruct(NV_ENC_REGISTER_RESOURCE, RegisterParam);

			switch(InBuffer.SourceFrame->GetFormat())
			{
#if PLATFORM_WINDOWS
			case EVideoFrameFormat::D3D11_R8G8B8A8_UNORM:
			case EVideoFrameFormat::D3D12_R8G8B8A8_UNORM:
				CreateResourceDIRECTX(InBuffer, RegisterParam, TextureSize);
				break;
#endif
			case EVideoFrameFormat::CUDA_R8G8B8A8_UNORM:
				CreateResourceCUDAARRAY(InBuffer, RegisterParam, TextureSize);
				break;
			case AVEncoder::EVideoFrameFormat::Undefined:
			default:
				break;
			}

			RegisterParam.resourceToRegister = InTexture;

			NVENCSTATUS Result = NVENC.nvEncRegisterResource(NVEncoder, &RegisterParam);
			if(Result != NV_ENC_SUCCESS)
			{
				UE_LOG(LogEncoderNVENC, Error, TEXT("Failed to register input texture with NVENC (%s)"), *NVENC.GetErrorString(NVEncoder, Result));
				return false;
			}
			InBuffer.RegisteredInput = RegisterParam.registeredResource;
		}

		return true;
	}

	bool FVideoEncoderNVENC_H264::FNVENCLayer::UnregisterInputTexture(FInputOutput& InBuffer)
	{
		UnmapInputTexture(InBuffer);
		if(InBuffer.RegisteredInput)
		{
			NVENCSTATUS Result = NVENC.nvEncUnregisterResource(NVEncoder, InBuffer.RegisteredInput);
			if(Result != NV_ENC_SUCCESS)
			{
				UE_LOG(LogEncoderNVENC, Error, TEXT("Failed to unregister input texture with NVENC (%s)"), *NVENC.GetErrorString(NVEncoder, Result));
				InBuffer.InputTexture = nullptr;
				InBuffer.RegisteredInput = nullptr;
				return false;
			}
			InBuffer.InputTexture = nullptr;
			InBuffer.RegisteredInput = nullptr;
		}
		return true;
	}

	bool FVideoEncoderNVENC_H264::FNVENCLayer::MapInputTexture(FInputOutput& InBuffer)
	{
		if(!InBuffer.MappedInput)
		{
			NVENCStruct(NV_ENC_MAP_INPUT_RESOURCE, MapInputResource);
			MapInputResource.registeredResource = InBuffer.RegisteredInput;
			NVENCSTATUS Result = NVENC.nvEncMapInputResource(NVEncoder, &MapInputResource);
			if(Result != NV_ENC_SUCCESS)
			{
				UE_LOG(LogEncoderNVENC, Error, TEXT("Failed to map input texture buffer (%s)"), *NVENC.GetErrorString(NVEncoder, Result));
				return false;
			}
			InBuffer.MappedInput = MapInputResource.mappedResource;
			check(InBuffer.BufferFormat == MapInputResource.mappedBufferFmt);
		}
		return true;
	}

	bool FVideoEncoderNVENC_H264::FNVENCLayer::UnmapInputTexture(FInputOutput& InBuffer)
	{
		if(InBuffer.MappedInput)
		{
			NVENCSTATUS Result = NVENC.nvEncUnmapInputResource(NVEncoder, InBuffer.MappedInput);
			if(Result != NV_ENC_SUCCESS)
			{
				UE_LOG(LogEncoderNVENC, Error, TEXT("Failed to unmap input texture buffer (%s)"), *NVENC.GetErrorString(NVEncoder, Result));
				InBuffer.MappedInput = nullptr;
				return false;
			}
			InBuffer.MappedInput = nullptr;
		}
		return true;
	}

	bool FVideoEncoderNVENC_H264::FNVENCLayer::LockOutputBuffer(FInputOutput& InBuffer)
	{
		if(!InBuffer.BitstreamData)
		{
			// lock output buffers for CPU access
			NVENCStruct(NV_ENC_LOCK_BITSTREAM, LockBitstreamParam);
			LockBitstreamParam.outputBitstream = InBuffer.OutputBitstream;
			NVENCSTATUS Result = NVENC.nvEncLockBitstream(NVEncoder, &LockBitstreamParam);
			if(Result != NV_ENC_SUCCESS)
			{
				UE_LOG(LogEncoderNVENC, Error, TEXT("Failed to lock output bitstream for NVENC encoder (%s)."), *NVENC.GetErrorString(NVEncoder, Result));
				return false;
			}
			else
			{
				InBuffer.BitstreamData = LockBitstreamParam.bitstreamBufferPtr;
				InBuffer.BitstreamDataSize = LockBitstreamParam.bitstreamSizeInBytes;
				InBuffer.PictureType = LockBitstreamParam.pictureType;
				InBuffer.FrameAvgQP = LockBitstreamParam.frameAvgQP;
				InBuffer.TimeStamp = LockBitstreamParam.outputTimeStamp;
			}
		}
		return true;
	}

	bool FVideoEncoderNVENC_H264::FNVENCLayer::UnlockOutputBuffer(FInputOutput& InBuffer)
	{
		if(InBuffer.BitstreamData)
		{
			NVENCSTATUS Result = NVENC.nvEncUnlockBitstream(NVEncoder, InBuffer.OutputBitstream);
			if(Result != NV_ENC_SUCCESS)
			{
				UE_LOG(LogEncoderNVENC, Error, TEXT("Failed to unlock output bitstream for NVENC encoder (%s)."), *NVENC.GetErrorString(NVEncoder, Result));
				return false;
			}
			else
			{
				InBuffer.BitstreamData = nullptr;
				InBuffer.BitstreamDataSize = 0;
			}
		}
		return true;
	}

#if PLATFORM_WINDOWS
	static bool CreateEncoderDevice(TRefCountPtr<ID3D11Device>& OutEncoderDevice, TRefCountPtr<ID3D11DeviceContext>& OutEncoderDeviceContext)
	{
		// need a d3d11 context to be able to set up encoder
		TRefCountPtr<IDXGIFactory1> DXGIFactory1;
		TRefCountPtr<IDXGIAdapter> Adapter;

		HRESULT Result = CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)DXGIFactory1.GetInitReference());
		if(Result != S_OK)
		{
			UE_LOG(LogEncoderNVENC, Error, TEXT("Failed to create DX factory for NVENC."));
			return false;
		}

		for(int GpuIndex = 0; GpuIndex < MAX_GPU_INDEXES; GpuIndex++)
		{
			if((Result = DXGIFactory1->EnumAdapters(GpuIndex, Adapter.GetInitReference())) != S_OK)
			{
				UE_LOG(LogEncoderNVENC, Error, TEXT("Failed to enum GPU #%d for NVENC."), GpuIndex);
				return false;
			}

			DXGI_ADAPTER_DESC AdapterDesc;
			Adapter->GetDesc(&AdapterDesc);
			if(AdapterDesc.VendorId != 0x10DE) // NVIDIA
			{
				continue;
			}

			if((Result = D3D11CreateDevice(Adapter, D3D_DRIVER_TYPE_UNKNOWN, NULL, 0, NULL, 0, D3D11_SDK_VERSION, OutEncoderDevice.GetInitReference(), NULL,
			                               OutEncoderDeviceContext.GetInitReference())) != S_OK)
			{
				UE_LOG(LogEncoderNVENC, Error, TEXT("Failed to create D3D11 device for NVENC."));
			}
			else
			{
				UE_LOG(LogEncoderNVENC, Log, TEXT("Created D3D11 device for NVENC on '%s'."), AdapterDesc.Description);
				return true;
			}
		}

		UE_LOG(LogEncoderNVENC, Error, TEXT("No compatible devices found for NVENC."));
		return false;
	}
#endif

#if PLATFORM_WINDOWS
	static void* CreateEncoderSession(FNVENCCommon& NVENC, TRefCountPtr<ID3D11Device> InD3D11Device)
	{
		void* EncoderSession = nullptr;
		// create the encoder session
		NVENCStruct(NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS, OpenEncodeSessionExParams);
		OpenEncodeSessionExParams.device = InD3D11Device;
		OpenEncodeSessionExParams.deviceType = NV_ENC_DEVICE_TYPE_DIRECTX; // Currently only DX11 is supported
		OpenEncodeSessionExParams.apiVersion = NVENCAPI_VERSION;

		NVENCSTATUS NvResult = NVENC.nvEncOpenEncodeSessionEx(&OpenEncodeSessionExParams, &EncoderSession);
		// UE_LOG(LogEncoderNVENC, Error, TEXT("NVENC.nvEncOpenEncodeSessionEx(&OpenEncodeSessionExParams, &EncoderSession); -> %d"), NvResult);
		if(NvResult != NV_ENC_SUCCESS)
		{
			UE_LOG(LogEncoderNVENC, Error, TEXT("Unable to open NvEnc encoding session (status: %d)."), NvResult);
			EncoderSession = nullptr;
		}
		return EncoderSession;
	}
#endif // PLATFORM_WINDOWS

	static void* CreateEncoderSession(FNVENCCommon& NVENC, CUcontext CudaContext)
	{
		void* EncoderSession = nullptr;
		// create the encoder session
		NVENCStruct(NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS, OpenEncodeSessionExParams);
		OpenEncodeSessionExParams.device = CudaContext;
		OpenEncodeSessionExParams.deviceType = NV_ENC_DEVICE_TYPE_CUDA; // We use cuda to pass vulkan device memory to nvenc
		OpenEncodeSessionExParams.apiVersion = NVENCAPI_VERSION;

		NVENCSTATUS NvResult = NVENC.nvEncOpenEncodeSessionEx(&OpenEncodeSessionExParams, &EncoderSession);
		//	UE_LOG(LogEncoderNVENC, Error, TEXT("NVENC.nvEncOpenEncodeSessionEx(&OpenEncodeSessionExParams, &EncoderSession); -> %d"), NvResult);
		if(NvResult != NV_ENC_SUCCESS)
		{
			UE_LOG(LogEncoderNVENC, Error, TEXT("Unable to open NvEnc encoding session (status: %d)."), NvResult);
			EncoderSession = nullptr;
		}
		return EncoderSession;
	}

	static int GetEncoderCapability(FNVENCCommon& NVENC, void* InEncoder, NV_ENC_CAPS InCapsToQuery)
	{
		int CapsValue = 0;
		NVENCStruct(NV_ENC_CAPS_PARAM, CapsParam);
		CapsParam.capsToQuery = InCapsToQuery;
		NVENCSTATUS Result = NVENC.nvEncGetEncodeCaps(InEncoder, NV_ENC_CODEC_H264_GUID, &CapsParam, &CapsValue);
		
		if(Result != NV_ENC_SUCCESS)
		{
			UE_LOG(LogEncoderNVENC, Warning, TEXT("Failed to query for NVENC capability %d (error %d)."), InCapsToQuery, Result);
			return 0;
		}
		return CapsValue;
	}

	static bool GetEncoderSupportedProfiles(FNVENCCommon& NVENC, void* InEncoder, uint32& OutSupportedProfiles)
	{
		const uint32 MaxProfileGUIDs = 32;
		GUID ProfileGUIDs[MaxProfileGUIDs];
		uint32 NumProfileGUIDs = 0;

		OutSupportedProfiles = 0;
		NVENCSTATUS Result = NVENC.nvEncGetEncodeProfileGUIDs(InEncoder, NV_ENC_CODEC_H264_GUID, ProfileGUIDs, MaxProfileGUIDs, &NumProfileGUIDs);
		
		if(Result != NV_ENC_SUCCESS)
		{
			UE_LOG(LogEncoderNVENC, Error, TEXT("Unable to query profiles supported by NvEnc (error: %d)."), Result);
			return false;
		}
		for(uint32 Index = 0; Index < NumProfileGUIDs; ++Index)
		{
			if(memcmp(&NV_ENC_H264_PROFILE_BASELINE_GUID, &ProfileGUIDs[Index], sizeof(GUID)) == 0)
			{
				OutSupportedProfiles |= H264Profile_Baseline;
				if(GetEncoderCapability(NVENC, InEncoder, NV_ENC_CAPS_SUPPORT_CONSTRAINED_ENCODING))
				{
					OutSupportedProfiles |= H264Profile_ConstrainedBaseline;
				}
			}
			else if(memcmp(&NV_ENC_H264_PROFILE_MAIN_GUID, &ProfileGUIDs[Index], sizeof(GUID)) == 0)
			{
				OutSupportedProfiles |= H264Profile_Main;
			}
			else if(memcmp(&NV_ENC_H264_PROFILE_HIGH_GUID, &ProfileGUIDs[Index], sizeof(GUID)) == 0)
			{
				OutSupportedProfiles |= H264Profile_High;
			}
			else if(memcmp(&NV_ENC_H264_PROFILE_CONSTRAINED_HIGH_GUID, &ProfileGUIDs[Index], sizeof(GUID)) == 0)
			{
				OutSupportedProfiles |= H264Profile_ConstrainedHigh;
			}
		}
		return OutSupportedProfiles != 0;
	}

	static bool GetEncoderSupportedInputFormats(FNVENCCommon& NVENC, void* InEncoder, TArray<EVideoFrameFormat>& OutSupportedInputFormats)
	{
		const uint32_t MaxInputFmtCount = 32;
		uint32_t InputFmtCount = 0;
		NV_ENC_BUFFER_FORMAT InputFormats[MaxInputFmtCount];
		NVENCSTATUS Result = NVENC.nvEncGetInputFormats(InEncoder, NV_ENC_CODEC_H264_GUID, InputFormats, MaxInputFmtCount, &InputFmtCount);
		if(Result != NV_ENC_SUCCESS)
		{
			UE_LOG(LogEncoderNVENC, Error, TEXT("Unable to query input formats supported by NvEnc (error: %d)."), Result);
			return false;
		}

		for(uint32_t Index = 0; Index < InputFmtCount; ++Index)
		{
			switch(InputFormats[Index])
			{
			case NV_ENC_BUFFER_FORMAT_IYUV:
				break;
			case NV_ENC_BUFFER_FORMAT_NV12:
				break;
			case NV_ENC_BUFFER_FORMAT_ARGB:
				break;
			case NV_ENC_BUFFER_FORMAT_ABGR:
#if PLATFORM_WINDOWS
				OutSupportedInputFormats.Push(EVideoFrameFormat::D3D11_R8G8B8A8_UNORM);
				OutSupportedInputFormats.Push(EVideoFrameFormat::D3D12_R8G8B8A8_UNORM);
#endif
				OutSupportedInputFormats.Push(EVideoFrameFormat::CUDA_R8G8B8A8_UNORM);
				break;
			}
		}
		return true;
	}

	static bool GetEncoderInfo(FNVENCCommon& NVENC, FVideoEncoderInfo& EncoderInfo)
	{
		bool bSuccess = true;

		// create a temporary encoder session
		void* EncoderSession = nullptr;

#if PLATFORM_WINDOWS
		// if we are under windows we can create a temporary dx11 device to get back infomation about the encoder
		TRefCountPtr<ID3D11Device> EncoderDevice;
		TRefCountPtr<ID3D11DeviceContext> EncoderDeviceContext;

		if(!CreateEncoderDevice(EncoderDevice, EncoderDeviceContext))
		{
			bSuccess = false;
		}

		if((EncoderSession = CreateEncoderSession(NVENC, EncoderDevice)) == nullptr)
		{
			bSuccess = false;
		}
#endif
		// if we dont already have an encoder session try with CUDA if its avaliable
		if(!EncoderSession && FModuleManager::GetModulePtr<FCUDAModule>("CUDA"))
		{
			FCUDAModule& CUDAModule = FModuleManager::GetModuleChecked<FCUDAModule>("CUDA");
			if(CUDAModule.IsAvailable())
			{
				EncoderSession = CreateEncoderSession(NVENC, CUDAModule.GetCudaContext());
				bSuccess = EncoderSession != nullptr;
			} 
			else
			{
				bSuccess = false;
			}
		}

		// if we dont have a session by now opt out. this will cause NVENC to not register
		if(!EncoderSession || !bSuccess)
		{
			return false;
		}

		EncoderInfo.CodecType = ECodecType::H264;
		EncoderInfo.MaxWidth = GetEncoderCapability(NVENC, EncoderSession, NV_ENC_CAPS_WIDTH_MAX);
		EncoderInfo.MaxHeight = GetEncoderCapability(NVENC, EncoderSession, NV_ENC_CAPS_HEIGHT_MAX);

		int LevelMax = GetEncoderCapability(NVENC, EncoderSession, NV_ENC_CAPS_LEVEL_MAX);
		int LevelMin = GetEncoderCapability(NVENC, EncoderSession, NV_ENC_CAPS_LEVEL_MIN);
		if(LevelMin > 0 && LevelMax > 0 && LevelMax >= LevelMin)
		{
			EncoderInfo.H264.MinLevel = (LevelMin > 9) ? LevelMin : 9;
			EncoderInfo.H264.MaxLevel = (LevelMax < 9) ? 9 : (LevelMax > NV_ENC_LEVEL_H264_52) ? NV_ENC_LEVEL_H264_52 : LevelMax;
		}
		else
		{
			UE_LOG(LogEncoderNVENC, Error, TEXT("Failed to query min/max h264 level supported by NvEnc (reported min/max=%d/%d)."), LevelMin, LevelMax);
			bSuccess = false;
		}

		if(!GetEncoderSupportedProfiles(NVENC, EncoderSession, EncoderInfo.H264.SupportedProfiles) || !GetEncoderSupportedInputFormats(NVENC, EncoderSession, EncoderInfo.SupportedInputFormats))
		{
			bSuccess = false;
		}

		// destroy encoder session
		if(EncoderSession)
		{
			NVENC.nvEncDestroyEncoder(EncoderSession);
		}

		return bSuccess;
	}

} /* namespace AVEncoder */

#undef MIN_UPDATE_FRAMERATE_SECS
