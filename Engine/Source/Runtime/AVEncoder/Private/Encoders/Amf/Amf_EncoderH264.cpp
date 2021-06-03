// Copyright Epic Games, Inc. All Rights Reserved.

#include "Amf_EncoderH264.h"
#include "HAL/Platform.h"

#include "VideoEncoderCommon.h"
#include "CodecPacket.h"
#include "AVEncoderDebug.h"

#include "RHI.h"

#include <stdio.h>

#define MAX_GPU_INDEXES 50
#define DEFAULT_BITRATE 1000000u
#define MAX_FRAMERATE_DIFF 0
#define MIN_UPDATE_FRAMERATE_SECS 15

#define AMF_VIDEO_ENCODER_START_TS L"StartTs"

namespace
{
	AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_ENUM ConvertRateControlModeAMF(AVEncoder::FVideoEncoder::RateControlMode mode)
	{
		switch (mode)
		{
		case AVEncoder::FVideoEncoder::RateControlMode::CONSTQP: return AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_CONSTANT_QP;
		case AVEncoder::FVideoEncoder::RateControlMode::VBR: return AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_LATENCY_CONSTRAINED_VBR;
		default:
		case AVEncoder::FVideoEncoder::RateControlMode::CBR: return AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_CBR;
		}
	}
}

namespace AVEncoder
{
	static bool GetEncoderInfo(FAmfCommon& Amf, FVideoEncoderInfo& EncoderInfo);

	bool FVideoEncoderAmf_H264::GetIsAvailable(FVideoEncoderInputImpl& InInput, FVideoEncoderInfo& OutEncoderInfo)
	{
		FAmfCommon& Amf = FAmfCommon::Setup();
		bool bIsAvailable = Amf.GetIsAvailable();
		if (bIsAvailable)
		{
			OutEncoderInfo.CodecType = ECodecType::H264;
		}
		return bIsAvailable;
	}

	void FVideoEncoderAmf_H264::Register(FVideoEncoderFactory& InFactory)
	{
		FAmfCommon& Amf = FAmfCommon::Setup();
		if (Amf.GetIsAvailable())
		{
			FVideoEncoderInfo	EncoderInfo;
			if (GetEncoderInfo(Amf, EncoderInfo))
			{
				InFactory.Register(EncoderInfo, []() {
					return TUniquePtr<FVideoEncoder>(new FVideoEncoderAmf_H264());
				});
			}
		}
	}

	FVideoEncoderAmf_H264::FVideoEncoderAmf_H264()
		: Amf(FAmfCommon::Setup())
	{
	}

	FVideoEncoderAmf_H264::~FVideoEncoderAmf_H264()
	{
		Shutdown();
	}

	bool FVideoEncoderAmf_H264::Setup(TSharedRef<FVideoEncoderInput> input, FLayerConfig const& config)
	{
		if (!Amf.GetIsAvailable())
		{
			UE_LOG(LogVideoEncoder, Error, TEXT("Amf not avaliable"));
			return false;
		}

		TSharedRef<FVideoEncoderInputImpl>	Input(StaticCastSharedRef<FVideoEncoderInputImpl>(input));

		// TODO fix initializing contexts
		FrameFormat = input->GetFrameFormat();
		switch (FrameFormat)
		{
#if PLATFORM_WINDOWS
		case AVEncoder::EVideoFrameFormat::D3D11_R8G8B8A8_UNORM:
			EncoderDevice = Input->ForceD3D11InputFrames();
			break;
		case AVEncoder::EVideoFrameFormat::D3D12_R8G8B8A8_UNORM:
			EncoderDevice = Input->ForceD3D11InputFrames(); // TODO use DX12 input when possible 
			if (!Amf.bIsCtxInitialized)
			{
				Amf.GetContext()->InitDX11(EncoderDevice);
				Amf.bIsCtxInitialized = true;
			}
			break;
#endif
#if PLATFORM_WINDOWS || PLATFORM_LINUX
		case AVEncoder::EVideoFrameFormat::VULKAN_R8G8B8A8_UNORM:
			EncoderDevice = Input->GetVulkanDevice();
			if (!Amf.bIsCtxInitialized)
			{
				AMFContext1Ptr(Amf.GetContext())->InitVulkan(EncoderDevice);
				Amf.bIsCtxInitialized = true;
			}
			break;
#endif
		case AVEncoder::EVideoFrameFormat::Undefined:
		default:
			UE_LOG(LogVideoEncoder, Error, TEXT("Frame format %s is not currently supported by Amf Encoder on this platform."), *ToString(FrameFormat));
			return false;
		}

		if (!EncoderDevice)
		{
			UE_LOG(LogVideoEncoder, Error, TEXT("Amf needs an encoder device."));
			return false;
		}

		auto mutableConfig = config;
		if (mutableConfig.MaxFramerate == 0)
			mutableConfig.MaxFramerate = 60;

		return AddLayer(config);
	}

