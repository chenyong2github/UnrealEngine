// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PlayerCore.h"
#include "BitDataStream.h"


namespace Electra
{
	namespace MPEG
	{

		class FESDescriptor
		{
		public:
			FESDescriptor();
			void SetRawData(const void* Data, int64 Size);
			const TArray<uint8>& GetRawData() const;

			bool Parse();

			const TArray<uint8>& GetCodecSpecificData() const;

			uint32 GetBufferSize() const
			{
				return BufferSize;
			}

			uint32 GetMaxBitrate() const
			{
				return MaxBitrate;
			}

			uint32 GetAvgBitrate() const
			{
				return AvgBitrate;
			}

			// See http://mp4ra.org/#/object_types
			enum class FObjectTypeID
			{
				Unknown = 0,
				System_V1 = 1,
				System_V2 = 2,
				MPEG4_Video = 32,
				MPEG4_AVC_SPS = 33,
				MPEG4_AVC_PPS = 34,
				MPEG4_Audio = 64,
				MPEG2_SimpleVideo = 96,
				MPEG2_MainVideo = 97,
				MPEG2_SNRVideo = 98,
				MPEG2_SpatialVideo = 99,
				MPEG2_HighVideo = 100,
				MPEG2_422Video = 101,
				MPEG4_ADTS_Main = 102,
				MPEG4_ADTS_LC = 103,
				MPEG4_ADTS_SSR = 104,
				MPEG2_ADTS = 105,
				MPEG1_Video = 106,
				MPEG1_ADTS = 107,
				JPEG_Video = 108,
				Private_Audio = 192,
				Private_Video = 208,
				PCM16LE_Audio = 224,
				Vorbis_Audio = 225,
				AC3_Audio = 226,
				ALaw_Audio = 227,
				MuLaw_Audio = 228,
				G723_ADPCM_Audio = 229,
				PCM16BE = 230,
				YV12_Video = 240,
				H264_Video = 241,
				H263_Video = 242,
				H261_Video = 243,
			};
			enum class FStreamType
			{
				Unknown = 0,
				ObjectDescription = 1,
				ClockReference = 2,
				SceneDescription = 4,  	 //!< aka. visual description
				Audio = 5,
				MPEG7 = 6,
				IPMP = 7,
				OCI = 8,
				MPEG_Java = 9,
				User_Private = 32
			};

			FObjectTypeID GetObjectTypeID() const
			{
				return ObjectTypeID;
			}

			FStreamType GetStreamType() const
			{
				return StreamTypeID;
			}

		private:
			TArray<uint8>						RawData;

			TArray<uint8>						CodecSpecificData;
			FObjectTypeID						ObjectTypeID;
			FStreamType							StreamTypeID;
			uint32								BufferSize;
			uint32								MaxBitrate;
			uint32								AvgBitrate;
			uint16								ESID;
			uint16								DependsOnStreamESID;
			uint8								StreamPriority;
			bool								bDependsOnStream;
		};


	} // namespace MPEG
} // namespace Electra


