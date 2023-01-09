// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"

#if !UE_SERVER

#include "ElectraTextureSample.h"

// -------------------------------------------------------------------------------------------------------------------------------------------------------

void IElectraTextureSampleBase::Initialize(FVideoDecoderOutput* InVideoDecoderOutput)
{
	VideoDecoderOutput = StaticCastSharedPtr<FVideoDecoderOutput, IDecoderOutputPoolable, ESPMode::ThreadSafe>(InVideoDecoderOutput->AsShared());
	HDRInfo = VideoDecoderOutput->GetHDRInformation();
	Colorimetry = VideoDecoderOutput->GetColorimetry();

	bool bFullRange = false;
	if (auto PinnedColorimetry = Colorimetry.Pin())
	{
		bFullRange = (PinnedColorimetry->GetMPEGDefinition()->VideoFullRangeFlag != 0);
	}

	// Prepare YUV -> RGB matrix containing all necessary offsets and scales to produce RGB straight from sample data
	const FMatrix* Mtx = bFullRange ? &MediaShaders::YuvToRgbRec709Unscaled : &MediaShaders::YuvToRgbRec709Scaled;
	FVector Off = (VideoDecoderOutput->GetFormat() == PF_NV12) ? (bFullRange ? MediaShaders::YUVOffsetNoScale8bits : MediaShaders::YUVOffset8bits)
														       : (bFullRange ? MediaShaders::YUVOffsetNoScale16bits : MediaShaders::YUVOffset16bits);
	float Scale = GetSampleDataScale(false);

	if (auto PinnedHDRInfo = HDRInfo.Pin())
	{
		switch (PinnedHDRInfo->GetHDRType())
		{
		case IVideoDecoderHDRInformation::EType::PQ10:
		case IVideoDecoderHDRInformation::EType::HLG10:
		case IVideoDecoderHDRInformation::EType::HDR10:
			Mtx = &MediaShaders::YuvToRgbRec2020Unscaled;
			Off = MediaShaders::YUVOffsetNoScale16bits;
			Scale = GetSampleDataScale(true);
			break;
		default:
			break;
		}
	}

	// Matrix to transform sample data to standard YUV values
	FMatrix PreMtx = FMatrix::Identity;
	PreMtx.M[0][0] = Scale;
	PreMtx.M[1][1] = Scale;
	PreMtx.M[2][2] = Scale;
	PreMtx.M[0][3] = -Off.X;
	PreMtx.M[1][3] = -Off.Y;
	PreMtx.M[2][3] = -Off.Z;

	// Combine this with the actual YUV-RGB conversion
	YuvToRgbMtx = FMatrix44f(*Mtx * PreMtx);
}

void IElectraTextureSampleBase::InitializePoolable()
{
}

void IElectraTextureSampleBase::ShutdownPoolable()
{
	VideoDecoderOutput.Reset();
}

FIntPoint IElectraTextureSampleBase::GetDim() const
{
	if (VideoDecoderOutput)
	{
		return VideoDecoderOutput->GetDim();
	}
	return FIntPoint::ZeroValue;
}


FIntPoint IElectraTextureSampleBase::GetOutputDim() const
{
	if (VideoDecoderOutput)
	{
		return VideoDecoderOutput->GetOutputDim();
	}
	return FIntPoint::ZeroValue;
}


FMediaTimeStamp IElectraTextureSampleBase::GetTime() const
{
	if (VideoDecoderOutput)
	{
		const FDecoderTimeStamp TimeStamp = VideoDecoderOutput->GetTime();
		return FMediaTimeStamp(TimeStamp.Time, TimeStamp.SequenceIndex);
	}
	return FMediaTimeStamp();
}


FTimespan IElectraTextureSampleBase::GetDuration() const
{
	if (VideoDecoderOutput)
	{
		return VideoDecoderOutput->GetDuration();
	}
	return FTimespan::Zero();
}

