// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Amf_Common.h"

#include "HAL/Thread.h"
#include "HAL/Event.h"

#include "VideoEncoderFactory.h"
#include "VideoEncoderInputImpl.h"

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
			void UpdateConfig();

			template<class T> 
			bool GetProperty(const TCHAR* PropertyToQuery, T& outProperty, const T& (*func)(const AMFVariantStruct*)) const;

			template<class T>
			bool GetCapability(const TCHAR* CapToQuery, T& OutCap) const;

			AMF_RESULT Encode(FVideoEncoderInputFrameImpl const* frame, FEncodeOptions const& options);
			void Flush();
			void Shutdown();
			void UpdateBitrate(uint32 InMaxBitRate, uint32 InTargetBitRate);
			void UpdateResolution(uint32 InMaxBitRate, uint32 InTargetBitRate);

			FVideoEncoderAmf_H264& Encoder;
			FAmfCommon& Amf;
			uint32 LayerIndex;
			AMFComponentPtr AmfEncoder = NULL;
			FThreadSafeCounter PendingFrames;
			FDateTime LastKeyFrameTime = 0;
			bool bForceNextKeyframe = false;

			class FInputOutput
			{
			public:
				void* TextureToCompress;
				AMFSurfacePtr Surface;
			};

			void MaybeReconfigure();

			TSharedPtr<FInputOutput> GetOrCreateSurface(const FVideoEncoderInputFrameImpl* InFrame);
			bool CreateSurface(TSharedPtr<FInputOutput>& OutBuffer, const FVideoEncoderInputFrameImpl* SourceFrame, void* TextureToCompress);

			TArray<TSharedPtr<FInputOutput>> CreatedSurfaces;
			FThreadSafeBool bUpdateConfig = false;

			uint32 CurrentWidth;
			uint32 CurrentHeight;
			uint32 CurrentFrameRate;

			void ProcessFrameThreadFunc();
			TUniquePtr<FThread> ProcessFrameThread;
			FThreadSafeBool bShouldRunProcessingThread = true;
			FThreadSafeBool bWaitingForFrames = true;
			FEventRef FramesPending;
		};

		FAmfCommon& Amf;
		EVideoFrameFormat FrameFormat = EVideoFrameFormat::Undefined;
		void* EncoderDevice;

		uint32 MaxFramerate = 0;
		amf_int64 MinQP = -1;
		RateControlMode RateMode = RateControlMode::CBR;
		bool FillData = false;
	};
}
