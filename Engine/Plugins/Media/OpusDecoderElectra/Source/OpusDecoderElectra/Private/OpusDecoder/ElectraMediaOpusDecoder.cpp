// Copyright Epic Games, Inc. All Rights Reserved.

#include "ElectraMediaOpusDecoder.h"
#include "OpusDecoderElectraModule.h"
#include "IElectraCodecRegistry.h"
#include "IElectraCodecFactory.h"
#include "Misc/ScopeLock.h"
#include "Templates/SharedPointer.h"
#include "Features/IModularFeatures.h"
#include "Features/IModularFeature.h"
#include "IElectraCodecFactoryModule.h"
#include "IElectraDecoderFeaturesAndOptions.h"
#include "IElectraDecoder.h"
#include "IElectraDecoderOutputAudio.h"
#include "IElectraDecoderResourceDelegate.h"
#include "ElectraDecodersUtils.h"
#include "Utils/MPEG/ElectraUtilsMP4.h"		// for parsing the 'dOps' box

#include "opus_multistream.h"

#define ERRCODE_INTERNAL_NO_ERROR							0
#define ERRCODE_INTERNAL_ALREADY_CLOSED						1
#define ERRCODE_INTERNAL_FAILED_TO_PARSE_CSD				2
#define ERRCODE_INTERNAL_FAILED_TO_DECODE_INPUT				3
#define ERRCODE_INTERNAL_UNSUPPORTED_CHANNEL_LAYOUT			4

/*********************************************************************************************************************/
/*********************************************************************************************************************/
/*********************************************************************************************************************/

class FElectraDecoderDefaultAudioOutputFormatOpus_Common : public IElectraDecoderDefaultAudioOutputFormat
{
public:
	virtual ~FElectraDecoderDefaultAudioOutputFormatOpus_Common()
	{ }

	int32 GetNumChannels() const override
	{
		return NumChannels;
	}
	int32 GetSampleRate() const override
	{
		return SampleRate;
	}
	int32 GetNumFrames() const override
	{
		return NumFrames;
	}

	int32 NumChannels = 0;
	int32 SampleRate = 0;
	int32 NumFrames = 0;
};


class FElectraAudioDecoderOutputOpus_Common : public IElectraDecoderAudioOutput
{
public:
	virtual ~FElectraAudioDecoderOutputOpus_Common()
	{
		FMemory::Free(Buffer);
	}

	FTimespan GetPTS() const override
	{
		return PTS;
	}
	uint64 GetUserValue() const override
	{
		return UserValue;
	}

	int32 GetNumChannels() const override
	{
		return NumChannels;
	}
	int32 GetSampleRate() const override
	{
		return SampleRate;
	}
	int32 GetNumFrames() const override
	{
		return NumFrames - PreSkip;
	}
	bool IsInterleaved() const override
	{
		return true;
	}
	EChannelPosition GetChannelPosition(int32 InChannelNumber) const override
	{
		return InChannelNumber >= 0 && InChannelNumber < ChannelPositions.Num() ? ChannelPositions[InChannelNumber] : EChannelPosition::Invalid;
	}
	ESampleFormat GetSampleFormat() const override
	{
		return ESampleFormat::Float;
	}
	int32 GetBytesPerSample() const override
	{
		return sizeof(float);
	}
	int32 GetBytesPerFrame() const override
	{
		return GetBytesPerSample() * GetNumChannels();
	}
	const void* GetData(int32 InChannelNumber) const override
	{
		return InChannelNumber >= 0 && InChannelNumber < GetNumChannels() ? Buffer + InChannelNumber + (PreSkip * NumChannels) : nullptr;
	}

public:
	TArray<EChannelPosition> ChannelPositions;
	FTimespan PTS;
	float* Buffer = nullptr;
	uint64 UserValue = 0;
	int32 NumChannels = 0;
	int32 SampleRate = 0;
	int32 NumFrames = 0;
	int32 PreSkip = 0;
};


