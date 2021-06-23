// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

namespace Electra {
	/**
	 * H265 video decoder class implementation.
	**/
	class FVideoDecoderH265 : public IVideoDecoderH265, public FMediaThread
	{
	public:
		static bool Startup(const FParamDict& Options);
		static void Shutdown();

		static bool GetStreamDecodeCapability(FStreamDecodeCapability& OutResult, const FStreamDecodeCapability& InStreamParameter);

		FVideoDecoderH265();
		virtual ~FVideoDecoderH265();

		void TestHardwareDecoding();

		virtual void SetPlayerSessionServices(IPlayerSessionServices* SessionServices) override;

		virtual void Open(const FInstanceConfiguration& InConfig) override;
		virtual void Close() override;
		virtual void DrainForCodecChange() override;

		virtual void SetMaximumDecodeCapability(int32 MaxTier, int32 MaxWidth, int32 MaxHeight, int32 MaxProfile, int32 MaxProfileLevel, const FParamDict& AdditionalOptions) override;

		virtual void SetAUInputBufferListener(IAccessUnitBufferListener* InListener) override;

		virtual void SetReadyBufferListener(IDecoderOutputBufferListener* InListener) override;

		virtual void SetRenderer(TSharedPtr<IMediaRenderer, ESPMode::ThreadSafe> InRenderer) override;

		virtual void SetResourceDelegate(const TSharedPtr<IVideoDecoderResourceDelegate, ESPMode::ThreadSafe>& ResourceDelegate) override;

		virtual IAccessUnitBufferInterface::EAUpushResult AUdataPushAU(FAccessUnit* InAccessUnit) override;
		virtual void AUdataPushEOD() override;
		virtual void AUdataFlushEverything() override;

	protected:
		struct FDecoderOutputBuffer
		{
			FDecoderOutputBuffer()
			{
				FMemory::Memzero(mOutputStreamInfo);
				FMemory::Memzero(mOutputBuffer);
			}
			~FDecoderOutputBuffer()
			{
				if (mOutputBuffer.pSample)
				{
					mOutputBuffer.pSample->Release();
				}
			}
			TRefCountPtr<IMFSample> DetachOutputSample()
			{
				TRefCountPtr<IMFSample> pOutputSample;
				if (mOutputBuffer.pSample)
				{
					pOutputSample = TRefCountPtr<IMFSample>(mOutputBuffer.pSample, false);
					mOutputBuffer.pSample = nullptr;
				}
				return pOutputSample;
			}
			void PrepareForProcess()
			{
				mOutputBuffer.dwStatus = 0;
				mOutputBuffer.dwStreamID = 0;
				mOutputBuffer.pEvents = nullptr;
			}
			void UnprepareAfterProcess()
			{
				if (mOutputBuffer.pEvents)
				{
					// https://docs.microsoft.com/en-us/windows/desktop/api/mftransform/nf-mftransform-imftransform-processoutput
					// The caller is responsible for releasing any events that the MFT allocates.
					mOutputBuffer.pEvents->Release();
					mOutputBuffer.pEvents = nullptr;
				}
			}
			MFT_OUTPUT_STREAM_INFO	mOutputStreamInfo;
			MFT_OUTPUT_DATA_BUFFER	mOutputBuffer;
		};

		void InternalDecoderDestroy();
		void DecoderCreate();
		bool DecoderSetInputType();
		bool DecoderSetOutputType();
		bool DecoderVerifyStatus();
		void StartThread();
		void StopThread();
		void WorkerThread();

		bool CreateDecodedImagePool();
		void DestroyDecodedImagePool();

		void NotifyReadyBufferListener(bool bHaveOutput);

		void SetupBufferAcquisitionProperties();

		bool AcquireOutputBuffer();
		bool ConvertDecodedImage(const TRefCountPtr<IMFSample>& DecodedSample);

		void PrepareAU(FAccessUnit* InAccessUnit);

		bool Decode(FAccessUnit* InAccessUnit);
		bool DecodeDummy(FAccessUnit* InAccessUnit);

		void ReturnUnusedFrame();

		void PostError(int32 ApiReturnValue, const FString& Message, uint16 Code, UEMediaError Error = UEMEDIA_ERROR_OK);
		void LogMessage(IInfoLog::ELevel Level, const FString& Message);
		bool ReconfigureForSwDecoding(FString Reason);
		bool Configure();
		bool StartStreaming();

		void HandleApplicationHasEnteredForeground();
		void HandleApplicationWillEnterBackground();

		// Per platform specialization
		virtual bool InternalDecoderCreate() = 0;
		virtual bool CreateDecoderOutputBuffer() = 0;
		virtual void PreInitDecodeOutputForSW(const FIntPoint& Dim) = 0;
		virtual bool SetupDecodeOutputData(const FIntPoint& ImageDim, const TRefCountPtr<IMFSample>& DecodedOutputSample, FParamDict* OutputBufferSampleProperties) = 0;
		virtual void PlatformTick() {}

		FInstanceConfiguration								Config;

		FMediaEvent											ApplicationRunningSignal;
		FMediaEvent											ApplicationSuspendConfirmedSignal;

		FMediaEvent											TerminateThreadSignal;
		FMediaEvent											FlushDecoderSignal;
		FMediaEvent											DecoderFlushedSignal;
		bool												bThreadStarted;
		bool												bDrainForCodecChange;

		IPlayerSessionServices*								PlayerSessionServices;

		TSharedPtr<IMediaRenderer, ESPMode::ThreadSafe>		Renderer;

		TWeakPtr<IVideoDecoderResourceDelegate, ESPMode::ThreadSafe> ResourceDelegate;

		FAccessUnitBuffer									AccessUnits;
		FStreamCodecInformation								NewSampleInfo;
		FStreamCodecInformation								CurrentSampleInfo;
		FAccessUnit*										CurrentAccessUnit;

		FMediaCriticalSection								ListenerMutex;
		IAccessUnitBufferListener*							InputBufferListener;
		IDecoderOutputBufferListener*						ReadyBufferListener;

		TRefCountPtr<IMFTransform>							DecoderTransform;
		TRefCountPtr<IMFMediaType>							CurrentOutputMediaType;
		MFT_OUTPUT_STREAM_INFO								DecoderOutputStreamInfo;
		bool												bIsHardwareAccelerated;
		int32												NumFramesInDecoder;
		bool												bError;

		TUniquePtr<FDecoderOutputBuffer>					CurrentDecoderOutputBuffer;
		IMediaRenderer::IBuffer*							CurrentRenderOutputBuffer;
		FParamDict											BufferAcquireOptions;
		bool												bHaveDecoder;
		int32												MaxDecodeBufferSize;

		FIntPoint											MaxDecodeDim;
	};

} // namespace
