// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PlayerCore.h"
#include "BitDataStream.h"

namespace Electra
{
	namespace MPEG
	{

		class FAACDecoderConfigurationRecord
		{
		public:
			FAACDecoderConfigurationRecord();

			bool ParseFrom(const void* Data, int64 Size);
			void Reset();
			const TArray<uint8>& GetCodecSpecificData() const;

			int32		SBRSignal;
			int32		PSSignal;
			uint32		ChannelConfiguration;
			uint32		SamplingFrequencyIndex;
			uint32		SamplingRate;
			uint32		ExtSamplingFrequencyIndex;
			uint32		ExtSamplingFrequency;
			uint32		AOT;
			uint32		ExtAOT;
		private:
			TArray<uint8>	CodecSpecificData;
		};




	} // namespace MPEG
} // namespace Electra

