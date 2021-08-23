// Copyright Epic Games, Inc. All Rights Reserved.

#include "PlayerCore.h"

#include "StreamAccessUnitBuffer.h"
#include "Decoder/AudioDecoderAAC.h"
#include "Renderer/RendererBase.h"
#include "Player/PlayerSessionServices.h"
#include "Utilities/Utilities.h"
#include "Utilities/UtilsMPEGAudio.h"
#include "Utilities/StringHelpers.h"
#include "Utilities/AudioChannelMapper.h"
#include "DecoderErrors_Android.h"
#include "HAL/LowLevelMemTracker.h"
#include "ElectraPlayerPrivate.h"

#include "AudioDecoderAAC_JavaWrapper_Android.h"

DECLARE_CYCLE_STAT(TEXT("FAudioDecoderAAC::Decode()"), STAT_ElectraPlayer_AudioAACDecode, STATGROUP_ElectraPlayer);
DECLARE_CYCLE_STAT(TEXT("FAudioDecoderAAC::ConvertOutput()"), STAT_ElectraPlayer_AudioAACConvertOutput, STATGROUP_ElectraPlayer);


namespace Electra
{

namespace AACDecoderChannelOutputMappingAndroid
{
	#define CP FAudioChannelMapper::EChannelPosition
	static const CP _1[] = { CP::C };
	static const CP _2[] = { CP::L, CP::R };
	static const CP _3[] = { CP::L, CP::R, CP::C };
	static const CP _4[] = { CP::L, CP::R, CP::C, CP::Cs };
	static const CP _5[] = { CP::L, CP::R, CP::C, CP::Ls, CP::Rs };
	static const CP _6[] = { CP::L, CP::R, CP::C, CP::LFE, CP::Ls, CP::Rs };
	static const CP _7[] = { CP::L, CP::R, CP::C, CP::LFE, CP::Ls, CP::Rs, CP::Cs };
	static const CP _8[] = { CP::L, CP::R, CP::C, CP::LFE, CP::Ls, CP::Rs, CP::Lsr, CP::Rsr };
	static const CP * const Order[] = { _1, _2, _3, _4, _5, _6, _7, _8 };
	#undef CP
};


/**
 * AAC audio decoder class implementation.
**/
class FAudioDecoderAAC : public IAudioDecoderAAC, public FMediaThread
{
public:
	static bool Startup(const IAudioDecoderAAC::FSystemConfiguration& InConfig);
	static void Shutdown();

	static bool CanDecodeStream(const FStreamCodecInformation& InCodecInfo);

	FAudioDecoderAAC();
	virtual ~FAudioDecoderAAC();

	virtual void SetPlayerSessionServices(IPlayerSessionServices* SessionServices) override;

	virtual void Open(const FInstanceConfiguration& InConfig) override;
	virtual void Close() override;

	virtual void SetRenderer(TSharedPtr<IMediaRenderer, ESPMode::ThreadSafe> InRenderer) override;

	virtual void SetAUInputBufferListener(IAccessUnitBufferListener* Listener) override;

	virtual void SetReadyBufferListener(IDecoderOutputBufferListener* Listener) override;

	virtual IAccessUnitBufferInterface::EAUpushResult AUdataPushAU(FAccessUnit* InAccessUnit) override;
	virtual void AUdataPushEOD() override;
	virtual void AUdataFlushEverything() override;

private:
	void StartThread();
	void StopThread();
	void WorkerThread();

	bool InternalDecoderCreate();
	void InternalDecoderDestroy();

	bool CreateDecodedSamplePool();
	void DestroyDecodedSamplePool();

	void ReturnUnusedOutputBuffer();

	void NotifyReadyBufferListener(bool bHaveOutput);

	bool Decode(FAccessUnit* InAccessUnit);
	bool FlushDecoder();
	bool IsDifferentFormat(const FAccessUnit* Data);

	void PostError(int32 ApiReturnValue, const FString& Message, uint16 Code, UEMediaError Error = UEMEDIA_ERROR_OK);
	void LogMessage(IInfoLog::ELevel Level, const FString& Message);

private:
	FInstanceConfiguration													Config;

	FAccessUnitBuffer														AccessUnits;

	FMediaEvent																TerminateThreadSignal;
	FMediaEvent																FlushDecoderSignal;
	FMediaEvent																DecoderFlushedSignal;
	bool																	bThreadStarted;