class FElectraOpusDecoder : public IElectraDecoder
{
public:
	static void GetConfigurationOptions(TMap<FString, FVariant>& OutOptions)
	{
	}

	FElectraOpusDecoder(const TMap<FString, FVariant>& InOptions, TSharedPtr<IElectraDecoderResourceDelegate, ESPMode::ThreadSafe> InResourceDelegate);

	virtual ~FElectraOpusDecoder();

	IElectraDecoder::EType GetType() const override
	{
		return IElectraDecoder::EType::Audio;
	}

	void GetFeatures(TMap<FString, FVariant>& OutFeatures) const override;

	FError GetError() const override;

	void Close() override;
	IElectraDecoder::ECSDCompatibility IsCompatibleWith(const TMap<FString, FVariant>& CSDAndAdditionalOptions) override;
	bool ResetToCleanStart() override;

	TSharedPtr<IElectraDecoderDefaultOutputFormat, ESPMode::ThreadSafe> GetDefaultOutputFormatFromCSD(const TMap<FString, FVariant>& CSDAndAdditionalOptions) override;

	EDecoderError DecodeAccessUnit(const FInputAccessUnit& InInputAccessUnit, const TMap<FString, FVariant>& InAdditionalOptions) override;
	EDecoderError SendEndOfData() override;
	EDecoderError Flush() override;
	EOutputStatus HaveOutput() override;
	TSharedPtr<IElectraDecoderOutput, ESPMode::ThreadSafe> GetOutput() override;

	void Suspend() override
	{ }
	void Resume() override
	{ }

private:

	static constexpr uint32 Make4CC(const uint8 A, const uint8 B, const uint8 C, const uint8 D)
	{
		return (static_cast<uint32>(A) << 24) | (static_cast<uint32>(B) << 16) | (static_cast<uint32>(C) << 8) | static_cast<uint32>(D);
	}

	static constexpr int32 OpusSamplingRate()
	{
		// Opus internal rate is fixed at 48kHz, no matter what the source rate has been.
		return 48000;
	}

	bool Parse_dOps();

	bool PostError(int32 ApiReturnValue, FString Message, int32 Code);

	bool InternalDecoderCreate();
	void InternalDecoderDestroy();

	bool SetupChannelMap();
	bool ProcessInput(const void* InData, int64 InDataSize);

	IElectraDecoder::FError LastError;

	OpusMSDecoder* DecoderHandle = nullptr;

	uint32 Codec4CC = 0;
	TSharedPtr<FElectraAudioDecoderOutputOpus_Common, ESPMode::ThreadSafe> CurrentOutput;
	int32 RemainingPreSkip = -1;
	bool bFlushPending = false;

	// Input configuration
	TArray<uint8> dOpsBox;
	bool bHaveParseddOps = false;
	int32 CfgNumberOfOutputChannels = 0;
	int32 CfgSampleRate = 0;
	int32 CfgPreSkip = 0;
	int32 CfgOutputGain = 0;
	int32 CfgChannelMappingFamily = 0;
	int32 CfgStreamCount = 0;
	int32 CfgCoupledCount = 0;
	uint8 CfgChannelMapping[256] = {255};
	
	// Output
	TArray<IElectraDecoderAudioOutput::EChannelPosition> OutputChannelMap;
};


/*********************************************************************************************************************/
/*********************************************************************************************************************/
/*********************************************************************************************************************/

class FElectraCommonAudioOpusDecoderFactory : public IElectraCodecFactory, public IElectraCodecModularFeature, public TSharedFromThis<FElectraCommonAudioOpusDecoderFactory, ESPMode::ThreadSafe>
{
public:
	virtual ~FElectraCommonAudioOpusDecoderFactory()
	{ }

	void GetListOfFactories(TArray<TWeakPtr<IElectraCodecFactory, ESPMode::ThreadSafe>>& OutCodecFactories) override
	{
		OutCodecFactories.Add(AsShared());
	}

