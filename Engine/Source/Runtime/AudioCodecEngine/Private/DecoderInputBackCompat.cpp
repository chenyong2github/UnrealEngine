// Copyright Epic Games, Inc. All Rights Reserved.

#include "DecoderInputBackCompat.h"
#include "AudioDevice.h"
#include "AudioDeviceManager.h"
#include "Interfaces/IAudioFormat.h"
#include "AudioDecompress.h"
#include "Sound/SoundWave.h"
#include "DecoderBackCompat.h"

namespace Audio
{	
	ICompressedAudioInfo* FBackCompatInput::GetInfo(
		FFormatDescriptorSection* OutDescriptor /*= nullptr*/) const
	{
		FAudioDeviceHandle Handle = FAudioDeviceManager::Get()->GetActiveAudioDevice();

		if (!OldInfoObject.IsValid())
		{
			OldInfoObject.Reset(Handle->CreateCompressedAudioInfo(Wave));
			audio_ensure(OldInfoObject.IsValid());

			FSoundQualityInfo Info;
			if (Wave->IsStreaming())
			{
				if (!OldInfoObject->StreamCompressedInfo(Wave, &Info))
				{
					return nullptr;
				}
			}
			else
			{
				// FSoundWaveProxy needs to be updated to support non-streaming audio
				return nullptr;

// 				if (Wave->ResourceData == nullptr)
// 				{
// 					Wave->InitAudioResource(Wave->GetRuntimeFormat());
// 				}
// 
// 				if (!OldInfoObject->ReadCompressedInfo(Wave->ResourceData, Wave->ResourceSize, &Info))
// 				{
// 					return nullptr;
// 				}
			}

			Desc.NumChannels		= Info.NumChannels;
			Desc.NumFramesPerSec	= Info.SampleRate;
			Desc.NumFrames			= (uint32)((float)Info.Duration * Info.SampleRate);
			Desc.NumBytesPerPacket	= ~0;

			Desc.CodecName			= FBackCompatCodec::GetDetailsStatic().Name;
			Desc.CodecFamilyName	= FBackCompatCodec::GetDetailsStatic().FamilyName;
			Desc.CodecVersion		= FBackCompatCodec::GetDetailsStatic().Version;		
		}
		if( OutDescriptor )
		{		
			*OutDescriptor = Desc;
		}		
		return OldInfoObject.Get();
	}

	bool FBackCompatInput::FindSection(FEncodedSectionBase& OutSection)
	{
		if (FFormatDescriptorSection::kSectionName == OutSection.SectionName)
		{
			FFormatDescriptorSection& Descriptor = static_cast<FFormatDescriptorSection&>(OutSection);
			return GetInfo(&Descriptor) != nullptr;
		}
		return false;
	}

	bool FBackCompatInput::HasError() const
	{
		return GetInfo() == nullptr;
	}

	int64 FBackCompatInput::Tell() const
	{
		// Not implemented.
		audio_ensure(false);
		return -1;
	}

	void FBackCompatInput::SeekToTime(const float InSeconds)
	{
		ICompressedAudioInfo* Info = GetInfo();
		ensure(Info);

		Info->SeekToTime(InSeconds);
	}

	bool FBackCompatInput::IsEndOfStream() const
	{
		// Not implemented.
		audio_ensure(false);
		return false;
	}

	TArrayView<const uint8> FBackCompatInput::PeekNextPacket(int32 InMaxPacketLength) const
	{
		// Not implemented.
		audio_ensure(false);
		return MakeArrayView<const uint8>(nullptr,0);
	}

	TArrayView<const uint8> FBackCompatInput::PopNextPacket(int32 InPacketSize)
	{
		// Not implemented.
		audio_ensure(false);
		return MakeArrayView<const uint8>(nullptr,0);
	}
}
