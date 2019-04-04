// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "WmfAudioEncoder.h"
#include "GameplayMediaEncoderSample.h"

GAMEPLAYMEDIAENCODER_START

FWmfAudioEncoder::FWmfAudioEncoder(const FOutputSampleCallback& OutputCallback) :
	OutputCallback(OutputCallback)
{}

bool FWmfAudioEncoder::Initialize(const FWmfAudioEncoderConfig& InConfig)
{
	if (InConfig.SampleRate != 44100 && InConfig.SampleRate != 48000)
	{
		UE_LOG(GameplayMediaEncoder, Error, TEXT("AAC SampleRate must be 44100 or 48000, configured: %d. see: https://docs.microsoft.com/en-us/windows/desktop/medfound/aac-encoder"), InConfig.SampleRate);
		return false;
	}
	if (InConfig.NumChannels != 1 && InConfig.NumChannels != 2 && InConfig.NumChannels != 6)
	{
		UE_LOG(GameplayMediaEncoder, Error, TEXT("AAC NumChannels must be 1, 2 or 6 (5.1), configured: %d. see: https://docs.microsoft.com/en-us/windows/desktop/medfound/aac-encoder"), InConfig.NumChannels);
		return false;
	}

	UE_LOG(GameplayMediaEncoder, Log, TEXT("AudioEncoder config: %d channels, %d Hz, %.2f Kbps"), InConfig.NumChannels, InConfig.SampleRate, InConfig.Bitrate / 1000.0f);

	Config = InConfig;

	CHECK_HR(CoCreateInstance(CLSID_AACMFTEncoder, nullptr, CLSCTX_INPROC_SERVER, IID_IMFTransform, reinterpret_cast<void**>(&Encoder)));

	if (!SetInputType() || !SetOutputType() || !RetrieveStreamInfo() || !StartStreaming())
	{
		Encoder->Release();
		return false;
	}

	return true;
}

bool FWmfAudioEncoder::SetInputType()
{
	TRefCountPtr<IMFMediaType> MediaType;
	CHECK_HR(MFCreateMediaType(MediaType.GetInitReference()));
	CHECK_HR(MediaType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio));
	CHECK_HR(MediaType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM));
	CHECK_HR(MediaType->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16)); // the only value supported
	CHECK_HR(MediaType->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, Config.SampleRate));
	CHECK_HR(MediaType->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, Config.NumChannels));

	CHECK_HR(Encoder->SetInputType(0, MediaType, 0));

	return true;
}

bool FWmfAudioEncoder::SetOutputType()
{
	TRefCountPtr<IMFMediaType> InputType;
	CHECK_HR(Encoder->GetInputCurrentType(0, InputType.GetInitReference()));

	CHECK_HR(MFCreateMediaType(OutputType.GetInitReference()));
	CHECK_HR(OutputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio));
	CHECK_HR(OutputType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_AAC));
	CHECK_HR(OutputType->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16)); // the only value supported
	uint32 SampleRate;
	CHECK_HR(InputType->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &SampleRate));
	CHECK_HR(OutputType->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, SampleRate));
	uint32 NumChannels;
	CHECK_HR(InputType->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &NumChannels));
	CHECK_HR(OutputType->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, NumChannels));
	CHECK_HR(OutputType->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, Config.Bitrate));

	CHECK_HR(Encoder->SetOutputType(0, OutputType, 0));

	return true;
}

bool FWmfAudioEncoder::GetOutputType(TRefCountPtr<IMFMediaType>& OutType)
{
	OutType = OutputType;
	return OutputType.IsValid();
}

bool FWmfAudioEncoder::RetrieveStreamInfo()
{
	CHECK_HR(Encoder->GetInputStreamInfo(0, &InputStreamInfo));
	CHECK_HR(Encoder->GetOutputStreamInfo(0, &OutputStreamInfo));

	return true;
}

bool FWmfAudioEncoder::StartStreaming()
{
	CHECK_HR(Encoder->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0));
	CHECK_HR(Encoder->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0));

	return true;
}