	int32 SupportsFormat(const FString& InCodecFormat, bool bInEncoder, const TMap<FString, FVariant>& InOptions) const override
	{
		// Quick check if this is an ask for an encoder or for a 4CC we do not support.
		if (bInEncoder || !Permitted4CCs.Contains(InCodecFormat))
		{
			return 0;
		}
		return 5;
	}

	void GetConfigurationOptions(TMap<FString, FVariant>& OutOptions) const override
	{
		FElectraOpusDecoder::GetConfigurationOptions(OutOptions);
	}

	TSharedPtr<IElectraDecoder, ESPMode::ThreadSafe> CreateDecoderForFormat(const FString& InCodecFormat, const TMap<FString, FVariant>& InOptions, TSharedPtr<IElectraDecoderResourceDelegate, ESPMode::ThreadSafe> InResourceDelegate) override
	{
		return MakeShared<FElectraOpusDecoder, ESPMode::ThreadSafe>(InOptions, InResourceDelegate);
	}

	static TSharedPtr<FElectraCommonAudioOpusDecoderFactory, ESPMode::ThreadSafe> Self;
	static TArray<FString> Permitted4CCs;
};
TSharedPtr<FElectraCommonAudioOpusDecoderFactory, ESPMode::ThreadSafe> FElectraCommonAudioOpusDecoderFactory::Self;
TArray<FString> FElectraCommonAudioOpusDecoderFactory::Permitted4CCs = { TEXT("Opus") };

/*********************************************************************************************************************/
/*********************************************************************************************************************/
/*********************************************************************************************************************/

void FElectraMediaOpusDecoder::Startup()
{
	// Make sure the codec factory module has been loaded.
	FModuleManager::Get().LoadModule(TEXT("ElectraCodecFactory"));

	// Create an instance of the factory, which is also the modular feature.
	check(!FElectraCommonAudioOpusDecoderFactory::Self.IsValid());
	FElectraCommonAudioOpusDecoderFactory::Self = MakeShared<FElectraCommonAudioOpusDecoderFactory, ESPMode::ThreadSafe>();
	// Register as modular feature.
	IModularFeatures::Get().RegisterModularFeature(IElectraCodecFactoryModule::GetModularFeatureName(), FElectraCommonAudioOpusDecoderFactory::Self.Get());

	//const char* OpusVer = opus_get_version_string();
}

void FElectraMediaOpusDecoder::Shutdown()
{
	IModularFeatures::Get().UnregisterModularFeature(IElectraCodecFactoryModule::GetModularFeatureName(), FElectraCommonAudioOpusDecoderFactory::Self.Get());
	FElectraCommonAudioOpusDecoderFactory::Self.Reset();
}

TSharedPtr<IElectraCodecFactory, ESPMode::ThreadSafe> FElectraMediaOpusDecoder::CreateFactory()
{
	return MakeShared<FElectraCommonAudioOpusDecoderFactory, ESPMode::ThreadSafe>();;
}

/*********************************************************************************************************************/
/*********************************************************************************************************************/
/*********************************************************************************************************************/

FElectraOpusDecoder::FElectraOpusDecoder(const TMap<FString, FVariant>& InOptions, TSharedPtr<IElectraDecoderResourceDelegate, ESPMode::ThreadSafe> InResourceDelegate)
{
	Codec4CC = (uint32)ElectraDecodersUtil::GetVariantValueSafeU64(InOptions, TEXT("codec_4cc"), 0);
	if (InOptions.Contains(TEXT("$dOps_box")))
	{
		dOpsBox = ElectraDecodersUtil::GetVariantValueUInt8Array(InOptions, TEXT("$dOps_box"));
	}
}

FElectraOpusDecoder::~FElectraOpusDecoder()
{
	// Close() must have been called already!
	check(LastError.Code == ERRCODE_INTERNAL_ALREADY_CLOSED);
	// We do it nonetheless...
	Close();
}

