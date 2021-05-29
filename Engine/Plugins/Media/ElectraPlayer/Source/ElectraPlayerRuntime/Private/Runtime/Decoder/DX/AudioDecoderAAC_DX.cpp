// Copyright Epic Games, Inc. All Rights Reserved.

#ifdef ELECTRA_ENABLE_MFDECODER

#include "PlayerCore.h"
#include "ElectraPlayerPrivate.h"
#include "ElectraPlayerPrivate_Platform.h"

#include "StreamAccessUnitBuffer.h"
#include "Decoder/AudioDecoderAAC.h"
#include "Renderer/RendererBase.h"
#include "Player/PlayerSessionServices.h"
#include "Utilities/UtilsMPEG.h"
#include "Utilities/UtilsMPEGAudio.h"
#include "Utilities/StringHelpers.h"

#include "DecoderErrors_DX.h"

/*********************************************************************************************************************/
/*********************************************************************************************************************/
/*********************************************************************************************************************/

#include "Windows/AllowWindowsPlatformTypes.h"
#include "Windows/WindowsHWrapper.h"
#include "HAL/LowLevelMemTracker.h"

THIRD_PARTY_INCLUDES_START
#include "mftransform.h"
#include "mfapi.h"
#include "mferror.h"
#include "mfidl.h"
//#include "wmcodecdsp.h" // for MEDIASUBTYPE_RAW_AAC1, but requires linking of additional dll for the constant.
THIRD_PARTY_INCLUDES_END

#include "Windows/HideWindowsPlatformTypes.h"

namespace
{
	static const GUID MFTmsAACDecoder_Audio = { 0x32d186a7, 0x218f, 0x4c75, { 0x88, 0x76, 0xdd, 0x77, 0x27, 0x3a, 0x89, 0x99 } };
	static const GUID MEDIASUBTYPE_RAW_AAC1_Audio = { 0x000000FF, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };
}


/*********************************************************************************************************************/
/*********************************************************************************************************************/
/*********************************************************************************************************************/

#define VERIFY_HR(FNcall, Msg, What)	\
res = FNcall;							\
if (FAILED(res))						\
{										\
	PostError(res, Msg, What);			\
	return false;						\
}

/*********************************************************************************************************************/
/*********************************************************************************************************************/
/*********************************************************************************************************************/

DECLARE_CYCLE_STAT(TEXT("FAudioDecoderAAC::Decode()"), STAT_ElectraPlayer_AudioAACDecode, STATGROUP_ElectraPlayer);
DECLARE_CYCLE_STAT(TEXT("FAudioDecoderAAC::ConvertOutput()"), STAT_ElectraPlayer_AudioAACConvertOutput, STATGROUP_ElectraPlayer);


#define AACDEC_PCM_SAMPLE_SIZE  2048
#define AACDEC_MAX_CHANNELS		6


namespace Electra
{


	/**
	 * AAC audio decoder class implementation.
	**/
	class FAudioDecoderAAC : public IAudioDecoderAAC, public FMediaThread
	{
	public:
		static bool Startup(const IAudioDecoderAAC::FSystemConfiguration& InConfig);
		static void Shutdown();

		FAudioDecoderAAC();
		virtual ~FAudioDecoderAAC();

		virtual void SetPlayerSessionServices(IPlayerSessionServices* SessionServices) override;

		virtual void Open(const FInstanceConfiguration& InConfig) override;
		virtual void Close() override;

		virtual void SetRenderer(TSharedPtr<IMediaRenderer, ESPMode::ThreadSafe> InRenderer) override;

		virtual void SetAUInputBufferListener(IAccessUnitBufferListener* Listener) override;

		virtual void SetReadyBufferListener(IDecoderOutputBufferListener* Listener) override;

		virtual IAccessUnitBufferInterface::EAUpushResult AUdataPushAU(FAccessUnit* AccessUnit) override;
		virtual void AUdataPushEOD() override;
		virtual void AUdataFlushEverything() override;

