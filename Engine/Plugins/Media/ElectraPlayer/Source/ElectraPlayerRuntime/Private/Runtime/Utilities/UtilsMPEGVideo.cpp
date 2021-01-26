// Copyright Epic Games, Inc. All Rights Reserved.

#include "PlayerCore.h"
#include "Utilities/UtilsMPEGVideo.h"
#include "BitDataStream.h"


namespace Electra
{
	namespace MPEG
	{

		FAVCDecoderConfigurationRecord::FAVCDecoderConfigurationRecord()
		{
			ConfigurationVersion = 0;
			AVCProfileIndication = 0;
			ProfileCompatibility = 0;
			AVCLevelIndication = 0;
			NALUnitLength = 0;
			ChromaFormat = 0;
			BitDepthLumaMinus8 = 0;
			BitDepthChromaMinus8 = 0;
			bHaveAdditionalProfileIndication = false;
		}

		void FAVCDecoderConfigurationRecord::SetRawData(const void* Data, int64 Size)
		{
			RawData.Empty();
			if (Size)
			{
				RawData.Reserve((uint32)Size);
				RawData.SetNumUninitialized((uint32)Size);
				FMemory::Memcpy(RawData.GetData(), Data, Size);
			}
		}

		const TArray<uint8>& FAVCDecoderConfigurationRecord::GetRawData() const
		{
			return RawData;
		}

		const TArray<uint8>& FAVCDecoderConfigurationRecord::GetCodecSpecificData() const
		{
			return CodecSpecificData;
		}

		const TArray<uint8>& FAVCDecoderConfigurationRecord::GetCodecSpecificDataSPS() const
		{
			return CodecSpecificDataSPSOnly;
		}

		const TArray<uint8>& FAVCDecoderConfigurationRecord::GetCodecSpecificDataPPS() const
		{
			return CodecSpecificDataPPSOnly;
		}

		int32 FAVCDecoderConfigurationRecord::GetNumberOfSPS() const
		{
			return ParsedSPSs.Num();
		}

		const FISO14496_10_seq_parameter_set_data& FAVCDecoderConfigurationRecord::GetParsedSPS(int32 SpsIndex) const
		{
			check(SpsIndex < GetNumberOfSPS());
			return ParsedSPSs[SpsIndex];
		}