bool FElectraOpusDecoder::Parse_dOps()
{
	if (dOpsBox.Num() == 0)
	{
		return PostError(0, TEXT("There is no 'dOps' box to get Opus information from"), ERRCODE_INTERNAL_FAILED_TO_PARSE_CSD);
	}
	else if (dOpsBox.Num() < 11)
	{
		return PostError(0, TEXT("Incomplete 'dOps' box"), ERRCODE_INTERNAL_FAILED_TO_PARSE_CSD);
	}

	ElectraDecodersUtil::FMP4AtomReader rd(dOpsBox.GetData(), dOpsBox.Num());
	uint32 Value32;
	uint16 Value16;
	uint8 Value8;
	rd.Read(Value8);		// Version
	if (Value8 != 0)
	{
		return PostError(0, TEXT("Unsupported 'dOps' box version"), ERRCODE_INTERNAL_FAILED_TO_PARSE_CSD);
	}
	rd.Read(Value8);		// OutputChannelCount
	rd.Read(Value16);		// PreSkip
	rd.Read(Value32);		// InputSampleRate
	CfgNumberOfOutputChannels = Value8;
	CfgPreSkip = Value16;
	CfgSampleRate = Value32;
	rd.Read(Value16);		// OutputGain
	CfgOutputGain = Value16;
	rd.Read(Value8);		// ChannelMappingFamily
	CfgChannelMappingFamily = Value8;
	if (CfgChannelMappingFamily)
	{
		/*
			Channel mapping family:
				0 : mono, L/R stereo
				1 : 1-8 channel surround
				2 : Ambisonics with individual channels
				3 : Ambisonics with demixing matrix
				255 : discrete channels
		*/
		if (CfgChannelMappingFamily == 2 || CfgChannelMappingFamily == 3)
		{
			return PostError(0, TEXT("Ambisonics is not supported"), ERRCODE_INTERNAL_FAILED_TO_PARSE_CSD);
		}
		else if (CfgChannelMappingFamily > 3 && CfgChannelMappingFamily < 255)
		{
			return PostError(0, TEXT("Unsupported channel mapping family"), ERRCODE_INTERNAL_FAILED_TO_PARSE_CSD);
		}
		if (rd.GetNumBytesRemaining() < 2+CfgNumberOfOutputChannels)
		{
			return PostError(0, TEXT("Incomplete 'dOps' box"), ERRCODE_INTERNAL_FAILED_TO_PARSE_CSD);
		}
		rd.Read(Value8);	// StreamCount
		CfgStreamCount = Value8;
		rd.Read(Value8);	// CoupledCount
		CfgCoupledCount = Value8;
		// Pre-set channel mapping array to all disabled.
		FMemory::Memset(CfgChannelMapping, 255);
		/*
			The channel mapping array determines the order in which the input stream is mapped to the
			output channels. The default order produces output according to the Vorbis order
			 (see: https://www.xiph.org/vorbis/doc/Vorbis_I_spec.html#x1-810004.3.9 )
			for 1-8 channels. More than 8 channels have essentially no defined order.

			We could change the order here to produce output in any which order we like, including
			disabling channels we are not interested in, but we do not do this here as we have our
			own channel mapper that we use for this.

			Internally Opus first decodes coupled streams, followed by uncoupled ones.
			Coupled streams are stereo and thus have 2 channels, while uncoupled streams are single channel only.
			The index values in this table are defined like this:
				For j=0; j<OutputChannelCount:
					Index = CfgChannelMapping[j];
					If Index == 255:
						OutputChannel[j] is made silent
					Elseif Index < 2*CoupledCount:
						OutputChannel[j] is taken from coupled stream [Index/2][Index&1]
					Else:
						OutputChannel[j] is taken from uncoupled stream [Index - CoupledCount]

			You do not necessarily know which stream contains which original input channel, which is why
			the default mapping table exists and indicates the abovementioned Vorbis mapping.
		*/
		for(int32 i=0; i<CfgNumberOfOutputChannels; ++i)
		{
			rd.Read(CfgChannelMapping[i]);
		}
	}
	else
	{
		CfgStreamCount = 1;
		CfgCoupledCount = CfgNumberOfOutputChannels > 1 ? 1 : 0;
		CfgChannelMapping[0] = 0;
		CfgChannelMapping[1] = 1;
	}
	bHaveParseddOps = true;
	return true;
}



