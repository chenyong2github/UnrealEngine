// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IAudioCodec.h"
#include "UObject/NameTypes.h"
#include "AudioDecompress.h"

class ICompressedAudioInfo; 	// Forward declares.

namespace Audio
{
	struct FBackCompatInput : public IDecoderInput
	{
		FName OldFormatName;
		TUniquePtr<FSoundWaveProxy> Wave;
		mutable FFormatDescriptorSection Desc;
		mutable TUniquePtr<ICompressedAudioInfo> OldInfoObject;

		FBackCompatInput(
			FName InOldFormatName,
			const FSoundWaveProxy& InWave)
			: OldFormatName(InOldFormatName)
			, Wave(MakeUnique<FSoundWaveProxy>(InWave))
		{
		}

		bool HasError() const override;
		bool IsEndOfStream() const override;

		ICompressedAudioInfo* GetInfo(
			FFormatDescriptorSection* OutDescriptor = nullptr) const;

		bool FindSection(FEncodedSectionBase& OutSection) override;
		int64 Tell() const override;
		
		TArrayView<const uint8> PeekNextPacket(
			int32 InMaxPacketLength) const override;

		TArrayView<const uint8> PopNextPacket(
			int32 InPacketSize) override;
	};
}