		bool FAVCDecoderConfigurationRecord::Parse()
		{
			CodecSpecificData.Empty();

			if (RawData.Num())
			{
				FByteReader BitReader(RawData.GetData(), RawData.Num());

				if (!BitReader.ReadByte(ConfigurationVersion))
				{
					return false;
				}
				if (!BitReader.ReadByte(AVCProfileIndication))
				{
					return false;
				}
				if (!BitReader.ReadByte(ProfileCompatibility))
				{
					return false;
				}
				if (!BitReader.ReadByte(AVCLevelIndication))
				{
					return false;
				}
				if (!BitReader.ReadByte(NALUnitLength))
				{
					return false;
				}
				NALUnitLength = (NALUnitLength & 3) + 1;
				uint8 nSPS = 0;
				if (!BitReader.ReadByte(nSPS))
				{
					return false;
				}
				nSPS &= 31;
				if (nSPS)
				{
					SequenceParameterSets.Reserve(nSPS);
					ParsedSPSs.Reserve(nSPS);
				}
				int32 TotalSPSSize = 0;
				for(int32 i = 0; i < nSPS; ++i)
				{
					uint16 spsLen = 0;
					if (!BitReader.ReadByte(spsLen))
					{
						return false;
					}
					TArray<uint8>& sps = SequenceParameterSets.AddDefaulted_GetRef();
					sps.Reserve(spsLen);
					sps.SetNumUninitialized(spsLen);
					if (!BitReader.ReadBytes(sps.GetData(), spsLen))
					{
						return false;
					}
					TotalSPSSize += 4 + spsLen;		// 4 because we always use 32 bit startcode and not NALUnitLength
					ParseH264SPS(ParsedSPSs.AddDefaulted_GetRef(), sps.GetData(), sps.Num());
				}
				uint8 nPPS = 0;
				if (!BitReader.ReadByte(nPPS))
				{
					return false;
				}
				if (nPPS)
				{
					PictureParameterSets.Reserve(nPPS);
				}
				int32 TotalPPSSize = 0;
				for(int32 i = 0; i < nPPS; ++i)
				{
					uint16 ppsLen = 0;
					if (!BitReader.ReadByte(ppsLen))
					{
						return false;
					}
					TArray<uint8>& pps = PictureParameterSets.AddDefaulted_GetRef();
					pps.Reserve(ppsLen);
					pps.SetNumUninitialized(ppsLen);
					if (!BitReader.ReadBytes(pps.GetData(), ppsLen))
					{
						return false;
					}
					TotalPPSSize += 4 + ppsLen;		// 4 because we always use 32 bit startcode and not NALUnitLength
				}

				if (AVCProfileIndication == 100 || AVCProfileIndication == 110 || AVCProfileIndication == 122 || AVCProfileIndication == 144)
				{
					// At least according to the ISO 14496-15:2014 standard these values must appear.
					// I do have however files that are of AVC profile 100 but omit these values.
					// Therefore let's do a quick check if we can read at least 4 more bytes.
					if (BitReader.BytesRemaining() >= 4)
					{
						if (!BitReader.ReadByte(ChromaFormat))
						{
							return false;
						}
						ChromaFormat &= 3;
						if (!BitReader.ReadByte(BitDepthLumaMinus8))
						{
							return false;
						}
						BitDepthLumaMinus8 &= 7;
						if (!BitReader.ReadByte(BitDepthChromaMinus8))
						{
							return false;
						}
						BitDepthChromaMinus8 &= 7;
						if (!BitReader.ReadByte(nSPS))
						{
							return false;
						}
						if (nSPS)
						{
							SequenceParameterSetsExt.Reserve(nSPS);
						}
						for(int32 i = 0; i < nSPS; ++i)
						{
							uint16 spsLenExt = 0;
							if (!BitReader.ReadByte(spsLenExt))
							{
								return false;
							}
							TArray<uint8>& spsExt = SequenceParameterSetsExt.AddDefaulted_GetRef();
							spsExt.Reserve(spsLenExt);
							spsExt.SetNumUninitialized(spsLenExt);
							if (!BitReader.ReadBytes(spsExt.GetData(), spsLenExt))
							{
								return false;
							}
						}
						bHaveAdditionalProfileIndication = true;
					}
				}

				int32 TotalCSDSize = TotalSPSSize + TotalPPSSize;
				if (TotalCSDSize)
				{
					CodecSpecificData.Reserve(TotalCSDSize);
					CodecSpecificDataSPSOnly.Reserve(TotalSPSSize);
					for(int32 i = 0; i < SequenceParameterSets.Num(); ++i)
					{
						CodecSpecificDataSPSOnly.Push(0);
						CodecSpecificDataSPSOnly.Push(0);
						CodecSpecificDataSPSOnly.Push(0);
						CodecSpecificDataSPSOnly.Push(1);
						CodecSpecificData.Push(0);
						CodecSpecificData.Push(0);
						CodecSpecificData.Push(0);
						CodecSpecificData.Push(1);
						for(int32 j = 0, jMax = SequenceParameterSets[i].Num(); j < jMax; ++j)
						{
							CodecSpecificDataSPSOnly.Push((SequenceParameterSets[i].GetData())[j]);
							CodecSpecificData.Push((SequenceParameterSets[i].GetData())[j]);
						}
					}
					CodecSpecificDataPPSOnly.Reserve(TotalPPSSize);
					for(int32 i = 0; i < PictureParameterSets.Num(); ++i)
					{
						CodecSpecificDataPPSOnly.Push(0);
						CodecSpecificDataPPSOnly.Push(0);
						CodecSpecificDataPPSOnly.Push(0);
						CodecSpecificDataPPSOnly.Push(1);
						CodecSpecificData.Push(0);
						CodecSpecificData.Push(0);
						CodecSpecificData.Push(0);
						CodecSpecificData.Push(1);
						for(int32 j = 0, jMax = PictureParameterSets[i].Num(); j < jMax; ++j)
						{
							CodecSpecificDataPPSOnly.Push((PictureParameterSets[i].GetData())[j]);
							CodecSpecificData.Push((PictureParameterSets[i].GetData())[j]);
						}
					}
				}

				return true;
			}
			return false;
		}