bool FElectraOpusDecoder::InternalDecoderCreate()
{
	if (!DecoderHandle)
	{
		int32 DecoderAllocSize = (int32) opus_multistream_decoder_get_size(CfgStreamCount, CfgCoupledCount);
		if (DecoderAllocSize < 0)
		{
			return PostError(DecoderAllocSize, TEXT("opus_multistream_decoder_get_size() failed"), ERRCODE_INTERNAL_FAILED_TO_DECODE_INPUT);
		}
		DecoderHandle = (OpusMSDecoder*) FMemory::Malloc(DecoderAllocSize);
		int32 Result = opus_multistream_decoder_init(DecoderHandle, OpusSamplingRate(), CfgNumberOfOutputChannels, CfgStreamCount, CfgCoupledCount, CfgChannelMapping);
		if (Result != OPUS_OK)
		{
			return PostError(Result, TEXT("opus_multistream_decoder_init() failed"), ERRCODE_INTERNAL_FAILED_TO_DECODE_INPUT);
		}
	}
	return true;
}

void FElectraOpusDecoder::InternalDecoderDestroy()
{
	if (DecoderHandle)
	{
		FMemory::Free(DecoderHandle);
		DecoderHandle = nullptr;
	}
}


void FElectraOpusDecoder::GetFeatures(TMap<FString, FVariant>& OutFeatures) const
{
	GetConfigurationOptions(OutFeatures);
}

IElectraDecoder::FError FElectraOpusDecoder::GetError() const
{
	return LastError;
}

bool FElectraOpusDecoder::PostError(int32 ApiReturnValue, FString Message, int32 Code)
{
	LastError.Code = Code;
	LastError.SdkCode = ApiReturnValue;
	LastError.Message = MoveTemp(Message);
	return false;
}

void FElectraOpusDecoder::Close()
{
	ResetToCleanStart();
	InternalDecoderDestroy();
	// Set the error state that all subsequent calls will fail.
	PostError(0, TEXT("Already closed"), ERRCODE_INTERNAL_ALREADY_CLOSED);
}

IElectraDecoder::ECSDCompatibility FElectraOpusDecoder::IsCompatibleWith(const TMap<FString, FVariant>& CSDAndAdditionalOptions)
{
	return IElectraDecoder::ECSDCompatibility::Compatible;
}

bool FElectraOpusDecoder::ResetToCleanStart()
{
	bFlushPending = false;
	CurrentOutput.Reset();
	RemainingPreSkip = -1;

	bHaveParseddOps = false;
	CfgNumberOfOutputChannels = 0;
	CfgSampleRate = 0;
	CfgPreSkip = 0;
	CfgOutputGain = 0;
	CfgChannelMappingFamily = 0;
	CfgStreamCount = 0;
	CfgCoupledCount = 0;
	OutputChannelMap.Empty();
	return true;
}

TSharedPtr<IElectraDecoderDefaultOutputFormat, ESPMode::ThreadSafe> FElectraOpusDecoder::GetDefaultOutputFormatFromCSD(const TMap<FString, FVariant>& CSDAndAdditionalOptions)
{
	return nullptr;
}