	TSharedPtr<IMediaRenderer, ESPMode::ThreadSafe>							Renderer;
	int32																	MaxDecodeBufferSize;

	FMediaCriticalSection													ListenerMutex;
	IAccessUnitBufferListener*												InputBufferListener;
	IDecoderOutputBufferListener*											ReadyBufferListener;

	IPlayerSessionServices* 												SessionServices;

	TSharedPtr<MPEG::FAACDecoderConfigurationRecord, ESPMode::ThreadSafe>	ConfigRecord;
	TSharedPtrTS<FAccessUnit::CodecData>									CurrentCodecData;

	TSharedPtr<IAndroidJavaAACAudioDecoder, ESPMode::ThreadSafe>			DecoderInstance;
	IAndroidJavaAACAudioDecoder::FOutputFormatInfo							CurrentOutputFormatInfo;
	bool																	bInDummyDecodeMode;
	bool																	bHaveDiscontinuity;

	IMediaRenderer::IBuffer*												CurrentOutputBuffer;
	FParamDict																BufferAcquireOptions;
	FParamDict																OutputBufferSampleProperties;

	int32																	PCMBufferSize;
	int16*																	PCMBuffer;
	FAudioChannelMapper														ChannelMapper;
	static const uint8														NumChannelsForConfig[16];
public:
	static FSystemConfiguration												SystemConfig;
};

const uint8								FAudioDecoderAAC::NumChannelsForConfig[16] = { 0, 1, 2, 3, 4, 5, 6, 7, 0, 0, 0, 7, 8, 0, 8, 0 };
IAudioDecoderAAC::FSystemConfiguration	FAudioDecoderAAC::SystemConfig;


/***************************************************************************************************************************************************/
/***************************************************************************************************************************************************/
/***************************************************************************************************************************************************/

IAudioDecoderAAC::FSystemConfiguration::FSystemConfiguration()
{
	ThreadConfig.Decoder.Priority 	   = TPri_Normal;
	ThreadConfig.Decoder.StackSize	   = 65536;
	ThreadConfig.Decoder.CoreAffinity    = -1;
	// Not needed, but setting meaningful values anyway.
	ThreadConfig.PassOn.Priority  	   = TPri_Normal;
	ThreadConfig.PassOn.StackSize 	   = 32768;
	ThreadConfig.PassOn.CoreAffinity     = -1;
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

bool IAudioDecoderAAC::CanDecodeStream(const FStreamCodecInformation& InCodecInfo)
{
	return FAudioDecoderAAC::CanDecodeStream(InCodecInfo);
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
 * Determines if this stream can be decoded based on its channel configuration.
 */
bool FAudioDecoderAAC::CanDecodeStream(const FStreamCodecInformation& InCodecInfo)
{
	return InCodecInfo.GetChannelConfiguration() != 0;
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
	, bInDummyDecodeMode(false)
	, CurrentOutputBuffer(nullptr)
	, PCMBuffer(nullptr)
	, PCMBufferSize(0)
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


//-----------------------------------------------------------------------------
/**
 * Sets the owning player's session service interface.
 *
 * @param InSessionServices
 *
 * @return
 */
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
	uint32 frameSize = sizeof(int16) * 8 * 2048;
	poolOpts.Set("max_buffer_size", FVariantValue((int64) frameSize));
	poolOpts.Set("num_buffers",     FVariantValue((int64) 8));

	UEMediaError Error = Renderer->CreateBufferPool(poolOpts);
	check(Error == UEMEDIA_ERROR_OK);

	if (Error != UEMEDIA_ERROR_OK)
	{
		PostError(0, "Failed to create sample pool", ERRCODE_INTERNAL_ANDROID_COULD_NOT_CREATE_SAMPLE_POOL, Error);
	}

	MaxDecodeBufferSize = (int32) Renderer->GetBufferPoolProperties().GetValue("max_buffers").GetInt64();

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
 * @param InConfig
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
 * @param InListener
 */
void FAudioDecoderAAC::SetAUInputBufferListener(IAccessUnitBufferListener* InListener)
{
	FMediaCriticalSection::ScopedLock lock(ListenerMutex);
	InputBufferListener = InListener;
}


//-----------------------------------------------------------------------------
/**
 * Sets a buffer-ready listener.
 *
 * @param InListener
 */
void FAudioDecoderAAC::SetReadyBufferListener(IDecoderOutputBufferListener* InListener)
{
	FMediaCriticalSection::ScopedLock lock(ListenerMutex);
	ReadyBufferListener = InListener;
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
		TerminateThreadSignal.Signal();
		ThreadWaitDone();
		bThreadStarted = false;
	}
}


//-----------------------------------------------------------------------------
/**
 * Called to receive a new input access unit for decoding.
 *
 * @param InAccessUnit
 */
IAccessUnitBufferInterface::EAUpushResult FAudioDecoderAAC::AUdataPushAU(FAccessUnit* InAccessUnit)
{
	// Add a ref count to the AU before adding it to the buffer. If it can be added successfully it could be
	// used immediately by the worker thread and it would be too late for us to increase the count then.
	InAccessUnit->AddRef();
	bool bOk = AccessUnits.Push(InAccessUnit);
	if (!bOk)
	{
		// Could not add. Undo the refcount increase.
		FAccessUnit::Release(InAccessUnit);
	}
	return bOk ? IAccessUnitBufferInterface::EAUpushResult::Ok : IAccessUnitBufferInterface::EAUpushResult::Full;
}


//-----------------------------------------------------------------------------
/**
 * "Pushes" an End Of Data marker indicating no further access units will be added.
 */
void FAudioDecoderAAC::AUdataPushEOD()
{
	AccessUnits.PushEndOfData();
}


//-----------------------------------------------------------------------------
/**
 * Flushes the decoder and clears the input access unit buffer.
 */
void FAudioDecoderAAC::AUdataFlushEverything()
{
	FlushDecoderSignal.Signal();
	DecoderFlushedSignal.WaitAndReset();
}


//-----------------------------------------------------------------------------
/**
 * Notifies the buffer-ready listener that we will now be producing output data
 * and need an output buffer.
 * This call is not intended to create or obtain an output buffer. It is merely
 * indicating that new output will be produced.
 *
 * @param bHaveOutput
 */
void FAudioDecoderAAC::NotifyReadyBufferListener(bool bHaveOutput)
{
	if (ReadyBufferListener)
	{
		IDecoderOutputBufferListener::FDecodeReadyStats stats;
		stats.MaxDecodedElementsReady = MaxDecodeBufferSize;
		stats.NumElementsInDecoder    = CurrentOutputBuffer ? 1 : 0;
		stats.bOutputStalled		  = !bHaveOutput;
		stats.bEODreached   		  = AccessUnits.IsEndOfData() && stats.NumDecodedElementsReady == 0 && stats.NumElementsInDecoder == 0;
		ListenerMutex.Lock();
		if (ReadyBufferListener)
		{
			ReadyBufferListener->DecoderOutputReady(stats);
		}
		ListenerMutex.Unlock();
	}
}


//-----------------------------------------------------------------------------
/**
 * Posts an error to the session service error listeners.
 *
 * @param ApiReturnValue
 * @param Message
 * @param Code
 * @param Error
 */
void FAudioDecoderAAC::PostError(int32 ApiReturnValue, const FString& Message, uint16 Code, UEMediaError Error)
{
	check(SessionServices);	// there better be a session service interface to receive the error!
	if (SessionServices)
	{
		FErrorDetail err;
		err.SetError(Error != UEMEDIA_ERROR_OK ? Error : UEMEDIA_ERROR_DETAIL);
		err.SetFacility(Facility::EFacility::AACDecoder);
		err.SetCode(Code);
		err.SetMessage(Message);
		err.SetPlatformMessage(FString::Printf(TEXT("%d (0x%08x)"), (int32) ApiReturnValue, (int32) ApiReturnValue));
		SessionServices->PostError(err);
	}
}


//-----------------------------------------------------------------------------
/**
 * Sends a log message to the session service log.
 *
 * @param Level
 * @param Message
 */
void FAudioDecoderAAC::LogMessage(IInfoLog::ELevel Level, const FString& Message)
{
	if (SessionServices)
	{
		SessionServices->PostLog(Facility::EFacility::AACDecoder, Level, Message);
	}
}


//-----------------------------------------------------------------------------
/**
 * Creates an audio decoder.
 *
 * Note: This requires the configuration record to have been parsed successfully.
 */
bool FAudioDecoderAAC::InternalDecoderCreate()
{
	if (!ConfigRecord.IsValid())
	{
		PostError(0, "No CSD to create audio decoder with", ERRCODE_INTERNAL_ANDROID_COULD_NOT_CREATE_AUDIO_DECODER);
		return false;
	}

	InternalDecoderDestroy();

	DecoderInstance = IAndroidJavaAACAudioDecoder::Create();
	int32 result = DecoderInstance->InitializeDecoder(*ConfigRecord);
	if (result)
	{
		PostError(result, "Failed to create decoder", ERRCODE_INTERNAL_ANDROID_COULD_NOT_CREATE_AUDIO_DECODER);
		return false;
	}
	// Start it.
	result = DecoderInstance->Start();
	if (result)
	{
		PostError(result, "Failed to start decoder", ERRCODE_INTERNAL_ANDROID_COULD_NOT_START_DECODER);
		return false;
	}
	return true;
}


//-----------------------------------------------------------------------------
/**
 * Destroys the audio decoder.
 */
void FAudioDecoderAAC::InternalDecoderDestroy()
{
	if (DecoderInstance.IsValid())
	{
		DecoderInstance->Flush();
		DecoderInstance->Stop();
		DecoderInstance->ReleaseDecoder();
		DecoderInstance.Reset();
	}
}


//-----------------------------------------------------------------------------
/**
 * Returns the acquired sample output buffer back to the renderer without having it rendered.
 */
void FAudioDecoderAAC::ReturnUnusedOutputBuffer()
{
	if (CurrentOutputBuffer)
	{
		OutputBufferSampleProperties.Clear();
		Renderer->ReturnBuffer(CurrentOutputBuffer, false, OutputBufferSampleProperties);
		CurrentOutputBuffer = nullptr;
	}
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
 * Flushes the decoder
 */
bool FAudioDecoderAAC::FlushDecoder()
{
	if (DecoderInstance.IsValid())
	{
		int32 result = DecoderInstance->Flush();
		return result == 0;
	}
	return true;
}


//-----------------------------------------------------------------------------
/**
 * Decodes an access unit
 */
bool FAudioDecoderAAC::Decode(FAccessUnit* InAccessUnit)
{
	uint32 nDataOffset = 0;
	// Loop until all data has been consumed.
	bool bAllInputConsumed = false;
	while(!bAllInputConsumed && !TerminateThreadSignal.IsSignaled() && !FlushDecoderSignal.IsSignaled())
	{
		// Need a new output buffer?
		if (CurrentOutputBuffer == nullptr)
		{
			SCOPE_CYCLE_COUNTER(STAT_ElectraPlayer_AudioAACDecode);
			CSV_SCOPED_TIMING_STAT(ElectraPlayer, AudioAACDecode);
			UEMediaError bufResult = Renderer->AcquireBuffer(CurrentOutputBuffer, 0, BufferAcquireOptions);
			check(bufResult == UEMEDIA_ERROR_OK || bufResult == UEMEDIA_ERROR_INSUFFICIENT_DATA);
			if (bufResult != UEMEDIA_ERROR_OK && bufResult != UEMEDIA_ERROR_INSUFFICIENT_DATA)
			{
				PostError(0, "Failed to acquire sample buffer", ERRCODE_INTERNAL_ANDROID_COULD_NOT_GET_RENDER_BUFFER, bufResult);
				return false;
			}
		}

		bool bHaveAvailSmpBlk = CurrentOutputBuffer != nullptr;

		// Check if the renderer can accept the output we will want to send to it.
		// If it can't right now we treat this as if we do not have an available output buffer.
		if (Renderer.IsValid() && !Renderer->CanReceiveOutputFrames(1))
		{
			bHaveAvailSmpBlk = false;
		}

		NotifyReadyBufferListener(bHaveAvailSmpBlk);
		if (bHaveAvailSmpBlk)
		{
			FTimeValue CurrentPTS = InAccessUnit->PTS;

			if (DecoderInstance.IsValid() && !InAccessUnit->bIsDummyData)
			{
				if (bInDummyDecodeMode)
				{
					bHaveDiscontinuity = true;
					bInDummyDecodeMode = false;
				}

				int32 result;
				// Check if there is an available input buffer.
				int32 InputBufferIndex = DecoderInstance->DequeueInputBuffer(1000 * 2);
				if (InputBufferIndex >= 0)
				{
					int64 pts = InAccessUnit->PTS.GetAsMicroseconds();
					result = DecoderInstance->QueueInputBuffer(InputBufferIndex, Electra::AdvancePointer(InAccessUnit->AUData, nDataOffset), InAccessUnit->AUSize - nDataOffset, pts);
					if (result == 0)
					{
						// Presently, with RAW AAC data, we pass the entire AU as this is how the data is packetized. With ADTS framing there could potentially be multiple packets that need to be fed individually.
						nDataOffset += InAccessUnit->AUSize;
					}
					else
					{
						PostError(result, "Failed to submit decoder input buffer", ERRCODE_INTERNAL_ANDROID_FAILED_TO_DECODE_VIDEO);
						return false;
					}
				}

				// Check if there is available output.
				IAndroidJavaAACAudioDecoder::FOutputBufferInfo OutputBufferInfo;
				result = DecoderInstance->DequeueOutputBuffer(OutputBufferInfo, 1000 * 2);
				if (result == 0)
				{
					SCOPE_CYCLE_COUNTER(STAT_ElectraPlayer_AudioAACConvertOutput);
					CSV_SCOPED_TIMING_STAT(ElectraPlayer, AudioAACConvertOutput);
					if (OutputBufferInfo.BufferIndex >= 0)
					{
						int32 OutputByteCount    = OutputBufferInfo.Size;
						int32 SamplingRate  	 = CurrentOutputFormatInfo.SampleRate;
						int32 NumberOfChannels   = CurrentOutputFormatInfo.NumChannels;
						int32 NumSamplesProduced = OutputByteCount / (CurrentOutputFormatInfo.BytesPerSample * NumberOfChannels);
						FTimeValue Duration;
						Duration.SetFromND(NumSamplesProduced, SamplingRate);
						FTimeValue PTS;
						PTS.SetFromMicroseconds(OutputBufferInfo.PresentationTimestamp);
						bool bEOS = OutputBufferInfo.bIsEOS;

						check(PCMBufferSize >= OutputByteCount);
						if (PCMBufferSize >= OutputByteCount)
						{
							void* CopyBuffer = (void*)PCMBuffer;
							result = DecoderInstance->GetOutputBufferAndRelease(CopyBuffer, PCMBufferSize, OutputBufferInfo);
							if (result == 0)
							{
								if (!ChannelMapper.IsInitialized())
								{
									if (NumberOfChannels)
									{
										TArray<FAudioChannelMapper::FSourceLayout> Layout;
										const FAudioChannelMapper::EChannelPosition* ChannelPositions = AACDecoderChannelOutputMappingAndroid::Order[NumberOfChannels - 1];
										for(uint32 i=0; i<NumberOfChannels; ++i)
										{
											FAudioChannelMapper::FSourceLayout lo;
											lo.ChannelPosition = ChannelPositions[i];
											Layout.Emplace(MoveTemp(lo));
										}
										if (ChannelMapper.Initialize(sizeof(int16), Layout))
										{
											// Ok.
										}
										else
										{
											PostError(0, "Failed to initialize audio channel mapper", ERRCODE_INTERNAL_ANDROID_UNSUPPORTED_NUMBER_OF_AUDIO_CHANNELS);
											return false;
										}
									}
									else
									{
										PostError(0, "Bad number (0) of decoded audio channels", ERRCODE_INTERNAL_ANDROID_UNSUPPORTED_NUMBER_OF_AUDIO_CHANNELS);
										return false;
									}
								}

								int32 CurrentRenderOutputBufferSize = (int32)CurrentOutputBuffer->GetBufferProperties().GetValue("size").GetInt64();
								void* CurrentRenderOutputBufferAddress = CurrentOutputBuffer->GetBufferProperties().GetValue("address").GetPointer();
								int32 NumDecodedSamplesPerChannel = OutputByteCount / (sizeof(int16) * NumberOfChannels);
								ChannelMapper.MapChannels(CurrentRenderOutputBufferAddress, CurrentRenderOutputBufferSize, PCMBuffer, OutputByteCount, NumDecodedSamplesPerChannel);

								OutputBufferSampleProperties.Clear();
								OutputBufferSampleProperties.Set("num_channels",  FVariantValue((int64) ChannelMapper.GetNumTargetChannels()));
								OutputBufferSampleProperties.Set("sample_rate",   FVariantValue((int64) SamplingRate));
								OutputBufferSampleProperties.Set("byte_size",     FVariantValue((int64) (ChannelMapper.GetNumTargetChannels() * sizeof(int16) * NumDecodedSamplesPerChannel)));
								OutputBufferSampleProperties.Set("duration",      FVariantValue(Duration));
								OutputBufferSampleProperties.Set("pts",           FVariantValue(PTS));
								OutputBufferSampleProperties.Set("discontinuity", FVariantValue((bool)bHaveDiscontinuity));

								Renderer->ReturnBuffer(CurrentOutputBuffer, true, OutputBufferSampleProperties);
								CurrentOutputBuffer = nullptr;
								bHaveDiscontinuity = false;
							}
							else
							{
								PostError(0, "Failed to get decoder output buffer", ERRCODE_INTERNAL_ANDROID_COULD_NOT_GET_OUTPUT_BUFFER);
								return false;
							}
						}
						else
						{
							PostError(0, "Output buffer not large enough", ERRCODE_INTERNAL_ANDROID_RENDER_BUFFER_TOO_SMALL);
							return false;
						}
					}
					else if (OutputBufferInfo.BufferIndex == IAndroidJavaAACAudioDecoder::FOutputBufferInfo::EBufferIndexValues::MediaCodec_INFO_TRY_AGAIN_LATER)
					{
					}
					else if (OutputBufferInfo.BufferIndex == IAndroidJavaAACAudioDecoder::FOutputBufferInfo::EBufferIndexValues::MediaCodec_INFO_OUTPUT_FORMAT_CHANGED)
					{
						IAndroidJavaAACAudioDecoder::FOutputFormatInfo OutputFormatInfo;
						result = DecoderInstance->GetOutputFormatInfo(OutputFormatInfo, -1);
						if (result == 0)
						{
							CurrentOutputFormatInfo = OutputFormatInfo;
						}
						else
						{
							PostError(result, "Failed to get decoder output format", ERRCODE_INTERNAL_ANDROID_COULD_NOT_GET_OUTPUT_FORMAT);
							return false;
						}
					}
					else if (OutputBufferInfo.BufferIndex == IAndroidJavaAACAudioDecoder::FOutputBufferInfo::EBufferIndexValues::MediaCodec_INFO_OUTPUT_BUFFERS_CHANGED)
					{
					}
					else
					{
						// What new value might this be?
					}
				}
				else
				{
					PostError(result, "Failed to get decoder output buffer", ERRCODE_INTERNAL_ANDROID_COULD_NOT_GET_OUTPUT_BUFFER);
					return false;
				}
				bAllInputConsumed = nDataOffset >= InAccessUnit->AUSize;
			}
			else
			{
				SCOPE_CYCLE_COUNTER(STAT_ElectraPlayer_AudioAACConvertOutput);
				CSV_SCOPED_TIMING_STAT(ElectraPlayer, AudioAACConvertOutput);
				// Check if we are in dummy decode mode already. If not we need to flush the decoder if we have one.
				if (!bInDummyDecodeMode)
				{
					if (DecoderInstance.IsValid())
					{
						// The decoder must have been started in order to flush it, so we must not stop it here!
						// It does not require to be started again after flushing unless it is operating asynchronously, which we do not do.
						DecoderInstance->Flush();
					}
					bInDummyDecodeMode = true;
					bHaveDiscontinuity = true;
				}

				// Clear to silence
				FMemory::Memzero(CurrentOutputBuffer->GetBufferProperties().GetValue("address").GetPointer(), CurrentOutputBuffer->GetBufferProperties().GetValue("size").GetInt64());

				// Assume sensible defaults.
				int64 NumChannels = 2;
				int64 SampleRate = 48000;
				int64 SamplesPerBlock = 1024;

				// With a valid configuration record we can use the actual values.
				if (ConfigRecord.IsValid())
				{
					// Parameteric stereo results in stereo (2 channels).
					NumChannels = ConfigRecord->PSSignal > 0 ? 2 : NumChannelsForConfig[ConfigRecord->ChannelConfiguration];
					SampleRate = ConfigRecord->ExtSamplingFrequency ? ConfigRecord->ExtSamplingFrequency : ConfigRecord->SamplingRate;
					SamplesPerBlock = ConfigRecord->SBRSignal > 0 ? 2048 : 1024;
				}
				else if (CurrentOutputFormatInfo.IsValid())
				{
					NumChannels = CurrentOutputFormatInfo.NumChannels;
					SampleRate = CurrentOutputFormatInfo.SampleRate;
					SamplesPerBlock = 1024;	// Whether this was SBR with 2048 samples or not we don't get back from the decoder :-(
				}

				FTimeValue Duration;
				Duration.SetFromND(SamplesPerBlock, (uint32) SampleRate);

				// Note: The duration in the AU should be an exact multiple of the "SamplesPerBlock / SampleRate" as this is how it gets set
				//       up in the stream reader. Should there ever be any discrepancy such that "Duration > pData->mDuration" it is simply
				//       possible to recalculate "SamplesPerBlock" such that we return a buffer with less than the usual 1024 or 2048 samples.
				OutputBufferSampleProperties.Clear();
				OutputBufferSampleProperties.Set("num_channels",  FVariantValue(NumChannels));
				OutputBufferSampleProperties.Set("sample_rate",   FVariantValue(SampleRate));
				OutputBufferSampleProperties.Set("byte_size",     FVariantValue((int64)(NumChannels * sizeof(int16) * SamplesPerBlock)));
				OutputBufferSampleProperties.Set("duration",      FVariantValue(Duration));
				OutputBufferSampleProperties.Set("pts",           FVariantValue(CurrentPTS));
				OutputBufferSampleProperties.Set("discontinuity", FVariantValue((bool)bHaveDiscontinuity));

				Renderer->ReturnBuffer(CurrentOutputBuffer, true, OutputBufferSampleProperties);
				CurrentOutputBuffer = nullptr;
				bHaveDiscontinuity = false;

				CurrentPTS += Duration;
				InAccessUnit->Duration -= Duration;
				bAllInputConsumed = InAccessUnit->Duration <= FTimeValue::GetZero();
			}
		}
		else
		{
			// No available buffer. Sleep for a bit. Can't sleep on a signal since we have to check two: abort and flush
			// We sleep for 20ms since a sample block for LC AAC produces 1024 samples which amount to 21.3ms @48kHz (or more at lower sample rates).
			FMediaRunnable::SleepMilliseconds(20);
		}
	}
	return true;
}


//-----------------------------------------------------------------------------
/**
 * AAC audio decoder main threaded decode loop
 */
void FAudioDecoderAAC::WorkerThread()
{
	LLM_SCOPE(ELLMTag::MediaStreaming);

	FAccessUnit*	CurrentAccessUnit = nullptr;
	bool			bError			  = false;

	CurrentOutputBuffer = nullptr;
	bInDummyDecodeMode = false;
	bHaveDiscontinuity = false;

	PCMBufferSize = sizeof(int16) * 8 * 2048;
	PCMBuffer = (int16*)FMemory::Malloc(PCMBufferSize);

	bError = !CreateDecodedSamplePool();
	check(!bError);

	while(!TerminateThreadSignal.IsSignaled())
	{
		// Notify the buffer listener that we will now be needing an AU for our input buffer.
		if (!bError && InputBufferListener && AccessUnits.Num() == 0)
		{
			SCOPE_CYCLE_COUNTER(STAT_ElectraPlayer_AudioAACDecode);
			CSV_SCOPED_TIMING_STAT(ElectraPlayer, AudioAACDecode);
			FAccessUnitBufferInfo	sin;
			IAccessUnitBufferListener::FBufferStats	stats;
			AccessUnits.GetStats(sin);
			stats.NumAUsAvailable  = sin.NumCurrentAccessUnits;
			stats.NumBytesInBuffer = sin.CurrentMemInUse;
			stats.MaxBytesOfBuffer = sin.MaxDataSize;
			stats.bEODSignaled     = sin.bEndOfData;
			stats.bEODReached      = sin.bEndOfData && sin.NumCurrentAccessUnits == 0;
			ListenerMutex.Lock();
			if (InputBufferListener)
			{
				InputBufferListener->DecoderInputNeeded(stats);
			}
			ListenerMutex.Unlock();
		}
		// Get the AU to be decoded if one is there.
		bool bHaveData = AccessUnits.WaitForData(1000 * 10);
		if (bHaveData)
		{
			AccessUnits.Pop(CurrentAccessUnit);

			// Check if the format has changed such that we need to destroy and re-create the decoder.
			if (CurrentAccessUnit->bTrackChangeDiscontinuity || IsDifferentFormat(CurrentAccessUnit) || (CurrentAccessUnit->bIsDummyData && !bInDummyDecodeMode))
			{
				SCOPE_CYCLE_COUNTER(STAT_ElectraPlayer_AudioAACDecode);
				CSV_SCOPED_TIMING_STAT(ElectraPlayer, AudioAACDecode);
				if (DecoderInstance.IsValid())
				{
					InternalDecoderDestroy();
					bHaveDiscontinuity = true;
				}
				ReturnUnusedOutputBuffer();
				ConfigRecord.Reset();
				CurrentCodecData.Reset();
				ChannelMapper.Reset();
			}

			// Parse the CSD into a configuration record.
			if (!ConfigRecord.IsValid() && !bError)
			{
				SCOPE_CYCLE_COUNTER(STAT_ElectraPlayer_AudioAACDecode);
				CSV_SCOPED_TIMING_STAT(ElectraPlayer, AudioAACDecode);
				if (CurrentAccessUnit->AUCodecData.IsValid())
				{
					CurrentCodecData = CurrentAccessUnit->AUCodecData;
					ConfigRecord = MakeShared<MPEG::FAACDecoderConfigurationRecord, ESPMode::ThreadSafe>();
					if (!ConfigRecord->ParseFrom(CurrentCodecData->CodecSpecificData.GetData(), CurrentCodecData->CodecSpecificData.Num()))
					{
						ConfigRecord.Reset();
						CurrentCodecData.Reset();
						PostError(0, "Failed to parse AAC configuration record", ERRCODE_INTERNAL_ANDROID_FAILED_TO_PARSE_CSD);
						bError = true;
					}
					else
					{
						// Channel configuration must be specified. Internal CPE/SCE elements are not supported.
						if (ConfigRecord->ChannelConfiguration == 0)
						{
							ConfigRecord.Reset();
							CurrentCodecData.Reset();
							PostError(0, "Unsupported AAC channel configuration", ERRCODE_INTERNAL_ANDROID_UNSUPPORTED_NUMBER_OF_AUDIO_CHANNELS);
							bError = true;
						}
					}
				}
			}

			// Check if this audio packet is to be dropped
			if (!CurrentAccessUnit->DropState)
			{
				// Need to create a decoder instance?
				if (!DecoderInstance.IsValid() && !bError)
				{
					SCOPE_CYCLE_COUNTER(STAT_ElectraPlayer_AudioAACDecode);
					CSV_SCOPED_TIMING_STAT(ElectraPlayer, AudioAACDecode);
					if (!InternalDecoderCreate())
					{
						bError = true;
					}
				}

				// Decode if not in error, otherwise just spend some idle time.
				if (!bError)
				{
					if (!Decode(CurrentAccessUnit))
					{
						bError = true;
					}
				}
				else
				{
					// Pace ourselves in consuming any more data until we are being stopped.
					FMediaRunnable::SleepMicroseconds(CurrentAccessUnit->Duration.GetAsMicroseconds());
				}
			}

			// We're done with this access unit. Delete it.
			FAccessUnit::Release(CurrentAccessUnit);
			CurrentAccessUnit = nullptr;
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
		if (FlushDecoderSignal.IsSignaled())
		{
			SCOPE_CYCLE_COUNTER(STAT_ElectraPlayer_AudioAACDecode);
			CSV_SCOPED_TIMING_STAT(ElectraPlayer, AudioAACDecode);
			ReturnUnusedOutputBuffer();
			AccessUnits.Flush();
			FlushDecoder();

			FlushDecoderSignal.Reset();
			DecoderFlushedSignal.Signal();
		}
	}

	ReturnUnusedOutputBuffer();
	// Close the decoder.
	InternalDecoderDestroy();
	DestroyDecodedSamplePool();

	CurrentCodecData.Reset();
	ConfigRecord.Reset();

	// Flush any remaining input data.
	AccessUnits.Flush();
	AccessUnits.CapacitySet(0);
	FMemory::Free(PCMBuffer);
	}


} // namespace Electra


