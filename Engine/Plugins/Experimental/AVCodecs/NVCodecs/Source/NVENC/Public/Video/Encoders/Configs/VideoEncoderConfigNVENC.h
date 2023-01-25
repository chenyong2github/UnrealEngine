// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AVExtension.h"
#include "Video/VideoEncoder.h"

#include "NVENC.h"

struct NVENC_API FVideoEncoderConfigNVENC : public FAVConfig, public NV_ENC_INITIALIZE_PARAMS
{
public:
	static TAVResult<NV_ENC_PARAMS_RC_MODE> ConvertRateControlMode(ERateControlMode Mode);
	static TAVResult<ERateControlMode> ConvertRateControlMode(NV_ENC_PARAMS_RC_MODE Mode);
	static TAVResult<NV_ENC_MULTI_PASS> ConvertMultipassMode(EMultipassMode Mode);
	static TAVResult<EMultipassMode> ConvertMultipassMode(NV_ENC_MULTI_PASS Mode);
	static TAVResult<NV_ENC_BUFFER_FORMAT> ConvertFormat(EVideoFormat const& Format);
	
private:
	NV_ENC_CONFIG Config = {};

public:
	FVideoEncoderConfigNVENC()
		: FAVConfig()
		, NV_ENC_INITIALIZE_PARAMS({})
	{
		version = NV_ENC_INITIALIZE_PARAMS_VER;
		presetGUID = NV_ENC_PRESET_P4_GUID;
		frameRateNum = 60;
		frameRateDen = 1;
		enablePTD = 1;
		reportSliceOffsets = 0;
		enableSubFrameWrite = 0;
		maxEncodeWidth = 4096;
		maxEncodeHeight = 4096;

		Config.version = NV_ENC_CONFIG_VER;
		encodeConfig = &Config;

		// From the NVENC ultra low latency preset
		Config.gopLength = -1;
		Config.frameIntervalP = 1;
		Config.frameFieldMode = NV_ENC_PARAMS_FRAME_FIELD_MODE_FRAME;
		Config.mvPrecision = NV_ENC_MV_PRECISION_QUARTER_PEL;//NV_ENC_MV_PRECISION_DEFAULT
		Config.rcParams.constQP = { 28, 31, 25 };
		Config.rcParams.lowDelayKeyFrameScale = 1;
		Config.rcParams.multiPass = NV_ENC_TWO_PASS_QUARTER_RESOLUTION;//NV_ENC_MULTI_PASS_DISABLED
	}
		
	FVideoEncoderConfigNVENC(FVideoEncoderConfigNVENC const& Other)
	{
		*this = Other;
	}

	FVideoEncoderConfigNVENC& operator=(FVideoEncoderConfigNVENC const& Other)
	{
		FMemory::Memcpy(*this, Other);
		encodeConfig = &Config;

		FMemory::Memcpy(Config, Other.Config);

		return *this;
	}

	bool operator==(FVideoEncoderConfigNVENC const& Other) const
	{
		NV_ENC_CONFIG* const TempEncodeConfig = encodeConfig;
		const_cast<FVideoEncoderConfigNVENC*>(this)->encodeConfig = Other.encodeConfig;

		bool const IsEqual = !FMemory::Memcmp(this, &Other, sizeof(FVideoEncoderConfigNVENC))
			|| !FMemory::Memcmp(encodeConfig, Other.encodeConfig, sizeof(NV_ENC_CONFIG));

		const_cast<FVideoEncoderConfigNVENC*>(this)->encodeConfig = TempEncodeConfig;

		return IsEqual;
	}

	bool operator!=(FVideoEncoderConfigNVENC const& Other) const
	{
		return !(*this == Other);
	}
};

template <>
FAVResult FAVExtension::TransformConfig(FVideoEncoderConfigNVENC& OutConfig, struct FVideoEncoderConfig const& InConfig);

template <>
FAVResult FAVExtension::TransformConfig(struct FVideoEncoderConfig& OutConfig, FVideoEncoderConfigNVENC const& InConfig);

template <>
FAVResult FAVExtension::TransformConfig(FVideoEncoderConfigNVENC& OutConfig, struct FVideoEncoderConfigH264 const& InConfig);

template <>
FAVResult FAVExtension::TransformConfig(FVideoEncoderConfigNVENC& OutConfig, struct FVideoEncoderConfigH265 const& InConfig);

DECLARE_TYPEID(FVideoEncoderConfigNVENC, NVENC_API);