		static int32 FindStartCode(const uint8* InData, SIZE_T InDataSize, int32& NALUnitLength)
		{
			for(const uint8* Data = InData; InDataSize >= 3; ++Data, --InDataSize)
			{
				if (Data[0] == 0 && Data[1] == 0 && (Data[2] == 1 || (InDataSize >= 4 && Data[2] == 0 && Data[3] == 1)))
				{
					NALUnitLength = Data[2] ? 3 : 4;
					return Data - reinterpret_cast<const uint8*>(InData);
				}
			}
			NALUnitLength = -1;
			return -1;
		}



		void ParseBitstreamForNALUs(TArray<FNaluInfo>& outNALUs, const void* InBitstream, uint64 InBitstreamLength)
		{
			outNALUs.Reset();

			uint64 Pos = 0;
			uint64 BytesToGo = InBitstreamLength;
			const uint8* BitstreamData = (const uint8*)InBitstream;
			while(1)
			{
				int32 UnitLength = 0, StartCodePos = FindStartCode(BitstreamData, BytesToGo, UnitLength);
				if (StartCodePos >= 0)
				{
					if (outNALUs.Num())
					{
						outNALUs.Last().Size = StartCodePos;
						outNALUs.Last().Type = *BitstreamData;
					}
					FNaluInfo n;
					n.Offset = Pos + StartCodePos;
					n.Size = 0;
					n.Type = 0;
					n.UnitLength = UnitLength;
					outNALUs.Push(n);
					BitstreamData = Electra::AdvancePointer(BitstreamData, StartCodePos + UnitLength);
					Pos += StartCodePos + UnitLength;
					BytesToGo -= StartCodePos + UnitLength;
				}
				else
				{
					if (outNALUs.Num())
					{
						outNALUs.Last().Size = BytesToGo;
						outNALUs.Last().Type = *BitstreamData;
					}
					break;
				}
			}

		}



		int32 EBSPtoRBSP(uint8* OutBuf, const uint8* InBuf, int32 NumBytesIn)
		{
			uint8* OutBase = OutBuf;
			while(NumBytesIn-- > 0)
			{
				uint8 b = *InBuf++;
				*OutBuf++ = b;
				if (b == 0)
				{
					if (NumBytesIn > 1)
					{
						if (InBuf[0] == 0x00 && InBuf[1] == 0x03)
						{
							*OutBuf++ = 0x00;
							InBuf += 2;
							NumBytesIn -= 2;
						}
					}
				}
			}
			return OutBuf - OutBase;
		}



		template <typename T>
		struct FScopedDataPtr
		{
			FScopedDataPtr(void* Addr)
				: Data(static_cast<T*>(Addr))
			{
			}
			~FScopedDataPtr()
			{
				FMemory::Free(static_cast<void*>(Data));
			}
			operator T* ()
			{
				return Data;
			}
			T* Data;
		};