bool FWmfAudioEncoder::Process(const int8* SampleData, uint32 Size, FTimespan Timestamp, FTimespan Duration)
{
	UE_LOG(GameplayMediaEncoder, Verbose, TEXT("Audio input: time %.3f, duration %.3f, size %d"), Timestamp.GetTotalSeconds(), Duration.GetTotalSeconds(), Size);

	FGameplayMediaEncoderSample InputSample{ EMediaType::Audio };
	if (!CreateInputSample(SampleData, Size, Timestamp, Duration, InputSample))
	{
		return false;
	}

	CHECK_HR(Encoder->ProcessInput(0, InputSample.GetSample(), 0));

	while (true)
	{
		FGameplayMediaEncoderSample OutputSample{ EMediaType::Audio };
		if (!GetOutputSample(OutputSample))
		{
			return false;
		}
		if (!OutputSample.IsValid())
		{
			break;
		}

		DWORD OutputSize;
		CHECK_HR(OutputSample.GetSample()->GetTotalLength(&OutputSize));

		UE_LOG(GameplayMediaEncoder, Verbose, TEXT("Audio encoded: time %.3f, duration %.3f, size %d"), OutputSample.GetTime().GetTotalSeconds(), OutputSample.GetDuration().GetTotalSeconds(), OutputSize);

		if (!OutputCallback(OutputSample))
		{
			return false;
		}
	}

	return true;
}

// TODO(andriy): consider pooling IMFSample allocations, same for video encoder

bool FWmfAudioEncoder::CreateInputSample(const int8* SampleData, uint32 Size, FTimespan Timestamp, FTimespan Duration, FGameplayMediaEncoderSample& Sample)
{
	if (!Sample.CreateSample())
	{
		return false;
	}

	int32 BufferSize = FMath::Max<int32>(InputStreamInfo.cbSize, Size);
	uint32 Alignment = InputStreamInfo.cbAlignment > 1 ? InputStreamInfo.cbAlignment - 1 : 0;
	TRefCountPtr<IMFMediaBuffer> WmfBuffer;
	CHECK_HR(MFCreateAlignedMemoryBuffer(BufferSize, Alignment, WmfBuffer.GetInitReference()));

	uint8* Dst = nullptr;
	CHECK_HR(WmfBuffer->Lock(&Dst, nullptr, nullptr));
	FMemory::Memcpy(Dst, SampleData, Size);
	CHECK_HR(WmfBuffer->Unlock());

	CHECK_HR(WmfBuffer->SetCurrentLength(Size));

	CHECK_HR(Sample.GetSample()->AddBuffer(WmfBuffer));
	Sample.SetTime(Timestamp);
	Sample.SetDuration(Duration);

	return true;
}

bool FWmfAudioEncoder::CreateOutputSample(FGameplayMediaEncoderSample& Sample)
{
	if (!Sample.CreateSample())
	{
		return false;
	}

	TRefCountPtr<IMFMediaBuffer> Buffer = nullptr;
	uint32 Alignment = OutputStreamInfo.cbAlignment > 1 ? OutputStreamInfo.cbAlignment - 1 : 0;
	CHECK_HR(MFCreateAlignedMemoryBuffer(OutputStreamInfo.cbSize, Alignment, Buffer.GetInitReference()));

	CHECK_HR(Sample.GetSample()->AddBuffer(Buffer));

	return true;
}

bool FWmfAudioEncoder::GetOutputSample(FGameplayMediaEncoderSample& Sample)
{
	bool bFlag1 = OutputStreamInfo.dwFlags&MFT_OUTPUT_STREAM_PROVIDES_SAMPLES;
	bool bFlag2 = OutputStreamInfo.dwFlags&MFT_OUTPUT_STREAM_CAN_PROVIDE_SAMPLES;

	// Right now, we are always creating our samples, so make sure MFT will be ok with that
	verify((bFlag1 == false && bFlag2 == false) || (bFlag1 == false && bFlag2 == true));

	MFT_OUTPUT_DATA_BUFFER Output = {};
	if (!CreateOutputSample(Sample))
	{
		return false;
	}
	Output.pSample = Sample.GetSample();

	DWORD Status = 0;
	HRESULT Res = Encoder->ProcessOutput(0, 1, &Output, &Status);
	TRefCountPtr<IMFCollection> Events = Output.pEvents; // unsure this is released
	if (Res == MF_E_TRANSFORM_NEED_MORE_INPUT || !Sample.IsValid())
	{
		Sample.Reset(); // do not return empty sample
		return true; // not an error, just not enough input
	}
	else if (Res == MF_E_TRANSFORM_STREAM_CHANGE)
	{
		if (!SetOutputType())
		{
			return false;
		}

		return GetOutputSample(Sample);
	}

	return SUCCEEDED(Res);
}

bool FWmfAudioEncoder::Flush()
{
	CHECK_HR(Encoder->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, 0));
	CHECK_HR(Encoder->ProcessMessage(MFT_MESSAGE_COMMAND_DRAIN, 0));

	return true;
}

GAMEPLAYMEDIAENCODER_END

