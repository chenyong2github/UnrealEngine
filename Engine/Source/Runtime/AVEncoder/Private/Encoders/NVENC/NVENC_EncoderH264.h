// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NVENC_Common.h"

#if PLATFORM_DESKTOP && !PLATFORM_APPLE

#include "VideoEncoderFactory.h"
#include "VideoEncoderInputImpl.h"

namespace AVEncoder
{
    class FVideoEncoderNVENC_H264 : public FVideoEncoder
    {
    public:
        virtual ~FVideoEncoderNVENC_H264() override;

        // query whether or not encoder is supported and available
        static bool GetIsAvailable(FVideoEncoderInputImpl &InInput, FVideoEncoderInfo &OutEncoderInfo);

        // register encoder with video encoder factory
        static void Register(FVideoEncoderFactory &InFactory);

        bool Setup(TSharedRef<FVideoEncoderInput> input, FLayerConfig const& config) override;
        void Encode(FVideoEncoderInputFrame const* frame, FEncodeOptions const& options) override;
        void Flush();
        void Shutdown() override;

    protected:
        FLayer *CreateLayer(uint32 InLayerIndex, const FLayerConfig &InLayerConfig) override;
		void DestroyLayer(FLayer* layer) override;

    private:
        FVideoEncoderNVENC_H264();

        class FNVENCLayer : public FLayer
        {
        public:
            FNVENCLayer(uint32 layerIdx, FLayerConfig const& config, FVideoEncoderNVENC_H264& encoder);
            ~FNVENCLayer();

            bool Setup();
            bool CreateSession();
            bool CreateInitialConfig();
            int GetCapability(NV_ENC_CAPS CapsToQuery) const;
            FString GetError(NVENCSTATUS ForStatus) const;
			void MaybeReconfigure();
			void UpdateConfig();
            void Encode(FVideoEncoderInputFrameImpl const* frame, FEncodeOptions const& options);
            void Flush();
            void Shutdown();
            void UpdateBitrate(uint32 InMaxBitRate, uint32 InTargetBitRate);
            void UpdateResolution(uint32 InMaxBitRate, uint32 InTargetBitRate);

            FVideoEncoderNVENC_H264 &Encoder;
            FNVENCCommon &NVENC;
            GUID CodecGUID;
            uint32 LayerIndex;
            void *NVEncoder = nullptr;
            NV_ENC_INITIALIZE_PARAMS EncoderInitParams;
            NV_ENC_CONFIG EncoderConfig;
            bool bAsyncMode = false;
            FDateTime LastKeyFrameTime = 0;
			bool bForceNextKeyframe = false;

            struct FInputOutput
            {
                const FVideoEncoderInputFrameImpl *SourceFrame = nullptr;

                void*  InputTexture = nullptr;
                uint32 Width = 0;
                uint32 Height = 0;
                uint32 Pitch = 0;
                NV_ENC_BUFFER_FORMAT BufferFormat = NV_ENC_BUFFER_FORMAT_UNDEFINED;
                NV_ENC_REGISTERED_PTR RegisteredInput = nullptr;
                NV_ENC_INPUT_PTR MappedInput = nullptr;

                NV_ENC_OUTPUT_PTR OutputBitstream = nullptr;
                const void *BitstreamData = nullptr;
                uint32 BitstreamDataSize = 0;
                void *CompletionEvent = nullptr;
                NV_ENC_PIC_TYPE PictureType = NV_ENC_PIC_TYPE_UNKNOWN;
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

            void CreateResourceDIRECTX(FInputOutput &InBuffer, NV_ENC_REGISTER_RESOURCE &RegisterParam, FIntPoint TextureSize);
            void CreateResourceCUDAARRAY(FInputOutput &InBuffer, NV_ENC_REGISTER_RESOURCE &RegisterParam, FIntPoint TextureSize);

            TArray<FInputOutput *> CreatedBuffers;
            TQueue<FInputOutput *> PendingEncodes;
            FCriticalSection ProtectedWaitingForPending;
            bool WaitingForPendingActive = false;
        };

        FNVENCCommon &NVENC;
        EVideoFrameFormat FrameFormat = EVideoFrameFormat::Undefined;

        // TODO (M84FIX) make this neater
#if PLATFORM_WINDOWS
        TRefCountPtr<ID3D11Device> EncoderDevice;
#elif WITH_CUDA
        CUcontext EncoderDevice;
#else
        void* EncoderDevice;
#endif

        // event thread for nvenc async (windows only)
        void OnEvent(void *InEvent, TUniqueFunction<void()> &&InCallback);
        void StartEventThread();
        void StopEventThread();
        void EventLoop();

        TUniquePtr<FThread> EventThread;
        FCriticalSection ProtectEventThread;
        bool bExitEventThread = false;

        void* EventThreadCheckEvent = nullptr;

        using FWaitForEvent = TPair<void *, TUniqueFunction<void()>>;
        TArray<FWaitForEvent> EventThreadWaitingFor;
    };

}
#endif // PLATFORM_DESKTOP && !PLATFORM_APPLE