bool FElectraOpusDecoder::SetupChannelMap()
{
	if (OutputChannelMap.Num())
	{
		return true;
	}
	// Pre-init with all channels disabled.
	OutputChannelMap.Empty();
	OutputChannelMap.Init(IElectraDecoderAudioOutput::EChannelPosition::Disabled, CfgNumberOfOutputChannels);

	if (CfgChannelMappingFamily == 0)
	{
		if (CfgNumberOfOutputChannels != 1 && CfgNumberOfOutputChannels != 2)
		{
			return PostError(0, FString::Printf(TEXT("Unsupported number of channels (%d) for mapping family 0"), CfgNumberOfOutputChannels), ERRCODE_INTERNAL_UNSUPPORTED_CHANNEL_LAYOUT);
		}
		if (CfgNumberOfOutputChannels == 1)
		{
			OutputChannelMap[0] = IElectraDecoderAudioOutput::EChannelPosition::C;
		}
		else if (CfgNumberOfOutputChannels == 2)
		{
			OutputChannelMap[0] = IElectraDecoderAudioOutput::EChannelPosition::L;
			OutputChannelMap[1] = IElectraDecoderAudioOutput::EChannelPosition::R;
		}
	}
	else if (CfgChannelMappingFamily == 1)
	{
		// Channel mapping family 1 provides a default channel mapping table in Vorbis channel order.
		// We use that order to remap the channels to our positions.
		if (CfgNumberOfOutputChannels == 1)
		{
			OutputChannelMap[0] = IElectraDecoderAudioOutput::EChannelPosition::C;
		}
		else if (CfgNumberOfOutputChannels == 2)
		{
			OutputChannelMap[0] = IElectraDecoderAudioOutput::EChannelPosition::L;
			OutputChannelMap[1] = IElectraDecoderAudioOutput::EChannelPosition::R;
		}
		else if (CfgNumberOfOutputChannels == 3)
		{
			OutputChannelMap[0] = IElectraDecoderAudioOutput::EChannelPosition::L;
			OutputChannelMap[1] = IElectraDecoderAudioOutput::EChannelPosition::C;
			OutputChannelMap[2] = IElectraDecoderAudioOutput::EChannelPosition::R;
		}
		else if (CfgNumberOfOutputChannels == 4)
		{
			OutputChannelMap[0] = IElectraDecoderAudioOutput::EChannelPosition::L;
			OutputChannelMap[1] = IElectraDecoderAudioOutput::EChannelPosition::R;
			OutputChannelMap[2] = IElectraDecoderAudioOutput::EChannelPosition::Ls;
			OutputChannelMap[3] = IElectraDecoderAudioOutput::EChannelPosition::Rs;
		}
		else if (CfgNumberOfOutputChannels == 5)
		{
			OutputChannelMap[0] = IElectraDecoderAudioOutput::EChannelPosition::L;
			OutputChannelMap[1] = IElectraDecoderAudioOutput::EChannelPosition::C;
			OutputChannelMap[2] = IElectraDecoderAudioOutput::EChannelPosition::R;
			OutputChannelMap[3] = IElectraDecoderAudioOutput::EChannelPosition::Ls;
			OutputChannelMap[4] = IElectraDecoderAudioOutput::EChannelPosition::Rs;
		}
		else if (CfgNumberOfOutputChannels == 6)
		{
			OutputChannelMap[0] = IElectraDecoderAudioOutput::EChannelPosition::L;
			OutputChannelMap[1] = IElectraDecoderAudioOutput::EChannelPosition::C;
			OutputChannelMap[2] = IElectraDecoderAudioOutput::EChannelPosition::R;
			OutputChannelMap[3] = IElectraDecoderAudioOutput::EChannelPosition::Ls;
			OutputChannelMap[4] = IElectraDecoderAudioOutput::EChannelPosition::Rs;
			OutputChannelMap[5] = IElectraDecoderAudioOutput::EChannelPosition::LFE;
		}
		else if (CfgNumberOfOutputChannels == 7)
		{
			OutputChannelMap[0] = IElectraDecoderAudioOutput::EChannelPosition::L;
			OutputChannelMap[1] = IElectraDecoderAudioOutput::EChannelPosition::C;
			OutputChannelMap[2] = IElectraDecoderAudioOutput::EChannelPosition::R;
			OutputChannelMap[3] = IElectraDecoderAudioOutput::EChannelPosition::Ls;
			OutputChannelMap[4] = IElectraDecoderAudioOutput::EChannelPosition::Rs;
			OutputChannelMap[5] = IElectraDecoderAudioOutput::EChannelPosition::Cs;
			OutputChannelMap[6] = IElectraDecoderAudioOutput::EChannelPosition::LFE;
		}
		else if (CfgNumberOfOutputChannels == 8)
		{
			OutputChannelMap[0] = IElectraDecoderAudioOutput::EChannelPosition::L;
			OutputChannelMap[1] = IElectraDecoderAudioOutput::EChannelPosition::C;
			OutputChannelMap[2] = IElectraDecoderAudioOutput::EChannelPosition::R;
			OutputChannelMap[3] = IElectraDecoderAudioOutput::EChannelPosition::Ls;
			OutputChannelMap[4] = IElectraDecoderAudioOutput::EChannelPosition::Rs;
			OutputChannelMap[5] = IElectraDecoderAudioOutput::EChannelPosition::Lsr;
			OutputChannelMap[6] = IElectraDecoderAudioOutput::EChannelPosition::Rsr;
			OutputChannelMap[7] = IElectraDecoderAudioOutput::EChannelPosition::LFE;
		}
		else
		{
			return PostError(0, FString::Printf(TEXT("Unsupported number of channels (%d) for mapping family 1"), CfgNumberOfOutputChannels), ERRCODE_INTERNAL_UNSUPPORTED_CHANNEL_LAYOUT);
		}
	}
	else if (CfgChannelMappingFamily == 255)
	{
		// Unspecified order
		for(int32 i=0; i<CfgNumberOfOutputChannels; ++i)
		{
			OutputChannelMap[i] = static_cast<IElectraDecoderAudioOutput::EChannelPosition>(static_cast<int32>(IElectraDecoderAudioOutput::EChannelPosition::Unspec0) + i);
		}
	}
	else
	{
		return PostError(0, FString::Printf(TEXT("Unsupported channel mapping family (%d)"), CfgChannelMappingFamily), ERRCODE_INTERNAL_UNSUPPORTED_CHANNEL_LAYOUT);
	}
	return true;
}


