// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NVENC_Common.h"

#include "VideoEncoderFactory.h"
#include "VideoEncoderInputImpl.h"

namespace AVEncoder
{
	class FVideoEncoderNVENC_H264 : public FVideoEncoder
	{
	public:
		~FVideoEncoderNVENC_H264();

		// query whether or not encoder is supported and available
		static bool GetIsAvailable(FVideoEncoderInputImpl &InInput, FVideoEncoderInfo &OutEncoderInfo);

		// register encoder with video encoder factory
		static void Register(FVideoEncoderFactory &InFactory);

		// create instance of video encoder
		static TUniquePtr<FVideoEncoderNVENC_H264> Create(FVideoEncoderInputImpl &InInput);

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
		FVideoEncoderNVENC_H264();

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

			FVideoEncoderNVENC_H264 &Encoder;
			FNVENCCommon &NVENC;
			GUID CodecGUID;
			uint32 LayerIndex;
			void *NVEncoder = nullptr;
			NV_ENC_INITIALIZE_PARAMS EncoderInitParams;
			NV_ENC_CONFIG EncoderConfig;
			bool bAsyncMode = false;
			uint32 MaxFramerate = -1;
			FDateTime LastKeyFrameTime = 0;

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
			FThreadSafeBool bUpdateConfig = false;
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

		uint32 MaxFramerate = 0;

		// event thread for nvenc async (windows only)
		void OnEvent(void *InEvent, TUniqueFunction<void()> &&InCallback);
		void StartEventThread();
		void StopEventThread();
		void EventLoop();

		TUniquePtr<FThread> EventThread;
		FCriticalSection ProtectEventThread;
		bool bExitEventThread = false;

		void *EventThreadCheckEvent = nullptr;

		using FWaitForEvent = TPair<void *, TUniqueFunction<void()>>;
		TArray<FWaitForEvent> EventThreadWaitingFor;
	};

}