	private:
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
					mOutputBuffer.pSample->Release();
			}
			TRefCountPtr<IMFSample> DetachOutputSample()
			{
				TRefCountPtr<IMFSample> pOutputSample;
				if (mOutputBuffer.pSample)
				{
					// mOutputBuffer.pSample already holds a reference, don't need to addref here.
					pOutputSample = TRefCountPtr<IMFSample>(mOutputBuffer.pSample, false);
					mOutputBuffer.pSample = nullptr;
				}
				return(pOutputSample);
			}
			void PrepareForProcess()
			{
				mOutputBuffer.dwStatus = 0;
				mOutputBuffer.dwStreamID = 0;
				mOutputBuffer.pEvents = nullptr;
			}
			MFT_OUTPUT_STREAM_INFO	mOutputStreamInfo;
			MFT_OUTPUT_DATA_BUFFER	mOutputBuffer;
		};

		void StartThread();
		void StopThread();
		void WorkerThread();

		bool InternalDecoderCreate();
		bool SetDecoderOutputType();

		bool CreateDecodedSamplePool();
		void DestroyDecodedSamplePool();

		void ReturnUnusedFrame();

		void NotifyReadyBufferListener(bool bHaveOutput);

		void PostError(HRESULT ApiReturnValue, const FString& Message, uint16 Code, UEMediaError Error = UEMEDIA_ERROR_OK);

		bool CreateDecoderOutputBuffer();
		bool Decode(int32& OutInputSizeConsumed, const void* InData, int32 InSize, int64 PTS, int64 Duration, bool bFlushOnly, bool bRender);

		bool IsDifferentFormat(const FAccessUnit* Data);

		FInstanceConfiguration													Config;

		FAccessUnitBuffer														AccessUnits;

		FMediaEvent																TerminateThreadEvent;
		FMediaEvent																FlushDecoderEvent;
		FMediaEvent																DecoderFlushedEvent;
		bool																	bThreadStarted;

		TSharedPtr<IMediaRenderer, ESPMode::ThreadSafe>							Renderer;
		int32																	MaxDecodeBufferSize;

		FMediaCriticalSection													ListenerCriticalSection;
		IAccessUnitBufferListener* InputBufferListener;
		IDecoderOutputBufferListener* ReadyBufferListener;

		IPlayerSessionServices* SessionServices;

		TSharedPtr<MPEG::FAACDecoderConfigurationRecord, ESPMode::ThreadSafe>	ConfigRecord;
		TSharedPtrTS<FAccessUnit::CodecData>									CurrentCodecData;
		bool																	bHaveDiscontinuity;

		TRefCountPtr<IMFTransform>												DecoderTransform;
		TRefCountPtr<IMFMediaType>												CurrentOutputMediaType;
		MFT_INPUT_STREAM_INFO													DecoderInputStreamInfo;
		MFT_OUTPUT_STREAM_INFO													DecoderOutputStreamInfo;

		FTimeValue																CurrentPTS;
		FTimeValue																NextExpectedPTS;
		TUniquePtr<FDecoderOutputBuffer>										CurrentDecoderOutputBuffer;
		IMediaRenderer::IBuffer* CurrentRenderOutputBuffer;
		FParamDict																BufferAcquireOptions;
		FParamDict																OutputBufferSampleProperties;

	public:
		static FSystemConfiguration												SystemConfig;
	};

	IAudioDecoderAAC::FSystemConfiguration	FAudioDecoderAAC::SystemConfig;


	/***************************************************************************************************************************************************/
	/***************************************************************************************************************************************************/
	/***************************************************************************************************************************************************/

	IAudioDecoderAAC::FSystemConfiguration::FSystemConfiguration()
	{
		ThreadConfig.Decoder.Priority = TPri_Normal;
		ThreadConfig.Decoder.StackSize = 65536;
		ThreadConfig.Decoder.CoreAffinity = -1;
		// Not needed, but setting meaningful values anyway.
		ThreadConfig.PassOn.Priority = TPri_Normal;
		ThreadConfig.PassOn.StackSize = 32768;
		ThreadConfig.PassOn.CoreAffinity = -1;
	}

	IAudioDecoderAAC::FInstanceConfiguration::FInstanceConfiguration()
		: ThreadConfig(FAudioDecoderAAC::SystemConfig.ThreadConfig)
	{
	}

	bool IAudioDecoderAAC::Startup(const IAudioDecoderAAC::FSystemConfiguration& InConfig)
	{
		return FAudioDecoderAAC::Startup(InConfig);
	}

	void IAudioDecoderAAC::Shutdown()
	{
		FAudioDecoderAAC::Shutdown();
	}

	IAudioDecoderAAC* IAudioDecoderAAC::Create()
	{
		return new FAudioDecoderAAC;
	}

	/***************************************************************************************************************************************************/
	/***************************************************************************************************************************************************/
	/***************************************************************************************************************************************************/




	//-----------------------------------------------------------------------------
	/**
	 * Decoder system startup
	 *
	 * @param config
	 *
	 * @return
	 */
	bool FAudioDecoderAAC::Startup(const IAudioDecoderAAC::FSystemConfiguration& InConfig)
	{
		SystemConfig = InConfig;
		return true;
	}


	//-----------------------------------------------------------------------------
	/**
	 * Decoder system shutdown.
	 */
	void FAudioDecoderAAC::Shutdown()
	{
	}



	//-----------------------------------------------------------------------------
	/**
	 * Constructor
	 */
	FAudioDecoderAAC::FAudioDecoderAAC()
		: FMediaThread("ElectraPlayer::AAC decoder")
		, bThreadStarted(false)
		, Renderer(nullptr)
		, MaxDecodeBufferSize(0)
		, InputBufferListener(nullptr)
		, ReadyBufferListener(nullptr)
		, SessionServices(nullptr)
		, CurrentRenderOutputBuffer(nullptr)
	{
	}


	//-----------------------------------------------------------------------------
	/**
	 * Destructor
	 */
	FAudioDecoderAAC::~FAudioDecoderAAC()
	{
		Close();
	}


	void FAudioDecoderAAC::SetPlayerSessionServices(IPlayerSessionServices* InSessionServices)
	{
		SessionServices = InSessionServices;
	}


	//-----------------------------------------------------------------------------
	/**
	 * Creates the decoded sample blocks output buffers.
	 *
	 * @return
	 */
	bool FAudioDecoderAAC::CreateDecodedSamplePool()
	{
		check(Renderer);
		FParamDict poolOpts;
		// Assume LC-AAC with at most 6 channels and 16 bit decoded PCM samples. This is ok for HE-AAC as well which can at most be stereo.
		uint32 frameSize = sizeof(int16) * AACDEC_MAX_CHANNELS * 1024;
		static_assert(sizeof(int16) * AACDEC_MAX_CHANNELS * 1024 >= sizeof(int16) * 2 * 2048, "Sample pool size not correct!");

		poolOpts.Set("max_buffer_size", FVariantValue((int64)frameSize));
		//	poolOpts.Set("buffer_alignment", FVariantValue((int64) 32));
		poolOpts.Set("num_buffers", FVariantValue((int64)8));

		UEMediaError Error = Renderer->CreateBufferPool(poolOpts);
		check(Error == UEMEDIA_ERROR_OK);

		if (Error != UEMEDIA_ERROR_OK)
		{
			PostError(S_OK, "Failed to create sample pool", ERRCODE_INTERNAL_COULD_NOT_CREATE_SAMPLE_POOL, Error);
		}

		MaxDecodeBufferSize = (int32)Renderer->GetBufferPoolProperties().GetValue("max_buffers").GetInt64();

		return Error == UEMEDIA_ERROR_OK;
	}



	//-----------------------------------------------------------------------------
	/**
	 * Destroys the pool of decoded sample blocks.
	 */
	void FAudioDecoderAAC::DestroyDecodedSamplePool()
	{
		Renderer->ReleaseBufferPool();
	}



	//-----------------------------------------------------------------------------
	/**
	 * Opens a decoder instance
	 *
	 * @param config
	 *
	 * @return
	 */
	void FAudioDecoderAAC::Open(const IAudioDecoderAAC::FInstanceConfiguration& InConfig)
	{
		Config = InConfig;

		// Set a large enough size to hold a single access unit. Since we will be asking for a new AU on
		// demand there is no need to overly restrict ourselves here. But it must be large enough to
		// hold at least the largest expected access unit.
		AccessUnits.CapacitySet(FAccessUnitBuffer::FConfiguration(2 << 20, 60.0));

		StartThread();
	}



	//-----------------------------------------------------------------------------
	/**
	 * Closes the decoder instance.
	 *
	 * @param bKeepDecoder
	 */
	void FAudioDecoderAAC::Close()
	{
		StopThread();
	}



	//-----------------------------------------------------------------------------
	/**
	 * Sets a new renderer.
	 *
	 * @param InRenderer
	 */
	void FAudioDecoderAAC::SetRenderer(TSharedPtr<IMediaRenderer, ESPMode::ThreadSafe> InRenderer)
	{
		Renderer = InRenderer;
	}


	//-----------------------------------------------------------------------------
	/**
	 * Sets an AU input buffer listener.
	 *
	 * @param Listener
	 */
	void FAudioDecoderAAC::SetAUInputBufferListener(IAccessUnitBufferListener* Listener)
	{
		FMediaCriticalSection::ScopedLock lock(ListenerCriticalSection);
		InputBufferListener = Listener;
	}

	void FAudioDecoderAAC::SetReadyBufferListener(IDecoderOutputBufferListener* Listener)
	{
		FMediaCriticalSection::ScopedLock lock(ListenerCriticalSection);
		ReadyBufferListener = Listener;
	}




	//-----------------------------------------------------------------------------
	/**
	 * Creates and runs the decoder thread.
	 */
	void FAudioDecoderAAC::StartThread()
	{
		ThreadSetPriority(Config.ThreadConfig.Decoder.Priority);
		ThreadSetStackSize(Config.ThreadConfig.Decoder.StackSize);
		ThreadSetCoreAffinity(Config.ThreadConfig.Decoder.CoreAffinity);
		ThreadStart(Electra::MakeDelegate(this, &FAudioDecoderAAC::WorkerThread));
		bThreadStarted = true;
	}



	//-----------------------------------------------------------------------------
	/**
	 * Stops the decoder thread.
	 */
	void FAudioDecoderAAC::StopThread()
	{
		if (bThreadStarted)
		{
			TerminateThreadEvent.Signal();
			ThreadWaitDone();
			bThreadStarted = false;
		}
	}




	IAccessUnitBufferInterface::EAUpushResult FAudioDecoderAAC::AUdataPushAU(FAccessUnit* AccessUnit)
	{
		bool bOk;
		AccessUnit->AddRef();
		bOk = AccessUnits.Push(AccessUnit);
		if (!bOk)
		{
			FAccessUnit::Release(AccessUnit);
		}
		return bOk ? IAccessUnitBufferInterface::EAUpushResult::Ok : IAccessUnitBufferInterface::EAUpushResult::Full;
	}

	void FAudioDecoderAAC::AUdataPushEOD()
	{
		AccessUnits.PushEndOfData();
	}

	void FAudioDecoderAAC::AUdataFlushEverything()
	{
		FlushDecoderEvent.Signal();
		DecoderFlushedEvent.WaitAndReset();
	}


	//-----------------------------------------------------------------------------
	/**
	 * Notify optional decode-ready listener that we will now be producing output data.
	 *
	 * @param bHaveOutput
	 */
	void FAudioDecoderAAC::NotifyReadyBufferListener(bool bHaveOutput)
	{
		if (ReadyBufferListener)
		{
			IDecoderOutputBufferListener::FDecodeReadyStats stats;
			stats.MaxDecodedElementsReady = MaxDecodeBufferSize;
			stats.NumElementsInDecoder = CurrentRenderOutputBuffer ? 1 : 0;
			stats.bOutputStalled = !bHaveOutput;
			stats.bEODreached = AccessUnits.IsEndOfData() && stats.NumDecodedElementsReady == 0 && stats.NumElementsInDecoder == 0;
			ListenerCriticalSection.Lock();
			if (ReadyBufferListener)
			{
				ReadyBufferListener->DecoderOutputReady(stats);
			}
			ListenerCriticalSection.Unlock();
		}
	}



	void FAudioDecoderAAC::PostError(HRESULT ApiReturnValue, const FString& Message, uint16 Code, UEMediaError Error)
	{
		check(SessionServices);
		if (SessionServices)
		{
			FErrorDetail err;
			err.SetError(Error != UEMEDIA_ERROR_OK ? Error : UEMEDIA_ERROR_DETAIL);
			err.SetFacility(Facility::EFacility::AACDecoder);
			err.SetCode(Code);
			err.SetMessage(Message);

			if (ApiReturnValue != S_OK)
			{
				err.SetPlatformMessage(FString::Printf(TEXT("%s (0x%08lx)"), *GetComErrorDescription(ApiReturnValue), ApiReturnValue));
			}
			SessionServices->PostError(err);
		}
	}



	bool FAudioDecoderAAC::InternalDecoderCreate()
	{
		TRefCountPtr<IMFTransform>	Decoder;
		TRefCountPtr<IMFMediaType>	MediaType;
		HRESULT					res;

		if (!ConfigRecord.IsValid())
		{
			PostError(0, "No CSD to create audio decoder with", ERRCODE_INTERNAL_COULD_NOT_CREATE_DECODER);
			return false;
		}

		// Create decoder transform
		VERIFY_HR(CoCreateInstance(MFTmsAACDecoder_Audio, nullptr, CLSCTX_INPROC_SERVER, IID_IMFTransform, reinterpret_cast<void**>(&Decoder)), "CoCreateInstance failed", ERRCODE_INTERNAL_COULD_NOT_CREATE_DECODER);

		// Create input media type
		VERIFY_HR(MFCreateMediaType(MediaType.GetInitReference()), "MFCreateMediaType failed", ERRCODE_INTERNAL_COULD_NOT_CREATE_INPUT_MEDIA_TYPE);
		VERIFY_HR(MediaType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio), "Failed to set input media type for audio", ERRCODE_INTERNAL_COULD_NOT_SET_INPUT_AUDIO_MAJOR_TYPE);
		UINT32 PayloadType = 0;	// 0=raw, 1=adts, 2=adif, 3=latm
		VERIFY_HR(MediaType->SetGUID(MF_MT_SUBTYPE, MEDIASUBTYPE_RAW_AAC1_Audio), "Failed to set input media audio type to RAW AAC", ERRCODE_INTERNAL_COULD_NOT_SET_INPUT_AUDIO_AAC_SUBTYPE);
		VERIFY_HR(MediaType->SetUINT32(MF_MT_AAC_PAYLOAD_TYPE, PayloadType), "Failed to set input media audio payload type", ERRCODE_INTERNAL_COULD_NOT_SET_INPUT_AUDIO_AAC_SUBTYPE);
		VERIFY_HR(MediaType->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, ConfigRecord->SamplingRate), FString::Printf(TEXT("Failed to set input audio sampling rate to %u"), ConfigRecord->SamplingRate), ERRCODE_INTERNAL_COULD_NOT_SET_INPUT_AUDIO_SAMPLING_RATE);
		VERIFY_HR(MediaType->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, ConfigRecord->ChannelConfiguration), FString::Printf(TEXT("Failed to set input audio number of channels to %u"), ConfigRecord->ChannelConfiguration), ERRCODE_INTERNAL_COULD_NOT_SET_INPUT_AUDIO_CHANNEL_COUNT);
		VERIFY_HR(MediaType->SetBlob(MF_MT_USER_DATA, ConfigRecord->GetCodecSpecificData().GetData(), ConfigRecord->GetCodecSpecificData().Num()), "Failed to set input audio CSD", ERRCODE_INTERNAL_COULD_NOT_SET_INPUT_AUDIO_CSD);
		// Set input media type with decoder
		VERIFY_HR(Decoder->SetInputType(0, MediaType, 0), "Failed to set audio decoder input type", ERRCODE_INTERNAL_COULD_NOT_SET_INPUT_TYPE);
		DecoderTransform = Decoder;

		// Set decoder output type to PCM
		if (!SetDecoderOutputType())
		{
			DecoderTransform = nullptr;
			return false;
		}
		// Get input and output stream information from decoder
		VERIFY_HR(DecoderTransform->GetInputStreamInfo(0, &DecoderInputStreamInfo), "Failed to get audio decoder input stream information", ERRCODE_INTERNAL_COULD_NOT_GET_INPUT_STREAM_FORMAT_INFO);
		VERIFY_HR(DecoderTransform->GetOutputStreamInfo(0, &DecoderOutputStreamInfo), "Failed to get audio decoder output stream information", ERRCODE_INTERNAL_COULD_NOT_GET_OUTPUT_STREAM_FORMAT_INFO);

		// Start the decoder transform
		VERIFY_HR(DecoderTransform->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0), "Failed to set audio decoder stream begin", ERRCODE_INTERNAL_COULD_NOT_SET_DECODER_BEGIN);
		VERIFY_HR(DecoderTransform->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0), "Failed to start audio decoder", ERRCODE_INTERNAL_COULD_NOT_SET_DECODER_START);

		return true;
	}


	bool FAudioDecoderAAC::SetDecoderOutputType()
	{
		TRefCountPtr<IMFMediaType> MediaType;
		HRESULT res;

		uint32 TypeIndex = 0;
		while (SUCCEEDED(DecoderTransform->GetOutputAvailableType(0, TypeIndex++, MediaType.GetInitReference())))
		{
			GUID Subtype;
			res = MediaType->GetGUID(MF_MT_SUBTYPE, &Subtype);
			if (SUCCEEDED(res) && Subtype == MFAudioFormat_PCM)
			{
				VERIFY_HR(DecoderTransform->SetOutputType(0, MediaType, 0), "Failed to set audio decoder output type", ERRCODE_INTERNAL_COULD_NOT_SET_OUTPUT_TYPE);
				CurrentOutputMediaType = MediaType;
				return true;
			}
		}
		PostError(S_OK, "Failed to set audio decoder output type to PCM", ERRCODE_INTERNAL_COULD_NOT_SET_OUTPUT_TYPE_TO_PCM);
		return false;
	}



	void FAudioDecoderAAC::ReturnUnusedFrame()
	{
		if (CurrentRenderOutputBuffer)
		{
			OutputBufferSampleProperties.Clear();
			Renderer->ReturnBuffer(CurrentRenderOutputBuffer, false, OutputBufferSampleProperties);
			CurrentRenderOutputBuffer = nullptr;
		}
	}



	bool FAudioDecoderAAC::CreateDecoderOutputBuffer()
	{
		HRESULT									res;
		TUniquePtr<FDecoderOutputBuffer>		NewDecoderOutputBuffer(new FDecoderOutputBuffer);

		VERIFY_HR(DecoderTransform->GetOutputStreamInfo(0, &NewDecoderOutputBuffer->mOutputStreamInfo), "Failed to get audio decoder output stream information", ERRCODE_INTERNAL_COULD_NOT_GET_OUTPUT_STREAM_FORMAT_INFO);
		// Do we need to provide the sample output buffer or does the decoder create it for us?
		if ((NewDecoderOutputBuffer->mOutputStreamInfo.dwFlags & (MFT_OUTPUT_STREAM_PROVIDES_SAMPLES | MFT_OUTPUT_STREAM_CAN_PROVIDE_SAMPLES)) == 0)
		{
			// We have to provide the output sample buffer.
			TRefCountPtr<IMFSample>		OutputSample;
			TRefCountPtr<IMFMediaBuffer>	OutputBuffer;
			VERIFY_HR(MFCreateSample(OutputSample.GetInitReference()), "Failed to create output sample for audio decoder", ERRCODE_INTERNAL_COULD_NOT_CREATE_OUTPUT_SAMPLE);
			if (NewDecoderOutputBuffer->mOutputStreamInfo.cbAlignment > 0)
			{
				VERIFY_HR(MFCreateAlignedMemoryBuffer(NewDecoderOutputBuffer->mOutputStreamInfo.cbSize, NewDecoderOutputBuffer->mOutputStreamInfo.cbAlignment, OutputBuffer.GetInitReference()), "Failed to create aligned output buffer for audio decoder", ERRCODE_INTERNAL_COULD_NOT_CREATE_ALIGNED_OUTPUTBUFFER);
			}
			else
			{
				VERIFY_HR(MFCreateMemoryBuffer(NewDecoderOutputBuffer->mOutputStreamInfo.cbSize, OutputBuffer.GetInitReference()), "Failed to create output buffer for audio decoder", ERRCODE_INTERNAL_COULD_NOT_CREATE_OUTPUTBUFFER);
			}
			VERIFY_HR(OutputSample->AddBuffer(OutputBuffer.GetReference()), "Failed to add sample buffer to output sample for audio decoder", ERRCODE_INTERNAL_COULD_NOT_ADD_OUTPUT_BUFFER_TO_SAMPLE);
			(NewDecoderOutputBuffer->mOutputBuffer.pSample = OutputSample.GetReference())->AddRef();
			OutputSample = nullptr;
		}
		CurrentDecoderOutputBuffer = MoveTemp(NewDecoderOutputBuffer);
		return true;
	}

	bool FAudioDecoderAAC::Decode(int32& OutInputSizeConsumed, const void* InData, int32 InSize, int64 PTS, int64 Duration, bool bFlushOnly, bool bRender)
	{
		HRESULT				res;
		TRefCountPtr<IMFSample>	InputSample;
		bool				bFlush = InData == nullptr;

		OutInputSizeConsumed = 0;
		if (bFlush)
		{
			VERIFY_HR(DecoderTransform->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, 0), "Failed to set audio decoder end of stream notification", ERRCODE_INTERNAL_COULD_NOT_SET_DECODER_ENDOFSTREAM);
			if (bFlushOnly)
			{
				VERIFY_HR(DecoderTransform->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, 0), "Failed to issue audio decoder flush command", ERRCODE_INTERNAL_COULD_NOT_SET_DECODER_FLUSHCOMMAND);
			}
			else
			{
				VERIFY_HR(DecoderTransform->ProcessMessage(MFT_MESSAGE_COMMAND_DRAIN, 0), "Failed to issue audio decoder drain command", ERRCODE_INTERNAL_COULD_NOT_SET_DECODER_DRAINCOMMAND);
			}
		}
		else
		{
			// Create the input sample.
			TRefCountPtr<IMFMediaBuffer>	InputSampleBuffer;
			BYTE* pbNewBuffer = nullptr;
			DWORD					dwMaxBufferSize = 0;
			DWORD					dwSize = 0;
			LONGLONG				llSampleTime = 0;

			SCOPE_CYCLE_COUNTER(STAT_ElectraPlayer_AudioAACDecode);
			CSV_SCOPED_TIMING_STAT(ElectraPlayer, AudioAACDecode);

			VERIFY_HR(MFCreateSample(InputSample.GetInitReference()), "Failed to create audio decoder input sample", ERRCODE_INTERNAL_COULD_NOT_CREATE_INPUT_SAMPLE);
			VERIFY_HR(MFCreateMemoryBuffer((DWORD)InSize, InputSampleBuffer.GetInitReference()), "Failed to create audio decoder input sample memory buffer", ERRCODE_INTERNAL_COULD_NOT_CREATE_INPUTBUFFER);
			VERIFY_HR(InputSample->AddBuffer(InputSampleBuffer.GetReference()), "Failed to set audio decoder input buffer with sample", ERRCODE_INTERNAL_COULD_NOT_ADD_INPUT_BUFFER_TO_SAMPLE);
			VERIFY_HR(InputSampleBuffer->Lock(&pbNewBuffer, &dwMaxBufferSize, &dwSize), "Failed to lock audio decoder input sample buffer", ERRCODE_INTERNAL_COULD_NOT_LOCK_INPUT_BUFFER);
			FMemory::Memcpy(pbNewBuffer, InData, InSize);
			VERIFY_HR(InputSampleBuffer->Unlock(), "Failed to unlock audio decoder input sample buffer", ERRCODE_INTERNAL_COULD_NOT_UNLOCK_INPUT_BUFFER);
			VERIFY_HR(InputSampleBuffer->SetCurrentLength((DWORD)InSize), "Failed to set audio decoder input sample buffer length", ERRCODE_INTERNAL_COULD_NOT_SET_BUFFER_CURRENT_LENGTH);
			// Set sample attributes
			llSampleTime = PTS;
			VERIFY_HR(InputSample->SetSampleTime(llSampleTime), "Failed to set audio decoder input sample decode time", ERRCODE_INTERNAL_COULD_NOT_SET_INPUT_SAMPLE_TIME);
			llSampleTime = Duration;
			VERIFY_HR(InputSample->SetSampleDuration(llSampleTime), "Failed to set audio decode intput sample duration", ERRCODE_INTERNAL_COULD_NOT_SET_INPUT_SAMPLE_DURATION);
		}

		while (!TerminateThreadEvent.IsSignaled())
		{
			if (FlushDecoderEvent.IsSignaled() && !bFlush)
			{
				break;
			}
			if (!CurrentDecoderOutputBuffer.IsValid())
			{
				SCOPE_CYCLE_COUNTER(STAT_ElectraPlayer_AudioAACDecode);
				CSV_SCOPED_TIMING_STAT(ElectraPlayer, AudioAACDecode);

				if (!CreateDecoderOutputBuffer())
				{
					return false;
				}
			}

			DWORD	dwStatus = 0;
			CurrentDecoderOutputBuffer->PrepareForProcess();

			{
				SCOPE_CYCLE_COUNTER(STAT_ElectraPlayer_AudioAACDecode);
				CSV_SCOPED_TIMING_STAT(ElectraPlayer, AudioAACDecode);
				res = DecoderTransform->ProcessOutput(0, 1, &CurrentDecoderOutputBuffer->mOutputBuffer, &dwStatus);
			}

			if (res == MF_E_TRANSFORM_NEED_MORE_INPUT)
			{
				// Flushing / draining?
				if (bFlush)
				{
					// Yes. This means we have received all pending output and are done now.
					SCOPE_CYCLE_COUNTER(STAT_ElectraPlayer_AudioAACDecode);
					CSV_SCOPED_TIMING_STAT(ElectraPlayer, AudioAACDecode);
					if (!bFlushOnly)
					{
						// After a drain issue a flush.
						VERIFY_HR(DecoderTransform->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, 0), "Failed to issue audio decoder flush command", ERRCODE_INTERNAL_COULD_NOT_SET_DECODER_FLUSHCOMMAND);
					}
					// And start over.
					VERIFY_HR(DecoderTransform->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0), "Failed to set audio decoder stream begin", ERRCODE_INTERNAL_COULD_NOT_SET_DECODER_BEGIN);
					VERIFY_HR(DecoderTransform->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0), "Failed to start audio decoder", ERRCODE_INTERNAL_COULD_NOT_SET_DECODER_START);
					CurrentDecoderOutputBuffer.Reset();
					return true;
				}
				else if (InputSample.IsValid())
				{
					VERIFY_HR(DecoderTransform->ProcessInput(0, InputSample.GetReference(), 0), "Failed to process audio decoder input", ERRCODE_INTERNAL_COULD_NOT_PROCESS_INPUT);
					// Used this sample. Have no further input data for now, but continue processing to produce output if possible.
					InputSample = nullptr;
					OutInputSizeConsumed = InSize;
				}
				else
				{
					// Need more input but have none right now.
					return true;
				}
			}
			else if (res == MF_E_TRANSFORM_STREAM_CHANGE)
			{
				SCOPE_CYCLE_COUNTER(STAT_ElectraPlayer_AudioAACDecode);
				CSV_SCOPED_TIMING_STAT(ElectraPlayer, AudioAACDecode);
				// Update output type.
				if (!SetDecoderOutputType())
				{
					return false;
				}
				// For the sake of argument lets get rid of the output buffer (might be too small)
				CurrentDecoderOutputBuffer.Reset();
			}
			else if (SUCCEEDED(res))
			{
				TRefCountPtr<IMFSample> DecodedOutputSample = CurrentDecoderOutputBuffer->DetachOutputSample();
				CurrentDecoderOutputBuffer.Reset();

				TRefCountPtr<IMFMediaBuffer>	DecodedLinearOutputBuffer;
				DWORD					dwBufferLen;
				DWORD					dwMaxBufferLen;
				BYTE* pDecompressedData = nullptr;
				LONGLONG				llTimeStamp = 0;
				WAVEFORMATEX			OutputWaveFormat;
				WAVEFORMATEX* OutputWaveFormatPtr = nullptr;
				UINT32					WaveFormatSize = 0;

				VERIFY_HR(DecodedOutputSample->GetSampleTime(&llTimeStamp), "Failed to get audio decoder output sample timestamp", ERRCODE_INTERNAL_COULD_NOT_GET_OUTPUT_SAMPLE_TIME);
				VERIFY_HR(DecodedOutputSample->ConvertToContiguousBuffer(DecodedLinearOutputBuffer.GetInitReference()), "Failed to convert audio decoder output sample to contiguous buffer", ERRCODE_INTERNAL_COULD_NOT_MAKE_CONTIGUOUS_OUTPUT_BUFFER);
				VERIFY_HR(DecodedLinearOutputBuffer->GetCurrentLength(&dwBufferLen), "Failed to get audio decoder output buffer current length", ERRCODE_INTERNAL_COULD_NOT_GET_OUTPUT_BUFFER_LENGTH);
				VERIFY_HR(DecodedLinearOutputBuffer->Lock(&pDecompressedData, &dwMaxBufferLen, &dwBufferLen), "Failed to lock audio decoder output buffer", ERRCODE_INTERNAL_COULD_NOT_LOCK_OUTPUT_BUFFER)
				VERIFY_HR(MFCreateWaveFormatExFromMFMediaType(CurrentOutputMediaType.GetReference(), &OutputWaveFormatPtr, &WaveFormatSize, MFWaveFormatExConvertFlag_Normal), "Failed to create audio decoder output buffer format info", ERRCODE_INTERNAL_COULD_NOT_CREATE_OUTPUT_BUFFER_FORMAT_INFO);
				FMemory::Memcpy(&OutputWaveFormat, OutputWaveFormatPtr, sizeof(OutputWaveFormat));
				CoTaskMemFree(OutputWaveFormatPtr);
				OutputWaveFormatPtr = nullptr;

				FTimeValue pts;
				pts.SetFromHNS((int64)llTimeStamp);

				int32 nSamplesProduced = dwBufferLen / (sizeof(int16) * OutputWaveFormat.nChannels);
				const int16* pPCMSamples = (const int16*)pDecompressedData;
				//int32 nSamplesProduced = dwBufferLen / (sizeof(float) * OutputWaveFormat.nChannels);
				//const float *pPCMSamples = (const float *)pDecompressedData;

				// Get an sample block from the pool.
				while (!TerminateThreadEvent.IsSignaled())
				{
					if (FlushDecoderEvent.IsSignaled() && !bFlush)
					{
						break;
					}

					if (CurrentRenderOutputBuffer == nullptr)
					{
						SCOPE_CYCLE_COUNTER(STAT_ElectraPlayer_AudioAACDecode);
						CSV_SCOPED_TIMING_STAT(ElectraPlayer, AudioAACDecode);

						UEMediaError bufResult = Renderer->AcquireBuffer(CurrentRenderOutputBuffer, 0, BufferAcquireOptions);
						check(bufResult == UEMEDIA_ERROR_OK || bufResult == UEMEDIA_ERROR_INSUFFICIENT_DATA);
						if (bufResult != UEMEDIA_ERROR_OK && bufResult != UEMEDIA_ERROR_INSUFFICIENT_DATA)
						{
							PostError(S_OK, "Failed to acquire sample buffer", ERRCODE_INTERNAL_COULD_NOT_GET_SAMPLE_BUFFER, bufResult);
							return false;
						}
					}
					bool bHaveAvailSmpBlk = CurrentRenderOutputBuffer != nullptr;
					NotifyReadyBufferListener(bHaveAvailSmpBlk);
					if (bHaveAvailSmpBlk)
					{
						SCOPE_CYCLE_COUNTER(STAT_ElectraPlayer_AudioAACConvertOutput);
						CSV_SCOPED_TIMING_STAT(ElectraPlayer, AudioAACConvertOutput);

						FTimeValue OutputSampleDuration;
						OutputSampleDuration.SetFromND(nSamplesProduced, OutputWaveFormat.nSamplesPerSec);

						DWORD CurrentRenderOutputBufferSize = (DWORD)CurrentRenderOutputBuffer->GetBufferProperties().GetValue("size").GetInt64();
						check(dwBufferLen <= CurrentRenderOutputBufferSize);
						if (dwBufferLen <= CurrentRenderOutputBufferSize)
						{
							void* CurrentRenderOutputBufferAddress = CurrentRenderOutputBuffer->GetBufferProperties().GetValue("address").GetPointer();
							FMemory::Memcpy(CurrentRenderOutputBufferAddress, pPCMSamples, dwBufferLen);

							OutputBufferSampleProperties.Clear();
							OutputBufferSampleProperties.Set("num_channels", FVariantValue((int64)OutputWaveFormat.nChannels));
							OutputBufferSampleProperties.Set("sample_rate", FVariantValue((int64)OutputWaveFormat.nSamplesPerSec));
							OutputBufferSampleProperties.Set("byte_size", FVariantValue((int64)dwBufferLen));
							OutputBufferSampleProperties.Set("duration", FVariantValue(OutputSampleDuration));
							OutputBufferSampleProperties.Set("pts", FVariantValue(pts));
							OutputBufferSampleProperties.Set("discontinuity", FVariantValue((bool)bHaveDiscontinuity));

							Renderer->ReturnBuffer(CurrentRenderOutputBuffer, bRender, OutputBufferSampleProperties);
							CurrentRenderOutputBuffer = nullptr;
							bHaveDiscontinuity = false;

							break;
						}
						else
						{
							PostError(S_OK, "Audio renderer buffer too small to receive decoded audio samples", ERRCODE_INTERNAL_RENDER_BUFFER_TOO_SMALL);
							return false;
						}
					}
					else
					{
						// No available buffer. Sleep for a bit. Can't sleep on a signal since we have to check two: abort and flush
						// We sleep for 20ms since a sample block for LC AAC produces 1024 samples which amount to 21.3ms @48kHz (or more at lower sample rates).
						FMediaRunnable::SleepMilliseconds(20);
					}
				}
				DecodedLinearOutputBuffer->Unlock();
			}
			else
			{
				// Error!
				VERIFY_HR(res, "Failed to process audio decoder output", ERRCODE_INTERNAL_COULD_NOT_PROCESS_OUTPUT);
				return false;
			}
		}
		return true;
	}



	//-----------------------------------------------------------------------------
	/**
	 * Checks if the codec specific format has changed.
	 *
	 * @param Data
	 *
	 * @return
	 */
	bool FAudioDecoderAAC::IsDifferentFormat(const FAccessUnit* Data)
	{
		if (Data->AUCodecData.IsValid() && (!CurrentCodecData.IsValid() || CurrentCodecData->CodecSpecificData != Data->AUCodecData->CodecSpecificData))
		{
			return true;
		}
		return false;
	}


	//-----------------------------------------------------------------------------
	/**
	 * AAC audio decoder main threaded decode loop
	 */
	void FAudioDecoderAAC::WorkerThread()
	{
		LLM_SCOPE(ELLMTag::ElectraPlayer);

		bool bError = false;
		bool bInDummyDecodeMode = false;

		bHaveDiscontinuity = false;
		CurrentRenderOutputBuffer = nullptr;

		DecoderInputStreamInfo = {};
		DecoderOutputStreamInfo = {};

		bError = !CreateDecodedSamplePool();
		check(!bError);

		while (!TerminateThreadEvent.IsSignaled())
		{
			// Notify optional buffer listener that we will now be needing an AU for our input buffer.
			if (!bError && InputBufferListener && AccessUnits.Num() == 0)
			{
				SCOPE_CYCLE_COUNTER(STAT_ElectraPlayer_AudioAACDecode);
				CSV_SCOPED_TIMING_STAT(ElectraPlayer, AudioAACDecode);

				FAccessUnitBufferInfo	sin;
				IAccessUnitBufferListener::FBufferStats	stats;
				AccessUnits.GetStats(sin);
				stats.NumAUsAvailable = sin.NumCurrentAccessUnits;
				stats.NumBytesInBuffer = sin.CurrentMemInUse;
				stats.MaxBytesOfBuffer = sin.MaxDataSize;
				stats.bEODSignaled = sin.bEndOfData;
				stats.bEODReached = sin.bEndOfData && sin.NumCurrentAccessUnits == 0;
				ListenerCriticalSection.Lock();
				if (InputBufferListener)
				{
					InputBufferListener->DecoderInputNeeded(stats);
				}
				ListenerCriticalSection.Unlock();
			}
			// Wait for data to arrive. If no data for 10ms check if we're supposed to abort.
			// When data arrives the wait is of course cut short and we don't spend the full 10ms waiting!
			bool bHaveData = AccessUnits.WaitForData(1000 * 10);
			if (bHaveData)
			{
				FAccessUnit* pData = nullptr;
				AccessUnits.Pop(pData);

				// Check if the format has changed such that we need to flush and re-create the decoder.
				if (pData->bTrackChangeDiscontinuity || IsDifferentFormat(pData) || (pData->bIsDummyData && !bInDummyDecodeMode))
				{
					if (DecoderTransform.IsValid())
					{
						int32 NumBytesConsumed = 0;

						// Check that the difference in expected PTS is not too great. If it is we should just discard the pending samples from the decoder and not output them.
						FTimeValue PtsDiff = pData->PTS - NextExpectedPTS;
						bool bRenderPending = PtsDiff.IsValid() && PtsDiff.GetAsMilliseconds() >= 0 && PtsDiff.GetAsMilliseconds() < 100;

						Decode(NumBytesConsumed, nullptr, 0, 0, 0, true, bRenderPending);
						bHaveDiscontinuity = true;
					}
					ReturnUnusedFrame();
					DecoderTransform = nullptr;
					CurrentOutputMediaType = nullptr;
					ConfigRecord = nullptr;
					CurrentCodecData = nullptr;
				}

				// Parse the CSD into a configuration record.
				if (!ConfigRecord.IsValid() && !bError)
				{
					if (pData->AUCodecData.IsValid())
					{
						CurrentCodecData = pData->AUCodecData;
						ConfigRecord = MakeShared<MPEG::FAACDecoderConfigurationRecord, ESPMode::ThreadSafe>();
						if (!ConfigRecord->ParseFrom(CurrentCodecData->CodecSpecificData.GetData(), CurrentCodecData->CodecSpecificData.Num()))
						{
							ConfigRecord.Reset();
							CurrentCodecData.Reset();
							PostError(S_OK, "Failed to parse AAC configuration record", ERRCODE_INTERNAL_FAILED_TO_PARSE_CSD);
							bError = true;
						}
					}
				}

				CurrentPTS = pData->PTS;
				NextExpectedPTS = pData->PTS + pData->Duration;

				// Is this audio packet to be dropped?
				if (pData->DropState)
				{
					FAccessUnit::Release(pData);
					pData = nullptr;
					continue;
				}

				// Need to create a decoder instance?
				if (!DecoderTransform.IsValid() && !bError)
				{
					SCOPE_CYCLE_COUNTER(STAT_ElectraPlayer_AudioAACDecode);
					CSV_SCOPED_TIMING_STAT(ElectraPlayer, AudioAACDecode);

					// Can't create a decoder based on dummy data.
					if (!pData->bIsDummyData)
					{
						if (InternalDecoderCreate())
						{
							// Ok
						}
						else
						{
							bError = true;
						}
					}
				}

				// Loop until all data has been consumed.
				bool bAllInputConsumed = false;
				while (!bError && !bAllInputConsumed && !TerminateThreadEvent.IsSignaled() && !FlushDecoderEvent.IsSignaled())
				{
					if (!pData->bIsDummyData)
					{
						if (bInDummyDecodeMode)
						{
							bInDummyDecodeMode = false;
							bHaveDiscontinuity = true;
						}
						int32 NumBytesConsumed = 0;
						if (Decode(NumBytesConsumed, pData->AUData, pData->AUSize, pData->PTS.GetAsHNS(), pData->Duration.GetAsHNS(), false, true))
						{
							check(NumBytesConsumed == pData->AUSize);
							bAllInputConsumed = true;
							break;
						}
						else
						{
							bError = true;
						}
					}
					else
					{
						if (!bInDummyDecodeMode)
						{
							bInDummyDecodeMode = true;
							bHaveDiscontinuity = true;
						}

						// Need a new output buffer?
						if (CurrentRenderOutputBuffer == nullptr)
						{
							SCOPE_CYCLE_COUNTER(STAT_ElectraPlayer_AudioAACDecode);
							CSV_SCOPED_TIMING_STAT(ElectraPlayer, AudioAACDecode);

							UEMediaError bufResult = Renderer->AcquireBuffer(CurrentRenderOutputBuffer, 0, BufferAcquireOptions);
							check(bufResult == UEMEDIA_ERROR_OK || bufResult == UEMEDIA_ERROR_INSUFFICIENT_DATA);
							if (bufResult != UEMEDIA_ERROR_OK && bufResult != UEMEDIA_ERROR_INSUFFICIENT_DATA)
							{
								PostError(S_OK, "Failed to acquire sample buffer", ERRCODE_INTERNAL_COULD_NOT_GET_SAMPLE_BUFFER, bufResult);
								bError = true;
								break;
							}
						}
						bool bHaveAvailSmpBlk = CurrentRenderOutputBuffer != nullptr;
						NotifyReadyBufferListener(bHaveAvailSmpBlk);
						if (bHaveAvailSmpBlk)
						{
							SCOPE_CYCLE_COUNTER(STAT_ElectraPlayer_AudioAACConvertOutput);
							CSV_SCOPED_TIMING_STAT(ElectraPlayer, AudioAACConvertOutput);

							// Clear to silence
							SIZE_T CurrentRenderOutputBufferSize = (SIZE_T)CurrentRenderOutputBuffer->GetBufferProperties().GetValue("size").GetInt64();
							void* CurrentRenderOutputBufferAddress = CurrentRenderOutputBuffer->GetBufferProperties().GetValue("address").GetPointer();
							FMemory::Memzero(CurrentRenderOutputBufferAddress, CurrentRenderOutputBufferSize);

							// Assume sensible defaults.
							int64 NumChannels = 2;
							int64 SampleRate = 48000;
							int64 SamplesPerBlock = 1024;

							// With a valid configuration record we can use the actual values.
							if (ConfigRecord.IsValid())
							{
								// Parameteric stereo results in stereo (2 channels).
								NumChannels = ConfigRecord->PSSignal > 0 ? 2 : ConfigRecord->ChannelConfiguration;
								SampleRate = ConfigRecord->ExtSamplingFrequency ? ConfigRecord->ExtSamplingFrequency : ConfigRecord->SamplingRate;
								SamplesPerBlock = ConfigRecord->SBRSignal > 0 ? 2048 : 1024;
							}

							FTimeValue Duration;
							Duration.SetFromND(SamplesPerBlock, (uint32)SampleRate);

							// Note: The duration in the AU should be an exact multiple of the "SamplesPerBlock / SampleRate" as this is how it gets set
							//       up in the stream reader. Should there ever be any discrepancy such that "Duration > pData->mDuration" it is simply
							//       possible to recalculate "SamplesPerBlock" such that we return a buffer with less than the usual 1024 or 2048 samples.
							OutputBufferSampleProperties.Clear();
							OutputBufferSampleProperties.Set("num_channels", FVariantValue(NumChannels));
							OutputBufferSampleProperties.Set("sample_rate", FVariantValue(SampleRate));
							OutputBufferSampleProperties.Set("byte_size", FVariantValue((int64)(NumChannels * sizeof(int16) * SamplesPerBlock)));
							OutputBufferSampleProperties.Set("duration", FVariantValue(Duration));
							OutputBufferSampleProperties.Set("pts", FVariantValue(CurrentPTS));
							OutputBufferSampleProperties.Set("eod", FVariantValue((bool)false));
							OutputBufferSampleProperties.Set("discontinuity", FVariantValue((bool)bHaveDiscontinuity));

							Renderer->ReturnBuffer(CurrentRenderOutputBuffer, true, OutputBufferSampleProperties);
							CurrentRenderOutputBuffer = nullptr;
							bHaveDiscontinuity = false;

							CurrentPTS += Duration;
							pData->Duration -= Duration;
							bAllInputConsumed = pData->Duration <= FTimeValue::GetZero();
						}
						else
						{
							// No available buffer. Sleep for a bit. Can't sleep on a signal since we have to check two: abort and flush
							// We sleep for 20ms since a sample block for LC AAC produces 1024 samples which amount to 21.3ms @48kHz (or more at lower sample rates).
							FMediaRunnable::SleepMilliseconds(20);
						}
					}
				}

				// We're done with this access unit. Delete it.
				FAccessUnit::Release(pData);
				pData = nullptr;
			}
			else
			{
				// No data. Is the buffer at EOD?
				if (AccessUnits.IsEndOfData())
				{
					NotifyReadyBufferListener(true);
					FMediaRunnable::SleepMilliseconds(20);
				}
			}

			// Flush?
			if (FlushDecoderEvent.IsSignaled())
			{
				if (DecoderTransform.IsValid())
				{
					int32 NumBytesConsumed = 0;
					Decode(NumBytesConsumed, nullptr, 0, 0, 0, true, true);
				}

				ReturnUnusedFrame();
				AccessUnits.Flush();

				CurrentPTS.SetToInvalid();
				NextExpectedPTS.SetToInvalid();

				FlushDecoderEvent.Reset();
				DecoderFlushedEvent.Signal();
			}
		}

		ReturnUnusedFrame();
		// Close the decoder.
		DecoderTransform = nullptr;
		CurrentOutputMediaType = nullptr;

		DestroyDecodedSamplePool();

		CurrentCodecData.Reset();
		ConfigRecord.Reset();

		// Flush any remaining input data.
		AccessUnits.Flush();
		AccessUnits.CapacitySet(0);

		CurrentPTS.SetToInvalid();
		NextExpectedPTS.SetToInvalid();
	}


} // namespace Electra


#endif

