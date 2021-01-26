// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once


#include "PlayerCore.h"

#include "PlayerTime.h"
#include "BitDataStream.h"



namespace Electra
{
	namespace MPEG
	{

		/**
		 * Sequence parameter set as per ISO/IEC 14496-10:2012, section 7.3.2.1.1
		 */
		struct FISO14496_10_seq_parameter_set_data
		{
			uint8	profile_idc;
			uint8	constraint_set0_flag;
			uint8	constraint_set1_flag;
			uint8	constraint_set2_flag;
			uint8	constraint_set3_flag;
			uint8	constraint_set4_flag;
			uint8	constraint_set5_flag;
			uint8	level_idc;
			uint32	seq_parameter_set_id;
			uint32	chroma_format_idc;
			uint8	separate_colour_plane_flag;
			uint32	bit_depth_luma_minus8;
			uint32	bit_depth_chroma_minus8;
			uint8	qpprime_y_zero_transform_bypass_flag;
			uint8	seq_scaling_matrix_present_flag;
			uint32	log2_max_frame_num_minus4;
			uint32	pic_order_cnt_type;
			uint32	log2_max_pic_order_cnt_lsb_minus4;
			uint32	delta_pic_order_always_zero_flag;
			int32	offset_for_non_ref_pic;
			int32	offset_for_top_to_bottom_field;
			uint32	num_ref_frames_in_pic_order_cnt_cycle;
			uint32	max_num_ref_frames;
			uint8	gaps_in_frame_num_value_allowed_flag;
			uint32	pic_width_in_mbs_minus1;
			uint32	pic_height_in_map_units_minus1;
			uint8	frame_mbs_only_flag;
			uint8	mb_adaptive_frame_field_flag;
			uint8	direct_8x8_inference_flag;
			uint8	frame_cropping_flag;
			uint32	frame_crop_left_offset;
			uint32	frame_crop_right_offset;
			uint32	frame_crop_top_offset;
			uint32	frame_crop_bottom_offset;
			uint8	vui_parameters_present_flag;
			uint8	aspect_ratio_info_present_flag;
			uint8	aspect_ratio_idc;
			uint16	sar_width;
			uint16	sar_height;
			uint8	overscan_info_present_flag;
			uint8	overscan_appropriate_flag;
			uint8	video_signal_type_present_flag;
			uint8	video_format;
			uint8	video_full_range_flag;
			uint8	colour_description_present_flag;
			uint8	colour_primaries;
			uint8	transfer_characteristics;
			uint8	matrix_coefficients;
			uint8	chroma_loc_info_present_flag;
			uint32	chroma_sample_loc_type_top_field;
			uint32	chroma_sample_loc_type_bottom_field;
			uint8	timing_info_present_flag;
			uint32	num_units_in_tick;
			uint32	time_scale;
			uint8	fixed_frame_rate_flag;

			int32 GetWidth() const
			{
				return (pic_width_in_mbs_minus1 + 1) * 16;
			}

			int32 GetHeight() const
			{
				return (pic_height_in_map_units_minus1 + 1) * 16;
			}

			void GetCrop(int32& Left, int32& Right, int32& Top, int32& Bottom) const
			{
				if (frame_cropping_flag)
				{
					// The scaling factors are determined by the chroma_format_idc (see ISO/IEC 14496-10, table 6.1)
					// For our purposes this will be 1, so the sub width/height are 2.
					const int32 CropUnitX = 2;
					const int32 CropUnitY = 2;
					Left = (uint16)frame_crop_left_offset * CropUnitX;
					Right = (uint16)frame_crop_right_offset * CropUnitX;
					Top = (uint16)frame_crop_top_offset * CropUnitY;
					Bottom = (uint16)frame_crop_bottom_offset * CropUnitY;
				}
				else
				{
					Left = Right = Top = Bottom = 0;
				}
			}

			void GetAspect(int32& SarW, int32& SarH) const
			{
				if (vui_parameters_present_flag && aspect_ratio_info_present_flag)
				{
					switch (aspect_ratio_idc)
					{
						default:	SarW = SarH = 0;		break;
						case 0:		SarW = SarH = 0;		break;
						case 1:		SarW = SarH = 1;		break;
						case 2:		SarW = 12; SarH = 11;	break;
						case 3:		SarW = 10; SarH = 11;	break;
						case 4:		SarW = 16; SarH = 11;	break;
						case 5:		SarW = 40; SarH = 33;	break;
						case 6:		SarW = 24; SarH = 11;	break;
						case 7:		SarW = 20; SarH = 11;	break;
						case 8:		SarW = 32; SarH = 11;	break;
						case 9:		SarW = 80; SarH = 33;	break;
						case 10:	SarW = 18; SarH = 11;	break;
						case 11:	SarW = 15; SarH = 11;	break;
						case 12:	SarW = 64; SarH = 33;	break;
						case 13:	SarW = 160; SarH = 99;	break;
						case 14:	SarW = 4; SarH = 3;		break;
						case 15:	SarW = 3; SarH = 2;		break;
						case 16:	SarW = 2; SarH = 1;		break;
						case 255:	SarW = sar_width; SarH = sar_height;	break;
					}
				}
				else
				{
					SarW = SarH = 1;
				}
			}
			FTimeFraction GetTiming() const
			{
				if (vui_parameters_present_flag && timing_info_present_flag)
				{
					return FTimeFraction(time_scale, num_units_in_tick * 2);
				}
				return FTimeFraction();
			}
		};

		/**
		 * ISO/IEC 14496-15:2014
		 */
		class FAVCDecoderConfigurationRecord
		{
		public:
			FAVCDecoderConfigurationRecord();
			void SetRawData(const void* Data, int64 Size);
			const TArray<uint8>& GetRawData() const;

			bool Parse();

			const TArray<uint8>& GetCodecSpecificData() const;
			const TArray<uint8>& GetCodecSpecificDataSPS() const;
			const TArray<uint8>& GetCodecSpecificDataPPS() const;

			int32 GetNumberOfSPS() const;
			const FISO14496_10_seq_parameter_set_data& GetParsedSPS(int32 SpsIndex) const;

		private:
			TArray<uint8>													RawData;
			TArray<uint8>													CodecSpecificData;
			TArray<uint8>													CodecSpecificDataSPSOnly;
			TArray<uint8>													CodecSpecificDataPPSOnly;
			TArray<TArray<uint8>>											SequenceParameterSets;
			TArray<TArray<uint8>>											PictureParameterSets;
			TArray<TArray<uint8>>											SequenceParameterSetsExt;
			uint8															ConfigurationVersion;
			uint8															AVCProfileIndication;
			uint8															ProfileCompatibility;
			uint8															AVCLevelIndication;
			uint8															NALUnitLength;
			uint8															ChromaFormat;
			uint8															BitDepthLumaMinus8;
			uint8															BitDepthChromaMinus8;
			bool															bHaveAdditionalProfileIndication;

			TArray<FISO14496_10_seq_parameter_set_data>						ParsedSPSs;
		};



		//! Parses a H.264 (ISO/IEC 14496-10) bitstream for NALUs.
		struct FNaluInfo
		{
			uint64		Offset;
			uint64		Size;
			uint8		Type;
			uint8		UnitLength;
		};
		void ParseBitstreamForNALUs(TArray<FNaluInfo>& outNALUs, const void* InBitstream, uint64 InBitstreamLength);


		//! Parses a H.264 (ISO/IEC 14496-10) SPS NALU.
		bool ParseH264SPS(FISO14496_10_seq_parameter_set_data& OutSPS, const void* Data, int32 Size);


	} // namespace MPEG
} // namespace Electra