bool FElectraOpusDecoder::ProcessInput(const void* InData, int64 InDataSize)
{
	if (!DecoderHandle)
	{
		return false;
	}

	// Set the pre-skip if necessary (first packet)
	if (RemainingPreSkip < 0)
	{
		RemainingPreSkip = CfgPreSkip;
	}

	int32 NumExpectedDecodedSamples = opus_packet_get_nb_samples(reinterpret_cast<const unsigned char*>(InData), (int32)InDataSize, OpusSamplingRate());
	if (NumExpectedDecodedSamples < 0)
	{
		return PostError(NumExpectedDecodedSamples, TEXT("opus_packet_get_nb_samples() failed"), ERRCODE_INTERNAL_FAILED_TO_DECODE_INPUT);
	}

	int32 AllocSize = sizeof(float) * CfgNumberOfOutputChannels * NumExpectedDecodedSamples;
	CurrentOutput->Buffer = (float*)FMemory::Malloc(AllocSize);
	CurrentOutput->ChannelPositions = OutputChannelMap;
	CurrentOutput->NumChannels = CfgNumberOfOutputChannels;
	CurrentOutput->SampleRate = OpusSamplingRate();

	int32 Result = opus_multistream_decode_float(DecoderHandle, reinterpret_cast<const unsigned char*>(InData), (int32)InDataSize, CurrentOutput->Buffer, NumExpectedDecodedSamples, 0);
	if (Result < 0)
	{
		return PostError(Result, TEXT("opus_multistream_decode_float() failed"), ERRCODE_INTERNAL_FAILED_TO_DECODE_INPUT);
	}
	CurrentOutput->NumFrames = Result;

#if 0
	/*
		We do not need to apply the pre-skip here right now. It must be included in an edit list which offsets
		the PTS accordingly and the audio decoder will adjust the output accordingly.
		
		We leave the code here in case the audio decoder will not do this in which case it should be easy
		to activate this bit via some configuration option.
	*/

	// Apply pre skip
	if (RemainingPreSkip > 0)
	{
		// Entire output to be discarded?
		if (RemainingPreSkip >= CurrentOutput->NumFrames)
		{
			CurrentOutput.Reset();
		}
		else
		{
			CurrentOutput->PreSkip = RemainingPreSkip;
			// We need to advance the PTS of this output.
			FTimespan SkipTime(ETimespan::TicksPerSecond * RemainingPreSkip / CurrentOutput->SampleRate);
			CurrentOutput->PTS += SkipTime;
		}
		// Done?
		if ((RemainingPreSkip -= CurrentOutput->NumFrames) < 0)
		{
			RemainingPreSkip = 0;
		}
	}
#endif
	return true;
}

