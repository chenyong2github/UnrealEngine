// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundWave.h"
#include "MetasoundPrimitives.h"
#include "IAudioCodec.h"
#include "IAudioCodecRegistry.h"
#include "Sound/SoundWave.h"

namespace Metasound
{	
	// Simple for now, just wrap around the TArray<uint8>
	class FWaveDecoderInput : public Audio::FDecoderInputArrayView
	{
		FWaveReadRef WaveRef;	// These are not thread safe, and will need to be
	public:
		FWaveDecoderInput(const FWaveReadRef& InWaveRef)
			: Audio::FDecoderInputArrayView(MakeArrayView(InWaveRef->CompressedBytes.GetData(), InWaveRef->CompressedBytes.Num()), 0)
			, WaveRef(InWaveRef)
		{}
	};

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
	
	FWave::FWave(USoundWave* InWave) 
	{
		DoInlineEncode(InWave,CompressedBytes);
	}

	FWave::FWave(const TArray<uint8>& InBytes) 
		: CompressedBytes(InBytes)
	{
	}

	FWave::FDecoderInputPtr FWave::CreateDecoderInput(const FWaveReadRef& InWaveRef)
	{
		if( InWaveRef->CompressedBytes.Num() > 0)
		{
			return MakeShared<FWaveDecoderInput, ESPMode::ThreadSafe>(InWaveRef);
		}
		return nullptr;
	}
}