	FVideoEncoder::FLayer* FVideoEncoderAmf_H264::CreateLayer(uint32 layerIdx, FLayerConfig const& config)
	{
		auto const layer = new FAMFLayer(layerIdx, config, *this);
		if (!layer->Setup())
		{
			delete layer;
			return nullptr;
		}
		return layer;
	}

	void FVideoEncoderAmf_H264::DestroyLayer(FLayer* layer)
	{
		delete layer;
	}

	void FVideoEncoderAmf_H264::Encode(FVideoEncoderInputFrame const* frame, FEncodeOptions const& options)
	{
		// todo: reconfigure encoder
		auto const amfFrame = static_cast<const FVideoEncoderInputFrameImpl*>(frame);
		for (auto&& layer : Layers)
		{
			auto const amfLayer = static_cast<FAMFLayer*>(layer);
			amfLayer->Encode(amfFrame, options);
		}
	}

	void FVideoEncoderAmf_H264::Flush()
	{
		for (auto&& layer : Layers)
		{
			auto const amfLayer = static_cast<FAMFLayer*>(layer);
			amfLayer->Flush();
		}
	}

	void FVideoEncoderAmf_H264::Shutdown()
	{
		for (auto&& layer : Layers)
		{
			auto const amfLayer = static_cast<FAMFLayer*>(layer);
			amfLayer->Shutdown();
			DestroyLayer(amfLayer);
		}
		Layers.Reset();
		StopEventThread();
	}

	void FVideoEncoderAmf_H264::OnEvent(void* InEvent, TUniqueFunction<void()>&& InCallback)
	{
#if PLATFORM_WINDOWS
		FScopeLock Guard(&ProtectEventThread);
		StartEventThread();
		EventThreadWaitingFor.Emplace(FWaitForEvent(InEvent, MoveTemp(InCallback)));
		SetEvent(EventThreadCheckEvent);
#else
		UE_LOG(LogVideoEncoder, Fatal, TEXT("FVideoEncoderAmf_H264::OnEvent should not be called as NVENC async mode only works on Windows!"))
#endif
	}

	// TODO (M84FIX) de-windows this
	void FVideoEncoderAmf_H264::StartEventThread()
	{
#if PLATFORM_WINDOWS
		bExitEventThread = false;
		if (!EventThread)
		{
			if (!EventThreadCheckEvent)
			{
				EventThreadCheckEvent = CreateEvent(nullptr, false, false, nullptr);
				//EventThreadCheckEvent = FPlatformProcess::GetSynchEventFromPool(false);
			}
			EventThread = MakeUnique<FThread>(TEXT("NVENC_EncoderCommon"), [this]() { EventLoop(); });
		}
#else
		UE_LOG(LogVideoEncoder, Fatal, TEXT("FVideoEncoderAmf_H264::StartEventThread should not be called as NVENC async mode only works on Windows!"))
#endif
	}

