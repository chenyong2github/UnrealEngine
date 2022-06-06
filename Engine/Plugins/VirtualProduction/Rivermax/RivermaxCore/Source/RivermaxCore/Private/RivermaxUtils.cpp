// Copyright Epic Games, Inc. All Rights Reserved.

#include "RivermaxUtils.h"

#include "RivermaxTypes.h"


namespace UE::RivermaxCore::Private::Utils
{
	FString PixelFormatToSamplingDesc(ERivermaxOutputPixelFormat PixelFormat)
	{
		switch (PixelFormat)
		{
			case ERivermaxOutputPixelFormat::RMAX_10BIT_RGB:
			case ERivermaxOutputPixelFormat::RMAX_8BIT_RGB:
			case ERivermaxOutputPixelFormat::RMAX_16F_RGB:
			{
				return FString(TEXT("RGB"));
			}
			case ERivermaxOutputPixelFormat::RMAX_10BIT_YCBCR:
			case ERivermaxOutputPixelFormat::RMAX_8BIT_YCBCR:
			default:
			{
				return FString(TEXT("YCbCr-4:2:2"));
			}
		}
	}

	FString PixelFormatToBitDepth(ERivermaxOutputPixelFormat PixelFormat)
	{
		switch (PixelFormat)
		{
			case ERivermaxOutputPixelFormat::RMAX_10BIT_RGB:
			case ERivermaxOutputPixelFormat::RMAX_10BIT_YCBCR:
			{
				return FString(TEXT("10"));
			}
			case ERivermaxOutputPixelFormat::RMAX_16F_RGB:
			{
				return FString(TEXT("16f"));
			}
			case ERivermaxOutputPixelFormat::RMAX_8BIT_RGB:
			case ERivermaxOutputPixelFormat::RMAX_8BIT_YCBCR:
			default:
			{
				return FString(TEXT("8"));
			}
		}
	}

	void StreamOptionsToSDPDescription(const UE::RivermaxCore::FRivermaxStreamOptions& Options, FAnsiStringBuilderBase& OutSDPDescription)
	{
		// Basic SDP string creation from a set of options. At some point, having a proper SDP loader / creator.
		// Refer to https://datatracker.ietf.org/doc/html/rfc4570

		FString FrameRateDescription;
		if (FMath::IsNearlyZero(FMath::Frac(Options.FrameRate.AsDecimal())) == false)
		{
			FrameRateDescription = FString::Printf(TEXT("%d/%d"), Options.FrameRate.Numerator, Options.FrameRate.Denominator);
		}
		else
		{
			FrameRateDescription = FString::Printf(TEXT("%d"), (uint32)Options.FrameRate.AsDecimal());
		}



		OutSDPDescription.Appendf("v=0\n");
		OutSDPDescription.Appendf("s=SMPTE ST2110 20 streams\n");
		OutSDPDescription.Appendf("m=video %d RTP/AVP 96\n", Options.Port);
		OutSDPDescription.Appendf("c=IN IP4 %S/64\n", *Options.StreamAddress);
		OutSDPDescription.Appendf("a=source-filter: incl IN IP4 %S %S\n", *Options.StreamAddress, *Options.InterfaceAddress);
		OutSDPDescription.Appendf("a=rtpmap:96 raw/90000\n");
		OutSDPDescription.Appendf("a=fmtp: 96 sampling=%S; width=%d; height=%d; exactframerate=%S; depth=%S; TCS=SDR; colorimetry=BT709; PM=2110GPM; SSN=ST2110-20:2017; TP=2110TPN;\n"
			, *PixelFormatToSamplingDesc(Options.PixelFormat)
			, Options.Resolution.X
			, Options.Resolution.Y
			, *FrameRateDescription
			, *PixelFormatToBitDepth(Options.PixelFormat));

		OutSDPDescription.Appendf("a=mediaclk:direct=0\n");
		OutSDPDescription.Appendf("a=mid:VID");
	}
}