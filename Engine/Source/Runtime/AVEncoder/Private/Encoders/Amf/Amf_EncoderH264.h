// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Amf_Common.h"

#include "VideoEncoderFactory.h"
#include "VideoEncoderInputImpl.h"

namespace AVEncoder
{
    class FVideoEncoderAmf_H264 : public FVideoEncoder
    {
    public:
        ~FVideoEncoderAmf_H264();

        // query whether or not encoder is supported and available
        static bool GetIsAvailable(FVideoEncoderInputImpl &InInput, FVideoEncoderInfo &OutEncoderInfo);

        // register encoder with video encoder factory
        static void Register(FVideoEncoderFactory &InFactory);

        // create instance of video encoder
        static TUniquePtr<FVideoEncoderAmf_H264> Create(FVideoEncoderInputImpl &InInput);

        bool Setup(TSharedRef<FVideoEncoderInput> InInput, const FInit &InInit) override;
        void Encode(const FVideoEncoderInputFrame *InFrame, const FEncodeOptions &InOptions) override;
        void Flush();
        void Shutdown() override;

        void UpdateFrameRate(uint32 InMaxFramerate) override;
        void UpdateLayerBitrate(uint32 InLayerIndex, uint32 InMaxBitRate, uint32 InTargetBitRate) override;
        void UpdateLayerResolution(uint32 InLayerIndex, uint32 InWidth, uint32 InHeight) override;

    protected:
        FLayerInfo *CreateLayer(uint32 InLayerIndex, const FLayerInfo &InLayerInfo) override;
        void DestroyLayer(FLayerInfo *InLayerInfo) override;

    private:
        FVideoEncoderAmf_H264();

        class FLayer : public FLayerInfo
		{
		public:
			FLayer(uint32 InLayerIndex, const FLayerInfo &InLayerInfo, FVideoEncoderNVENC_H264 &InEncoder);
			~FLayer();

			bool Setup();
			bool CreateSession();
			bool CreateInitialConfig();
			int GetCapability(NV_ENC_CAPS CapsToQuery) const;
			FString GetError(NVENCSTATUS ForStatus) const;
			void Encode(const FVideoEncoderInputFrameImpl *InFrame, const FEncodeOptions &InOptions);
			void Flush();
			void Shutdown();
			void UpdateBitrate(uint32 InMaxBitRate, uint32 InTargetBitRate);
			void UpdateResolution(uint32 InMaxBitRate, uint32 InTargetBitRate);

			FVideoEncoderAmf_H264 &Encoder;
			FAmfCommon &Amf;
			GUID CodecGUID;
			uint32 LayerIndex;
			void *NVEncoder = nullptr;

            // TODO Amf equivalents
			// NV_ENC_INITIALIZE_PARAMS EncoderInitParams;
			// NV_ENC_CONFIG EncoderConfig;
			
            bool bAsyncMode = false;
			uint32 MaxFramerate = -1;
			FDateTime LastKeyFrameTime = 0;

			struct FInputOutput
			{
				const FVideoEncoderInputFrameImpl *SourceFrame = nullptr;

				// HACK (M84FIX)
				void *InputTexture = nullptr;

				uint32 Width = 0;
				uint32 Height = 0;
				uint32 Pitch = 0;
                // TODO Amf equivalents 
				// NV_ENC_BUFFER_FORMAT BufferFormat = NV_ENC_BUFFER_FORMAT_UNDEFINED;
				// NV_ENC_REGISTERED_PTR RegisteredInput = nullptr;
				// NV_ENC_INPUT_PTR MappedInput = nullptr;
				// NV_ENC_OUTPUT_PTR OutputBitstream = nullptr;

				const void *BitstreamData = nullptr;
				uint32 BitstreamDataSize = 0;
				void *CompletionEvent = nullptr;
                
                // TODO Amf equivalents 
				// NV_ENC_PIC_TYPE PictureType = NV_ENC_PIC_TYPE_UNKNOWN;
				uint32 FrameAvgQP = 0;
				uint64 TimeStamp;
				FEvent *TriggerOnCompletion;

				FTimespan EncodeStartTs;
			};

			FInputOutput *GetOrCreateBuffer(const FVideoEncoderInputFrameImpl *InFrame);
			FInputOutput *CreateBuffer();
			void DestroyBuffer(FInputOutput *InBuffer);
			bool RegisterInputTexture(FInputOutput &InBuffer, void *InTexture, FIntPoint TextureSize);
			bool UnregisterInputTexture(FInputOutput &InBuffer);
			bool MapInputTexture(FInputOutput &InBuffer);
			bool UnmapInputTexture(FInputOutput &InBuffer);
			bool LockOutputBuffer(FInputOutput &InBuffer);
			bool UnlockOutputBuffer(FInputOutput &InBuffer);

			void ProcessNextPendingFrame();
			void WaitForNextPendingFrame();

			void CreateResourceDIRECTX(FInputOutput &InBuffer, /* NV_ENC_REGISTER_RESOURCE &RegisterParam, */ FIntPoint TextureSize);
			void CreateResourceVulkan(FInputOutput &InBuffer, /* NV_ENC_REGISTER_RESOURCE &RegisterParam, */ FIntPoint TextureSize);

			TArray<FInputOutput *> CreatedBuffers;
			TQueue<FInputOutput *> PendingEncodes;
			FCriticalSection ProtectedWaitingForPending;
			bool WaitingForPendingActive = false;
			FThreadSafeBool bUpdateConfig = false;
		};
    };
}