	void FVideoEncoderAmf_H264::StopEventThread()
	{
#if PLATFORM_WINDOWS
		FScopeLock				Guard(&ProtectEventThread);
		TUniquePtr<FThread>		StopThread = MoveTemp(EventThread);
		if (StopThread)
		{
			bExitEventThread = true;
			SetEvent(EventThreadCheckEvent);
			Guard.Unlock();
			StopThread->Join();
		}
#else
		UE_LOG(LogVideoEncoder, Fatal, TEXT("FVideoEncoderAmf_H264::StopEventThread should not be called as NVENC async mode only works on Windows!"))
#endif
	}

#if PLATFORM_WINDOWS
	void WindowsError(LPTSTR lpszFunction)
	{
		// Retrieve the system error message for the last-error code

		LPVOID lpMsgBuf;
		LPVOID lpDisplayBuf;
		DWORD dw = GetLastError();

		FormatMessage(
			FORMAT_MESSAGE_ALLOCATE_BUFFER |
			FORMAT_MESSAGE_FROM_SYSTEM |
			FORMAT_MESSAGE_IGNORE_INSERTS,
			NULL,
			dw,
			MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
			(LPTSTR)&lpMsgBuf,
			0, NULL);

		lpDisplayBuf = (LPVOID)LocalAlloc(LMEM_ZEROINIT, (lstrlen((LPCTSTR)lpMsgBuf) + lstrlen((LPCTSTR)lpszFunction) + 40) * sizeof(TCHAR));
		UE_LOG(LogVideoEncoder, Error, TEXT("%s failed with error %d: %s"), lpszFunction, dw, lpMsgBuf);

		LocalFree(lpMsgBuf);
		LocalFree(lpDisplayBuf);
	}
#endif

	// TODO (M84FIX) de-windows this
	void FVideoEncoderAmf_H264::EventLoop()
	{
#if PLATFORM_WINDOWS
		while (true)
		{
			// Make an empty array of events to wait for
			void* EventsToWaitFor[MAXIMUM_WAIT_OBJECTS];
			int				NumEventsToWaitFor = 0;

			{
				// Guard the thread and see if we should terminate
				FScopeLock		Guard(&ProtectEventThread);
				if (bExitEventThread)
				{
					break;
				}

				// collect events to wait for
				EventsToWaitFor[NumEventsToWaitFor++] = EventThreadCheckEvent;
				for (const FWaitForEvent& WaitFor : EventThreadWaitingFor)
				{
					check(NumEventsToWaitFor < MAXIMUM_WAIT_OBJECTS);
					EventsToWaitFor[NumEventsToWaitFor++] = WaitFor.Key;
				}
			}
			// wait for the events
			DWORD	WaitResult = WaitForMultipleObjects(NumEventsToWaitFor, EventsToWaitFor, false, INFINITE);
			if (WaitResult >= WAIT_OBJECT_0 && WaitResult < (WAIT_OBJECT_0 + NumEventsToWaitFor))
			{
				// Guard the thread and get the event that was triggered
				FScopeLock		Guard(&ProtectEventThread);
				void* EventTriggered = EventsToWaitFor[WaitResult - WAIT_OBJECT_0];

				// loop through the the events then callback on the one that was triggered (could this be better as a)
				for (int32 Index = 0; Index < EventThreadWaitingFor.Num(); ++Index)
				{
					if (EventThreadWaitingFor[Index].Key == EventTriggered)
					{
						TUniqueFunction<void()>	Callback = MoveTemp(EventThreadWaitingFor[Index].Value);
						EventThreadWaitingFor.RemoveAtSwap(Index);
						Guard.Unlock();
						Callback();
						break;
					}
				}
			}
			else if (WaitResult >= WAIT_ABANDONED_0 && WaitResult < (WAIT_ABANDONED_0 + NumEventsToWaitFor))
			{
			}
			else if (WaitResult == WAIT_TIMEOUT)
			{
			}
			else if (WaitResult == WAIT_FAILED)
			{
				const FString WaitError(TEXT("WaitForMultipleObjects"));
				WindowsError((LPTSTR)*WaitError);
			}
		}
#else
		UE_LOG(LogVideoEncoder, Fatal, TEXT("Amf_EncoderH264::EventLoop should not be called as NVENC async mode only works on Windows!"))
#endif
	}

	// --- Amf_EncoderH264::FLayer ------------------------------------------------------------
	FVideoEncoderAmf_H264::FAMFLayer::FAMFLayer(uint32 layerIdx, FLayerConfig const& config, FVideoEncoderAmf_H264& encoder)
		: FLayer(config)
		, Encoder(encoder)
		, Amf(FAmfCommon::Setup())
		, CodecGUID()
		, LayerIndex(layerIdx)
	{
	}