		bool ParseH264SPS(FISO14496_10_seq_parameter_set_data& OutSPS, const void* Data, int32 Size)
		{
			struct FSyntaxElement
			{
				static uint32 ue_v(FBitDataStream& BitStream)
				{
					int32 lz = -1;
					for(uint32 b = 0; b == 0; ++lz)
					{
						b = BitStream.GetBits(1);
					}
					if (lz)
					{
						return ((1 << lz) | BitStream.GetBits(lz)) - 1;
					}
					return 0;
				}
				static int32 se_v(FBitDataStream& BitStream)
				{
					uint32 c = ue_v(BitStream);
					return c & 1 ? int32((c + 1) >> 1) : -int32((c + 1) >> 1);
				}
			};

			// SPS is usually an EBSP so we need to strip it down.
			FScopedDataPtr<uint8> RBSP(FMemory::Malloc(Size));
			int32 RBSPsize = EBSPtoRBSP(RBSP, static_cast<const uint8*>(Data), Size);
			FBitDataStream BitReader(RBSP, RBSPsize);

			FMemory::Memzero(OutSPS);

			uint8 nalUnitType = (uint8)BitReader.GetBits(8);
			if ((nalUnitType & 0x1f) != 0x7)	// SPS NALU?
			{
				return false;
			}

			OutSPS.profile_idc = (uint8)BitReader.GetBits(8);
			OutSPS.constraint_set0_flag = (uint8)BitReader.GetBits(1);
			OutSPS.constraint_set1_flag = (uint8)BitReader.GetBits(1);
			OutSPS.constraint_set2_flag = (uint8)BitReader.GetBits(1);
			OutSPS.constraint_set3_flag = (uint8)BitReader.GetBits(1);
			OutSPS.constraint_set4_flag = (uint8)BitReader.GetBits(1);
			OutSPS.constraint_set5_flag = (uint8)BitReader.GetBits(1);
			BitReader.SkipBits(2);
			OutSPS.level_idc = (uint8)BitReader.GetBits(8);
			OutSPS.seq_parameter_set_id = FSyntaxElement::ue_v(BitReader);
			if (OutSPS.profile_idc == 100 || OutSPS.profile_idc == 110 || OutSPS.profile_idc == 122 || OutSPS.profile_idc == 244 ||
				OutSPS.profile_idc == 44 || OutSPS.profile_idc == 83 || OutSPS.profile_idc == 86 || OutSPS.profile_idc == 118 || OutSPS.profile_idc == 128)
			{
				OutSPS.chroma_format_idc = FSyntaxElement::ue_v(BitReader);
				if (OutSPS.chroma_format_idc == 3)
				{
					OutSPS.separate_colour_plane_flag = (uint8)BitReader.GetBits(1);
				}
				OutSPS.bit_depth_luma_minus8 = FSyntaxElement::ue_v(BitReader);
				OutSPS.bit_depth_chroma_minus8 = FSyntaxElement::ue_v(BitReader);
				OutSPS.qpprime_y_zero_transform_bypass_flag = (uint8)BitReader.GetBits(1);
				OutSPS.seq_scaling_matrix_present_flag = (uint8)BitReader.GetBits(1);
				if (OutSPS.seq_scaling_matrix_present_flag)
				{
					auto scaling_list = [&BitReader](int32* scalingList, int32 sizeOfScalingList, bool& useDefaultScalingMatrixFlag) -> void
					{
						int32 lastScale = 8;
						int32 nextScale = 8;
						for(int32 j=0; j<sizeOfScalingList; ++j)
						{
							if (nextScale)
							{
								int32 delta_scale = FSyntaxElement::se_v(BitReader);
								nextScale = (lastScale + delta_scale + 256) % 256;
								useDefaultScalingMatrixFlag = (j == 0 && nextScale == 0);
							}
							scalingList[j] = (nextScale == 0) ? lastScale : nextScale;
							lastScale = scalingList[j];
						}
					};

					// Skip over the scaling matrices.
					int32 dummyScalingMatrix[64] = {0};
					bool bDummyDefaultScalingMatrixFlag = false;
					for(int32 i=0, iMax=OutSPS.chroma_format_idc!=3?8:12; i<iMax; ++i)
					{
						uint8 seq_scaling_list_present_flag = (uint8)BitReader.GetBits(1);
						if (seq_scaling_list_present_flag)
						{
							if (i < 6)
							{
								scaling_list(dummyScalingMatrix, 16, bDummyDefaultScalingMatrixFlag);
							}
							else
							{
								scaling_list(dummyScalingMatrix, 64, bDummyDefaultScalingMatrixFlag);
							}
						}
					}
				}
			}
			OutSPS.log2_max_frame_num_minus4 = FSyntaxElement::ue_v(BitReader);
			OutSPS.pic_order_cnt_type = FSyntaxElement::ue_v(BitReader);
			if (OutSPS.pic_order_cnt_type == 0)
			{
				OutSPS.log2_max_pic_order_cnt_lsb_minus4 = FSyntaxElement::ue_v(BitReader);
			}
			else if (OutSPS.pic_order_cnt_type == 1)
			{
				OutSPS.delta_pic_order_always_zero_flag = FSyntaxElement::ue_v(BitReader);
				OutSPS.offset_for_non_ref_pic = FSyntaxElement::se_v(BitReader);
				OutSPS.offset_for_top_to_bottom_field = FSyntaxElement::se_v(BitReader);
				OutSPS.num_ref_frames_in_pic_order_cnt_cycle = FSyntaxElement::ue_v(BitReader);
				for(uint32 i = 0; i < OutSPS.num_ref_frames_in_pic_order_cnt_cycle; ++i)
				{
					FSyntaxElement::se_v(BitReader);		// discard
				}
			}
			OutSPS.max_num_ref_frames = FSyntaxElement::ue_v(BitReader);
			OutSPS.gaps_in_frame_num_value_allowed_flag = (uint8)BitReader.GetBits(1);
			OutSPS.pic_width_in_mbs_minus1 = FSyntaxElement::ue_v(BitReader);
			OutSPS.pic_height_in_map_units_minus1 = FSyntaxElement::ue_v(BitReader);
			OutSPS.frame_mbs_only_flag = (uint8)BitReader.GetBits(1);
			if (!OutSPS.frame_mbs_only_flag)
			{
				OutSPS.mb_adaptive_frame_field_flag = (uint8)BitReader.GetBits(1);
			}
			OutSPS.direct_8x8_inference_flag = (uint8)BitReader.GetBits(1);
			OutSPS.frame_cropping_flag = (uint8)BitReader.GetBits(1);
			if (OutSPS.frame_cropping_flag)
			{
				OutSPS.frame_crop_left_offset = FSyntaxElement::ue_v(BitReader);
				OutSPS.frame_crop_right_offset = FSyntaxElement::ue_v(BitReader);
				OutSPS.frame_crop_top_offset = FSyntaxElement::ue_v(BitReader);
				OutSPS.frame_crop_bottom_offset = FSyntaxElement::ue_v(BitReader);
			}
			OutSPS.vui_parameters_present_flag = (uint8)BitReader.GetBits(1);
			if (OutSPS.vui_parameters_present_flag)
			{
				OutSPS.aspect_ratio_info_present_flag = (uint8)BitReader.GetBits(1);
				if (OutSPS.aspect_ratio_info_present_flag)
				{
					OutSPS.aspect_ratio_idc = (uint8)BitReader.GetBits(8);
					if (OutSPS.aspect_ratio_idc == 255)
					{
						OutSPS.sar_width = (uint16)BitReader.GetBits(16);
						OutSPS.sar_height = (uint16)BitReader.GetBits(16);
					}
				}
				OutSPS.overscan_info_present_flag = (uint8)BitReader.GetBits(1);
				if (OutSPS.overscan_info_present_flag)
				{
					OutSPS.overscan_appropriate_flag = (uint8)BitReader.GetBits(1);
				}
				OutSPS.video_signal_type_present_flag = (uint8)BitReader.GetBits(1);
				if (OutSPS.video_signal_type_present_flag)
				{
					OutSPS.video_format = (uint8)BitReader.GetBits(3);
					OutSPS.video_full_range_flag = (uint8)BitReader.GetBits(1);
					OutSPS.colour_description_present_flag = (uint8)BitReader.GetBits(1);
					if (OutSPS.colour_description_present_flag)
					{
						OutSPS.colour_primaries = (uint8)BitReader.GetBits(8);
						OutSPS.transfer_characteristics = (uint8)BitReader.GetBits(8);
						OutSPS.matrix_coefficients = (uint8)BitReader.GetBits(8);
					}
				}
				OutSPS.chroma_loc_info_present_flag = (uint8)BitReader.GetBits(1);
				if (OutSPS.chroma_loc_info_present_flag)
				{
					OutSPS.chroma_sample_loc_type_top_field = FSyntaxElement::ue_v(BitReader);
					OutSPS.chroma_sample_loc_type_bottom_field = FSyntaxElement::ue_v(BitReader);
				}
				OutSPS.timing_info_present_flag = (uint8)BitReader.GetBits(1);
				if (OutSPS.timing_info_present_flag)
				{
					OutSPS.num_units_in_tick = BitReader.GetBits(32);
					OutSPS.time_scale = BitReader.GetBits(32);
					OutSPS.fixed_frame_rate_flag = (uint8)BitReader.GetBits(1);
				}
				// The remainder is of no interest to us at the moment.
			}
			return true;
		}



	} // namespace MPEG
} // namespace Electra

