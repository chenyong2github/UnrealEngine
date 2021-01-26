// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

namespace Electra {

	// Define the H.264 class GUID ourselves. Using the CLSID_CMSH264DecoderMFT from wmcodecdsp.h requires linking with some lib again...
	static const GUID MFTmsH264Decoder = { 0x62CE7E72, 0x4C71, 0x4D20, { 0xB1, 0x5D, 0x45, 0x28, 0x31, 0xA8, 0x7D, 0x9D } };

	static const GUID CODECAPI_AVLowLatencyMode = { 0x9c27891a, 0xed7a, 0x40e1, { 0x88, 0xe8, 0xb2, 0x27, 0x27, 0xa0, 0x24, 0xee } };

	/**
	 * H264 video decoder class implementation.
	**/
	class FVideoDecoderH264 : public IVideoDecoderH264, public FMediaThread
	{
	public:
		static bool Startup(const IVideoDecoderH264::FSystemConfiguration& InConfig);
		static void Shutdown();

		static bool GetStreamDecodeCapability(FStreamDecodeCapability& OutResult, const FStreamDecodeCapability& InStreamParameter);

		FVideoDecoderH264();
		virtual ~FVideoDecoderH264();

		void TestHardwareDecoding();

		virtual void SetPlayerSessionServices(IPlayerSessionServices* SessionServices) override;

		virtual void Open(const FInstanceConfiguration& InConfig) override;
		virtual void Close() override;

		virtual void SetMaximumDecodeCapability(int32 MaxWidth, int32 MaxHeight, int32 MaxProfile, int32 MaxProfileLevel, const FParamDict& AdditionalOptions) override;

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

		bool Decode(FAccessUnit* InAccessUnit, bool bResolutionChanged);
		bool PerformFlush();
		bool DecodeDummy(FAccessUnit* InAccessUnit);

		void ReturnUnusedFrame();

		void PostError(int32 ApiReturnValue, const FString& Message, uint16 Code, UEMediaError Error = UEMEDIA_ERROR_OK);
		void LogMessage(IInfoLog::ELevel Level, const FString& Message);
		bool FallbackToSwDecoding(FString Reason);
		bool ReconfigureForSwDecoding(FString Reason);
		bool Configure();
		bool StartStreaming();

		// Per platform specialization
		virtual bool InternalDecoderCreate() = 0;
		virtual bool CreateDecoderOutputBuffer() = 0;
		virtual void PreInitDecodeOutputForSW(const FIntPoint& Dim) = 0;
		virtual bool SetupDecodeOutputData(const FIntPoint& ImageDim, const TRefCountPtr<IMFSample>& DecodedOutputSample, FParamDict* OutputBufferSampleProperties) = 0;
		virtual void PlatformTick() {}

		FInstanceConfiguration								Config;

		FMediaEvent											TerminateThreadSignal;
		FMediaEvent											FlushDecoderSignal;
		FMediaEvent											DecoderFlushedSignal;
		bool												bThreadStarted;

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
		bool												bRequiresReconfigurationForSW;
		int32												NumFramesInDecoder;
		bool												bDecoderFlushPending;
		bool												bError;

		TUniquePtr<FDecoderOutputBuffer>					CurrentDecoderOutputBuffer;
		IMediaRenderer::IBuffer*							CurrentRenderOutputBuffer;
		FParamDict											BufferAcquireOptions;
		bool												bHaveDecoder;
		int32												MaxDecodeBufferSize;

		FIntPoint											MaxDecodeDim;

	public:
		static FSystemConfiguration							SystemConfig;

#ifdef ELECTRA_ENABLE_SWDECODE
		static bool bDidCheckHWSupport;
		static bool bIsHWSupported;
#endif
	};

} // namespace
