// Copyright Epic Games, Inc. All Rights Reserved.

#include "DecoderBackCompat.h"
#include "DecoderInputBackCompat.h"
#include "AudioDecompress.h"

namespace Audio
{
	static const FCodecDetails Details =
	{
		TEXT("BackCompat"),
		TEXT("BackCompat"),
		1,
		{ FCodecFeatures::HasDecoder }
	};

	bool FBackCompatCodec::SupportsPlatform(FName InPlatformName) const 
	{
		// Everything right now.
		return true;
	}
	const FCodecDetails& FBackCompatCodec::GetDetails() const 
	{
		return Details;
	}

	const FCodecDetails& FBackCompatCodec::GetDetailsStatic()
	{
		return Details;
	}

	ICodec::FDecoderPtr FBackCompatCodec::CreateDecoder(
		IDecoder::FDecoderInputPtr InSrc,
		IDecoder::FDecoderOutputPtr InDst)
	{
		return MakeUnique<FBackCompat>(
			InSrc,
			InDst
		);
	}

	FBackCompat::FBackCompat(IDecoder::FDecoderInputPtr InSrc, IDecoder::FDecoderOutputPtr InDst) 
		: Src(InSrc)
		, Dst(InDst)
	{
		audio_ensure(InSrc->FindSection(Desc));
		FDecodedFormatInfo Info { Desc.NumChannels, Desc.NumFramesPerSec, EBitRepresentation::Int16_Interleaved };
		Reqs = Dst->GetRequirements(Info);
		ResidualBuffer.SetNum(Reqs.NumSampleFramesWanted * Desc.NumChannels);
	}

	Audio::IDecoder::FDecodeReturn FBackCompat::Decode(bool bLoop)
	{
		uint32 NumFramesRemaining = Reqs.NumSampleFramesWanted;
		FBackCompatInput& BackCompatSrc = static_cast<FBackCompatInput&>(*Src);
		ICompressedAudioInfo* Info = BackCompatSrc.GetInfo();
		
		bool bFinished = false;
		IDecoderOutput::FPushedAudioDetails PushedDetails(
			Desc.NumFramesPerSec,
			Desc.NumChannels,
			FrameOffset
		);

		uint32 BuffSizeInBytes = ResidualBuffer.Num() * sizeof(int16);
		uint32 BuffSizeInFrames = Reqs.NumSampleFramesWanted;
		uint8* Buff = (uint8*)ResidualBuffer.GetData();

		while(!bFinished && NumFramesRemaining > 0)
		{
			int32 NumBytesStreamed = BuffSizeInBytes;
			if (BackCompatSrc.Wave->IsStreaming())
			{
				bFinished = Info->StreamCompressedData(Buff, bLoop, BuffSizeInBytes, NumBytesStreamed);
			}
			else
			{
				bFinished = Info->ReadCompressedData(Buff, bLoop, BuffSizeInBytes);
			}						
			PushedDetails.SampleFramesStartOffset = FrameOffset;
			ResidualBuffer.SetNum(NumBytesStreamed / sizeof(int16));
			
			Dst->PushAudio( 
				PushedDetails,
				MakeArrayView(ResidualBuffer)
			);
			
			FrameOffset += BuffSizeInFrames;
			NumFramesRemaining -= BuffSizeInFrames;
		}
		
		if(!bFinished)
		{
			return EDecodeResult::MoreDataRemaining;
		}
		else if (bLoop) // (bFinished is true)
		{
			return EDecodeResult::Looped;
		}
		else // (bFinished is true and we aren't looping)
		{
			return EDecodeResult::Finished;
		}
	}

}