IElectraDecoder::EDecoderError FElectraOpusDecoder::DecodeAccessUnit(const FInputAccessUnit& InInputAccessUnit, const TMap<FString, FVariant>& InAdditionalOptions)
{
	// If already in error do nothing!
	if (LastError.IsSet())
	{
		return IElectraDecoder::EDecoderError::Error;
	}

	// Can not feed new input until draining has finished.
	if (bFlushPending)
	{
		return IElectraDecoder::EDecoderError::EndOfData;
	}

	// If there is pending output it is very likely that decoding this access unit would also generate output.
	// Since that would result in loss of the pending output we return now.
	if (CurrentOutput.IsValid())
	{
		return IElectraDecoder::EDecoderError::NoBuffer;
	}

	// Decode data.
	if (InInputAccessUnit.Data && InInputAccessUnit.DataSize)
	{
		// Parse the codec specific information
		if (!bHaveParseddOps && !Parse_dOps())
		{
			return IElectraDecoder::EDecoderError::Error;
		}
		// Set up the channel map accordingly.
		if (!SetupChannelMap())
		{
			// Error was already posted.
			return IElectraDecoder::EDecoderError::Error;
		}
		// Create decoder if necessary.
		if (!DecoderHandle && !InternalDecoderCreate())
		{
			return IElectraDecoder::EDecoderError::Error;
		}
		// Prepare the output
		if (!CurrentOutput.IsValid())
		{
			CurrentOutput = MakeShared<FElectraAudioDecoderOutputOpus_Common>();
			CurrentOutput->PTS = InInputAccessUnit.PTS;
			CurrentOutput->UserValue = InInputAccessUnit.UserValue;
		}
		// Decode
		if (!ProcessInput(InInputAccessUnit.Data, InInputAccessUnit.DataSize))
		{
			CurrentOutput.Reset();
			return IElectraDecoder::EDecoderError::Error;
		}
	}
	return IElectraDecoder::EDecoderError::None;
}

IElectraDecoder::EDecoderError FElectraOpusDecoder::SendEndOfData()
{
	// If already in error do nothing!
	if (LastError.IsSet())
	{
		return IElectraDecoder::EDecoderError::Error;
	}
	// Already draining?
	if (bFlushPending)
	{
		return IElectraDecoder::EDecoderError::EndOfData;
	}
	bFlushPending = true;
	return IElectraDecoder::EDecoderError::None;
}

IElectraDecoder::EDecoderError FElectraOpusDecoder::Flush()
{
	// If already in error do nothing!
	if (LastError.IsSet())
	{
		return IElectraDecoder::EDecoderError::Error;
	}
	ResetToCleanStart();
	return IElectraDecoder::EDecoderError::None;
}

IElectraDecoder::EOutputStatus FElectraOpusDecoder::HaveOutput()
{
	// If already in error do nothing!
	if (LastError.IsSet())
	{
		return IElectraDecoder::EOutputStatus::Error;
	}
	// Have output?
	if (CurrentOutput.IsValid())
	{
		return IElectraDecoder::EOutputStatus::Available;
	}
	// Pending flush?
	if (bFlushPending)
	{
		bFlushPending = false;
		RemainingPreSkip = -1;
		return IElectraDecoder::EOutputStatus::EndOfData;
	}
	return IElectraDecoder::EOutputStatus::NeedInput;
}

TSharedPtr<IElectraDecoderOutput, ESPMode::ThreadSafe> FElectraOpusDecoder::GetOutput()
{
	TSharedPtr<IElectraDecoderOutput, ESPMode::ThreadSafe> Out = CurrentOutput;
	CurrentOutput.Reset();
	return Out;
}