	FVideoEncoderAmf_H264::FAMFLayer::~FAMFLayer()
	{
	}

	bool FVideoEncoderAmf_H264::FAMFLayer::Setup()
	{
		return CreateSession() && CreateInitialConfig();
	}

	bool FVideoEncoderAmf_H264::FAMFLayer::CreateSession()
	{
		if (AmfEncoder == NULL)
		{
			Amf.CreateEncoder(AmfEncoder);

			// TODO maybe setup memory usage here though it looks like Amf doesnt need to have this pre-configured
		}

		return AmfEncoder != NULL;
	}

	// TODO (M84FIX) need to parameterize this
	bool FVideoEncoderAmf_H264::FAMFLayer::CreateInitialConfig()
	{
		AMF_RESULT Result = AMF_OK;

		// TODO this might be a double up
		Result = AmfEncoder->SetProperty(AMF_VIDEO_ENCODER_USAGE, AMF_VIDEO_ENCODER_USAGE_ULTRA_LOW_LATENCY);
		Result = AmfEncoder->SetProperty(AMF_VIDEO_ENCODER_LOWLATENCY_MODE, true);

		// TODO set to baseline to match NVENC this seems to allow the most devices to decode
		Result = AmfEncoder->SetProperty(AMF_VIDEO_ENCODER_PROFILE, AMF_VIDEO_ENCODER_PROFILE_BASELINE);
		// Result = AmfEncoder->SetProperty(AMF_VIDEO_ENCODER_PROFILE_LEVEL, 51);

		Result = AmfEncoder->SetProperty(AMF_VIDEO_ENCODER_FRAMERATE, CurrentConfig.MaxFramerate);

		Result = AmfEncoder->SetProperty(AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD, ConvertRateControlModeAMF(CurrentConfig.RateControlMode));

		// TODO will this do the same things as RcParams.enableMinQP/enableMaxQP = false in NVENC?
		Result = AmfEncoder->SetProperty(AMF_VIDEO_ENCODER_MIN_QP, CurrentConfig.QPMin == -1 ? 0 : CurrentConfig.QPMin);
		Result = AmfEncoder->SetProperty(AMF_VIDEO_ENCODER_MAX_QP, CurrentConfig.QPMax == -1 ? 51 : CurrentConfig.QPMax);

		Result = AmfEncoder->SetProperty(AMF_VIDEO_ENCODER_PEAK_BITRATE, CurrentConfig.MaxBitrate > -1 ? CurrentConfig.MaxBitrate : DEFAULT_BITRATE);
		Result = AmfEncoder->SetProperty(AMF_VIDEO_ENCODER_TARGET_BITRATE, CurrentConfig.TargetBitrate > -1 ? CurrentConfig.TargetBitrate : DEFAULT_BITRATE);

		// TODO Amf enables B frames by default NVENC does not should test both options
		Result = AmfEncoder->SetProperty(AMF_VIDEO_ENCODER_B_PIC_PATTERN, 0);

		Result = AmfEncoder->Init(AMF_SURFACE_YUV420P, CurrentConfig.Width, CurrentConfig.Height);

		return Result == AMF_OK;
	}