bool IElectraTextureSampleBase::IsOutputSrgb() const
{
	if (auto PinnedHDRInfo = HDRInfo.Pin())
	{
		// If the HDR type is unknown we also assume sRGB
		return (PinnedHDRInfo->GetHDRType() == IVideoDecoderHDRInformation::EType::Unknown);
	}
	// If no HDR info is present, we assume sRGB
	return true;
}

const FMatrix& IElectraTextureSampleBase::GetYUVToRGBMatrix() const
{
	if (auto PinnedHDRInfo = HDRInfo.Pin())
	{
		switch (PinnedHDRInfo->GetHDRType())
		{
		case IVideoDecoderHDRInformation::EType::PQ10:
		case IVideoDecoderHDRInformation::EType::HLG10:
		case IVideoDecoderHDRInformation::EType::HDR10:		return MediaShaders::YuvToRgbRec2020Unscaled;
		case IVideoDecoderHDRInformation::EType::Unknown:	break;
		}
	}

	// If no HDR info is present, we assume sRGB
	return GetFullRange() ? MediaShaders::YuvToRgbRec709Unscaled : MediaShaders::YuvToRgbRec709Scaled;
}

bool IElectraTextureSampleBase::GetFullRange() const
{
	if (auto PinnedColorimetry = Colorimetry.Pin())
	{
		if (auto PinnedHDRInfo = HDRInfo.Pin())
		{
			if (PinnedHDRInfo->GetHDRType() != IVideoDecoderHDRInformation::EType::Unknown)
			{
				// For HDR we assume full range at all times
				return true;
			}
		}
		// SDR honors the flag in the MPEG stream...
		return (PinnedColorimetry->GetMPEGDefinition()->VideoFullRangeFlag != 0);
	}
	// SDR with no MPEG info is assumed to be video range
	return false;
}

FMatrix44f IElectraTextureSampleBase::GetSampleToRGBMatrix() const
{
	return YuvToRgbMtx;
}

FMatrix44f IElectraTextureSampleBase::GetGamutToXYZMatrix() const
{
	if (auto PinnedHDRInfo = HDRInfo.Pin())
	{
		switch (PinnedHDRInfo->GetHDRType())
		{
		case IVideoDecoderHDRInformation::EType::PQ10:
		case IVideoDecoderHDRInformation::EType::HLG10:
		case IVideoDecoderHDRInformation::EType::HDR10:		return GamutToXYZMatrix(EDisplayColorGamut::Rec2020_D65);
		case IVideoDecoderHDRInformation::EType::Unknown:	return GamutToXYZMatrix(EDisplayColorGamut::sRGB_D65);
		}
	}

	// If no HDR info is present, we assume sRGB
	return GamutToXYZMatrix(EDisplayColorGamut::sRGB_D65);
}

FVector2f IElectraTextureSampleBase::GetWhitePoint() const
{
	if (auto PinnedHDRInfo = HDRInfo.Pin())
	{
		if (auto ColorVolume = PinnedHDRInfo->GetMasteringDisplayColourVolume())
		{
			return FVector2f(ColorVolume->white_point_x, ColorVolume->white_point_y);
		}
	}
	// If no HDR info is present, we assume sRGB
	return FVector2f(UE::Color::GetWhitePoint(UE::Color::EWhitePoint::CIE1931_D65));
}

UE::Color::EEncoding IElectraTextureSampleBase::GetEncodingType() const
{
	if (auto PinnedHDRInfo = HDRInfo.Pin())
	{
		switch (PinnedHDRInfo->GetHDRType())
		{
		case IVideoDecoderHDRInformation::EType::PQ10:
		case IVideoDecoderHDRInformation::EType::HDR10:		return UE::Color::EEncoding::ST2084;
		case IVideoDecoderHDRInformation::EType::Unknown:	return UE::Color::EEncoding::sRGB;
		case IVideoDecoderHDRInformation::EType::HLG10:
			{
			check(!"*** Implement support for HLG10 in UE::Color!");
			return UE::Color::EEncoding::sRGB;
			}
		}
	}
	// If no HDR info is present, we assume sRGB
	return UE::Color::EEncoding::sRGB;
}

#endif
