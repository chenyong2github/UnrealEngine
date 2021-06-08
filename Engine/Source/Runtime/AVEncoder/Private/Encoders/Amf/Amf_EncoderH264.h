// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Amf_Common.h"

#if PLATFORM_DESKTOP && !PLATFORM_APPLE

#include "VideoEncoderFactory.h"
#include "VideoEncoderInputImpl.h"

THIRD_PARTY_INCLUDES_START

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include "Windows/PreWindowsApi.h"
#endif

#include <nvEncodeAPI.h> // To get definition of GUID

#if PLATFORM_WINDOWS
#include "Windows/PostWindowsApi.h"
#include "Windows/HideWindowsPlatformTypes.h"
#endif

THIRD_PARTY_INCLUDES_END

namespace AVEncoder
{
	using namespace amf;

    class FVideoEncoderAmf_H264 : public FVideoEncoder
    {
    public:
        virtual ~FVideoEncoderAmf_H264() override;

        // query whether or not encoder is supported and available
        static bool GetIsAvailable(FVideoEncoderInputImpl &InInput, FVideoEncoderInfo &OutEncoderInfo);

        // register encoder with video encoder factory
        static void Register(FVideoEncoderFactory &InFactory);

        bool Setup(TSharedRef<FVideoEncoderInput> input, FLayerConfig const& config) override;
        void Encode(FVideoEncoderInputFrame const* frame, FEncodeOptions const& options) override;
        void Flush();
        void Shutdown() override;

    protected:
        FLayer* CreateLayer(uint32 layerIdx, FLayerConfig const& config) override;
        void DestroyLayer(FLayer *layer) override;

    private:
        FVideoEncoderAmf_H264();

		class FAMFLayer : public FLayer
		{
		public:
			FAMFLayer(uint32 layerIdx, FLayerConfig const& config, FVideoEncoderAmf_H264& encoder);
			~FAMFLayer();

			bool Setup();
			bool CreateSession();
			bool CreateInitialConfig();

			template<class T> 
			bool GetProperty(const TCHAR* PropertyToQuery, T& outProperty, const T& (*func)(const AMFVariantStruct*)) const;

			template<class T>
			bool GetCapability(const TCHAR* CapToQuery, T& OutCap) const;

			void Encode(FVideoEncoderInputFrameImpl const* frame, FEncodeOptions const& options);
			void Flush();
			void Shutdown();
			void UpdateBitrate(uint32 InMaxBitRate, uint32 InTargetBitRate);
			void UpdateResolution(uint32 InMaxBitRate, uint32 InTargetBitRate);

			FVideoEncoderAmf_H264& Encoder;
			FAmfCommon& Amf;
			GUID CodecGUID;
			uint32 LayerIndex;
			AMFComponentPtr AmfEncoder;
			bool bAsyncMode = true;
			FDateTime LastKeyFrameTime = 0;
			bool bForceNextKeyframe = false;

			class FInputOutput : public AMFSurfaceObserver
			{
			public:
				virtual void AMF_STD_CALL OnSurfaceDataRelease(AMFSurface* pSurface) override
				{
					SourceFrame->Release();
				}

				virtual ~FInputOutput()
				{
					SourceFrame->Release();
					Surface->Release();
				}

				const FVideoEncoderInputFrameImpl* SourceFrame;
				void* TextureToCompress;
				AMFSurfacePtr Surface;
			};

			void MaybeReconfigure(TSharedPtr<FInputOutput> buffer);

			TSharedPtr<FInputOutput> GetOrCreateSurface(const FVideoEncoderInputFrameImpl* InFrame);
			bool CreateSurface(TSharedPtr<FInputOutput>& OutBuffer, const FVideoEncoderInputFrameImpl* SourceFrame, void* TextureToCompress);

			void ProcessNextPendingFrame();

			TArray<TSharedPtr<FInputOutput>> CreatedSurfaces;
			FCriticalSection ProtectedWaitingForPending;
			bool WaitingForPendingActive = false;
			FThreadSafeBool bUpdateConfig = false;
		};

		FAmfCommon& Amf;
		EVideoFrameFormat FrameFormat = EVideoFrameFormat::Undefined;
		void* EncoderDevice;

		uint32 MaxFramerate = 0;
		int32 MinQP = -1;
		RateControlMode RateMode = RateControlMode::CBR;
		bool FillData = false;

		// event thread for amf async (supported on all platforms)
		void OnEvent(void* InEvent, TUniqueFunction<void()>&& InCallback);
		void StartEventThread();
		void StopEventThread();
		void EventLoop();

		TUniquePtr<FThread> EventThread;
		FCriticalSection ProtectEventThread;
		bool bExitEventThread = false;

		void* EventThreadCheckEvent = nullptr;

		using FWaitForEvent = TPair<void*, TUniqueFunction<void()>>;
		TArray<FWaitForEvent> EventThreadWaitingFor;
    };
}
#endif // PLATFORM_DESKTOP && !PLATFORM_APPLE