	void FVideoEncoderAmf_H264::FAMFLayer::MaybeReconfigure(TSharedPtr<FInputOutput> buffer)
	{
		FScopeLock lock(&ConfigMutex);
		if (NeedsReconfigure)
		{
			// TODO this is likely to work differently in Amf and should be changed accordingly
			// currently it mirrors NVENC to have paridy

			// Changing the framerate forces the generation of a new keyframe so we only do it when either:
			// 1) we need a keyframe anyway
			// 2) there is a big difference in framerate
			// 3) the framerate has changed and we haven't sent a Keyframe recently
			AMFRate CurrentEncoderFramerate;
			AmfEncoder->GetProperty(AMF_VIDEO_ENCODER_FRAMERATE, &CurrentEncoderFramerate);
			uint32 const FrameRateDiff = CurrentConfig.MaxFramerate > CurrentEncoderFramerate.num ? CurrentConfig.MaxFramerate - CurrentEncoderFramerate.num : CurrentEncoderFramerate.num - CurrentConfig.MaxFramerate;

			if (bForceNextKeyframe ||
				FrameRateDiff > MAX_FRAMERATE_DIFF ||
				(CurrentConfig.MaxFramerate != CurrentEncoderFramerate.num && (FDateTime::UtcNow() - LastKeyFrameTime).GetSeconds() > MIN_UPDATE_FRAMERATE_SECS))
			{
				if (AmfEncoder->SetProperty(AMF_VIDEO_ENCODER_FRAMERATE, CurrentConfig.MaxFramerate) != AMF_OK)
				{
					UE_LOG(LogVideoEncoder, Error, TEXT("Amf failed to set encoder Framerate"));
				}

				if (buffer->Surface->SetProperty(AMF_VIDEO_ENCODER_FORCE_PICTURE_TYPE, AMF_VIDEO_ENCODER_PICTURE_TYPE_IDR) != AMF_OK)
				{
					UE_LOG(LogVideoEncoder, Error, TEXT("Amf failed to force IDR picture type"));
				}
			}
			bForceNextKeyframe = false;

			if (AmfEncoder->SetProperty(AMF_VIDEO_ENCODER_FILLER_DATA_ENABLE, CurrentConfig.FillData) != AMF_OK)
			{
				UE_LOG(LogVideoEncoder, Error, TEXT("Amf failed to enable filler data to maintain CBR"));
			}

			if (AmfEncoder->SetProperty(AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD, ConvertRateControlModeAMF(CurrentConfig.RateControlMode)) != AMF_OK)
			{
				UE_LOG(LogVideoEncoder, Error, TEXT("Amf failed to set rate control method"));
			}

			if (AmfEncoder->SetProperty(AMF_VIDEO_ENCODER_PEAK_BITRATE, CurrentConfig.MaxBitrate) != AMF_OK)
			{
				UE_LOG(LogVideoEncoder, Error, TEXT("Amf failed to set max bitrate"));
			}

			// multi pass?

			if (AmfEncoder->SetProperty(AMF_VIDEO_ENCODER_MIN_QP, CurrentConfig.QPMin) != AMF_OK)
			{
				UE_LOG(LogVideoEncoder, Error, TEXT("Amf failed to set min qp"));
			}
			
			if (AmfEncoder->SetProperty(AMF_VIDEO_ENCODER_TARGET_BITRATE, CurrentConfig.TargetBitrate) != AMF_OK)
			{
				UE_LOG(LogVideoEncoder, Error, TEXT("Amf failed to set target bitrate"));
			}

			// Bitrate generated by NVENC is not very stable when the scene doesnt have a lot of movement
			// outputPictureTimingSEI enables the filling data when using CBR so that the bitrate generated
			// is much closer to the requested one and bandwidth estimation algorithms can work better.
			// Otherwise in a static scene it can send 50kbps when configuring 300kbps and it will never ramp up.
			if (CurrentConfig.TargetBitrate < 5000000)
			{
				if (AmfEncoder->SetProperty(AMF_VIDEO_ENCODER_MIN_QP, 0) != AMF_OK)
				{
					UE_LOG(LogVideoEncoder, Error, TEXT("Amf failed to disable min qp"));
				}
			}
			else
			{
				if (AmfEncoder->SetProperty(AMF_VIDEO_ENCODER_FILLER_DATA_ENABLE, true) != AMF_OK)
				{
					UE_LOG(LogVideoEncoder, Error, TEXT("Amf failed to enable filler data to maintain CBR"));
				}

				if (AmfEncoder->SetProperty(AMF_VIDEO_ENCODER_MIN_QP, 20) != AMF_OK)
				{
					UE_LOG(LogVideoEncoder, Error, TEXT("Amf failed to set min qp to 20"));
				}
			}

			NeedsReconfigure = false;
		}
	}

