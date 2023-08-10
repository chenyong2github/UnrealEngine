// Copyright Epic Games, Inc. All Rights Reserved.

#include "RivermaxUtils.h"

#include "HAL/IConsoleManager.h"
#include "RivermaxTypes.h"


namespace UE::RivermaxCore::Private::Utils
{
	static TAutoConsoleVariable<float> CVarRivermaxOutputFrameRateMultiplier(
		TEXT("Rivermax.Output.FrameRateMultiplier"), 1.00,
		TEXT("Multiplier applied to desired output frame rate in order to reduce time it takes to send out a frame and slowly correct misalignment that could happen."),
		ECVF_Default);

	FString PixelFormatToSamplingDesc(ESamplingType SamplingType)
	{
		switch (SamplingType)
		{
			case ESamplingType::RGB_8bit:
			case ESamplingType::RGB_10bit:
			case ESamplingType::RGB_12bit:
			case ESamplingType::RGB_16bit:
			case ESamplingType::RGB_16bitFloat:
			{
				return FString(TEXT("RGB"));
			}
			case ESamplingType::YUV422_8bit:
			case ESamplingType::YUV422_10bit:
			case ESamplingType::YUV422_12bit:
			case ESamplingType::YUV422_16bit:
			case ESamplingType::YUV422_16bitFloat:
			{
				return FString(TEXT("YCbCr-4:2:2"));
			}
			case ESamplingType::YUV444_8bit:
			case ESamplingType::YUV444_10bit:
			case ESamplingType::YUV444_12bit:
			case ESamplingType::YUV444_16bit:
			case ESamplingType::YUV444_16bitFloat:
			{
				return FString(TEXT("YCbCr-4:4:4"));
			}
			default:
			{
				checkNoEntry();
				return FString();
			}
		}
	}

	FString PixelFormatToBitDepth(ESamplingType SamplingType)
	{
		switch (SamplingType)
		{
		case ESamplingType::RGB_8bit:
		case ESamplingType::YUV422_8bit:
		case ESamplingType::YUV444_8bit:
		{
			return FString(TEXT("8"));
		}
		case ESamplingType::RGB_10bit:
		case ESamplingType::YUV422_10bit:
		case ESamplingType::YUV444_10bit:
		{
			return FString(TEXT("10"));
		}
		case ESamplingType::RGB_12bit:
		case ESamplingType::YUV422_12bit:
		case ESamplingType::YUV444_12bit:
		{
			return FString(TEXT("12"));
		}
		case ESamplingType::RGB_16bit:
		case ESamplingType::YUV422_16bit:
		case ESamplingType::YUV444_16bit:
		{
			return FString(TEXT("16"));
		}
		case ESamplingType::RGB_16bitFloat:
		case ESamplingType::YUV422_16bitFloat:
		case ESamplingType::YUV444_16bitFloat:
		{
			return FString(TEXT("16f"));
		}
		default:
		{
			checkNoEntry();
			return FString();
		}
		}
	}

	void StreamOptionsToSDPDescription(const UE::RivermaxCore::FRivermaxOutputStreamOptions& Options, FAnsiStringBuilderBase& OutSDPDescription)
	{
		// Basic SDP string creation from a set of options. At some point, having a proper SDP loader / creator would be useful.
		// Refer to https://datatracker.ietf.org/doc/html/rfc4570

		// Apply desired multiplier, if any, to the desired output frame rate.
		const float FrameRateMultiplier = FMath::Clamp(CVarRivermaxOutputFrameRateMultiplier.GetValueOnAnyThread(), 1.0, 1.2);
		FString FrameRateDescription;
		if (!FMath::IsNearlyEqual(FrameRateMultiplier, 1.0f))
		{
			// When a multiplier is involved, always write out frame rate using decimals
			const double AdjustedFrameRate = Options.FrameRate.AsDecimal() * CVarRivermaxOutputFrameRateMultiplier.GetValueOnAnyThread();
			FrameRateDescription = FString::Printf(TEXT("%0.9f"), AdjustedFrameRate);
		}
		else if (FMath::IsNearlyZero(FMath::Frac(Options.FrameRate.AsDecimal())) == false)
		{
			FrameRateDescription = FString::Printf(TEXT("%d/%d"), Options.FrameRate.Numerator, Options.FrameRate.Denominator);
		}
		else
		{
			FrameRateDescription = FString::Printf(TEXT("%d"), (int32)Options.FrameRate.AsDecimal());
		}


		constexpr int32 MulticastTTL = 64;
		OutSDPDescription.Appendf("v=0\n");
		OutSDPDescription.Appendf("s=SMPTE ST2110 20 streams\n");
		OutSDPDescription.Appendf("t=0 0\n");
		OutSDPDescription.Appendf("m=video %d RTP/AVP 96\n", Options.Port);
		OutSDPDescription.Appendf("c=IN IP4 %S/%d\n", *Options.StreamAddress, MulticastTTL);
		OutSDPDescription.Appendf("a=source-filter: incl IN IP4 %S %S\n", *Options.StreamAddress, *Options.InterfaceAddress);
		OutSDPDescription.Appendf("a=rtpmap:96 raw/90000\n");
		OutSDPDescription.Appendf("a=fmtp: 96 sampling=%S; width=%d; height=%d; exactframerate=%S; depth=%S; TCS=SDR; colorimetry=BT709; PM=2110GPM; SSN=ST2110-20:2017; TP=2110TPN;\n"
			, *PixelFormatToSamplingDesc(Options.PixelFormat)
			, Options.AlignedResolution.X
			, Options.AlignedResolution.Y
			, *FrameRateDescription
			, *PixelFormatToBitDepth(Options.PixelFormat));
	}

	uint32 TimestampToFrameNumber(uint32 Timestamp, const FFrameRate& FrameRate)
	{
		using namespace UE::RivermaxCore::Private::Utils;
		const double MediaFrameTime = Timestamp / MediaClockSampleRate;
		const uint32 FrameNumber = FMath::Floor(MediaFrameTime * FrameRate.AsDecimal());
		return FrameNumber;
	}
}