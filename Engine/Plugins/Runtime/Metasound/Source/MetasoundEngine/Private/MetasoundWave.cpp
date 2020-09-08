// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundWave.h"
#include "MetasoundPrimitives.h"
#include "IAudioCodec.h"
#include "IAudioCodecRegistry.h"
#include "Sound/SoundWave.h"
#include "DecoderInputFactory.h"
#include "AudioDeviceManager.h"
#include "AudioDevice.h"

namespace Metasound
{	
	// Small RAII helper.
	struct FScopedBulkDataLock
	{
		bool bLocked = false;
		FByteBulkData& Bd;
		FScopedBulkDataLock(FByteBulkData& InBd) 
			: Bd(InBd) 
		{}

		const void* LockReadOnly()
		{
			if (ensure(bLocked==false))
			{
				if (const void* Ptr =Bd.LockReadOnly() )
				{
					bLocked = true;
					return Ptr;
				}
			}
			return nullptr;
		}

		~FScopedBulkDataLock() 
		{ 
			if (bLocked) 
			{
				Bd.Unlock();
			}
		}
	};

	// Temp. We don't have any cooking at the moment, so do the encode here.
	bool DoInlineEncode(
		USoundWave* InWave,
		TArray<uint8>& OutCompressedBytes)
	{
#if WITH_EDITORONLY_DATA
		using namespace Audio;		
		FScopeLock Lock(&InWave->RawDataCriticalSection);
		FByteBulkData& Raw = InWave->RawData;
		FScopedBulkDataLock BdLock(Raw);
		if (const void* RawPtr = BdLock.LockReadOnly())
		{
			IEncoderInput::FFormat Format { static_cast<uint32>(InWave->NumChannels), static_cast<uint32>(InWave->GetSampleRateForCurrentPlatform()), EBitRepresentation::Int16_Interleaved };
			TUniquePtr<IEncoderInput> Input = IEncoderInput::Create(
				MakeArrayView(static_cast<const int16*>(RawPtr), Raw.GetBulkDataSize() / sizeof(int16)),
				Format
			);

			if (ICodecRegistry::FCodecPtr DefaultCodec = ICodecRegistry::Get().FindDefaultCodec() )
			{
				ICodec::FEncoderPtr Encoder = DefaultCodec->CreateEncoder(Input.Get());
				if (Encoder)
				{
					// Encode default everything.
					return Encoder->Encode(
						Input.Get(),
						IEncoderOutput::Create(OutCompressedBytes).Get(),
						nullptr
					);
				}
			}
		}		
		return false;
#else //WITH_EDITORONLY_DATA
		return true;
#endif //WITH_EDITORONLY_DATA
	}
	

	FWaveAsset::FDecoderInputPtr FWaveAsset::CreateDecoderInput(const FWaveAssetReadRef& InWaveRef)
	{				
		FName OldFormat = FAudioDeviceManager::Get()->GetActiveAudioDevice()->GetRuntimeFormat(
			const_cast<USoundWave*>(InWaveRef->GetSoundWave())
		);

		TUniquePtr<Audio::IDecoderInput> Input = Audio::CreateBackCompatDecoderInput(
			OldFormat,
			InWaveRef->GetSoundWave()
		);		
		return FWaveAsset::FDecoderInputPtr(Input.Release());

		return nullptr;
	}
}