	void FVideoEncoderAmf_H264::FAMFLayer::Encode(FVideoEncoderInputFrameImpl const* frame, FEncodeOptions const& options)
	{
		TSharedPtr<FInputOutput> Buffer = GetOrCreateSurface(frame);

		if (Buffer)
		{
			Buffer->Surface->SetPts(FTimespan::FromSeconds(FPlatformTime::Seconds()).GetTicks());

			bForceNextKeyframe = options.bForceKeyFrame;
			MaybeReconfigure(Buffer);

			if (Buffer.IsValid())
			{
				AMF_RESULT Result = AmfEncoder->SubmitInput(Buffer->Surface);

				if (Result == AMF_NEED_MORE_INPUT)
				{
				}
				else if (Result != AMF_OK)
				{
					UE_LOG(LogVideoEncoder, Error, TEXT("Amf submit error with %d"), Result);
					// release input frame
					if (Buffer->SourceFrame)
					{
						Buffer->SourceFrame->Release();
					}
				}
			}
		}
	}

	void FVideoEncoderAmf_H264::FAMFLayer::Flush()
	{
		AmfEncoder->Flush();
	}

	void FVideoEncoderAmf_H264::FAMFLayer::Shutdown()
	{
		Flush();
		if (AmfEncoder != NULL)
		{
			AmfEncoder->Terminate();
			AmfEncoder = NULL;
		}
	}

	void FVideoEncoderAmf_H264::FAMFLayer::ProcessNextPendingFrame()
	{
		if (Encoder.OnEncodedPacket)
		{
			while (true)
			{
				amf::AMFDataPtr data;
				AMF_RESULT Result = AmfEncoder->QueryOutput(&data);

				if (Result == AMF_OK && data != NULL)
				{
					AMFBufferPtr OutBuffer(data);

					// create packet with buffer contents
					FCodecPacketImpl	Packet;

					Packet.PTS = data->GetPts();
					Packet.Data = static_cast<const uint8*>(OutBuffer->GetNative());
					Packet.DataSize = OutBuffer->GetSize();
					uint32 PictureType = AMF_VIDEO_ENCODER_PICTURE_TYPE_NONE;
					if (OutBuffer->GetProperty(AMF_VIDEO_ENCODER_FORCE_PICTURE_TYPE, &PictureType) != AMF_OK)
					{
						UE_LOG(LogVideoEncoder, Fatal, TEXT("Amf failed to get picture type."));
					}
					else if (PictureType == AMF_VIDEO_ENCODER_PICTURE_TYPE_IDR)
					{
						UE_LOG(LogVideoEncoder, Verbose, TEXT("Generated IDR Frame"));
						Packet.IsKeyFrame = true;
					}

					if (OutBuffer->GetProperty(AMF_VIDEO_ENCODER_STATISTIC_AVERAGE_QP, &Packet.VideoQP) != AMF_OK)
					{
						UE_LOG(LogVideoEncoder, Fatal, TEXT("Amf failed to get average QP."));
					}

					amf_int64 StartTs;
					if (OutBuffer->GetProperty(AMF_VIDEO_ENCODER_START_TS, &StartTs) != AMF_OK)
					{
						UE_LOG(LogVideoEncoder, Fatal, TEXT("Amf failed to get average QP."));
					}
					Packet.Timings.StartTs = StartTs;

					Packet.Timings.FinishTs = FTimespan::FromSeconds(FPlatformTime::Seconds());
					Packet.Framerate = CurrentConfig.MaxFramerate;

					Encoder.OnEncodedPacket(LayerIndex, nullptr, Packet); // TODO probs should not be null might need a refactor to instead pass the size as thats what its needed for
				}
				else
				{
					break;
				}
			}
		}
	}

	template<class T>
	bool FVideoEncoderAmf_H264::FAMFLayer::GetCapability(const TCHAR* CapToQuery, T& OutCap) const
	{
		amf::AMFCapsPtr EncoderCaps;
		
		if (AmfEncoder->GetCaps(&EncoderCaps) != AMF_OK)
		{
			return false;
		}

		if (EncoderCaps->GetProperty(CapToQuery, &OutCap))
		{
			return false;
		}

		return true;
	}

	TSharedPtr<FVideoEncoderAmf_H264::FAMFLayer::FInputOutput> FVideoEncoderAmf_H264::FAMFLayer::GetOrCreateSurface(const FVideoEncoderInputFrameImpl* InFrame)
	{
		void* TextureToCompress = nullptr;

		switch (InFrame->GetFormat())
		{
#if PLATFORM_WINDOWS
		case EVideoFrameFormat::D3D11_R8G8B8A8_UNORM:
			TextureToCompress = InFrame->GetD3D11().EncoderTexture;
			break;
		case EVideoFrameFormat::D3D12_R8G8B8A8_UNORM:
			TextureToCompress = InFrame->GetD3D12().EncoderTexture;
			break;
#endif
		case EVideoFrameFormat::VULKAN_R8G8B8A8_UNORM:
			TextureToCompress = InFrame->GetVulkan().EncoderTexture;
			break;
		case EVideoFrameFormat::Undefined:
		default:
			UE_LOG(LogVideoEncoder, Error, TEXT("Video Frame Format %s not supported by Amf on this platform."), *ToString(InFrame->GetFormat()));
			break;
		}

		if (!TextureToCompress)
		{
			UE_LOG(LogVideoEncoder, Fatal, TEXT("Got passed a null pointer."));
				return nullptr;
		}

		// Check if texture already has a buffer surface
		TSharedPtr<FInputOutput> Buffer = nullptr;
		for (TSharedPtr<FInputOutput> SearchBuffer : CreatedSurfaces)
		{
			if (SearchBuffer->TextureToCompress == TextureToCompress)
			{
				Buffer = SearchBuffer;
				break;
			}
		}

		// if texture does not already have a buffer surface create one
		if (!Buffer)
		{
			if (!CreateSurface(Buffer, InFrame, TextureToCompress))
			{
				InFrame->Release();
				UE_LOG(LogVideoEncoder, Error, TEXT("Amf failed to create buffer."));
			}
			else
			{
				CreatedSurfaces.Push(Buffer);
			}
		}

		return Buffer;
	}

	bool FVideoEncoderAmf_H264::FAMFLayer::CreateSurface(TSharedPtr<FVideoEncoderAmf_H264::FAMFLayer::FInputOutput>& OutBuffer, const FVideoEncoderInputFrameImpl* SourceFrame, void* TextureToCompress)
	{
		AMF_RESULT Result = AMF_OK;

		OutBuffer = MakeShared<FInputOutput>();
		OutBuffer->SourceFrame = SourceFrame;
		OutBuffer->TextureToCompress = TextureToCompress;

		if (TextureToCompress)
		{
			switch (SourceFrame->GetFormat())
			{
#if PLATFORM_WINDOWS
			case EVideoFrameFormat::D3D11_R8G8B8A8_UNORM:
				Result = Amf.GetContext()->CreateSurfaceFromDX11Native(TextureToCompress, &(OutBuffer->Surface), OutBuffer.Get());
				break;
			case EVideoFrameFormat::D3D12_R8G8B8A8_UNORM:
				Result = amf::AMFContext2Ptr(Amf.GetContext())->CreateSurfaceFromDX12Native(TextureToCompress, &(OutBuffer->Surface), OutBuffer.Get());
				break;
#endif
			case EVideoFrameFormat::VULKAN_R8G8B8A8_UNORM:
				Result = amf::AMFContext2Ptr(Amf.GetContext())->CreateSurfaceFromVulkanNative(TextureToCompress, &(OutBuffer->Surface), OutBuffer.Get());
				break;
			case EVideoFrameFormat::Undefined:
			default:
				UE_LOG(LogVideoEncoder, Error, TEXT("Video format %s not inplemented for Amf on this platform"), *ToString(SourceFrame->GetFormat()));
				break;
			}
		}
		else
		{
			UE_LOG(LogVideoEncoder, Error, TEXT("Amf recieved nullptr to texture."));
			return false;
		}

		return Result == AMF_OK;
	}


	static void* CreateEncoderSession(FAmfCommon& Amf)
	{
		void* EncoderSession = nullptr;
		return EncoderSession;
	}

	static bool GetEncoderSupportedProfiles(AMFCapsPtr EncoderCaps, uint32& OutSupportedProfiles)
	{
		uint32 maxProfile;
		if (EncoderCaps->GetProperty(AMF_VIDEO_ENCODER_CAP_MAX_PROFILE, &maxProfile) != AMF_OK)
		{
			return false;
		}

		if (maxProfile >= AMF_VIDEO_ENCODER_PROFILE_BASELINE)
		{
			OutSupportedProfiles |= H264Profile_Baseline;
		}

		if (maxProfile >= AMF_VIDEO_ENCODER_PROFILE_MAIN)
		{
			OutSupportedProfiles |= H264Profile_Main;
		}

		if (maxProfile >= AMF_VIDEO_ENCODER_PROFILE_HIGH)
		{
			OutSupportedProfiles |= H264Profile_High;
		}

		if (maxProfile >= AMF_VIDEO_ENCODER_PROFILE_CONSTRAINED_BASELINE)
		{
			OutSupportedProfiles |= H264Profile_ConstrainedBaseline;
		}

		if (maxProfile >= AMF_VIDEO_ENCODER_PROFILE_CONSTRAINED_HIGH)
		{
			OutSupportedProfiles |= H264Profile_ConstrainedHigh;
		}

		return true;
	}

	static bool GetEncoderSupportedInputFormats(AMFIOCapsPtr IOCaps, TArray<EVideoFrameFormat>& OutSupportedInputFormats)
	{
		// TODO check if we actually need to query Amf for this

#if PLATFORM_WINDOWS
		OutSupportedInputFormats.Push(EVideoFrameFormat::D3D11_R8G8B8A8_UNORM);
		OutSupportedInputFormats.Push(EVideoFrameFormat::D3D12_R8G8B8A8_UNORM);
#endif
		OutSupportedInputFormats.Push(EVideoFrameFormat::VULKAN_R8G8B8A8_UNORM);

		return true;
	}


	static bool GetEncoderInfo(FAmfCommon& AMF, FVideoEncoderInfo& EncoderInfo)
	{
		bool bSuccess = true;
		// create a temporary encoder component
		AMFComponentPtr AmfEncoder = nullptr;

		{
			FString RHIName = FString(GDynamicRHI->GetName());
			void* EncoderDevice = GDynamicRHI->RHIGetNativeDevice();

			// TODO replace GDynamicRHI with a passed in handle to the RHI Device so that this can be used agnostically
#if PLATFORM_WINDOWS
			if (RHIName == FString("D3D12"))
			{
				AMFContext2Ptr(AMF.GetContext())->InitDX12(EncoderDevice);
			}

			if (RHIName == FString("D3D11"))
			{
				AMF.GetContext()->InitDX11(EncoderDevice);
			}
#endif			
			if (RHIName == FString("Vulkan"))
			{
				AMFContext1Ptr(AMF.GetContext())->InitVulkan(EncoderDevice);
			}

			if (EncoderDevice == nullptr || !AMF.CreateEncoder(AmfEncoder))
			{
				UE_LOG(LogVideoEncoder, Error, TEXT("RHI not intitialized or not supported by Amf on this platform."));
				return false;
			}
		}

		EncoderInfo.CodecType = ECodecType::H264;

		AMFCapsPtr EncoderCaps;
		AmfEncoder->GetCaps(&EncoderCaps);

		AMFIOCapsPtr InputCaps;
		EncoderCaps->GetInputCaps(&InputCaps);

		uint32 LevelMin = 1;
		uint32 LevelMax;
		if (EncoderCaps->GetProperty(AMF_VIDEO_ENCODER_CAP_MAX_LEVEL, &LevelMax) == AMF_OK)
		{
			EncoderInfo.H264.MinLevel = (LevelMin > 9) ? LevelMin : 9;							// Like NVENC we hard min at 9
			EncoderInfo.H264.MaxLevel = (LevelMax < 9) ? 9 : (LevelMax > 52) ? 52 : LevelMax;	// Like NVENC we hard min at 52
		}
		else
		{
			UE_LOG(LogVideoEncoder, Error, TEXT("Failed to query min/max h264 level supported by Amf (reported min/max=%d/%d)."), LevelMin, LevelMax);
			bSuccess = false;
		}

		if (!GetEncoderSupportedProfiles(EncoderCaps, EncoderInfo.H264.SupportedProfiles) ||
			!GetEncoderSupportedInputFormats(InputCaps, EncoderInfo.SupportedInputFormats))
		{
			bSuccess = false;
		}

		// destroy encoder session
		AmfEncoder->Terminate();

		return bSuccess;
	}

} /* namespace AVEncoder */

#undef MIN_UPDATE_FRAMERATE_SECS